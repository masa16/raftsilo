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

enum svd_message_type_e {
  SVD_READY = -3,
  SVD_GO,
  SVD_END,
};

class Server;

class ServerData {
public:
  raft_node_id_t id_;
  raft_server_t *raft_;
  std::vector<HostData> &hosts_;
  void *context_;
  std::thread *thread_;
  int socket_;
  //TxQueue *tx_queue_;
  Server *server_;

  ServerData(raft_node_id_t id, std::vector<HostData> &hosts, void *context) :
    id_(id), hosts_(hosts), context_(context) {}
  ~ServerData() {
    destroy();
  }

  void setup(void (*func)(zsock_t*, ServerData*)) {
    int r;
    int pipe_out[2], pipe_in[2];
    r = pipe(pipe_out);
    //int r = socketpair(AF_UNIX, SOCK_STREAM, SOCK_STREAM, pair);
    if (r == -1) {
      perror("pipe create error ");
      abort();
    }
    r = pipe(pipe_in);
    if (r == -1) {
      perror("pipe create error ");
      abort();
    }
    pipes_.emplace_back({pipe_out[0],pipe_in[1]});
    //std::thread th(worker, pipes);

    //actor_ = zactor_new(func, this);
    thread_ = new std::thread(func, pipes);
    //printf("new actor_=%lu\n",(unsigned long)actor_);
    assert(thread_);
    // sync
    int code;
    int r = recv(socket_, &code, sizeof(int));
    assert(code==SVD_READY);
  }

  void start() {
    //printf("start actor_=%lu\n",(unsigned long)actor_);
    int code = SVD_GO;
    int r == send(socket_, &code, sizeof(int));
  }

  void destroy() {
    int code = SVD_END;
    int r == send(socket_, &code, sizeof(int));
    thread_->join();
  }
};
