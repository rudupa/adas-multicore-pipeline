#pragma once

#include "core/types.h"
#include <functional>
#include <memory>

namespace adas {

/// A task submitted to the scheduler.
struct ScheduledTask {
    std::function<void()> work;
    TaskPriority          priority = TaskPriority::Normal;
    uint64_t              frame_id = 0;
    // TODO: add deadline field for deadline-aware scheduler
    //       TimePoint deadline;
};

/// Abstract scheduler interface.
/// Implementations decide the order in which tasks are dispatched
/// to the thread pool.
class Scheduler {
public:
    virtual ~Scheduler() = default;

    /// Enqueue a task for execution.
    virtual void enqueue(ScheduledTask task) = 0;

    /// Retrieve the next task to run (blocking).
    /// Returns nullptr-equivalent when shut down and empty.
    virtual bool dequeue(ScheduledTask& out) = 0;

    /// Signal that no more tasks will be enqueued.
    virtual void shutdown() = 0;

    virtual const std::string& name() const = 0;
};

} // namespace adas
