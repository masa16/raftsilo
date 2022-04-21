#pragma once
#include <string>
#include <vector>
#include <msgpack.hpp>
extern "C" {
  #include "raft.h"
  #include "raft_log.h"
  #include "raft_private.h"
  #include <zmq.h>
}

#if 0
typedef int                raft_entry_id_t;
typedef long int           raft_term_t;
typedef long int           raft_index_t;
typedef unsigned long long raft_size_t;
typedef int                raft_node_id_t;
typedef unsigned long      raft_msg_id_t;
#endif

typedef enum {
  RAFT_MSG_REQUESTVOTE,
  RAFT_MSG_REQUESTVOTE_RESPONSE,
  RAFT_MSG_APPENDENTRIES,
  RAFT_MSG_APPENDENTRIES_RESPONSE,
  RAFT_MSG_ENTRY,
  RAFT_MSG_ENTRY_RESPONSE,
  RAFT_MSG_CLIENTREQUEST,
  RAFT_MSG_CLIENTREQUEST_RESPONSE,
  RAFT_MSG_LEADER_HINT,
} raft_message_type_e;


static char _state_char(raft_server_t *raft)
{
  switch(raft_get_state(raft)) {
  //case RAFT_STATE_NONE: return 'N';
  case RAFT_STATE_FOLLOWER: return 'F';
  case RAFT_STATE_PRECANDIDATE: return 'P';
  case RAFT_STATE_CANDIDATE: return 'C';
  case RAFT_STATE_LEADER: return 'L';
  }
  return ' ';
}


class ClientRequest
{
public:
  static const int MSG_TYPE = RAFT_MSG_CLIENTREQUEST;
  raft_node_id_t   source_node_;
  raft_node_id_t   target_node_;
  long             sequence_num_;
  std::string      command_;
  MSGPACK_DEFINE(source_node_,target_node_,command_);

  ClientRequest() {}
  ClientRequest(raft_node_id_t source_node, raft_node_id_t target_node, std::string command) :
    source_node_(source_node), target_node_(target_node), command_(command) {}
  void print_send() {
    printf("send CR   %d->%d: \"%s\"\n", source_node_, target_node_, command_.c_str());
  }
  void print_recv() {
    printf("recv CR   %d->%d: \"%s\"\n", source_node_, target_node_, command_.c_str());
  }
};

const int ClientRequest::MSG_TYPE;

class ClientRequestResponse
{
public:
  static const int MSG_TYPE = RAFT_MSG_CLIENTREQUEST_RESPONSE;
  raft_node_id_t   source_node_;
  raft_node_id_t   target_node_;
  int              status_;
  std::string      leader_hint_;
  MSGPACK_DEFINE(source_node_,target_node_,status_);

  ClientRequestResponse() {}
  ClientRequestResponse(raft_node_id_t source_node, raft_node_id_t target_node, int status) :
    source_node_(source_node), target_node_(target_node), status_(status) {}
  void print_send() {
    printf("send CRR   %d->%d: status_=%d\n", source_node_, target_node_, status_);
  }
  void print_recv() {
    printf("recv CRR   %d->%d: status_=%d\n", source_node_, target_node_, status_);
  }
};

const int ClientRequestResponse::MSG_TYPE;


class Entry
{
public:
  static const int MSG_TYPE = RAFT_MSG_ENTRY;
  raft_term_t     term_;
  raft_entry_id_t id_;
  short           type_;
  std::string     data_;
  MSGPACK_DEFINE(term_, id_, type_, data_);
  Entry() {}
  Entry(raft_term_t term, raft_entry_id_t id, short type, std::string data) :
    term_(term), id_(id), type_(type), data_(data) {}
  Entry(raft_entry_t *e) {
    term_ = e->term;
    id_   = e->id;
    type_ = e->type;
    data_ = std::string(e->data, e->data_len);
  }
  raft_entry_t *restore() {
    unsigned int data_len = (unsigned int)(data_.size());
    raft_entry_t *e = raft_entry_new(data_len);
    e->term = term_;
    e->id = id_;
    e->type = type_;
    e->refs = 1;
    e->free_func = 0;
    memcpy(e->data, data_.c_str(), data_len);
    return e;
  }
  void print(int i) {
    printf(" entries[%d]: term=%ld id=%d type=%hd data=\"%s\"\n",i,
      term_, id_, type_, data_.c_str());
  }
};


class EntryResponse
{
private:
  raft_server_t * raft_ = NULL;
public:
  static const int MSG_TYPE = RAFT_MSG_ENTRY_RESPONSE;
  raft_node_id_t source_node_;
  raft_node_id_t target_node_=0;
  raft_entry_id_t id_;
  raft_term_t term_;
  raft_index_t idx_;
  MSGPACK_DEFINE(/*source_node_, target_node_,*/ id_, term_, idx_);
  EntryResponse() {}
  EntryResponse(raft_server_t *raft) : raft_(raft) {}
  EntryResponse(raft_server_t *raft, /*raft_node_t *raft_node,*/ raft_entry_resp_t &m) {
    raft_ = raft;
    source_node_  = raft_get_nodeid(raft);
    //target_node_  = raft_node_get_id(raft_node);
    id_   = m.id;
    term_ = m.term;
    idx_  = m.idx;
  }
  void restore(raft_entry_resp_t &m) {
    m.id = id_;
    m.term = term_;
    m.idx = idx_;
  }
  void print() {
    printf("id=%d term=%ld idx=%ld\n",
      id_, term_, idx_);
  }
  void print_send() {
    printf("send EtR %d%c->%d:", source_node_, _state_char(raft_), target_node_);
    print();
  }
  void print_recv() {
    printf("recv EtR %d->%d:", source_node_, target_node_);
    print();
  }
};

const int EntryResponse::MSG_TYPE;


class HostData {
public:
  static const int MSG_TYPE = RAFT_MSG_LEADER_HINT;
  raft_node_id_t id_ = -1;
  std::string hostname_;
  std::uint32_t port_ = 0;
  MSGPACK_DEFINE(id_, hostname_, port_);

  HostData() {};
  HostData(raft_node_id_t id, std::string hostname, std::uint32_t port) :
    id_(id), hostname_(hostname), port_(port) {}

  std::string connect_url() {
    std::string url = "tcp://";
    url.append(hostname_);
    url.append(":");
    url.append(std::to_string(port_));
    return url;
  }
  std::string bind_url() {
    std::string url = "tcp://*:";
    url.append(std::to_string(port_));
    return url;
  }

  void print() {
    printf("id=%d hostname=%s port=%d\n",
      id_, hostname_.c_str(), port_);
  }
  void print_send() {
    printf("send HostData :");
    print();
  }
  void print_recv() {
    printf("recv HostData :");
    print();
  }
};

const int HostData::MSG_TYPE;


class AppendEntries
{
private:
  raft_server_t * raft_ = NULL;
public:
  static const int MSG_TYPE = RAFT_MSG_APPENDENTRIES;
  raft_node_id_t source_node_;
  raft_node_id_t target_node_;
  raft_node_id_t leader_id_;
  raft_term_t    term_;
  raft_index_t   prev_log_idx_;
  raft_term_t    prev_log_term_;
  raft_index_t   leader_commit_;
  raft_msg_id_t  msg_id_;
  std::vector<Entry> entries_;
  MSGPACK_DEFINE(source_node_, target_node_, leader_id_, term_,
    prev_log_idx_, prev_log_term_, leader_commit_,
    msg_id_, entries_);
  AppendEntries(raft_server_t *raft) : raft_(raft) {}
  AppendEntries(raft_server_t *raft, raft_node_t *raft_node, raft_appendentries_req_t &m) {
    raft_ = raft;
    source_node_   = raft_get_nodeid(raft);
    target_node_   = raft_node_get_id(raft_node);
    leader_id_     = m.leader_id;
    term_          = m.term;
    prev_log_idx_  = m.prev_log_idx;
    prev_log_term_ = m.prev_log_term;
    leader_commit_ = m.leader_commit;
    msg_id_        = m.msg_id;
    for (int i=0; i < m.n_entries; i++) {
      entries_.emplace_back(m.entries[i]);
    }
  }
  void restore(raft_appendentries_req_t &m) {
    int n;
    m.leader_id     = leader_id_;
    m.term          = term_;
    m.prev_log_idx  = prev_log_idx_;
    m.prev_log_term = prev_log_term_;
    m.leader_commit = leader_commit_;
    m.msg_id        = msg_id_;
    m.n_entries = n = entries_.size();
    m.entries       = (raft_entry_t**)raft_malloc(sizeof(raft_entry_t*) * n);
    for (int i=0; i < n; ++i) {
      m.entries[i] = entries_[i].restore();
    }
  }
  void print() {
    raft_node_id_t l = raft_get_leader_id(raft_);
    printf("L=%d leader_id=%d msg_id=%lu term=%ld prev_log_idx=%ld prev_log_term=%ld leader_commit=%ld\n",
      l, leader_id_, msg_id_, term_, prev_log_idx_, prev_log_term_, leader_commit_);
    for (int i=0; i<entries_.size(); ++i) {
      entries_[i].print(i);
    }
  }
  void print_send() {
    printf("send AE  %d%c->%d:", source_node_, _state_char(raft_), target_node_);
    print();
  }
  void print_recv() {
    printf("recv AE  %d->%d%c:", source_node_, target_node_, _state_char(raft_));
    print();
  }
};

const int AppendEntries::MSG_TYPE;


class AppendEntriesResponse
{
private:
  raft_server_t * raft_ = NULL;
public:
  static const int MSG_TYPE = RAFT_MSG_APPENDENTRIES_RESPONSE;
  raft_node_id_t source_node_;
  raft_node_id_t target_node_;
  raft_msg_id_t  msg_id_;
  raft_term_t    term_;
  int            success_;
  raft_index_t   current_idx_;
  MSGPACK_DEFINE(source_node_, target_node_, msg_id_, term_, success_, current_idx_);
  AppendEntriesResponse(raft_server_t *raft) : raft_(raft) {}
  AppendEntriesResponse(raft_msg_id_t msg_id, raft_term_t term, int success, raft_index_t current_idx) :
    msg_id_(msg_id),
    term_(term),
    success_(success),
    current_idx_(current_idx) {}
  AppendEntriesResponse(raft_server_t *raft, raft_node_t *raft_node, raft_appendentries_resp_t &m) {
    raft_ = raft;
    source_node_ = raft_get_nodeid(raft);
    target_node_ = raft_node_get_id(raft_node);
    msg_id_      = m.msg_id;
    term_        = m.term;
    success_     = m.success;
    current_idx_ = m.current_idx;
  }
  void restore(raft_appendentries_resp_t &m) {
    m.msg_id      = msg_id_;
    m.term        = term_;
    m.success     = success_;
    m.current_idx = current_idx_;
  }
  void print() {
    raft_node_id_t l = raft_get_leader_id(raft_);
    printf("L=%d msg_id=%lu term=%ld success=%d current_idx=%ld\n",
      l, msg_id_, term_, success_, current_idx_);
  }
  void print_send() {
    printf("send AER %d%c->%d:", source_node_, _state_char(raft_), target_node_);
    print();
  }
  void print_recv() {
    printf("recv AER %d->%d%c:", source_node_, target_node_, _state_char(raft_));
    print();
  }
};

const int AppendEntriesResponse::MSG_TYPE;


class RequestVote
{
private:
  raft_server_t * raft_ = NULL;
public:
  static const int MSG_TYPE = RAFT_MSG_REQUESTVOTE;
  raft_node_id_t source_node_;
  raft_node_id_t target_node_;
  int            prevote_;
  raft_term_t    term_;
  raft_node_id_t candidate_id_;
  raft_index_t   last_log_idx_;
  raft_term_t    last_log_term_;
  //int            transfer_leader_;
  MSGPACK_DEFINE(source_node_, target_node_, prevote_, term_, candidate_id_, last_log_idx_, last_log_term_);
  RequestVote(raft_server_t *raft) : raft_(raft) {}
  RequestVote(raft_server_t *raft, raft_node_t *raft_node, raft_requestvote_req_t &m) {
    raft_ = raft;
    source_node_     = raft_get_nodeid(raft);
    target_node_     = raft_node_get_id(raft_node);
    prevote_         = m.prevote;
    term_            = m.term;
    candidate_id_    = m.candidate_id;
    last_log_idx_    = m.last_log_idx;
    last_log_term_   = m.last_log_term;
    //transfer_leader_ = m.transfer_leader;
  }
  void restore(raft_requestvote_req_t &m) {
    m.prevote         = prevote_;
    m.term            = term_;
    m.candidate_id    = candidate_id_;
    m.last_log_idx    = last_log_idx_;
    m.last_log_term   = last_log_term_;
    //m.transfer_leader = transfer_leader_;
  }
  void print() {
    raft_node_id_t l = raft_get_leader_id(raft_);
    printf("L=%d prevote=%d term=%ld candidate_id=%d last_log_idx=%ld last_log_term=%ld\n",
      l, prevote_, term_, candidate_id_, last_log_idx_, last_log_term_);
  }
  void print_send() {
    printf("send RV  %d%c->%d:", source_node_, _state_char(raft_), target_node_);
    print();
  }
  void print_recv() {
    printf("recv RV  %d->%d%c:", source_node_, target_node_, _state_char(raft_));
    print();
  }
};

const int RequestVote::MSG_TYPE;


class RequestVoteResponse
{
private:
  raft_server_t * raft_ = NULL;
public:
  static const int MSG_TYPE = RAFT_MSG_REQUESTVOTE_RESPONSE;
  raft_node_id_t source_node_;
  raft_node_id_t target_node_;
  int            prevote_;
  raft_term_t    request_term_;
  raft_term_t    term_;
  int            vote_granted_;
  MSGPACK_DEFINE(source_node_, target_node_, prevote_, request_term_, term_, vote_granted_);
  RequestVoteResponse(raft_server_t *raft) : raft_(raft) {}
  RequestVoteResponse(raft_server_t *raft, raft_node_t *raft_node, raft_requestvote_resp_t &m) {
    raft_ = raft;
    source_node_  = raft_get_nodeid(raft);
    target_node_  = raft_node_get_id(raft_node);
    prevote_      = m.prevote;
    request_term_ = m.request_term;
    term_         = m.term;
    vote_granted_ = m.vote_granted;
  }
  void restore(raft_requestvote_resp_t &m) {
    m.prevote      = prevote_;
    m.request_term = request_term_;
    m.term         = term_;
    m.vote_granted = vote_granted_;
  }
  void print() {
    raft_node_id_t l = raft_get_leader_id(raft_);
    printf("L=%d prevote=%d request_term=%ld term=%ld vote_granted=%d\n",
      l, prevote_, request_term_, term_, vote_granted_);
  }
  void print_send() {
    printf("send RVR %d%c->%d:", source_node_, _state_char(raft_), target_node_);
    print();
  }
  void print_recv() {
    printf("recv RVR %d->%d%c:", source_node_, target_node_, _state_char(raft_));
    print();
  }
};

const int RequestVoteResponse::MSG_TYPE;


template <class T>
  auto print_send(T a) -> decltype(a.print_send())
{
  a.print_send();
}
auto print_send(...) -> void
{}

template <class T>
  auto print_recv(T a, int more) -> decltype(a.print_recv())
{
  if (more) printf("more=%d,",more);
  a.print_recv();
}
auto print_recv(...) -> void
{}

template <typename T>
  void zmq_msgpk_send(void* socket, T& data, int flag)
{
  //print_send(data);
  msgpack::sbuffer packed;
  msgpack::pack(&packed, data);
  //size_t len = packed.size();
  //unsigned char *buf = (unsigned char *)packed.data();
  //fprintf(stderr,"send(%zd):",len); for(size_t i=0; i<len; i++) fprintf(stderr," %02x",buf[i]); fprintf(stderr,"\n"); fflush(stderr);
  int rc, sz;
  zmq_msg_t msg;
  rc = zmq_msg_init_size(&msg, packed.size());
  assert(rc==0);
  std::memcpy(zmq_msg_data(&msg), packed.data(), packed.size());
  sz = zmq_msg_size(&msg);
  rc = zmq_msg_send(&msg, socket, flag);
  //printf("rc=%d zmq_msg_size(&msg)=%d flag=%d socket=%p\n",rc,sz,flag,socket);
  if (rc==-1) {fprintf(stderr,"zmq_msgpk_send: %s\n",zmq_strerror(zmq_errno()));}
    //fprintf(stderr,"zmq_errno=%d - ",zmq_errno());perror("zmq_msgpk_send");}
  if (flag==0) assert(rc==sz);
}

/*
template <typename T>
  void zmq_msgpk_send_with_type(void* socket, T& data, raft_message_type_e mtype)
{
  int type = mtype;
  zmq_msgpk_send(socket, type, ZMQ_SNDMORE);
  zmq_msgpk_send(socket, data);
}
 */

template <typename T>
  auto zmq_msgpk_send_with_type(void* socket, T& data)
{
  zmq_msgpk_send(socket, T::MSG_TYPE, ZMQ_SNDMORE);
  print_send(data);
  zmq_msgpk_send(socket, data, 0);
}


template <typename T>
  void zmq_msgpk_recv(void* socket, T& data)
{
  zmq_msg_t msg;
  int rc;
  rc = zmq_msg_init(&msg);
  assert(rc==0);
  rc = zmq_msg_recv(&msg, socket, 0);
  assert(rc!=-1);
  size_t len = zmq_msg_size(&msg);
  //unsigned char *buffer = static_cast<unsigned char*>(zmq_msg_data(&msg));
  //fprintf(stderr,"recv(%zd):",len); for(size_t i=0; i<len; i++) fprintf(stderr," %02x",(unsigned int)buffer[i]); fprintf(stderr,"\n"); fflush(stderr);
  assert(len>0);
  msgpack::object_handle hd = msgpack::unpack(static_cast<char*>(zmq_msg_data(&msg)), len);
  hd.get().convert(data);
  int more = zmq_msg_more(&msg);
  print_recv(data,more);
  rc = zmq_msg_close(&msg);
  assert(rc==0);
}


template <typename T>
  void zmq_msgpk_pack(std::deque<msgpack::sbuffer*>& packs, T& data)
{
  msgpack::sbuffer *packed = new msgpack::sbuffer;
  msgpack::pack(packed, data);
  packs.push_back(packed);
}

template <typename T>
  auto zmq_msgpk_pack_with_type(std::deque<msgpack::sbuffer*>& packs, T& data)
{
  zmq_msgpk_pack(packs, T::MSG_TYPE);
  print_send(data);
  zmq_msgpk_pack(packs, data);
}

void zmq_msgpk_send_pack(void* socket, std::deque<msgpack::sbuffer*>& packs)
{
  printf("packs.size=%zd\n",packs.size());
  //if (packs.size()>2) printf("\n\n----\n\n");
  for (size_t i=0; i<2; ++i) {
    int rc, sz;
    zmq_msg_t msg;
    msgpack::sbuffer *packed = packs.front();
    packs.pop_front();
    int flag = (i==1) ? 0 : ZMQ_SNDMORE;
    rc = zmq_msg_init_size(&msg, packed->size());
    assert(rc==0);
    std::memcpy(zmq_msg_data(&msg), packed->data(), packed->size());
    sz = zmq_msg_size(&msg);
    rc = zmq_msg_send(&msg, socket, flag);
    //printf("rc=%d zmq_msg_size(&msg)=%d flag=%d socket=%p\n",rc,sz,flag,socket);
    if (rc==-1) {fprintf(stderr,"zmq_msgpk_send: %s\n",zmq_strerror(zmq_errno()));}
    //fprintf(stderr,"zmq_errno=%d - ",zmq_errno());perror("zmq_msgpk_send");}
    if (flag==0) assert(rc==sz);
    delete packed;
  }
}
