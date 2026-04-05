#include "pipeline/pipeline.h"

#include "pipeline/configured_stage.h"
#include "scheduler/fifo_scheduler.h"
#include "sensors/camera_sensor.h"
#include "sensors/radar_sensor.h"
#include "sensors/vehicle_state_sensor.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <utility>
#include <thread>

namespace adas {

namespace {

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool is_preempt_mode(const PipelineConfig& cfg) {
    return to_lower_copy(cfg.central_cycle_mode) == "preempt_previous";
}

bool is_skip_mode(const PipelineConfig& cfg) {
    return to_lower_copy(cfg.central_cycle_mode) == "skip_if_active";
}

bool is_allow_overlap_mode(const PipelineConfig& cfg) {
    const auto mode = to_lower_copy(cfg.central_cycle_mode);
    return mode == "allow_overlap" || mode.empty();
}

} // namespace

// ────────────────────────────────────────────────────────────────────
// Construction
// ────────────────────────────────────────────────────────────────────
Pipeline::Pipeline(const PipelineConfig& cfg)
    : cfg_(cfg),
    bw_manager_(cfg.global_bandwidth_limit_mbps, cfg.bandwidth_window_ms) {

    // ── Create sensors from config ─────────────────────────────────
    for (auto& sc : cfg.sensors) {
        if (sc.type == "camera") {
            sensors_.push_back(std::make_unique<CameraSensor>(
                sc.name, sc.fps, sc.frame_size_bytes, sc.bandwidth_limit_mbps));
        } else if (sc.type == "radar") {
            sensors_.push_back(std::make_unique<RadarSensor>(
                sc.name, sc.fps, sc.frame_size_bytes, sc.bandwidth_limit_mbps));
        } else if (sc.type == "vehicle_state") {
            sensors_.push_back(std::make_unique<VehicleStateSensor>(
                sc.name, sc.fps, sc.frame_size_bytes, sc.bandwidth_limit_mbps));
        }
        // TODO: add "lidar" branch when LidarSensor is implemented
        else {
            std::fprintf(stderr, "[pipeline] Unknown sensor type: %s\n",
                         sc.type.c_str());
        }
    }

    // ── Create pipeline stages ─────────────────────────────────────
    for (const auto& stage_cfg : cfg.stages) {
        stages_.push_back(std::make_unique<ConfiguredStage>(
            stage_cfg.name,
            stage_cfg.delay_us,
            stage_cfg.delay_us_min,
            stage_cfg.delay_us_max,
            stage_cfg.glyph,
            cfg_.stage_timing_mode,
            cfg_.stage_timing_sampled,
            cfg_.stage_spin_guard_us));
    }

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

    // Scheduler consumer: dequeues scheduled tasks → thread pool
    // (runs in its own thread so the scheduler ordering is respected)
    scheduler_consumer_thread_ = std::thread([this] {
        ScheduledTask task;
        while (scheduler_->dequeue(task)) {
            pool_->submit(std::move(task.work));
        }
    });

    central_loop_thread_ = std::thread([this] {
        central_loop();
    });

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

    scheduler_->shutdown();

    for (auto& t : sensor_threads_) {
        if (t.joinable()) t.join();
    }
    sensor_threads_.clear();

    if (central_loop_thread_.joinable()) central_loop_thread_.join();
    if (scheduler_consumer_thread_.joinable()) scheduler_consumer_thread_.join();
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
}

// ────────────────────────────────────────────────────────────────────
// Internal
// ────────────────────────────────────────────────────────────────────
void Pipeline::sensor_loop(Sensor* sensor) {
    uint64_t local_frame_id = 0;
    while (running_) {
        auto frame = sensor->generateFrame();
        if (!frame) break;

        frame->frame_id = local_frame_id++;

        // Visualize: short marker for frame creation (not the FPS sleep)
        visualizer_.record_event(sensor->name(), frame->created_at,
                                 frame->created_at +
                                     std::chrono::microseconds(200),
                                 '#', frame->frame_id, frame->sensor_name);

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
                                         Clock::now(), '~', frame->frame_id,
                                         frame->sensor_name);
            } else {
                metrics_.record_frame_drop(sensor->name());
                visualizer_.record_marker(
                    "frame-drop", Clock::now(), 'X', frame->frame_id,
                    sensor->name(),
                    "BW wait " + std::to_string(delay_us) + " us > drop threshold 5000 us");
                continue; // drop
            }
        }

        // ── Enqueue into pipeline ──────────────────────────────────
        frame->pipeline_enter = Clock::now();

        ScheduledTask task;
        task.frame_id = frame->frame_id;
        task.priority = TaskPriority::Normal;

        if (is_camera_sensor(*sensor)) {
            task.work = [this, f = std::move(frame)]() mutable {
                process_camera_frame(std::move(f));
            };
        } else if (is_radar_sensor(*sensor)) {
            task.work = [this, f = std::move(frame)]() mutable {
                process_radar_frame(std::move(f));
            };
        } else if (is_vehicle_state_sensor(*sensor)) {
            std::lock_guard lock(sensor_state_mutex_);
            latest_vehicle_state_id_ = frame->frame_id;
            continue;
        } else {
            metrics_.record_frame_drop(sensor->name());
            visualizer_.record_marker(
                "frame-drop", Clock::now(), 'X', frame->frame_id,
                sensor->name(), "Unsupported sensor type for enqueue path");
            continue;
        }

        scheduler_->enqueue(std::move(task));
    }
}

void Pipeline::central_loop() {
    const auto period = std::chrono::duration<double>(1.0 / std::max(1.0, cfg_.central_loop_rate_hz));
    auto next_tick = Clock::now();
    const bool preempt_mode = is_preempt_mode(cfg_);
    const bool skip_mode = is_skip_mode(cfg_);
    const bool allow_overlap_mode = is_allow_overlap_mode(cfg_);

    while (running_) {
        next_tick += std::chrono::duration_cast<Clock::duration>(period);

        if (skip_mode) {
            const uint64_t active = active_world_cycle_id_.load();
            if (active != kNoActiveCycle) {
                const uint64_t skipped_id = world_frame_id_.fetch_add(1);
                visualizer_.record_marker(
                    "cycle-miss", Clock::now(), '!', skipped_id,
                    "central_loop", "Periodic cycle skipped: previous cycle still active");
                std::this_thread::sleep_until(next_tick);
                continue;
            }
        }

        auto frame = std::make_shared<Frame>();
        frame->frame_id = world_frame_id_.fetch_add(1);
        frame->sensor_name = "central_loop";
        frame->pipeline_enter = Clock::now();
        metrics_.record_pipeline_enter(frame->frame_id, frame->sensor_name, frame->pipeline_enter);

        if (preempt_mode) {
            const uint64_t previous_active = active_world_cycle_id_.exchange(frame->frame_id);
            if (previous_active != kNoActiveCycle && previous_active != frame->frame_id) {
                visualizer_.record_marker(
                    "cycle-miss", Clock::now(), '!', previous_active,
                    "central_loop", "Periodic deadline missed: superseded by newer cycle");
            }
        } else if (skip_mode) {
            active_world_cycle_id_.store(frame->frame_id);
        } else if (allow_overlap_mode) {
            // Detect overlap: if a cycle is already active, record overlap event
            const uint64_t active = active_world_cycle_id_.load();
            if (active != kNoActiveCycle) {
                visualizer_.record_marker(
                    "overlap", Clock::now(), '^', frame->frame_id,
                    "central_loop", "Cycles overlapping: new cycle started before previous completed");
            }
            active_world_cycle_id_.store(frame->frame_id);
        }

        ScheduledTask task;
        task.frame_id = frame->frame_id;
        task.priority = TaskPriority::High;
        task.work = [this, f = std::move(frame)]() mutable {
            process_central_cycle(std::move(f));
        };
        scheduler_->enqueue(std::move(task));

        std::this_thread::sleep_until(next_tick);
    }
}

void Pipeline::process_camera_frame(std::shared_ptr<Frame> frame) {
    if (auto* stage = find_stage_by_id("sense_1_1_camera_processing")) {
        run_stage(*stage, frame, false);
    }

    {
        std::lock_guard lock(sensor_state_mutex_);
        latest_camera_output_id_ = frame->frame_id;
    }
}

void Pipeline::process_radar_frame(std::shared_ptr<Frame> frame) {
    if (auto* stage = find_stage_by_id("sense_1_2_radar_processing")) {
        run_stage(*stage, frame, false);
    }

    {
        std::lock_guard lock(sensor_state_mutex_);
        latest_radar_output_id_ = frame->frame_id;
    }
}

void Pipeline::process_central_cycle(std::shared_ptr<Frame> frame) {
    static const char* central_stage_ids[] = {
        "sense_1_3_sensor_acquisition",
        "sense_1_4_fusion",
        "sense_1_5_localization",
        "sense_1_6_world_map_build",
        "plan_2_1_prediction",
        "plan_2_2_behavior_planning",
        "plan_2_3_trajectory_planning",
        "plan_2_4_trajectory_plausibility_check",
        "act_3_1_control",
        "act_3_2_feedback"
    };

    const bool preempt_mode = is_preempt_mode(cfg_);

    for (const char* stage_id : central_stage_ids) {
        if (preempt_mode && active_world_cycle_id_.load() != frame->frame_id) {
            return; // superseded by a newer periodic cycle
        }
        if (auto* stage = find_stage_by_id(stage_id)) {
            run_stage(*stage, frame, true);
        }
    }

    frame->pipeline_exit = Clock::now();
    metrics_.record_pipeline_exit(frame->frame_id, frame->pipeline_exit);

    if (preempt_mode || is_skip_mode(cfg_)) {
        uint64_t expected = frame->frame_id;
        active_world_cycle_id_.compare_exchange_strong(expected, kNoActiveCycle);
    }
}

void Pipeline::run_stage(PipelineStage& stage, std::shared_ptr<Frame>& frame, bool collect_metrics) {
    auto queue_enter = Clock::now();
    auto stage_start = Clock::now();
    auto queue_wait  = std::chrono::duration_cast<Duration>(stage_start - queue_enter);

    if (collect_metrics) {
        metrics_.record_queue_wait(frame->frame_id, stage.name(), queue_wait);
    }

    stage.process(frame);

    auto stage_end = Clock::now();
    auto stage_dur = std::chrono::duration_cast<Duration>(stage_end - stage_start);
    if (collect_metrics) {
        metrics_.record_stage_time(frame->frame_id, stage.name(), stage_dur);
    }

    visualizer_.record_event(stage.name(), stage_start, stage_end,
                             stage.glyph(), frame->frame_id,
                             frame->sensor_name);
}

PipelineStage* Pipeline::find_stage_by_id(const std::string& stage_id) {
    for (size_t index = 0; index < cfg_.stages.size() && index < stages_.size(); ++index) {
        if (cfg_.stages[index].id == stage_id) {
            return stages_[index].get();
        }
    }
    return nullptr;
}

bool Pipeline::is_camera_sensor(const Sensor& sensor) const {
    return sensor.type() == SensorType::Camera;
}

bool Pipeline::is_radar_sensor(const Sensor& sensor) const {
    return sensor.type() == SensorType::Radar;
}

bool Pipeline::is_vehicle_state_sensor(const Sensor& sensor) const {
    return sensor.type() == SensorType::VehicleState;
}

} // namespace adas
