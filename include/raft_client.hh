#pragma once
#include <cassert>
#include <chrono>
extern "C" {
  #include <stdbool.h>
  #include <stdlib.h>
  #include <stdio.h>
  #include <string.h>
  #include <stdint.h>
  #include <zmq.h>
  #include <czmq.h>
}
#include "raft_msgpack.hh"
#include "procedurex.hh"
#include "../include/random.hh"
#include "../include/result.hh"
#include "../include/tsc.hh"
#include "../include/zipf.hh"
#include "include/util.hh"
#include "include/common.hh"

#define N() {}

class Client {
public:
  raft_node_id_t id_;
  msgpack::sbuffer packed_id_;
  void *context_;
  HostData server_;
  zsock_t *socket_ = NULL;
  zloop_t *loop_ = NULL;
  std::vector<ProcedureX> pro_set_;
  long sequence_num_ = 0;
  std::string data_;
  Result result_;
  Xoroshiro128Plus rnd_;
  FastZipf *zipf_;
  std::chrono::system_clock::time_point start_time_;

  Client(raft_node_id_t id, zloop_t *loop, HostData &server, void *context) :
    id_(id), loop_(loop), server_(server), context_(context) {
    pro_set_.reserve(FLAGS_max_ope);
    rnd_.init();
    zipf_ = new FastZipf(&rnd_, FLAGS_zipf_skew, FLAGS_tuple_num);
  }

  ~Client() {
    stop();
  }

  void connect();
  void reconnect(HostData &server);
  void send(std::vector<ProcedureX> &data);
  //void send(const char *data) {std::string s(data); send(s);}
  void send() {std::vector<ProcedureX> s; send(s);}
  void stop() {
    if (socket_) {
      zloop_reader_end(loop_, socket_);
      zmq_close(socket_);
      socket_ = NULL;
    }
  }
};

static int c_timer_event(zloop_t *loop, int timer_id, void *udata)
{
  Client *c = (Client*)udata;
  c->send();
  return 0;
}


inline static void makeProcedureX(std::vector <ProcedureX> &pro, Xoroshiro128Plus &rnd,
                                 FastZipf &zipf, size_t tuple_num, size_t max_ope,
                                 size_t thread_num, size_t rratio, bool rmw, bool ycsb,
                                 bool partition, size_t thread_id, [[maybe_unused]]Result &res) {
#if ADD_ANALYSIS
  uint64_t start = rdtscp();
#endif
  pro.clear();
  bool ronly_flag(true), wonly_flag(true);
  for (size_t i = 0; i < max_ope; ++i) {
    uint64_t tmpkey;
    // decide access destination key.
    if (ycsb) {
      if (partition) {
        size_t block_size = tuple_num / thread_num;
        tmpkey = (block_size * thread_id) + (zipf() % block_size);
      } else {
        tmpkey = zipf() % tuple_num;
      }
    } else {
      if (partition) {
        size_t block_size = tuple_num / thread_num;
        tmpkey = (block_size * thread_id) + (rnd.next() % block_size);
      } else {
        tmpkey = rnd.next() % tuple_num;
      }
    }

    // decide operation type.
    if ((rnd.next() % 100) < rratio) {
      wonly_flag = false;
      pro.emplace_back(Ope::READ, tmpkey);
    } else {
      ronly_flag = false;
      if (rmw) {
        pro.emplace_back(Ope::READ_MODIFY_WRITE, tmpkey);
      } else {
        pro.emplace_back(Ope::WRITE, tmpkey);
      }
    }
  }

  (*pro.begin()).ronly_ = ronly_flag;
  (*pro.begin()).wonly_ = wonly_flag;

#if KEY_SORT
  std::sort(pro.begin(), pro.end());
#endif // KEY_SORT

#if ADD_ANALYSIS
  res.local_make_procedure_latency_ += rdtscp() - start;
#endif
}



static int client_handler(zloop_t *loop, zsock_t *socket, void *udata)
{N();
  static int count=0;
  Client *c = (Client*)udata;
  assert(c->socket_==socket);

  //if (c->sequence_num_==0) {
  static auto start_time_ = std::chrono::system_clock::now();
  //}

  // receive empty delimiter frame
  int rc, more;
  size_t len;
  zmq_msg_t msg_null;
  rc = zmq_msg_init(&msg_null);
  assert(rc==0);
  rc = zmq_msg_recv(&msg_null, socket, 0);
  assert(rc!=-1);
  len = zmq_msg_size(&msg_null);
  if (len!=0) {
    zmq_msg_t msg;
    rc = zmq_msg_init(&msg);
    assert(rc==0);
    rc = zmq_msg_recv(&msg, socket, 0);
    assert(rc!=-1);
    size_t next_len = zmq_msg_size(&msg);
    printf("delimiter: len=%zd next_len=%zd\n",len, next_len);
  }
  assert(len==0);
  more = zmq_msg_more(&msg_null);
  assert(more);

  int type;
  zmq_msgpk_recv(socket, type);

  switch(type) {
  case RAFT_MSG_LEADER_HINT:
    {
      HostData h;
      zmq_msgpk_recv(socket, h);
      if (h.id_ >= 0) {
        printf("reconnect\n");
        c->reconnect(h);
        usleep(10000);
      } else {
        zloop_timer(loop, 1000, 1, c_timer_event, c);
        return 0;
      }
      break;
    }
  case RAFT_MSG_CLIENTREQUEST_RESPONSE:
    {
      ClientRequestResponse ccr;
      zmq_msgpk_recv(socket, ccr);
      //usleep(100000);
      usleep(100);
      break;
    }
  default:
    printf("type=%d\n",type);
    abort();
  }

  int thid = c->id_;
  makeProcedureX(c->pro_set_, c->rnd_, *(c->zipf_), FLAGS_tuple_num, FLAGS_max_ope,
    FLAGS_thread_num, FLAGS_rratio, FLAGS_rmw, FLAGS_ycsb, false,
    thid, c->result_);

  // send next data
  c->send(c->pro_set_);
  if (c->sequence_num_%100==0) {
    auto t = std::chrono::system_clock::now();
    auto dur = t - start_time_;
    auto usec = std::chrono::duration_cast<std::chrono::microseconds>(dur).count();
    printf("client id=%d seq_num=%ld t=%ld\n", c->id_, c->sequence_num_, usec/c->sequence_num_);
  }
  return 0;
  //if (c->sequence_num_==300) abort();
  //return (c->sequence_num_==300) ? -1 : 0;
}

void Client::connect()
{
  socket_ = (zsock_t*)zmq_socket(context_, ZMQ_DEALER);
  assert(socket_);
  msgpack::sbuffer packed_id_;
  msgpack::pack(&packed_id_, id_);
  int rc = zmq_setsockopt(socket_, ZMQ_IDENTITY, packed_id_.data(), packed_id_.size());
  assert(rc==0);

  std::string url = server_.connect_url();
  rc = zmq_connect(socket_, url.c_str());
  assert(rc==0);
  printf("%d:connect to %s\n", id_, url.c_str());

  zloop_reader(loop_, socket_, client_handler, this);

  // send empty data
  //send("");
}

void Client::reconnect(HostData &server)
{
  int rc;
  if (socket_) {
    zloop_reader_end(loop_, socket_);
    rc = zmq_close(socket_);
    assert(rc==0);
    socket_ = NULL;
  }
  server_ = server;
  connect();
}

void Client::send(std::vector<ProcedureX> &data)
{
  ClientRequest cmd(id_, server_.id_, ++sequence_num_, data);
  assert(socket_);

  // send empty delimiter frame
  zmq_msg_t msg;
  int rc;
  rc = zmq_msg_init_size(&msg, 0);
  assert(rc==0);
  rc = zmq_msg_send(&msg, socket_, ZMQ_SNDMORE);
  if (rc==-1) {fprintf(stderr,"zmq_msgpk_send: %s\n",zmq_strerror(zmq_errno()));}

  zmq_msgpk_send_with_type(socket_, cmd);
}
