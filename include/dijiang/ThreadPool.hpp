#pragma once

#include "Debug.hpp"

#include <thread>
#include <functional>
#include <utility>
#include <iostream>
#include <atomic>
#include <vector>
#include <queue>
#include <condition_variable>

typedef std::function<void(void)> Task;

class ThreadPool
{
public:
  ThreadPool(const int thread_num)
  {
    thread_num_ = thread_num;
    threads_.reserve(thread_num_);
    state_ = kRunning;

    for (int i = 0; i < thread_num_; ++i)
    {
      auto t = [&]()
      {
        while (true)
        {
          Task func = []()
          { return; };
          {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&]()
                     { return !queue_.empty() || state_ != kRunning; });
            if (state_ == kStop)
              return;
            func = queue_.front();
            queue_.pop();
          }
          try
          {
            func();
          }
          catch (std::exception &e)
          {
            std::cout << "running task, with exception..." << e.what() << std::endl;
            return;
          }
        }
      };
      threads_.emplace_back(std::move(t));
    }
  }

  ~ThreadPool()
  {
    if (state_ == kRunning)
      Stop();
  }

  void AddJob(const Task &job)
  {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      queue_.push(job);
    }
    cv_.notify_one();
  }

  int GetThreadNum() { return thread_num_; }

  int GetWaitNum()
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.size();
  }

  void Stop()
  {
    state_ = kStop;
    cv_.notify_all();
    for (auto &thread : threads_)
    {
      if (thread.joinable())
        thread.join();
    }
  }

private:
  // thread
  int thread_num_;
  std::vector<std::thread> threads_;
  enum State
  {
    kRunning,
    kStop,
  } state_;
  // task queue
  std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<Task> queue_;
};
