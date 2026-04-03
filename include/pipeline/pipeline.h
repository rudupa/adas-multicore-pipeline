#pragma once

#include "core/bandwidth_manager.h"
#include "core/concurrent_queue.h"
#include "core/config_loader.h"
#include "core/thread_pool.h"
#include "core/types.h"
#include "metrics/metrics_collector.h"
#include "pipeline/pipeline_stage.h"
#include "scheduler/scheduler.h"
#include "sensors/sensor.h"

#include <atomic>
#include <memory>
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

private:
    // Sensor producer loop (one per sensor, runs on its own thread).
    void sensor_loop(Sensor* sensor);

    // Process a single frame through all pipeline stages sequentially.
    void process_frame(std::shared_ptr<Frame> frame);

    PipelineConfig                              cfg_;
    std::vector<std::unique_ptr<Sensor>>        sensors_;
    std::vector<std::unique_ptr<PipelineStage>> stages_;
    std::unique_ptr<Scheduler>                  scheduler_;
    std::unique_ptr<ThreadPool>                 pool_;
    BandwidthManager                            bw_manager_;
    MetricsCollector                            metrics_;

    // Inter-stage queue (sensor → first stage entrance)
    ConcurrentQueue<std::shared_ptr<Frame>>     ingress_queue_;

    std::vector<std::thread>                    sensor_threads_;
    std::thread                                 dispatcher_thread_;
    std::thread                                 metrics_thread_;
    std::atomic<bool>                           running_{false};

    // Global monotonic frame-id counter (across all sensors)
    std::atomic<uint64_t>                       global_frame_id_{0};
};

} // namespace adas
