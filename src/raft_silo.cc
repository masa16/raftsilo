#include <ctype.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>

#include "boost/filesystem.hpp"

#define GLOBAL_VALUE_DEFINE

#include "include/atomic_tool.hh"
#include "include/common.hh"
#include "include/util.hh"
#if DURABLE_EPOCH
#include "include/common_lg.hh"
#include "include/util_lg.hh"
#include "include/result_lg.hh"
#include "include/silo_result.hh"
#include "include/transaction_lg.hh"
#include "include/logger.hh"
#include "include/notifier.hh"
#else
#include "../silo/include/result.hh"
#endif

#include "../include/atomic_wrapper.hh"
#include "../include/backoff.hh"
#include "../include/cpu.hh"
#include "../include/debug.hh"
#include "../include/fileio.hh"
#include "../include/masstree_wrapper.hh"
#include "../include/random.hh"
#include "../include/tsc.hh"
#include "../include/util.hh"
#include "../include/zipf.hh"

#include "raft_server.hh"
#include "raft_cc.hh"
#include "raft_txn.hh"

#if DURABLE_EPOCH
void worker(size_t thid, char &ready, const bool &start, const bool &quit, std::atomic<Logger*> *logp, RaftCC *raft_cc)
{
  ResultLg &myres_lg = std::ref(SiloResult[thid]);
  Result &myres = std::ref(myres_lg.result_);
  RaftTxnExecutor trans(thid, (ResultLg *) &myres_lg, raft_cc);
#else
void worker(size_t thid, char &ready, const bool &start, const bool &quit, RaftCC *raft_cc)
{
  Result &myres = std::ref(SiloResult[thid]);
  RaftTxnExecutor trans(thid, (Result *) &myres, raft_cc);
#endif
  Xoroshiro128Plus rnd;
  rnd.init();
  FastZipf zipf(&rnd, FLAGS_zipf_skew, FLAGS_tuple_num);
  uint64_t epoch_timer_start, epoch_timer_stop;
#if BACK_OFF
  Backoff backoff(FLAGS_clocks_per_us);
#endif

#ifdef Linux
  setThreadAffinity(thid);
  // printf("Thread #%d: on CPU %d\n", res.thid_, sched_getcpu());
  // printf("sysconf(_SC_NPROCESSORS_CONF) %d\n",
  // sysconf(_SC_NPROCESSORS_CONF));
#endif

#if MASSTREE_USE
  MasstreeWrapper<Tuple>::thread_init(int(thid));
#endif

#if DURABLE_EPOCH
  Logger* logger;
  for (;;) {
    logger = logp->load();
    if (logger != 0) break;
    std::this_thread::sleep_for(std::chrono::nanoseconds(100));
  }
  logger->add_txn_executor(trans);
#endif

  storeRelease(ready, 1);
  while (!loadAcquire(start)) _mm_pause();
  if (thid == 0) epoch_timer_start = rdtscp();
  while (!loadAcquire(quit)) {

    ClientRequest tx = std::move(raft_cc->tx_queue_.pop());
    std::vector<ProcedureX> pro_set = tx.command_;

#if PROCEDURE_SORT
    sort(pro_set.begin(), pro_set.end());
#endif

RETRY:
    if (thid == 0) {
      leaderWork(epoch_timer_start, epoch_timer_stop);
#if BACK_OFF
      leaderBackoffWork(backoff, SiloResult);
#endif
      // printf("Thread #%d: on CPU %d\n", thid, sched_getcpu());
    }

#if DURABLE_EPOCH
    trans.durableEpochWork(epoch_timer_start, epoch_timer_stop, quit);
#endif

    if (loadAcquire(quit)) break;

    trans.begin(tx.source_node_, tx.sequence_num_);
    for (auto itr = pro_set.begin(); itr != pro_set.end();
         ++itr) {
      if ((*itr).ope_ == Ope::READ) {
        trans.read((*itr).key_);
      } else if ((*itr).ope_ == Ope::WRITE) {
        trans.write((*itr).key_);
      } else if ((*itr).ope_ == Ope::READ_MODIFY_WRITE) {
        trans.read((*itr).key_);
        trans.write((*itr).key_);
      } else {
        ERR;
      }
    }

    if (trans.validationPhase()) {
      trans.writePhase();
      /**
       * local_commit_counts is used at ../include/backoff.hh to calcurate about
       * backoff.
       */
      storeRelease(myres.local_commit_counts_,
                   loadAcquire(myres.local_commit_counts_) + 1);
    } else {
      trans.abort();
      ++myres.local_abort_counts_;
      goto RETRY;
    }
  }

#if DURABLE_EPOCH
  trans.log_buffer_pool_.terminate(myres_lg); // swith buffer
  logger->worker_end(thid);
#endif
  return;
}

#if DURABLE_EPOCH
 void logger_th(int thid, Notifier &notifier, std::atomic<Logger*> *logp, RaftCC *raft_cc){
#if 0
  if (!FLAGS_affinity.empty()) {
    std::cout << "Logger #" << thid << ": on CPU " << sched_getcpu() << "\n";
  }
#endif
  alignas(CACHE_LINE_SIZE) Logger logger(thid, notifier);
  notifier.add_logger(&logger);
  logp->store(&logger);
  logger.worker();
}

void set_cpu(std::thread &th, int cpu) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu, &cpuset);
  int rc = pthread_setaffinity_np(th.native_handle(), sizeof(cpu_set_t), &cpuset);
  if (rc != 0) {
    std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
  }
}
#endif

void raft_thread(RaftCC *raft_cc) {
  raft_cc->start();
}

int main(int argc, char *argv[]) try {
  gflags::SetUsageMessage("Silo benchmark.");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
#if DURABLE_EPOCH
  LoggerAffinity affin;
  if (FLAGS_affinity.empty()) {
    affin.init(FLAGS_thread_num,FLAGS_logger_num);
  } else {
    affin.init(FLAGS_affinity);
    FLAGS_thread_num = affin.worker_num_;
    FLAGS_logger_num = affin.logger_num_;
  }
  chkArgLg();
#else
  chkArg();
#endif
  makeDB();

  alignas(CACHE_LINE_SIZE) bool start = false;
  alignas(CACHE_LINE_SIZE) bool quit = false;
  initResult();
  std::vector<char> readys(FLAGS_thread_num);
  std::vector<std::thread> thv;
#if PWAL
  GlobalLSN = (uint *)calloc(FLAGS_thread_num, sizeof(uint));
  if (!GlobalLSN) ERR;
#endif
  RaftCC raft_cc;
#if DURABLE_EPOCH
  std::atomic<Logger *> logs[FLAGS_logger_num];
  Notifier notifier;
  std::vector<std::thread> lthv;

  int i=0, j=0;
  for (auto itr = affin.nodes_.begin(); itr != affin.nodes_.end(); ++itr,++j) {
    int lcpu = itr->logger_cpu_;
    logs[j].store(0);
    lthv.emplace_back(logger_th, j, std::ref(notifier), &(logs[j]), &raft_cc);
    if (!FLAGS_affinity.empty()) {
      set_cpu(lthv.back(), lcpu);
    }
    for (auto wcpu = itr->worker_cpu_.begin(); wcpu != itr->worker_cpu_.end(); ++wcpu,++i) {
      thv.emplace_back(worker, i, std::ref(readys[i]),
        std::ref(start), std::ref(quit), &(logs[j]), &raft_cc);
      if (!FLAGS_affinity.empty()) {
        set_cpu(thv.back(), *wcpu);
      }
    }
  }
#else
  for (size_t i = 0; i < FLAGS_thread_num; ++i)
    thv.emplace_back(worker, i, std::ref(readys[i]), std::ref(start), std::ref(quit), &raft_cc);
#endif
  //raft_test_start(&tx_queue);
  //std::thread rth(raft_thread, &raft_cc);
  waitForReady(readys);
  storeRelease(start, true);
  raft_cc.start();
  for (size_t i = 0; i < FLAGS_extime; ++i) {
    sleepMs(1000);
  }
  storeRelease(quit, true);
#if DURABLE_EPOCH
  for (auto &th : lthv) th.join();
#endif
  for (auto &th : thv) th.join();

  for (unsigned int i = 0; i < FLAGS_thread_num; ++i) {
    SiloResult[0].addLocalAllResult(SiloResult[i]);
  }
  ShowOptParameters();
#if DURABLE_EPOCH
  notifier.display();
#endif
  SiloResult[0].displayAllResult(FLAGS_clocks_per_us, FLAGS_extime,
                                 FLAGS_thread_num);

  return 0;
} catch (bad_alloc) {
  ERR;
}
