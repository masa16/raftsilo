#pragma once
#include <cassert>
#include <vector>
extern "C" {
  // #include <zmq.h>
  // #include <czmq.h>
  #include "raft.h"
  #include "raft_log.h"
}
#include "../third_party/ccbench/silo/include/log.hh"
// #include "raft_msgpack.hh"
// #include "raft_server.hh"
// #include "tx_queue.hh"
#include "raft_hostdata.hh"

class RaftCC {
public:
  int logger_num_;
  int leader_id_;
  std::vector<HostData> hosts_;
  std::vector<uint64_t> send_time_;
  std::vector<uint64_t> send_log_count_;
  std::vector<uint64_t> send_log_latency_;
  std::vector<std::pair<int,int>> logger_pipes_;
  std::vector<std::pair<int,int>> sender_pipes_;
  std::vector<std::thread> server_threads_;
  std::atomic<bool> ready_;
  std::atomic<bool> quit_;

  raft_node_id_t wait_leader_elected();
  void read_server_data(std::string filename);
  void create_pipe(int logger_num);
  int start(int id, const bool &quit);
  void quit();
  void end();
  void send_log(void *log_set, size_t log_size, size_t thid);
  void send_quit(size_t thid);
  void recv_rep(size_t thid);
};
