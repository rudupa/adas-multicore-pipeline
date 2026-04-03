#pragma once

#include "core/types.h"
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace adas {

/// Collects per-frame and per-stage timing metrics for the pipeline.
/// Thread-safe — multiple pipeline workers can record concurrently.
class MetricsCollector {
public:
    /// Record that a frame entered the pipeline.
    void record_pipeline_enter(uint64_t frame_id, const std::string& sensor,
                               TimePoint enter_time);

    /// Record that a frame exited the pipeline.
    void record_pipeline_exit(uint64_t frame_id, TimePoint exit_time);

    /// Record per-stage processing time for a frame.
    void record_stage_time(uint64_t frame_id, const std::string& stage_name,
                           Duration processing_time);

    /// Record how long a frame waited in a queue before a stage picked it up.
    void record_queue_wait(uint64_t frame_id, const std::string& stage_name,
                           Duration wait_time);

    /// Record that a frame was dropped (bandwidth throttle, queue full, etc.).
    void record_frame_drop(const std::string& sensor);

    /// Print a summary of all collected metrics to stdout.
    void print_summary() const;

    /// Print a compact single-line periodic update.
    void print_periodic_update() const;

    /// Reset all counters.
    void reset();

private:
    struct FrameRecord {
        std::string sensor;
        TimePoint   enter_time;
        TimePoint   exit_time;
        bool        completed = false;
        std::map<std::string, double> stage_us;   // stage → processing µs
        std::map<std::string, double> queue_us;   // stage → queue-wait µs
    };

    mutable std::mutex                    mutex_;
    std::map<uint64_t, FrameRecord>       frames_;
    std::map<std::string, uint64_t>       drops_;     // sensor → count
    uint64_t                              total_completed_ = 0;
};

} // namespace adas
