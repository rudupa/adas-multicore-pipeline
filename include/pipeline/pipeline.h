#pragma once

#include "core/bandwidth_manager.h"
#include "core/concurrent_queue.h"
#include "core/config_loader.h"
#include "core/task_scheduler.h"
#include "core/thread_pool.h"
#include "core/types.h"
#include "metrics/metrics_collector.h"
#include "pipeline/pipeline_stage.h"
#include "visualization/timeline_visualizer.h"
#include "scheduler/scheduler.h"
#include "sensors/sensor.h"

#include <atomic>
#include <condition_variable>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace adas {

/// Top-level orchestrator.
///  – Owns sensors, stages, scheduler, thread pool, bandwidth manager.
///  – Runs sensor-producer threads that push frames through the pipeline.
///  – Collects metrics and prints periodic updates.
class Pipeline {
public:
    explicit Pipeline(const PipelineConfig& cfg);
    ~Pipeline();

    // Non-copyable
    Pipeline(const Pipeline&)            = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    /// Start all sensors and the processing loop.
    void start();

    /// Stop everything and join threads.
    void stop();

    /// Run for the configured duration, then stop and print metrics.
    void run();

    MetricsCollector&       metrics()       { return metrics_; }
    const MetricsCollector& metrics() const { return metrics_; }

    TimelineVisualizer&       visualizer()       { return visualizer_; }
    const TimelineVisualizer& visualizer() const { return visualizer_; }
    
    TaskScheduler&       task_scheduler()       { return *task_scheduler_; }
    const TaskScheduler& task_scheduler() const { return *task_scheduler_; }

private:
    static constexpr uint64_t kNoActiveCycle = std::numeric_limits<uint64_t>::max();

    // Sensor producer loop (one per sensor, runs on its own thread).
    void sensor_loop(Sensor* sensor);

    void central_loop();
    void process_camera_frame(std::shared_ptr<Frame> frame);
    void process_radar_frame(std::shared_ptr<Frame> frame);
    void process_central_cycle(std::shared_ptr<Frame> frame);
    void run_stage(PipelineStage& stage, std::shared_ptr<Frame>& frame, bool collect_metrics);
    PipelineStage* find_stage_by_id(const std::string& stage_id);
    bool is_camera_sensor(const Sensor& sensor) const;
    bool is_radar_sensor(const Sensor& sensor) const;
    bool is_vehicle_state_sensor(const Sensor& sensor) const;

    PipelineConfig                              cfg_;
    std::vector<std::unique_ptr<Sensor>>        sensors_;
    std::vector<std::unique_ptr<PipelineStage>> stages_;
    std::unique_ptr<Scheduler>                  scheduler_;
    std::unique_ptr<TaskScheduler>              task_scheduler_;
    std::unique_ptr<ThreadPool>                 pool_;
    BandwidthManager                            bw_manager_;
    MetricsCollector                            metrics_;
    TimelineVisualizer                          visualizer_;

    std::vector<std::thread>                    sensor_threads_;
    std::thread                                 central_loop_thread_;
    std::thread                                 scheduler_consumer_thread_;
    std::thread                                 metrics_thread_;
    std::atomic<bool>                           running_{false};

    // Central/world cycle counter.
    std::atomic<uint64_t>                       world_frame_id_{0};
    std::atomic<uint64_t>                       active_world_cycle_id_{kNoActiveCycle};
    std::atomic<int>                            active_cycle_count_{0};  // overlap-mode in-flight counter

    // Radar-frame cycle tracking (mirrors central cycle: skip / preempt / overlap).
    std::atomic<uint64_t>                       active_radar_frame_id_{kNoActiveCycle};
    std::atomic<int>                            active_radar_count_{0};   // overlap-mode in-flight counter for radar

    std::mutex                                  sensor_state_mutex_;
    uint64_t                                    latest_camera_output_id_{0};
    uint64_t                                    latest_radar_output_id_{0};
    uint64_t                                    latest_vehicle_state_id_{0};
};

} // namespace adas
