#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cassert>
extern "C" {
  #include <zmq.h>
  namespace czmq {
    using ::byte;
    #include <czmq.h>
  }
  #include "raft.h"
}
using namespace czmq;
#include "raft_msgpack.hh"
#include "raft_cc.hh"
#include "tx_queue.hh"
#include "include/log_writer.hh"

//#define N() do{fprintf(stderr, "%16s %4d %16s\n", __FILE__, __LINE__, __func__); fflush(stderr);} while(0)
#define N() {}

class Server;

class RequestHandler {
public:
  Server *server_;
  raft_server_t *raft_;
  raft_node_id_t id_;
  zsock_t *socket_;
  bool sendable_ = true;
  std::deque<msgpack::sbuffer*> packs_;

  RequestHandler(Server *sv, raft_server_t *raft, raft_node_id_t id, zsock_t *socket) :
    server_(sv), raft_(raft), id_(id), socket_(socket) {}

  ~RequestHandler() {
    //for (auto packed : packs_) delete packed;
    zmq_close(socket_);
  }

  template<typename T> void send(T &data) {
    if (sendable_) {
      sendable_ = false;
      zmq_msgpk_send_with_type(socket_, data);
    } else {
      zmq_msgpk_pack_with_type(packs_, data);
    }
  }

  void respond() {
    if (packs_.empty()) {
      sendable_ = true;
    } else {
      sendable_ = false;
      zmq_msgpk_send_pack(socket_, packs_);
    }
  }
};


class Server {
public:
  int id_;
  void *context_;
  raft_server_t *raft_ = NULL;
  zsock_t *bind_socket_ = NULL;
  zsock_t *publisher_socket_ = NULL;
  std::map<raft_node_id_t,zsock_t*> subscriber_sockets_;
  zloop_t *loop_ = NULL;
  int timer_id_ = 0;
  std::map<raft_node_id_t,HostData> host_map_;
  std::map<raft_node_id_t,RequestHandler*> request_handler_;
  TxQueue *tx_queue_;
  std::map<raft_entry_id_t,ClientRequestResponse*> waiting_response_;
  std::multimap<raft_msg_id_t,raft_entry_id_t> id_map_;
  PosixWriter logfile_;
  std::string logdir_;
  std::string logpath_;
  std::byte *buffer_data_;
  size_t buffer_tail_ = 0;
  size_t max_buffer_size_ = 512; //*1024;
  std::vector<std::pair<raft_node_id_t,raft_appendentries_resp_t>> ap_resp_vec_;
  std::vector<zsock_t*> receivers_, senders_;
  size_t thread_num_;
  size_t entry_latency_ = 0;
  size_t entry_count_ = 0;

  Server(void *context, int id, TxQueue *tx_queue, size_t thread_num) :
    context_(context), id_(id), tx_queue_(tx_queue), thread_num_(thread_num) {}

  ~Server() {
    //stop();
    //for (auto x : request_handler_) delete x.second;
    //request_handler_.clear();
    zloop_destroy(&loop_);
  }

  void setup(std::vector<HostData> &hosts);
  void start(zsock_t *pipe);
  void stop();
  void send_response_to_clinet(raft_msg_id_t msg_id);
  void applylog(raft_entry_t *entry, raft_index_t idx);

  void receive_entry(char *log_set, size_t log_size);

  RequestHandler *new_request_handler(raft_node_id_t id, zsock_t *sock) {
    RequestHandler *m = new RequestHandler(this, raft_, id, sock);
    request_handler_[id] = m;
    return m;
  }

  RequestHandler *get_request_handler(raft_node_id_t id) {
    return request_handler_[id];
  }

  void enq(ClientRequest &tx)
  {
    tx_queue_->push(tx);
  }

  void display(size_t clocks_per_us);
};
