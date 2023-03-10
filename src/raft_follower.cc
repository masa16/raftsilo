#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <errno.h>
#include "my_sock.h"

#if ZMQ_PUBSUB
#include <zmq.h>
#define ZERR(b) do{if(b){fprintf(stderr,"%16s %4d %16s: ZMQ error(%d): %s\n",__FILE__,__LINE__,__func__,zmq_errno() ,zmq_strerror(zmq_errno()));abort();}}while(0)
#endif

typedef struct _ae_req {
  int id;
  size_t size;
} ae_req_t;

typedef struct _ae_res {
  int id;
  int status;
} ae_res_t;

int sock_bind_listen(int port)
{
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("socket error ");
    exit(0);
  }

  struct sockaddr_in addr = {0,};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  const size_t addr_size = sizeof(addr);

  int opt = 1;
  // ポートが解放されない場合, SO_REUSEADDRを使う
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) == -1) {
    perror("setsockopt error ");
    close(sock);
    exit(0);
  }
  if (bind(sock, (struct sockaddr*)&addr, addr_size) == -1) {
    perror("bind error ");
    close(sock);
    exit(0);
  }
  printf("bind port=%d\n", port);

  // クライアントのコネクション待ち状態は最大10
  if (listen(sock, 10) == -1) {
    perror("listen error ");
    close(sock);
    exit(0);
  }
  printf("listen success!\n");
  return sock;
}


int main(int argc, char** argv)
{
  if (argc < 2) {
    printf("Usage : \n $> %s [port number]\n", argv[0]);
    exit(0);
  }
  int port = atoi(argv[1]);
  int sock = sock_bind_listen(port);

#if ZMQ_PUBSUB
  if (argc < 3) {
    printf("Usage : \n $> %s [port number]\n", argv[0]);
    exit(0);
  }
  char url[128];
  sprintf(url, "tcp://%s", argv[2]);
  void *context_ = zmq_ctx_new();
  //int sub_port = atoi(argv[2]);
  void *sub_sock = 0;
#else
  char *buffer = (char*)malloc(32*1024*1024);
#endif

  int last_id = 0;
  int sock_client = 0;

 ACCEPT:
  printf("last_id=%d\n",last_id);
  // 接続が切れた場合, acceptからやり直す
#if ZMQ_PUBSUB
  if (sub_sock != 0) {
    zmq_close(sub_sock);
  }
#endif
#if ZMQ_PUBSUB
  sub_sock = zmq_socket(context_, ZMQ_SUB);
  ZERR(sub_sock==0);
  int rc = zmq_setsockopt(sub_sock, ZMQ_SUBSCRIBE, "", 0);
  ZERR(rc!=0);
  rc = zmq_connect(sub_sock, url);
  fprintf(stderr,"connect to publsr: %s, %p\n", url, sub_sock);
  ZERR(rc!=0);
#endif
  int old_sock_client = sock_client;
  struct sockaddr_in accept_addr = {0,};
  socklen_t accpet_addr_size = sizeof(accept_addr);
  sock_client = accept(sock, (struct sockaddr*)&accept_addr, &accpet_addr_size);
  fprintf(stderr,"sock_client=%d\n", sock_client);
  if (sock_client==0 || sock_client<0) {
    perror("accept error ");
    exit(0);
  }
  fprintf(stderr,"accept success!\n");
  if (old_sock_client > 0) {
    close(old_sock_client);
  }

  while (true) {
    ae_req_t ae_req;

    if (my_recv(sock_client, &ae_req, sizeof(ae_req_t))) goto ACCEPT;
    //fprintf(stderr,"%d recv ae_req\n",port);
#if ZMQ_PUBSUB
    zmq_msg_t msg;
    rc = zmq_msg_init(&msg);
    ZERR(rc!=0);
    rc = zmq_msg_recv(&msg, sub_sock, 0);
    ZERR(rc==-1);
    int len = zmq_msg_size(&msg);
    if (len > 0 && len != ae_req.size) {
      fprintf(stderr, "len=%d != ae_req.size=%zd\n",len,ae_req.size);
      abort();
    }
    rc = zmq_msg_close(&msg);
    //fprintf(stderr,"%d recv data size=%d\n",port,len);
#else
    if (my_recv(sock_client, buffer, ae_req.size)) goto ACCEPT;
#endif
    ae_res_t ae_res;
    ae_res.id = last_id = ae_req.id;
    ae_res.status = 0;

    if (my_send(sock_client, &ae_res, sizeof(ae_res_t))) goto ACCEPT;
  }
  return 0;
}
