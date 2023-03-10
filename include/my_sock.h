#ifndef _MY_SOCK_H
#define _MY_SOCK_H  1

/*
#include "rdma/rsocket.h"

#define socket     rsocket
#define bind       rbind
#define listen     rlisten
#define accept     raccept
#define connect    rconnect
#define recv       rrecv
#define send       rsend
#define setsockopt rsetsockopt
#define getsockopt rgetsockopt
#define fcntl      rfcntl
*/

int my_recv(int sock, void *buffer, size_t size)
{
  char *buf = (char*)buffer;
  ssize_t sz = size;

  while (sz > 0) {
    ssize_t read_bytes = recv(sock, buf, sz, 0);
    if (read_bytes == 0) {
      // 接続が切れた
      fprintf(stderr,"my_recv : Connection closed\n");
      return 1;
    }
    if (read_bytes == -1) {
      if (errno == EAGAIN) {
        // 相手が準備できていない
        continue;
      } else {
        perror("recv error ");
        if (errno == ECONNRESET) {
          // 接続が切れた
          return 1;
        }
        // 想定外のエラー
        close(sock);
        abort();
      }
    }
    buf += read_bytes;
    sz -= read_bytes;
  }
  // 送信サイズをチェック
  if (sz != 0) {
    fprintf(stderr,"my_recv : Size check error, sz=%zd\n", sz);
    abort();
  }
  return 0;
}


int my_send(int sock, void *buffer, size_t size)
{
  char *buf = (char*)buffer;
  ssize_t sz = size;
  int w = 2000;

  while (sz > 0) {
    // MSG_NOSIGNALを指定した場合, シグナルは発生させず errno に EPIPE をセットする
    ssize_t write_bytes = send(sock, buf, sz, MSG_NOSIGNAL);
    if (write_bytes == -1) {
      if (errno == EAGAIN) {
        // 相手が準備できていない
        if (w < 1024000)
          w *= 2;
        else
          perror("send error, wait 1s");
        usleep(w);
        continue;
      }
      perror("send error ");
      if (errno == ECONNRESET || errno == EPIPE) {
        // 接続が切れた
        return 1;
      } else {
        // 想定外のエラー
        close(sock);
        abort();
      }
      w = 2000;
    }
    if (write_bytes == 0) {
      fprintf(stderr,"my_send : sent == 0");
      continue;
    }
    buf += write_bytes;
    sz -= write_bytes;
  }
  // 送信サイズをチェック
  if (sz != 0) {
    fprintf(stderr,"my_send : Size check error, sz=%zd\n", sz);
    close(sock);
    abort();
  }
  return 0;
}

#endif /* my_sock.h */
