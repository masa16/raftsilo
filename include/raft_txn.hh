#pragma once

extern "C" {
  #include "raft.h"
}
//#include "raft_msgpack.hh"
#include "raft_cc.hh"
#if DURABLE_EPOCH
#include "include/transaction_lg.hh"
#define TxnExecutor TxnExecutorLg
#define Result ResultLg
#else
#include "include/transaction.hh"
#endif
#include "../include/result.hh"

class RaftTxnExecutor : public TxnExecutor {
public:
  RaftCC *raft_cc_;
  raft_node_id_t client_id_;
  long sequence_num_;

  RaftTxnExecutor(int thid, Result *sres, RaftCC *raft_cc) :
    TxnExecutor(thid, sres), raft_cc_(raft_cc) {}

  void begin(int client_id, long sequence_num);
  void wal(std::uint64_t ctid) override;
};

#if DURABLE_EPOCH
#undef TxnExecutor
#undef Result
#endif
