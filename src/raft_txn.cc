#include <stdio.h>
#include <algorithm>
#include <string>

#include "../third_party/ccbench/silo/include/atomic_tool.hh"
#include "../third_party/ccbench/silo/include/log.hh"
//#if DURABLE_EPOCH
#include "../third_party/ccbench/silo/include/util.hh"
//#endif
#include "../third_party/ccbench/silo/include/transaction.hh"
#include "raft_txn.hh"
//#include "include/notifier.hh"


void RaftTxnExecutor::begin(int client_id, long sequence_num) {
  TxnExecutor::begin();
  //nid_ = NotificationId(, thid_, rdtscp());
  client_id_ = client_id;
  sequence_num_ = sequence_num;
}

void RaftTxnExecutor::wal(std::uint64_t ctid) {
  Tidword old_tid;
  Tidword new_tid;
  old_tid.obj_ = loadAcquire(CTIDW[thid_].obj_);
  new_tid.obj_ = ctid;
  bool new_epoch_begins = (old_tid.epoch != new_tid.epoch);

  for (auto &itr : write_set_) {
    log_set_.emplace_back(ctid, itr.key_, write_val_);
    //latest_log_header_.chkSum_ += log.computeChkSum();
    //++latest_log_header_.logRecNum_;
  }
  //log_buffer_pool_.push(ctid, nid_, write_set_, write_val_, new_epoch_begins);
  raft_cc_->send_log(client_id_, sequence_num_, ctid, log_set_);
  log_set_.clear();

  if (new_epoch_begins) {
    // store CTIDW
    __atomic_store_n(&(CTIDW[thid_].obj_), ctid, __ATOMIC_RELEASE);
  }
}

/*
bool RaftTxnExecutor::pauseCondition() {
  auto dlepoch = loadAcquire(ThLocalDurableEpoch[logger_thid_].obj_);
  return loadAcquire(ThLocalEpoch[thid_].obj_) > dlepoch + FLAGS_epoch_diff;
}

void RaftTxnExecutor::epochWork(uint64_t &epoch_timer_start,
                            uint64_t &epoch_timer_stop) {
  usleep(1);
  if (thid_ == 0) {
    leaderWork(epoch_timer_start, epoch_timer_stop);
  }
  Tidword old_tid;
  old_tid.obj_ = loadAcquire(CTIDW[thid_].obj_);
  // load Global Epoch
  atomicStoreThLocalEpoch(thid_, atomicLoadGE());
  uint64_t new_epoch = loadAcquire(ThLocalEpoch[thid_].obj_);
  if (old_tid.epoch != new_epoch) {
    Tidword tid;
    tid.epoch = new_epoch;
    tid.lock = 0;
    tid.latest = 1;
    // store CTIDW
    __atomic_store_n(&(CTIDW[thid_].obj_), tid.obj_, __ATOMIC_RELEASE);
  }
}

void RaftTxnExecutor::durableEpochWork(uint64_t &epoch_timer_start,
                                   uint64_t &epoch_timer_stop, const bool &quit) {
  std::uint64_t t = rdtscp();
  // pause this worker until Durable epoch catches up
  if (FLAGS_epoch_diff > 0) {
    if (pauseCondition()) {
      log_buffer_pool_.publish();
      do {
        epochWork(epoch_timer_start, epoch_timer_stop);
        if (loadAcquire(quit)) return;
      } while (pauseCondition());
    }
  }
  while (!log_buffer_pool_.is_ready()) {
    epochWork(epoch_timer_start, epoch_timer_stop);
    if (loadAcquire(quit)) return;
  }
  if (log_buffer_pool_.current_buffer_==NULL) std::abort();
  sres_lg_->local_wait_depoch_latency_ += rdtscp() - t;
}
 */
