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


class ServerData {
public:
  raft_node_id_t id_;
  raft_server_t *raft_;
  std::vector<HostData> &hosts_;
  void *context_;
  zactor_t *actor_;
  TxQueue *tx_queue_;

  ServerData(raft_node_id_t id, std::vector<HostData> &hosts, void *context, TxQueue *tx_queue) :
    id_(id), hosts_(hosts), context_(context), tx_queue_(tx_queue) {}

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

class RaftCC {
public:
  TxQueue tx_queue_;
  void *context_;
  std::vector<HostData> hosts_;
  std::vector<ServerData*> servers_;
  std::map<raft_node_id_t,zactor_t*> actors_;
  int start();
  void send_log(int client_id, long sequence_num, std::uint64_t tid, //NotificationId &nid,
    std::vector<LogRecord> &log_set);
};
