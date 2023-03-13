#pragma once
#include <cassert>
#include <vector>
extern "C" {
  #include <zmq.h>
  #include <czmq.h>
  #include "raft.h"
  #include "raft_log.h"
}
#include "../third_party/ccbench/silo/include/log.hh"
#include "raft_msgpack.hh"
#include "raft_server.hh"
#include "tx_queue.hh"

#define HAS_CLIENT 0

class Server;

class ServerData {
public:
  raft_node_id_t id_;
  raft_server_t *raft_;
  std::vector<HostData> &hosts_;
  void *context_;
  zactor_t *actor_;
  TxQueue *tx_queue_;
  Server *server_;

  ServerData(raft_node_id_t id, std::vector<HostData> &hosts, void *context, TxQueue *tx_queue) :
    id_(id), hosts_(hosts), context_(context), tx_queue_(tx_queue) {}
  ~ServerData() {
    destroy();
  }

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
    const char s[] = "END";
    zsock_send(actor_, "s", s);
    zactor_destroy(&actor_);
  }
};

class RaftCC {
public:
  TxQueue tx_queue_;
  void *context_;
  std::vector<HostData> hosts_;
  std::vector<ServerData*> serverdata_;
  std::map<raft_node_id_t,zactor_t*> actors_;
  std::map<raft_node_id_t,Server*> servers_;
  std::mutex mutex_;
  uint64_t send_time_;
  uint64_t send_log_count_ = 0;
  uint64_t send_log_latency_ = 0;

  raft_node_id_t wait_leader_elected();
  void read_server_data(std::string filename);
  int start(int id);
  void end();
  void send_log(int client_id, long sequence_num, std::uint64_t tid, //NotificationId &nid,
    std::vector<LogRecord> &log_set, size_t thid);
  void send_log(void *log_set, size_t log_size, size_t thid);
  void recv_rep(size_t thid);
  void send_quit(size_t thid) {}
};
