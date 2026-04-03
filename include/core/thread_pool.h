#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace adas {

/// Lightweight thread pool that executes submitted callables.
/// Designed so that priority scheduling and CPU affinity can be added later.
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    // Non-copyable, non-movable
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// Submit a task to the pool (FIFO by default).
    void submit(std::function<void()> task);

    /// Graceful shutdown: finish queued tasks, then stop.
    void shutdown();

    size_t thread_count() const { return workers_.size(); }

    // TODO: add submit_with_priority(TaskPriority, std::function<void()>)
    // TODO: add set_affinity(size_t thread_index, int core_id)

private:
    void worker_loop();

    std::vector<std::thread>            workers_;
    std::queue<std::function<void()>>   task_queue_;
    std::mutex                          mutex_;
    std::condition_variable             cv_;
    std::atomic<bool>                   stop_{false};
};

} // namespace adas
