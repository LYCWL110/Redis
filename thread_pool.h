#ifndef REDIS_THREAD_POOL_H_
#define REDIS_THREAD_POOL_H_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace redis {

class ThreadPool {
 public:
  explicit ThreadPool(size_t num_threads);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  void Submit(std::function<void()> task);

 private:
  void WorkerLoop();

  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<bool> stop_{false};
};

}  // namespace redis

#endif  // REDIS_THREAD_POOL_H_
