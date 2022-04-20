#pragma once
extern "C" {
  #include <stdbool.h>
  #include <assert.h>
  #include <stdlib.h>
  #include <stdio.h>
  #include <string.h>
  #include <stdint.h>
  #include <zmq.h>
  #include <czmq.h>
}
#include "raft_msgpack.hh"

#define N() {}

class Client {
public:
  raft_node_id_t id_;
  msgpack::sbuffer packed_id_;
  void *context_;
  HostData server_;
  zsock_t *socket_ = NULL;
  zloop_t *loop_ = NULL;

  Client(raft_node_id_t id, zloop_t *loop, HostData &server, void *context) :
    id_(id), loop_(loop), server_(server), context_(context) {}

  ~Client() {
    stop();
  }

  void start();
  void connect();
  void reconnect(HostData &server);
  void send(std::string &data);
  void send(const char *data) {std::string s(data); send(s);}
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
  c->send("");
  return 0;
}

static int client_handler(zloop_t *loop, zsock_t *socket, void *udata)
{N();
  Client *c = (Client*)udata;
  assert(c->socket_==socket);

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
  case RAFT_MSG_FORWARD_LEADER:
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
  case RAFT_MSG_CLIENTCOMMAND_RESPONSE:
    {
      ClientCommandResponse ccr;
      zmq_msgpk_recv(socket, ccr);
      usleep(100000);
      break;
    }
  default:
    printf("type=%d\n",type);
    abort();
  }

  // send next data
  c->send("abcdefghij");
  return 0;
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
  if (socket_) {
    zloop_reader_end(loop_, socket_);
    int rc = zmq_close(socket_);
    assert(rc==0);
    socket_ = NULL;
  }
  server_ = server;
  connect();
}

void Client::send(std::string &data)
{
  ClientCommand cmd(id_, server_.id_, data);
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
