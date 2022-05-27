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
  Server sv(a->context_, a->id_, a->tx_queue_);
  sv.setup(a->hosts_);
  a->raft_ = sv.raft_;
  // synchronize
  const char s[] = "READY";
  zsock_send(pipe, "s", s);
  char *r;
  zsock_recv(pipe, "s", &r);
  assert(strcmp(r,"GO")==0);
  free(r);
  sv.start(pipe);
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
    ServerData *sv = new ServerData(h.id_, hosts_, context_, &tx_queue_);
    servers_.emplace_back(sv);
  }
  for (auto sv : servers_) {
    sv->setup(server_thread);
    actors_[sv->id_] = sv->actor_;
  }
  for (auto sv : servers_) {
    sv->start();
  }

  ClientData *cdata = new ClientData(context_, hosts_[0], 8);
  zactor_t *c = zactor_new(client_thread, cdata);

  zactor_destroy(&c);
  for (auto sv : servers_) {
    sv->destroy();
    //delete sv;
  }
  delete cdata;

  rc = zmq_ctx_term(context_);
  assert(rc==0);
  return 0;
}


void RaftCC::send_log(int client_id, long sequence_num, std::uint64_t tid, //NotificationId &nid,
  std::vector<LogRecord> &log_set)
{
  raft_node_id_t leader_id = raft_get_leader_id(servers_[0]->raft_);
  assert(leader_id>=0);
  zactor_t *a = actors_[leader_id];
  int rc = zsock_send(a, "i88b", client_id, (uint64_t)sequence_num, tid,
    //&nid, sizeof(NotificationId),
    &log_set[0], log_set.size()*sizeof(LogRecord));
  assert(rc>0);
}
