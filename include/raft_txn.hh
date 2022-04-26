#pragma once

extern "C" {
  #include "raft.h"
}
#include "raft_msgpack.hh"
#include "raft_cc.hh"
#include "../third_party/ccbench/silo/include/transaction.hh"
#include "../third_party/ccbench/include/result.hh"
//#if DURABLE_EPOCH
//#include "log_buffer.hh"
//#include "notifier.hh"
//#endif

class RaftTxnExecutor : public TxnExecutor {
public:
  //Result *sres_lg_;
  //LogBufferPool log_buffer_pool_;
  //NotificationId nid_;
  std::uint32_t nid_counter_ = 0; // Notification ID
  int logger_thid_;
  RaftCC *raft_cc_;
  raft_node_id_t client_id_;
  long sequence_num_;

  RaftTxnExecutor(int thid, Result *sres, RaftCC *raft_cc) :
    TxnExecutor(thid, sres), raft_cc_(raft_cc) {}

  void begin(int client_id, long sequence_num);
  void wal(std::uint64_t ctid) override;
  /*
  bool pauseCondition();
  void epochWork(uint64_t &epoch_timer_start,
                 uint64_t &epoch_timer_stop);
  void durableEpochWork(uint64_t &epoch_timer_start,
                        uint64_t &epoch_timer_stop, const bool &quit);
   */
};
