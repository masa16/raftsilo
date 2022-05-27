#include <queue>
#include <mutex>
#include <condition_variable>
#include <cassert>

extern "C" {
  #include <stdbool.h>
  #include <stdlib.h>
  #include <stdio.h>
  #include <string.h>
  #include <stdint.h>
  #include <zmq.h>
  #include <czmq.h>
  #include "raft.h"
  #include "raft_log.h"
  //#include "raft_private.h"
}
#include "raft_msgpack.hh"
#include "raft_server.hh"


//#define N() do{fprintf(stderr, "%16s %4d %16s\n", __FILE__, __LINE__, __func__); fflush(stderr);} while(0)
#define N() {}



void send_header(zsock_t *socket, raft_node_id_t id)
{
  zmq_msgpk_send(socket, id, ZMQ_SNDMORE);
  zmq_msg_t msg;
  int rc;
  rc = zmq_msg_init_size(&msg, 0);
  ZERR(rc!=0);
  rc = zmq_msg_send(&msg, socket, ZMQ_SNDMORE);
  ZERR(rc==-1);
}


void Server::send_response_to_clinet(raft_msg_id_t msg_id)
{
  using iterator = decltype(id_map_)::iterator;
  std::pair<iterator, iterator> ret = id_map_.equal_range(msg_id);

  for (auto it = ret.first; it != ret.second; ++it) {
    raft_entry_id_t ety_id = it->second;
    auto ccr_p = waiting_response_.find(ety_id);
    if (ccr_p != waiting_response_.end()) {
      auto &ccr = ccr_p->second;
      if (++ccr->n_received_ >= (raft_get_num_nodes(raft_)+1)/2) {
        send_header(bind_socket_, ccr->target_node_);
        zmq_msgpk_send_with_type(bind_socket_, *ccr);
        delete ccr;
        waiting_response_.erase(ety_id);
      }
    }
  }
  id_map_.erase(ret.first,ret.second);
}

static int request_handler(zloop_t *loop, zsock_t *socket, void *udata)
{N();
  RequestHandler *rq = (RequestHandler*)udata;
  raft_server_t *raft = rq->raft_;
  raft_node_id_t id = rq->id_;
  int type;
  zmq_msgpk_recv(socket, type);

  switch(type) {
  case RAFT_MSG_REQUESTVOTE_RESPONSE:
    {
      RequestVoteResponse rvr(raft);
      zmq_msgpk_recv(socket, rvr);
      raft_requestvote_resp_t m;
      rvr.restore(m);
      raft_recv_requestvote_response(raft, raft_get_node(raft, id), &m);
      break;
    }
  case RAFT_MSG_APPENDENTRIES_RESPONSE:
    {
      AppendEntriesResponse aer(raft);
      zmq_msgpk_recv(socket, aer);
      raft_appendentries_resp_t m;
      aer.restore(m);
      raft_recv_appendentries_response(raft, raft_get_node(raft, id), &m);
      if (aer.success_) {
        Server *sv = rq->server_;
        sv->send_response_to_clinet(aer.msg_id_);
      }
      break;
    }
  default:
    printf("type=%d\n",type);
    abort();
  }
  rq->respond();
  return 0;
}


static int router_handler(zloop_t *loop, zsock_t *socket, void *udata)
{N();
  Server *sv = (Server*)udata;
  raft_server_t *raft = sv->raft_;
  raft_node_id_t id;
  size_t len;

  // receive node ID
  zmq_msg_t msg_id;
  int rc, more;
  rc = zmq_msg_init(&msg_id);
  ZERR(rc!=0);
  rc = zmq_msg_recv(&msg_id, socket, 0);
  ZERR(rc==-1);
  len = zmq_msg_size(&msg_id);
  ZERR(rc==-1);
  msgpack::object_handle hd = msgpack::unpack(static_cast<char*>(zmq_msg_data(&msg_id)), len);
  hd.get().convert(id);

  more = zmq_msg_more(&msg_id);
  //printf("id=%d\n",id);
  assert(more);

  // receive empty delimiter frame
  zmq_msg_t msg_null;
  rc = zmq_msg_init(&msg_null);
  ZERR(rc!=0);
  rc = zmq_msg_recv(&msg_null, socket, 0);
  ZERR(rc==-1);
  len = zmq_msg_size(&msg_null);
  if (len!=0) {
    zmq_msg_t msg;
    rc = zmq_msg_init(&msg);
    ZERR(rc!=0);
    rc = zmq_msg_recv(&msg, socket, 0);
    ZERR(rc==-1);
    size_t next_len = zmq_msg_size(&msg);
    printf("delimiter: len=%zd id=%d this_id=%d next_len=%zd\n",len,id,raft_get_nodeid(raft),next_len);
  }
  assert(len==0);
  more = zmq_msg_more(&msg_null);
  assert(more);

  // receive Raft message ID
  int type;
  zmq_msgpk_recv(socket, type);

  switch(type) {
  case RAFT_MSG_REQUESTVOTE:
    {
      RequestVote rv(raft);
      zmq_msgpk_recv(socket, rv);
      raft_requestvote_req_t mrv;
      rv.restore(mrv);
      raft_requestvote_resp_t mrvr;
      raft_recv_requestvote(raft, raft_get_node(raft, id), &mrv, &mrvr);
      rc = zmq_msg_send(&msg_id, socket, ZMQ_SNDMORE);
      ZERR(rc==-1);
      rc = zmq_msg_send(&msg_null, socket, ZMQ_SNDMORE);
      ZERR(rc==-1);
      RequestVoteResponse rvr(raft, raft_get_node(raft, id), mrvr);
      zmq_msgpk_send_with_type(socket, rvr);
      break;
    }
  case RAFT_MSG_APPENDENTRIES:
    {
      AppendEntries ae(raft);
      zmq_msgpk_recv(socket, ae);
      raft_appendentries_req_t mae;
      ae.restore(mae);
      raft_appendentries_resp_t maer;
      raft_recv_appendentries(raft, raft_get_node(raft, id), &mae, &maer);
      rc = zmq_msg_send(&msg_id, socket, ZMQ_SNDMORE);
      ZERR(rc==-1);
      rc = zmq_msg_send(&msg_null, socket, ZMQ_SNDMORE);
      ZERR(rc==-1);
      AppendEntriesResponse aer(raft, raft_get_node(raft, id), maer);
      zmq_msgpk_send_with_type(socket, aer);
      break;
    }
  case RAFT_MSG_CLIENTREQUEST:
    {
      ClientRequest crq;
      zmq_msgpk_recv(socket, crq);
      if (raft_is_leader(raft) && crq.command_.size() > 0) {
        sv->enq(crq);
      } else {
        raft_node_id_t leader_id = raft_get_leader_id(raft);
        HostData h;
        if (leader_id >= 0) {
          h = sv->host_map_[leader_id];
        }
        //printf("this_id=%d h.hostname_=%s h.port_=%d h.id_=%d client_id=%d\n",raft_get_nodeid(raft),h.hostname_.c_str(),h.port_,h.id_,id);
        rc = zmq_msg_send(&msg_id, socket, ZMQ_SNDMORE);
        ZERR(rc==-1);
        rc = zmq_msg_send(&msg_null, socket, ZMQ_SNDMORE);
        ZERR(rc==-1);
        zmq_msgpk_send_with_type(socket, h);
      }
      break;
    }
  default:
    printf("type=%d\n",type);
    abort();
  }
  return 0;
}


static int __raft_requestvote(raft_server_t* raft, void* udata,
  raft_node_t* node, raft_requestvote_req_t* msg)
{N();
  raft_node_id_t id = raft_node_get_id(node);
  Server *sv = (Server*)udata;
  RequestHandler *rq = sv->get_request_handler(id);
  RequestVote rv(raft, node, *msg);
  rq->send(rv);
  return 0;
}

static int __raft_appendentries(raft_server_t* raft, void* udata,
  raft_node_t* node, raft_appendentries_req_t* msg)
{N();
  raft_node_id_t id = raft_node_get_id(node);
  Server *sv = (Server*)udata;
  RequestHandler *rq = sv->get_request_handler(id);
  AppendEntries ae(raft, node, *msg);
  for (auto &ety : ae.entries_) {
    sv->id_map_.insert(std::make_pair(ae.msg_id_, ety.id_));
  }
  rq->send(ae);
  return 0;
}

static int __raft_persist_term(raft_server_t* raft, void *udata, raft_term_t term, int vote)
{
  return 0;
}

static int __raft_persist_vote(raft_server_t* raft, void *udata, int vote)
{
  return 0;
}

static int __raft_applylog(raft_server_t* raft, void *udata, raft_entry_t *ety, raft_index_t idx)
{
  return 0;
}

#define PERIOD_MSEC 50

static int timer_count=0;
static raft_entry_id_t entry_id=0;

static int s_timer_event(zloop_t *loop, int timer_id, void *udata)
{
  Server *sv = (Server*)udata;
  raft_server_t *raft = sv->raft_;
  raft_periodic(raft, PERIOD_MSEC);
  return 0;
  // leader down test
  if (raft_get_nodeid(raft)==raft_get_leader_id(raft)) {
    ++timer_count;
    if (timer_count % 60 == 0) {
      printf("--- entry ----\n");
      std::string data("abcdefghij");
      unsigned int data_len = (unsigned int)(data.size());
      raft_entry_t *e = raft_entry_new(data_len);
      e->term = raft_get_current_term(raft);
      e->id = ++entry_id;
      e->type = 99;
      e->refs = 1;
      e->free_func = 0;
      memcpy(e->data, data.c_str(), data_len);
      raft_entry_resp_t mer;
      int rc;
      rc = raft_recv_entry(raft, e, &mer);
      EntryResponse er(raft, mer);
      er.print_send();
      raft_entry_release(e);
    }
    /*
    if (timer_count>=60) {
      printf("node %d end\n",raft_get_nodeid(raft));
      sv->stop();
      timer_count = 0;
    }
     */
  }
  return 0;
}


void Server::setup(std::vector<HostData> &hosts) {
  // new Raft server
  raft_cbs_t funcs = {
    .send_requestvote = __raft_requestvote,
    .send_appendentries = __raft_appendentries,
    .applylog = __raft_applylog,
    .persist_term = __raft_persist_term,
    .log = NULL
  };
  raft_ = raft_new();
  raft_set_callbacks(raft_, &funcs, this);
  raft_set_election_timeout(raft_, 1000);
  raft_set_request_timeout(raft_, 1000);
  assert(0 == raft_get_timeout_elapsed(raft_));
  assert(0 == raft_get_log_count(raft_));

  // new zloop
  loop_ = zloop_new();
  assert (loop_);
  //zloop_set_verbose(loop, true);

  int rc;
  for (auto h : hosts) {
    raft_node_id_t id = h.id_;
    host_map_.emplace(id, h);
    if (id_ == id) {
      raft_add_node(raft_, NULL, id, 1);
      bind_socket_ = (zsock_t*)zmq_socket(context_, ZMQ_ROUTER);
      assert(bind_socket_);
      int mandatory=1;
      rc = zmq_setsockopt(bind_socket_, ZMQ_ROUTER_MANDATORY, &mandatory, sizeof(int));
      ZERR(rc!=0);
      std::string url = h.bind_url();
      for (int i=0; i<5; ++i) {
        rc = zmq_bind(bind_socket_, url.c_str());
        if (rc==0) break;
        usleep(100000);
      }
      printf("%d:bind %s\n", id_, url.c_str());
      ZERR(rc!=0);
      zloop_reader(loop_, bind_socket_, router_handler, this);
    } else {
      raft_add_node(raft_, NULL, id, 0);
      zsock_t *sock = (zsock_t*)zmq_socket(context_, ZMQ_REQ);
      msgpack::sbuffer packed;
      msgpack::pack(&packed, id_);
      rc = zmq_setsockopt(sock, ZMQ_IDENTITY, packed.data(), packed.size());
      ZERR(rc!=0);
      std::string url = h.connect_url();
      rc = zmq_connect(sock, url.c_str());
      ZERR(rc!=0);
      printf("%d:connect to %d: %s\n", id_, id, url.c_str());
      //add_socket(id, sock);
      zloop_reader(loop_, sock, request_handler, new_request_handler(id,sock));
    }
  }

  /* candidate to leader */
  //if (a->id_==0) {
  //  raft_set_state(raft_, RAFT_STATE_CANDIDATE);
  //  raft_become_leader(raft_);
  //}
  /* I'm the leader */
  //raft_set_state(raft_, RAFT_STATE_LEADER);
  //raft_set_current_term(raft_, 1);
  //raft_set_commit_idx(raft_, 0);
  /* the last applied idx will became 1, and then 2 */
  //raft_set_last_applied_idx(raft_, 0);
}

#define OK 0

static int pipe_handler(zloop_t *loop, zsock_t *pipe, void *udata)
{N();
  static raft_entry_id_t ety_id = 0;
  static short ety_type = 255;
  int client_id;
  uint64_t sequence_num;
  uint64_t tid;
  char *log_set;
  size_t log_size;
  Server *sv = (Server*)udata;

  int rc = zsock_recv(pipe, "i88b", &client_id, &sequence_num, &tid,
    //&nid, sizeof(NotificationId),
    &log_set, &log_size);

  int status = OK;
  if (status == OK) {
    Entry ety(raft_get_current_term(sv->raft_), ety_id, ety_type, log_set, log_size);
    free(log_set);
    raft_entry_t *mety = ety.restore();
    raft_entry_resp_t metyr;
    int rc;
    rc = raft_recv_entry(sv->raft_, mety, &metyr);
    raft_entry_release(mety);
    if (rc!=0)
      status = rc;
    sv->waiting_response_[ety_id] = new ClientRequestResponse(sv->id_, client_id, status);
    ++ety_id;
  }
  if (status != OK) {
    //send_header(sv->bind_socket_, client_id);
    //ClientRequestResponse ccr(sv->id_, client_id, -1);
    //zmq_msgpk_send_with_type(sv->bind_socket_, ccr);
  }
  return 0;
}

void Server::start(zsock_t *pipe) {
  zloop_reader(loop_, pipe, pipe_handler, this);
  //  Create a timer that for raft_periodic
  timer_id_ = zloop_timer(loop_, PERIOD_MSEC, 0, s_timer_event, this);
  zloop_start(loop_);
}

void Server::stop() {
  if (timer_id_) {
    zloop_timer_end(loop_, timer_id_);
    timer_id_ = 0;
  }
  if (bind_socket_) {
    zloop_reader_end(loop_, bind_socket_);
    zmq_close(bind_socket_);
    bind_socket_ = NULL;
  }
  for (auto a : request_handler_) {
    auto rq = a.second;
    zloop_reader_end(loop_, rq->socket_);
    delete rq;
  }
}
