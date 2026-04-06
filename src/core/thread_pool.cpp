#include "core/thread_pool.h"

namespace adas {

ThreadPool::ThreadPool(size_t num_threads) {
    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
        // TODO: set CPU affinity per thread here
        //       e.g. set_affinity(i, desired_core_id);
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard lock(mutex_);
        if (stop_) return;
        task_queue_.push(std::move(task));
    }
    cv_.notify_one();
}

void ThreadPool::shutdown() {
    {
        std::lock_guard lock(mutex_);
        if (stop_) return;
        // Strict stop behavior: drop queued (not-yet-started) tasks.
        // In-flight tasks are allowed to finish naturally.
        std::queue<std::function<void()>> empty;
        task_queue_.swap(empty);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
}

void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !task_queue_.empty(); });
            if (stop_ && task_queue_.empty()) return;
            task = std::move(task_queue_.front());
            task_queue_.pop();
        }
        task();
    }
}

} // namespace adas
