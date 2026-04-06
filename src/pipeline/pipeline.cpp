#include "pipeline/pipeline.h"

#include "pipeline/configured_stage.h"
#include "scheduler/fifo_scheduler.h"
#include "sensors/camera_sensor.h"
#include "sensors/radar_sensor.h"
#include "sensors/vehicle_state_sensor.h"
#include "core/task_scheduler.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <future>
#include <limits>
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

    // ── Create task scheduler ──────────────────────────────────────
    task_scheduler_ = std::make_unique<TaskScheduler>(
        cfg.cpu_cores.empty() ? cfg.thread_pool_size : cfg.cpu_cores.size(),
        cfg.central_loop_rate_hz);
    
    if (!cfg.cpu_cores.empty()) {
        task_scheduler_->initialize_cores(cfg.cpu_cores);
    }

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
            // Poll in short intervals so stop() is noticed quickly.
            for (int i = 0; i < 20 && running_; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
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

        // ── Apply sensor jitter ────────────────────────────────────
        // Find jitter config for this sensor
        double jitter_pct = 0.0;
        for (const auto& sensor_cfg : cfg_.sensors) {
            if (sensor_cfg.name == sensor->name()) {
                jitter_pct = sensor_cfg.jitter_percentage;
                break;
            }
        }
        
        if (jitter_pct > 0.0) {
            // Jitter the sensor's frame interval (not an absolute epoch timestamp)
            // to avoid overflow and unrealistic multi-second delays.
            const double fps = std::max(1e-6, sensor->fps());
            const uint32_t base_interval_us = static_cast<uint32_t>(
                std::llround(std::min(1'000'000.0 / fps,
                                      static_cast<double>(std::numeric_limits<uint32_t>::max()))));
            const uint32_t jittered_interval_us =
                JitterSimulator::apply_jitter(base_interval_us, jitter_pct);

            const int64_t jitter_delta_us =
                static_cast<int64_t>(jittered_interval_us) - static_cast<int64_t>(base_interval_us);
            if (jitter_delta_us > 0) {
                // Clamp to keep shutdown responsive even with aggressive jitter settings.
                constexpr int64_t kMaxJitterSleepUs = 50'000;
                std::this_thread::sleep_for(
                    std::chrono::microseconds(std::min(jitter_delta_us, kMaxJitterSleepUs)));
            }
        }

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
            // Apply cycle mode to radar frame dispatch (mirrors central_loop policy)
            if (is_skip_mode(cfg_)) {
                uint64_t expected = kNoActiveCycle;
                if (!active_radar_frame_id_.compare_exchange_strong(expected, frame->frame_id)) {
                    visualizer_.record_marker(
                        "cycle-miss", Clock::now(), '!', frame->frame_id,
                        sensor->name(), "Radar frame skipped: previous frame still processing");
                    continue;
                }
            } else if (is_preempt_mode(cfg_)) {
                const uint64_t prev = active_radar_frame_id_.exchange(frame->frame_id);
                if (prev != kNoActiveCycle) {
                    visualizer_.record_marker(
                        "frame-drop", Clock::now(), 'X', prev,
                        sensor->name(), "Radar frame dropped: preempted by newer frame");
                }
            } else if (is_allow_overlap_mode(cfg_)) {
                if (active_radar_count_.load() > 0) {
                    visualizer_.record_marker(
                        "overlap", Clock::now(), '^', frame->frame_id,
                        sensor->name(), "Radar frames overlapping: new frame started before previous completed");
                }
                active_radar_count_.fetch_add(1);
            }
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
                    "frame-drop", Clock::now(), 'X', previous_active,
                    "central_loop", "Frame dropped: cycle preempted by newer cycle");
            }
        } else if (skip_mode) {
            active_world_cycle_id_.store(frame->frame_id);
        } else if (allow_overlap_mode) {
            // Detect overlap: if any cycle is still in-flight, record overlap event
            if (active_cycle_count_.load() > 0) {
                visualizer_.record_marker(
                    "overlap", Clock::now(), '^', frame->frame_id,
                    "central_loop", "Cycles overlapping: new cycle started before previous completed");
            }
            active_cycle_count_.fetch_add(1);
        }

        ScheduledTask task;
        task.frame_id = frame->frame_id;
        task.priority = TaskPriority::High;
        task.work = [this, f = std::move(frame)]() mutable {
            process_central_cycle(std::move(f));
        };
        scheduler_->enqueue(std::move(task));

        // Sleep in small increments so stop() is noticed promptly.
        while (running_ && Clock::now() < next_tick)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
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
    const bool preempt_mode = is_preempt_mode(cfg_);

    // Preserve legacy behavior when only the coarse radar stage is configured.
    const bool has_detailed_radar_pipeline =
        find_stage_by_id("sense_1_2_1_adc_ingest_and_calibration") != nullptr;

    if (!has_detailed_radar_pipeline) {
        if (auto* stage = find_stage_by_id("sense_1_2_radar_processing")) {
            run_stage(*stage, frame, false);
        }
    } else {
        static const char* pre_stage_ids[] = {
            "sense_1_2_1_adc_ingest_and_calibration"
        };
        static const char* signal_branch_a_ids[] = {
            "sense_1_2_2_range_fft",
            "sense_1_2_3_doppler_fft"
        };
        static const char* signal_branch_b_ids[] = {
            "sense_1_2_4_interference_mitigation",
            "sense_1_2_5_static_clutter_suppression"
        };
        static const char* detect_and_track_ids[] = {
            "sense_1_2_6_cfar_detection_2d",
            "sense_1_2_7_angle_of_arrival_estimation",
            "sense_1_2_8_doppler_phase_unwrap_unambiguous",
            "sense_1_2_9_point_clustering",
            "sense_1_2_10_multi_target_tracking"
        };
        static const char* radar_only_branch_a_ids[] = {
            "sense_1_2_11_free_space_occupancy_grid"
        };
        static const char* radar_only_branch_b_ids[] = {
            "sense_1_2_12_road_boundary_and_guardrail_estimation"
        };
        static const char* post_stage_ids[] = {
            "sense_1_2_13_object_list_packaging"
        };

        auto run_stage_list = [this, &frame, preempt_mode](const char* const* stage_ids, size_t count) {
            for (size_t i = 0; i < count; ++i) {
                if (preempt_mode && active_radar_frame_id_.load() != frame->frame_id) {
                    return; // superseded by a newer radar frame
                }
                if (auto* stage = find_stage_by_id(stage_ids[i])) {
                    run_stage(*stage, frame, false);
                }
            }
        };

        run_stage_list(pre_stage_ids, sizeof(pre_stage_ids) / sizeof(pre_stage_ids[0]));

        if (preempt_mode && active_radar_frame_id_.load() != frame->frame_id) goto radar_done;

        {
            auto signal_branch_a_future = std::async(
                std::launch::async,
                run_stage_list,
                signal_branch_a_ids,
                sizeof(signal_branch_a_ids) / sizeof(signal_branch_a_ids[0]));
            auto signal_branch_b_future = std::async(
                std::launch::async,
                run_stage_list,
                signal_branch_b_ids,
                sizeof(signal_branch_b_ids) / sizeof(signal_branch_b_ids[0]));
            signal_branch_a_future.get();
            signal_branch_b_future.get();
        }

        if (preempt_mode && active_radar_frame_id_.load() != frame->frame_id) goto radar_done;

        run_stage_list(detect_and_track_ids, sizeof(detect_and_track_ids) / sizeof(detect_and_track_ids[0]));

        if (preempt_mode && active_radar_frame_id_.load() != frame->frame_id) goto radar_done;

        {
            auto radar_only_branch_a_future = std::async(
                std::launch::async,
                run_stage_list,
                radar_only_branch_a_ids,
                sizeof(radar_only_branch_a_ids) / sizeof(radar_only_branch_a_ids[0]));
            auto radar_only_branch_b_future = std::async(
                std::launch::async,
                run_stage_list,
                radar_only_branch_b_ids,
                sizeof(radar_only_branch_b_ids) / sizeof(radar_only_branch_b_ids[0]));
            radar_only_branch_a_future.get();
            radar_only_branch_b_future.get();
        }

        if (preempt_mode && active_radar_frame_id_.load() != frame->frame_id) goto radar_done;

        run_stage_list(post_stage_ids, sizeof(post_stage_ids) / sizeof(post_stage_ids[0]));
    }

    {
        std::lock_guard lock(sensor_state_mutex_);
        latest_radar_output_id_ = frame->frame_id;
    }

radar_done:
    if (preempt_mode || is_skip_mode(cfg_)) {
        uint64_t expected = frame->frame_id;
        active_radar_frame_id_.compare_exchange_strong(expected, kNoActiveCycle);
    } else if (is_allow_overlap_mode(cfg_)) {
        active_radar_count_.fetch_sub(1);
    }
}

void Pipeline::process_central_cycle(std::shared_ptr<Frame> frame) {
    static const char* pre_branch_stage_id = "sense_1_3_sensor_acquisition";
    static const char* deterministic_branch_ids[] = {
        "sense_1_4_fusion",
        "sense_1_5_localization",
        "sense_1_6_world_map_build"
    };
    static const char* cognitive_branch_ids[] = {
        "sense_1_7_cognitive_semantic_reasoning",
        "sense_1_8_cognitive_intent_generation",
        "sense_1_9_semantic_adapter",
        "sense_1_10_sce_validation",
        "sense_1_11_cdnp_negotiation"
    };
    static const char* post_merge_stage_ids[] = {
        "plan_2_0_context_fusion",
        "plan_2_1_prediction",
        "plan_2_2_behavior_planning",
        "plan_2_3_trajectory_planning",
        "plan_2_4_trajectory_plausibility_check",
        "act_3_1_control",
        "act_3_2_feedback"
    };

    const bool preempt_mode = is_preempt_mode(cfg_);

    // Stage 1: Acquire synchronized sensor snapshot before splitting branches
    if (auto* stage = find_stage_by_id(pre_branch_stage_id)) {
        run_stage(*stage, frame, true);
    }

    if (preempt_mode && active_world_cycle_id_.load() != frame->frame_id) {
        return; // superseded by a newer periodic cycle
    }

    // Stage 2: Execute deterministic and cognitive branches in parallel.
    // Both consume the same synchronized snapshot and rejoin before planning.
    auto run_branch = [this, &frame, preempt_mode](const char* const* stage_ids, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            if (preempt_mode && active_world_cycle_id_.load() != frame->frame_id) {
                return;
            }
            if (auto* stage = find_stage_by_id(stage_ids[i])) {
                run_stage(*stage, frame, true);
            }
        }
    };

    auto deterministic_future = std::async(
        std::launch::async,
        run_branch,
        deterministic_branch_ids,
        sizeof(deterministic_branch_ids) / sizeof(deterministic_branch_ids[0]));

    auto cognitive_future = std::async(
        std::launch::async,
        run_branch,
        cognitive_branch_ids,
        sizeof(cognitive_branch_ids) / sizeof(cognitive_branch_ids[0]));

    deterministic_future.get();
    cognitive_future.get();

    if (preempt_mode && active_world_cycle_id_.load() != frame->frame_id) {
        return; // superseded while branches were in-flight
    }

    // Stage 3: Merge deterministic + cognitive context, then plan and act.
    for (const char* stage_id : post_merge_stage_ids) {
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
    } else if (is_allow_overlap_mode(cfg_)) {
        active_cycle_count_.fetch_sub(1);
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
    
    // ── Deadline tracking (if enabled) ─────────────────────────────
    if (cfg_.track_deadline_misses) {
        // Find the stage config to get expected timing and priority
        const StageConfig* stage_cfg = nullptr;
        for (const auto& sc : cfg_.stages) {
            if (sc.id == stage.name() || sc.name == stage.name()) {
                stage_cfg = &sc;
                break;
            }
        }
        
        if (stage_cfg) {
            // Expected deadline: stage's avg execution time + some margin (10%)
            uint32_t expected_us = static_cast<uint32_t>(stage_cfg->delay_us * 1.1);
            uint32_t actual_us = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(stage_dur).count());
            
            if (actual_us > expected_us) {
                // Deadline miss detected
                metrics_.record_deadline_miss(frame->frame_id, stage.name(), actual_us, expected_us);
                task_scheduler_->check_deadlines();
            }
        }
    }
    
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
