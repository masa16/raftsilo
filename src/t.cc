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

//#define N() do{fprintf(stderr, "%16s %4d %16s\n", __FILE__, __LINE__, __func__); fflush(stderr);} while(0)
#define N() {}


static void server_thread(zsock_t *pipe, void *udata)
{
  zsock_signal(pipe, 0);
  ActorData *a = (ActorData*)udata;
  //printf("a->id_=%d\n",a->id_);
  Server sv(a->context_, a->id_);
  sv.setup(a->hosts_);
  // synchronize
  const char s[] = "READY";
  zsock_send(pipe, "s", s);
  char *r;
  zsock_recv(pipe, "s", &r);
  assert(strcmp(r,"GO")==0);
  free(r);
  sv.start();
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
  for (auto c : clients) c->send("");

  zloop_start(loop);

  for (auto c : clients) delete c;
}


#define NSERVERS 5

int main(void)
{
  int rc;
  void *context = zmq_ctx_new();
  assert(context);
  std::vector<HostData> hosts;
  std::vector<ActorData*> actors;

  for (int i=1; i<=NSERVERS; i++) {
    hosts.emplace_back(i,"localhost",51110+i);
  }
  for (auto h : hosts) {
    ActorData *a = new ActorData(h.id_, hosts, context);
    actors.emplace_back(a);
  }
  for (auto a : actors) {
    a->setup(server_thread);
  }
  for (auto a : actors) {
    a->start();
  }

  ClientData *cdata = new ClientData(context, hosts[0], 8);
  zactor_t *c = zactor_new(client_thread, cdata);

  zactor_destroy(&c);
  for (auto a : actors) {
    a->destroy();
    delete a;
  }
  delete cdata;

  rc = zmq_ctx_term(context);
  assert(rc==0);
  return 0;
}
