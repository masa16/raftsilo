extern "C" {
  #include <stdbool.h>
  #include <assert.h>
  #include <stdlib.h>
  #include <stdio.h>
  #include <string.h>
  #include <stdint.h>
  #include <zmq.h>
  #include <czmq.h>

  #include "raft.h"
  #include "raft_log.h"
  //#include "raft_private.h"
}
#include "include/common.hh"
#include "raft_msgpack.hh"
#include "raft_server.hh"
#include "raft_client.hh"
#include "raft_txn.hh"
#include "raft_cc.hh"
#include "../third_party/ccbench/silo/include/log.hh"

//#define N() do{fprintf(stderr, "%16s %4d %16s\n", __FILE__, __LINE__, __func__); fflush(stderr);} while(0)
#define N() {}




static void server_thread(zsock_t *pipe, void *udata)
{
  zsock_signal(pipe, 0);
  ServerData *a = (ServerData*)udata;
  //printf("a->id_=%d\n",a->id_);
  Server sv(a->context_, a->id_, a->tx_queue_, FLAGS_thread_num);
  sv.setup(a->hosts_);
  a->raft_ = sv.raft_;
  a->server_ = &sv;
  // synchronize
  const char s[] = "READY";
  zsock_send(pipe, "s", s);
  char *r;
  zsock_recv(pipe, "s", &r);
  assert(strcmp(r,"GO")==0);
  free(r);
  sv.start(pipe);
  sv.stop();
}


class ClientData {
public:
  HostData &server_;
  void *context_;
  int n_clients_;
  ClientData(void *context, HostData &server, int n_clients) :
    server_(server), context_(context), n_clients_(n_clients) {}
};

static void client_thread(zsock_t *pipe, void *udata)
{
  zsock_signal(pipe, 0);
  ClientData *d = (ClientData*)udata;
  std::vector<Client*> clients;

  zloop_t *loop = zloop_new();
  assert(loop);

  for (int i=0; i < d->n_clients_; i++) {
    int id = 1024 + i;
    Client *c = new Client(id, loop, d->server_, d->context_);
    clients.emplace_back(c);
  }
  for (auto c : clients) c->connect();
  //usleep(1000);
  for (auto c : clients) c->send();

  zloop_start(loop);

  for (auto c : clients) delete c;
}


#define NSERVERS 5

int RaftCC::start()
{
  int rc;
  context_ = zmq_ctx_new();
  assert(context_);

  for (int i=1; i<=NSERVERS; i++) {
    hosts_.emplace_back(i,"localhost",51110+i);
  }
  for (auto h : hosts_) {
    ServerData *svd = new ServerData(h.id_, hosts_, context_, &tx_queue_);
    serverdata_.emplace_back(svd);
  }
  for (auto svd : serverdata_) {
    svd->setup(server_thread);
    actors_[svd->id_] = svd->actor_;
    servers_[svd->id_] = svd->server_;
  }
  for (auto svd : serverdata_) {
    svd->start();
  }
  wait_leader_elected();
  //printf("pass\n");

#if HAS_CLIENT
  ClientData *cdata = new ClientData(context_, hosts_[0], 8);
  zactor_t *c = zactor_new(client_thread, cdata);

  zactor_destroy(&c);
  delete cdata;
#endif
  return 0;
}

void RaftCC::end()
{
  if (send_log_count_>0) {
    cout << "raft_send_log_count:\t" << send_log_count_ << endl;
    double t = (double)send_log_latency_ / FLAGS_clocks_per_us / send_log_count_;
    cout << std::fixed << std::setprecision(4) << "raft_send_log_latency[ms]:\t" << t/1000 << endl;
  }

  for (auto svd : serverdata_) {
    svd->server_->display(FLAGS_clocks_per_us);
  }

  for (auto svd : serverdata_) {
    svd->destroy();
  }
  // avoid freezing
  //int rc = zmq_ctx_term(context_);
  //assert(rc==0);
}


raft_node_id_t RaftCC::wait_leader_elected()
{
  raft_node_id_t leader_id;
  while(true) {
    leader_id = raft_get_leader_id(serverdata_[0]->raft_);
    if (leader_id>=0) break;
    printf("leader_id=%d\n",leader_id);
    sleep(1);
  }
  assert(leader_id>=0);
  return leader_id;
}


void RaftCC::send_log(int client_id, long sequence_num, std::uint64_t tid, //NotificationId &nid,
  std::vector<LogRecord> &log_set, size_t thid)
{
  raft_node_id_t leader_id = wait_leader_elected();
  zactor_t *a = actors_[leader_id];
  Server *s = servers_[leader_id];
  //return;
  int rc;
  {
    //std::lock_guard<std::mutex> lock(mutex_);
    uint64_t t = rdtscp();
    rc = zsock_send(s->senders_[thid], "i88b", client_id, (uint64_t)sequence_num, tid,
      &log_set[0], log_set.size()*sizeof(LogRecord));
    //s->receive_entry((char*)&log_set[0], log_set.size()*sizeof(LogRecord));
    send_log_latency_ += rdtscp() - t;
    send_log_count_ ++;
  }
  assert(rc>0);
}


void RaftCC::send_log(void *log_set, size_t log_size, size_t thid)
{
  static int client_id = 0;
  static uint64_t sequence_num = 0;
  static uint64_t tid = 0;
  raft_node_id_t leader_id = wait_leader_elected();
  Server *s = servers_[leader_id];
  //return;
  int rc;
  send_time_ = rdtscp();
  rc = zsock_send(s->senders_[thid], "i88b", client_id, (uint64_t)sequence_num, tid,
    log_set, log_size);
  assert(rc>0);
}


void RaftCC::recv_rep(size_t thid)
{
  raft_node_id_t leader_id = wait_leader_elected();
  Server *s = servers_[leader_id];
  //return;
  int rc;
  int r;
  rc = zsock_recv(s->senders_[thid], "i", &r);
  assert(r==0);
  send_log_latency_ += rdtscp() - send_time_;
  send_log_count_ ++;
}
