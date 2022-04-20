extern "C" {
  #include <stdbool.h>
  #include <assert.h>
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

//#define N() do{fprintf(stderr, "%16s %4d %16s\n", __FILE__, __LINE__, __func__); fflush(stderr);} while(0)
#define N() {}


class ActorData {
public:
  raft_node_id_t id_;
  std::vector<HostData> &hosts_;
  void *context_;
  zactor_t *actor_;

  ActorData(raft_node_id_t id, std::vector<HostData> &hosts, void *context) :
    id_(id), hosts_(hosts), context_(context) {}

  void setup(void (*func)(zsock_t*, void*)) {
    actor_ = zactor_new(func, this);
    //printf("new actor_=%lu\n",(unsigned long)actor_);
    assert(actor_);
    // sync
    char *r;
    zsock_recv(actor_, "s", &r);
    assert(strcmp(r,"READY")==0);
    free(r);
  }
  void start() {
    //printf("start actor_=%lu\n",(unsigned long)actor_);
    const char s[] = "GO";
    zsock_send(actor_, "s", s);
  }
  void destroy() {
    zactor_destroy(&actor_);
  }
};


class RequestHandler {
public:
  raft_server_t *raft_;
  raft_node_id_t id_;
  zsock_t *socket_;
  bool sendable_ = true;
  std::deque<msgpack::sbuffer*> packs_;

  RequestHandler(raft_server_t *raft, raft_node_id_t id, zsock_t *socket) :
    raft_(raft), id_(id), socket_(socket) {}

  ~RequestHandler() {
    for (auto packed : packs_) delete packed;
    zmq_close(socket_);
  }

  template<typename T> void send(T &data) {
    if (sendable_) {
      zmq_msgpk_send_with_type(socket_, data);
      sendable_ = false;
    } else {
      zmq_msgpk_pack_with_type(packs_, data);
    }
  }

  void respond() {
    if (packs_.empty()) {
      sendable_ = true;
    } else {
      zmq_msgpk_send_pack(socket_, packs_);
      sendable_ = false;
    }
  }

};


class Server {
public:
  int id_;
  void *context_;
  raft_server_t *raft_ = NULL;
  zsock_t *bind_socket_ = NULL;
  zloop_t *loop_ = NULL;
  int timer_id_ = 0;
  std::map<raft_node_id_t,HostData> host_map_;
  std::map<raft_node_id_t,RequestHandler*> request_handler_;

  Server(void *context, int id) :
    context_(context), id_(id) {}

  ~Server() {
    stop();
    for (auto x : request_handler_) delete x.second;
    request_handler_.clear();
    zloop_destroy(&loop_);
  }

  void setup(std::vector<HostData> &hosts);

  void start();

  void stop() {
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

  RequestHandler *new_request_handler(raft_node_id_t id, zsock_t *sock) {
    RequestHandler *m = new RequestHandler(raft_, id, sock);
    request_handler_[id] = m;
    return m;
  }

  RequestHandler *get_request_handler(raft_node_id_t id) {
    return request_handler_[id];
  }
};


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
      msg_requestvote_response_t m;
      rvr.restore(m);
      raft_recv_requestvote_response(raft, raft_get_node(raft, id), &m);
      break;
    }
  case RAFT_MSG_APPENDENTRIES_RESPONSE:
    {
      AppendEntriesResponse aer(raft);
      zmq_msgpk_recv(socket, aer);
      msg_appendentries_response_t m;
      aer.restore(m);
      raft_recv_appendentries_response(raft, raft_get_node(raft, id), &m);
      break;
    }
  default:
    printf("type=%d\n",type);
    abort();
  }
  rq->respond();
  return 0;
}


static raft_entry_id_t ety_id = 0;
static short ety_type = 255;

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
  assert(rc==0);
  rc = zmq_msg_recv(&msg_id, socket, 0);
  assert(rc!=-1);
  len = zmq_msg_size(&msg_id);

  msgpack::object_handle hd = msgpack::unpack(static_cast<char*>(zmq_msg_data(&msg_id)), len);
  hd.get().convert(id);

  more = zmq_msg_more(&msg_id);
  //printf("id=%d\n",id);
  assert(more);

  // receive empty delimiter frame
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
      msg_requestvote_t mrv;
      rv.restore(mrv);
      msg_requestvote_response_t mrvr;
      raft_recv_requestvote(raft, raft_get_node(raft, id), &mrv, &mrvr);
      rc = zmq_msg_send(&msg_id, socket, ZMQ_SNDMORE);
      assert(rc!=-1);
      rc = zmq_msg_send(&msg_null, socket, ZMQ_SNDMORE);
      assert(rc!=-1);
      RequestVoteResponse rvr(raft, raft_get_node(raft, id), mrvr);
      zmq_msgpk_send_with_type(socket, rvr);
      break;
    }
  case RAFT_MSG_APPENDENTRIES:
    {
      AppendEntries ae(raft);
      zmq_msgpk_recv(socket, ae);
      msg_appendentries_t mae;
      ae.restore(mae);
      msg_appendentries_response_t maer;
      raft_recv_appendentries(raft, raft_get_node(raft, id), &mae, &maer);
      rc = zmq_msg_send(&msg_id, socket, ZMQ_SNDMORE);
      assert(rc!=-1);
      rc = zmq_msg_send(&msg_null, socket, ZMQ_SNDMORE);
      assert(rc!=-1);
      AppendEntriesResponse aer(raft, raft_get_node(raft, id), maer);
      zmq_msgpk_send_with_type(socket, aer);
      break;
    }
  case RAFT_MSG_CLIENTCOMMAND:
    {
      ClientCommand cmd;
      zmq_msgpk_recv(socket, cmd);
      if (raft_is_leader(raft)) {
        Entry ety(raft_get_current_term(raft), ++ety_id, ety_type, cmd.data_);
        msg_entry_t *mety = ety.restore();
        msg_entry_response_t metyr;
        rc = raft_recv_entry(raft, mety, &metyr);
        ClientCommandResponse ccr(raft_get_nodeid(raft), id, rc);
        rc = zmq_msg_send(&msg_id, socket, ZMQ_SNDMORE);
        assert(rc!=-1);
        rc = zmq_msg_send(&msg_null, socket, ZMQ_SNDMORE);
        assert(rc!=-1);
        zmq_msgpk_send_with_type(socket, ccr);
        raft_entry_release(mety);
      } else {
        raft_node_id_t leader_id = raft_get_leader_id(raft);
        HostData h;
        if (leader_id >= 0) {
          h = sv->host_map_[leader_id];
        }
        printf("this_id=%d h.hostname_=%s h.port_=%d h.id_=%d client_id=%d\n",raft_get_nodeid(raft),h.hostname_.c_str(),h.port_,h.id_,id);
        rc = zmq_msg_send(&msg_id, socket, ZMQ_SNDMORE);
        assert(rc!=-1);
        rc = zmq_msg_send(&msg_null, socket, ZMQ_SNDMORE);
        assert(rc!=-1);
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
  raft_node_t* node, msg_requestvote_t* msg)
{N();
  raft_node_id_t id = raft_node_get_id(node);
  Server *sv = (Server*)udata;
  RequestHandler *rq = sv->get_request_handler(id);
  RequestVote rv(raft, node, *msg);
  rq->send(rv);
  return 0;
}

static int __raft_appendentries(raft_server_t* raft, void* udata,
  raft_node_t* node, msg_appendentries_t* msg)
{N();
  raft_node_id_t id = raft_node_get_id(node);
  Server *sv = (Server*)udata;
  RequestHandler *rq = sv->get_request_handler(id);
  AppendEntries ae(raft, node, *msg);
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
      msg_entry_t *e = raft_entry_new(data_len);
      e->term = raft_get_current_term(raft);
      e->id = ++entry_id;
      e->type = 99;
      e->refs = 1;
      e->free_func = 0;
      memcpy(e->data, data.c_str(), data_len);
      msg_entry_response_t mer;
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
      assert(rc==0);
      std::string url = h.bind_url();
      for (int i=0; i<5; ++i) {
        rc = zmq_bind(bind_socket_, url.c_str());
        if (rc==0) break;
        usleep(200000);
      }
      printf("%d:bind %s\n", id_, url.c_str());
      assert(rc==0);
      zloop_reader(loop_, bind_socket_, router_handler, this);
    } else {
      raft_add_node(raft_, NULL, id, 0);
      zsock_t *sock = (zsock_t*)zmq_socket(context_, ZMQ_REQ);
      msgpack::sbuffer packed;
      msgpack::pack(&packed, id_);
      rc = zmq_setsockopt(sock, ZMQ_IDENTITY, packed.data(), packed.size());
      assert(rc==0);
      std::string url = h.connect_url();
      rc = zmq_connect(sock, url.c_str());
      assert(rc==0);
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

void Server::start() {
  //  Create a timer that will be cancelled
  timer_id_ = zloop_timer(loop_, PERIOD_MSEC, 0, s_timer_event, this);
  zloop_start(loop_);
}
