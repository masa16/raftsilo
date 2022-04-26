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
#include "include/result.hh"
#include "include/transaction.hh"
#include "include/util.hh"

#include "../include/atomic_wrapper.hh"
#include "../include/backoff.hh"
#include "../include/cpu.hh"
#include "../include/debug.hh"
#include "../include/fileio.hh"
#include "../include/masstree_wrapper.hh"
#include "../include/random.hh"
#include "../include/result.hh"
#include "../include/tsc.hh"
#include "../include/util.hh"
#include "../include/zipf.hh"

#include "raft_server.hh"
#include "raft_cc.hh"
#include "raft_txn.hh"

//using namespace std;

void worker(size_t thid, const bool &quit, RaftCC *raft_cc) {
  Result &myres = std::ref(SiloResult[thid]);
  Xoroshiro128Plus rnd;
  rnd.init();
  RaftTxnExecutor trans(thid, (Result *) &myres, raft_cc);
  FastZipf zipf(&rnd, FLAGS_zipf_skew, FLAGS_tuple_num);
  uint64_t epoch_timer_start, epoch_timer_stop;
#if BACK_OFF
  Backoff backoff(FLAGS_clocks_per_us);
#endif

#if 0 //WAL
  /*
  const boost::filesystem::path log_dir_path("/tmp/ccbench");
  if (boost::filesystem::exists(log_dir_path)) {
  } else {
    boost::system::error_code error;
    const bool result = boost::filesystem::create_directory(log_dir_path,
  error); if (!result || error) { ERR;
    }
  }
  std::string logpath("/tmp/ccbench");
  */
  std::string logpath;
  genLogFile(logpath, thid);
  trans.logfile_.open(logpath, O_CREAT | O_TRUNC | O_WRONLY, 0644);
  trans.logfile_.ftruncate(10 ^ 9);
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

  //storeRelease(ready, 1);
  //while (!loadAcquire(start)) _mm_pause();
  if (thid == 0) epoch_timer_start = rdtscp();
  while (!loadAcquire(quit)) {
//#if PARTITION_TABLE
//    makeProcedure(trans.pro_set_, rnd, zipf, FLAGS_tuple_num, FLAGS_max_ope,
//                  FLAGS_thread_num, FLAGS_rratio, FLAGS_rmw, FLAGS_ycsb, true,
//                  thid, myres);
//#else
//    makeProcedure(trans.pro_set_, rnd, zipf, FLAGS_tuple_num, FLAGS_max_ope,
//                  FLAGS_thread_num, FLAGS_rratio, FLAGS_rmw, FLAGS_ycsb, false,
//                  thid, myres);
//#endif

    ClientRequest tx = std::move(raft_cc->tx_queue_.pop());
    std::vector<ProcedureX> pro_set = tx.command_;
    printf("       -------- start transaction --------\n");

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

  return;
}

//extern int raft_test_start(TxQueue *tx_queue);

int main(int argc, char *argv[]) try {
  gflags::SetUsageMessage("Silo benchmark.");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  chkArg();
  makeDB();

  //alignas(CACHE_LINE_SIZE) bool start = false;
  alignas(CACHE_LINE_SIZE) bool quit = false;
  initResult();
  //std::vector<char> readys(FLAGS_thread_num);
  std::vector<std::thread> thv;
  //TxQueue tx_queue;
  RaftCC raft_cc;
  for (size_t i = 0; i < FLAGS_thread_num; ++i)
    thv.emplace_back(worker, i, std::ref(quit), &raft_cc);
  //raft_test_start(&tx_queue);
  raft_cc.start();
  //waitForReady(readys);
  //storeRelease(start, true);
  for (size_t i = 0; i < FLAGS_extime; ++i) {
    sleepMs(1000);
  }
  storeRelease(quit, true);
  for (auto &th : thv) th.join();

  for (unsigned int i = 0; i < FLAGS_thread_num; ++i) {
    SiloResult[0].addLocalAllResult(SiloResult[i]);
  }
  ShowOptParameters();
  SiloResult[0].displayAllResult(FLAGS_clocks_per_us, FLAGS_extime,
                                 FLAGS_thread_num);

  return 0;
} catch (bad_alloc) {
  ERR;
}
