#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cassert>
#include "raft_msgpack.hh"

class TxQueue {
  std::queue<ClientRequest> queue_;
  std::mutex guard_;
  std::condition_variable cv_;
public:
  void push(ClientRequest val) {
    std::lock_guard<std::mutex> lock(guard_);
    queue_.push(std::move(val));
    cv_.notify_all();
  }
  ClientRequest pop() {
    std::unique_lock<std::mutex> lock(guard_);
    cv_.wait(lock, [this]{
      return !queue_.empty();
    });
    ClientRequest ret = std::move(queue_.front());
    queue_.pop();
    return ret;
  }
};
