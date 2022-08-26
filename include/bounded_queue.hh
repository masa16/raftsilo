#include <utility>
#include <queue>
#include <mutex>
#include <condition_variable>

template<typename T, size_t N>
class bounded_queue {
  std::queue<T> queue_;
  std::mutex guard_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
public:
  // 値の挿入
  void push(T val) {
    std::unique_lock<std::mutex> lk(guard_);
    not_full_.wait(lk, [this]{
      return queue_.size() < N;
    });
    queue_.push(std::move(val));
    not_empty_.notify_all();
  }
  // 値の取り出し
  T pop() {
    std::unique_lock<std::mutex> lk(guard_);
    not_empty_.wait(lk, [this]{
      return !queue_.empty();
    });
    T ret = std::move(queue_.front());
    queue_.pop();
    not_full_.notify_all();
    return ret;
  }
};
