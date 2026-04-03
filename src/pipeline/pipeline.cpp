#include "pipeline/pipeline.h"

#include "pipeline/detection_stage.h"
#include "pipeline/preprocess_stage.h"
#include "pipeline/tracking_stage.h"
#include "scheduler/fifo_scheduler.h"
#include "sensors/camera_sensor.h"
#include "sensors/radar_sensor.h"

#include <chrono>
#include <cstdio>
#include <map>
#include <thread>

namespace adas {

// ────────────────────────────────────────────────────────────────────
// Construction
// ────────────────────────────────────────────────────────────────────
Pipeline::Pipeline(const PipelineConfig& cfg)
    : cfg_(cfg),
      bw_manager_(cfg.global_bandwidth_limit_mbps, cfg.bandwidth_window_ms),
      ingress_queue_(cfg.queue_capacity) {

    // ── Create sensors from config ─────────────────────────────────
    for (auto& sc : cfg.sensors) {
        if (sc.type == "camera") {
            sensors_.push_back(std::make_unique<CameraSensor>(
                sc.name, sc.fps, sc.frame_size_bytes, sc.bandwidth_limit_mbps));
        } else if (sc.type == "radar") {
            sensors_.push_back(std::make_unique<RadarSensor>(
                sc.name, sc.fps, sc.frame_size_bytes, sc.bandwidth_limit_mbps));
        }
        // TODO: add "lidar" branch when LidarSensor is implemented
        else {
            std::fprintf(stderr, "[pipeline] Unknown sensor type: %s\n",
                         sc.type.c_str());
        }
    }

    // ── Create pipeline stages ─────────────────────────────────────
    stages_.push_back(std::make_unique<PreprocessStage>(cfg.preprocess_delay_us));
    stages_.push_back(std::make_unique<DetectionStage>(cfg.detection_delay_us));
    stages_.push_back(std::make_unique<TrackingStage>(cfg.tracking_delay_us));

    // ── Create scheduler ───────────────────────────────────────────
    if (cfg.scheduler_type == "fifo") {
        scheduler_ = std::make_unique<FifoScheduler>();
    }
    // TODO: else if (cfg.scheduler_type == "priority") { ... }
    // TODO: else if (cfg.scheduler_type == "deadline") { ... }
    else {
        std::fprintf(stderr, "[pipeline] Unknown scheduler '%s', defaulting to FIFO\n",
                     cfg.scheduler_type.c_str());
        scheduler_ = std::make_unique<FifoScheduler>();
    }

    // ── Create thread pool ─────────────────────────────────────────
    pool_ = std::make_unique<ThreadPool>(cfg.thread_pool_size);
}

Pipeline::~Pipeline() {
    stop();
}

// ────────────────────────────────────────────────────────────────────
// Lifecycle
// ────────────────────────────────────────────────────────────────────
void Pipeline::start() {
    if (running_.exchange(true)) return; // already running

    // Set timeline origin
    visualizer_.set_origin(Clock::now());

    // Start sensors
    for (auto& s : sensors_) {
        s->start();
    }

    // Launch one producer thread per sensor
    for (auto& s : sensors_) {
        sensor_threads_.emplace_back([this, raw = s.get()] {
            sensor_loop(raw);
        });
    }

    // Dispatcher thread: pulls from ingress queue, submits tasks to
    // the scheduler → thread pool.
    dispatcher_thread_ = std::thread([this] {
        while (running_) {
            auto frame = ingress_queue_.pop();
            if (!frame) break;

            ScheduledTask task;
            task.frame_id = (*frame)->frame_id;
            task.priority = TaskPriority::Normal;
            task.work     = [this, f = std::move(*frame)]() mutable {
                process_frame(std::move(f));
            };
            scheduler_->enqueue(std::move(task));
        }
    });

    // Scheduler consumer: dequeues scheduled tasks → thread pool
    // (runs in its own thread so the scheduler ordering is respected)
    std::thread scheduler_consumer([this] {
        ScheduledTask task;
        while (scheduler_->dequeue(task)) {
            pool_->submit(std::move(task.work));
        }
    });
    scheduler_consumer.detach(); // will terminate when scheduler shuts down

    // Periodic metrics printer
    metrics_thread_ = std::thread([this] {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (running_) metrics_.print_periodic_update();
        }
    });
}

void Pipeline::stop() {
    if (!running_.exchange(false)) return; // already stopped

    // Stop sensors first
    for (auto& s : sensors_) {
        s->stop();
    }

    // Close queues / scheduler so blocked threads unblock
    ingress_queue_.close();
    scheduler_->shutdown();

    for (auto& t : sensor_threads_) {
        if (t.joinable()) t.join();
    }
    sensor_threads_.clear();

    if (dispatcher_thread_.joinable()) dispatcher_thread_.join();
    if (metrics_thread_.joinable())    metrics_thread_.join();

    pool_->shutdown();
}

void Pipeline::run() {
    std::printf("[pipeline] Starting (duration=%us, threads=%zu, scheduler=%s)\n",
                cfg_.run_duration_seconds,
                cfg_.thread_pool_size,
                scheduler_->name().c_str());

    start();
    std::this_thread::sleep_for(
        std::chrono::seconds(cfg_.run_duration_seconds));
    stop();

    metrics_.print_summary();
    visualizer_.print();
    visualizer_.print_waterfall();
}

// ────────────────────────────────────────────────────────────────────
// Internal
// ────────────────────────────────────────────────────────────────────
void Pipeline::sensor_loop(Sensor* sensor) {
    while (running_) {
        auto frame = sensor->generateFrame();
        if (!frame) break;

        // Assign global monotonic id
        frame->frame_id = global_frame_id_.fetch_add(1);

        // Visualize: short marker for frame creation (not the FPS sleep)
        visualizer_.record_event(sensor->name(), frame->created_at,
                                 frame->created_at +
                                     std::chrono::microseconds(200),
                                 '#', frame->frame_id);

        // ── Bandwidth check ────────────────────────────────────────
        uint64_t delay_us = bw_manager_.request(
            sensor->name(), frame->data_size,
            sensor->bandwidth_limit_mbps());

        if (delay_us > 0) {
            // Throttled — either delay or drop.
            // Policy: delay up to 5 ms, else drop the frame.
            if (delay_us <= 5000) {
                auto throttle_start = Clock::now();
                std::this_thread::sleep_for(
                    std::chrono::microseconds(delay_us));
                visualizer_.record_event("BW-throttle", throttle_start,
                                         Clock::now(), '~', frame->frame_id);
            } else {
                metrics_.record_frame_drop(sensor->name());
                visualizer_.record_marker("frame-drop", Clock::now(), 'X');
                continue; // drop
            }
        }

        // ── Enqueue into pipeline ──────────────────────────────────
        frame->pipeline_enter = Clock::now();
        metrics_.record_pipeline_enter(
            frame->frame_id, sensor->name(), frame->pipeline_enter);

        if (!ingress_queue_.try_push(std::move(frame))) {
            // Queue full — drop
            metrics_.record_frame_drop(sensor->name());
            visualizer_.record_marker("frame-drop", Clock::now(), 'X');
        }
    }
}

void Pipeline::process_frame(std::shared_ptr<Frame> frame) {
    // Stage glyph mapping for the timeline
    static const std::map<std::string, char> stage_glyph = {
        {"preprocess", 'P'}, {"detection", 'D'}, {"tracking", 'T'}
    };

    for (auto& stage : stages_) {
        auto queue_enter = Clock::now();

        auto stage_start = Clock::now();
        auto queue_wait  = std::chrono::duration_cast<Duration>(
            stage_start - queue_enter);
        metrics_.record_queue_wait(frame->frame_id, stage->name(), queue_wait);

        stage->process(frame);

        auto stage_end   = Clock::now();
        auto stage_dur   = std::chrono::duration_cast<Duration>(
            stage_end - stage_start);
        metrics_.record_stage_time(frame->frame_id, stage->name(), stage_dur);

        // Visualize: per-stage span
        char g = '#';
        auto git = stage_glyph.find(stage->name());
        if (git != stage_glyph.end()) g = git->second;
        visualizer_.record_event(stage->name(), stage_start, stage_end,
                                 g, frame->frame_id);
    }

    frame->pipeline_exit = Clock::now();
    metrics_.record_pipeline_exit(frame->frame_id, frame->pipeline_exit);
}

} // namespace adas
