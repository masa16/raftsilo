#include <iostream>
#include <string>
#include <fstream> //for ifstream
#include <sstream> //for istringstream
extern "C" {
  #include <stdbool.h>
  #include <assert.h>
  #include <stdlib.h>
  #include <stdio.h>
  #include <string.h>
  #include <stdint.h>
  // #include <zmq.h>
  // #include <czmq.h>
  #include <sched.h>

  #include "raft.h"
  #include "raft_log.h"
  //#include "raft_private.h"
}
#include "include/common.hh"
#include "include/atomic_tool.hh"
// #include "raft_msgpack.hh"
// #include "raft_server.hh"
// #include "raft_client.hh"
#include "raft_txn.hh"
#include "raft_cc.hh"
#include "raft_sender.hh"
#include "raft_hostdata.hh"
#include "../third_party/ccbench/silo/include/log.hh"

//#define N() do{fprintf(stderr, "%16s %4d %16s\n", __FILE__, __LINE__, __func__); fflush(stderr);} while(0)
#define N() {}


void server_thread(RaftCC *raft_cc, const bool &quit)
{
  Sender sender;
  sender.read_server_data("hosts.dat");
  sender.connect_follower();
  for (auto pipe : raft_cc->sender_pipes_) {
    sender.add_pipe(pipe);
  }
  raft_cc->ready_.store(true);
  //for (auto pipe : raft_cc->logger_pipes_) {
  //  write_ok(pipe.second);
  //}
  while (!sender.quit()) {
    sender.handle_event();
  }
  //sender.quit();
}


void RaftCC::read_server_data(std::string filename) {
  std::ifstream ifs(filename);
  std::string str, hostname, state;
  int id, req_port, pub_port;

  while (std::getline(ifs, str)) {
    std::istringstream iss(str);
    iss >> id >> hostname >> req_port >> pub_port >> state;
    hosts_.emplace_back(id, hostname, req_port, pub_port, state);
    //std::cout << "id=" << id << ", hostname=" << hostname << ", req_port=" << req_port << ", pub_port=" << pub_port << std::endl;
  }
}

void RaftCC::create_pipe(int logger_num) {
  logger_num_ = logger_num;
  send_time_.resize(logger_num,0);
  send_log_count_.resize(logger_num,0);
  send_log_latency_.resize(logger_num,0);
  for (int i=0; i < logger_num; ++i) {
    int pipe_out[2], pipe_in[2];
    int r = pipe(pipe_out);
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
    logger_pipes_.emplace_back(std::pair{pipe_in[0],pipe_out[1]});
    sender_pipes_.emplace_back(std::pair{pipe_out[0],pipe_in[1]});
  }
}


int RaftCC::start(int id, const bool &quit)
{
  ready_.store(false);
  quit_.store(false);
  leader_id_ = id;
  server_threads_.emplace_back(server_thread, this, quit);
  while (!ready_.load()) {
    _mm_pause();
  }
  return 0;
}


void RaftCC::quit()
{
  quit_.store(true);
  pipe_data_t pipe_dat = {NULL, 0};
  for (auto pipe : logger_pipes_) {
    printf("write quit to %d\n", pipe.second);
    int ret = write(pipe.second, &pipe_dat, sizeof(pipe_dat));
    if (ret != sizeof(pipe_dat)) {
      perror("write pipe error ");
      abort();
    }
  }
}

void RaftCC::end()
{
  for (auto &th : server_threads_) th.join();
  uint64_t send_log_count = 0;
  uint64_t send_log_latency = 0;
  for (size_t i=0; i < send_log_count_.size(); ++i) {
    send_log_count += send_log_count_[i];
    send_log_latency += send_log_latency_[i];
  }
  if (send_log_count>0) {
    cout << "raft_send_log_count:\t" << send_log_count << endl;
    double t = (double)send_log_latency / FLAGS_clocks_per_us / send_log_count;
    cout << std::fixed << std::setprecision(4) << "raft_send_log_latency[ms]:\t" << t/1000 << endl;
  }
}


raft_node_id_t RaftCC::wait_leader_elected()
{
  return leader_id_;
}


void RaftCC::send_log(void *log_set, size_t log_size, size_t thid)
{
  static int client_id = 0;
  static uint64_t sequence_num = 0;
  static uint64_t tid = 0;
  //raft_node_id_t leader_id = wait_leader_elected();
  int rc;
  send_time_[thid] = rdtscp();

  pipe_data_t pipe_dat = {log_set, log_size};
  //return;
  rc = write(logger_pipes_[thid].second, &pipe_dat, sizeof(pipe_dat));
  //printf("thid=%zd write %d bytes wfd=%d\n", thid, rc, logger_pipes_[thid].second);
  if (rc != sizeof(pipe_dat)) {
    perror("write pipe error ");
    abort();
  }
}


void RaftCC::send_quit(size_t thid)
{
  pipe_data_t pipe_dat = {NULL, 0};
  int rc = write(logger_pipes_[thid].second, &pipe_dat, sizeof(pipe_dat));
  //printf("write quit to %d\n", logger_pipes_[thid].second);
  if (rc != sizeof(pipe_dat)) {
    perror("write quit error ");
    abort();
  }
  //close(logger_pipes_[thid].second);
}


void RaftCC::recv_rep(size_t thid)
{
  //raft_node_id_t leader_id = wait_leader_elected();

  //printf("thid=%zd recv_rep1 rfd=%d wfd=%d\n", thid, logger_pipes_[thid].first, logger_pipes_[thid].second);
  read_ok(logger_pipes_[thid].first);
  //printf("thid=%zd recv_rep2 rfd=%d wfd=%d\n", thid, logger_pipes_[thid].first, logger_pipes_[thid].second);

  send_log_latency_[thid] += rdtscp() - send_time_[thid];
  send_log_count_[thid] ++;
}
