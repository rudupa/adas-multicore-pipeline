#include "scheduler/fifo_scheduler.h"

namespace adas {

void FifoScheduler::enqueue(ScheduledTask task) {
    {
        std::lock_guard lock(mutex_);
        if (closed_) return;
        queue_.push(std::move(task));
    }
    cv_.notify_one();
}

bool FifoScheduler::dequeue(ScheduledTask& out) {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty() || closed_; });
    if (queue_.empty()) return false;
    out = std::move(queue_.front());
    queue_.pop();
    return true;
}

void FifoScheduler::shutdown() {
    {
        std::lock_guard lock(mutex_);
        closed_ = true;
    }
    cv_.notify_all();
}

} // namespace adas
