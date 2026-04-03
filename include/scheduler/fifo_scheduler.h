#pragma once

#include "scheduler/scheduler.h"
#include <condition_variable>
#include <mutex>
#include <queue>

namespace adas {

/// Simple first-in-first-out scheduler.
/// Tasks are dispatched in the order they are enqueued.
class FifoScheduler : public Scheduler {
public:
    FifoScheduler() = default;

    void enqueue(ScheduledTask task) override;
    bool dequeue(ScheduledTask& out) override;
    void shutdown() override;

    const std::string& name() const override { return name_; }

private:
    std::string                  name_{"fifo"};
    std::queue<ScheduledTask>    queue_;
    std::mutex                   mutex_;
    std::condition_variable      cv_;
    bool                         closed_ = false;
};

// TODO: Implement PriorityScheduler
//       - Use a priority queue ordered by ScheduledTask::priority
//       - Higher priority tasks are dequeued first

// TODO: Implement DeadlineScheduler
//       - Use earliest-deadline-first ordering
//       - Requires ScheduledTask::deadline field

} // namespace adas
