#include "core/config_loader.h"
#include "pipeline/pipeline.h"

#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <fstream>
#include <map>
#include <memory>
#include <limits>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

double to_ms(adas::TimePoint t, adas::TimePoint origin) {
    return std::chrono::duration<double, std::milli>(t - origin).count();
}

std::string to_lower_copy(const std::string& input) {
    std::string lowered = input;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

ImVec4 glyph_color(char glyph) {
    switch (glyph) {
        case '#': return ImVec4(0.60f, 0.80f, 1.00f, 1.00f); // sensor
        case 'S': return ImVec4(0.28f, 0.78f, 0.96f, 1.00f); // sense
        case 'P': return ImVec4(0.96f, 0.72f, 0.24f, 1.00f); // plan
        case 'A': return ImVec4(0.95f, 0.40f, 0.35f, 1.00f); // act
        case '~': return ImVec4(0.70f, 0.70f, 1.00f, 1.00f); // throttle
        case 'X': return ImVec4(1.00f, 0.25f, 0.25f, 1.00f); // drop
        case '!': return ImVec4(1.00f, 0.62f, 0.20f, 1.00f); // missed cycle
        case '^': return ImVec4(0.80f, 0.40f, 1.00f, 1.00f); // overlap
        default:  return ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
    }
}

ImVec4 blend_color(const ImVec4& base, const ImVec4& target, float t) {
    return ImVec4(
        base.x + (target.x - base.x) * t,
        base.y + (target.y - base.y) * t,
        base.z + (target.z - base.z) * t,
        base.w + (target.w - base.w) * t);
}

ImVec4 frame_base_color(uint64_t frame_id) {
    static const ImVec4 palette[5] = {
        ImVec4(0.10f, 0.67f, 0.98f, 1.00f), // blue
        ImVec4(0.12f, 0.80f, 0.50f, 1.00f), // green
        ImVec4(0.97f, 0.66f, 0.20f, 1.00f), // amber
        ImVec4(0.93f, 0.36f, 0.31f, 1.00f), // red
        ImVec4(0.66f, 0.52f, 0.95f, 1.00f), // violet
    };
    return palette[frame_id % 5ULL];
}

ImVec4 event_color(const adas::TimelineVisualizer::Event& event) {
    const std::string lane_lower = to_lower_copy(event.lane);
    if (lane_lower.find("camera processing") != std::string::npos) {
        return ImVec4(0.78f, 0.82f, 0.88f, 1.00f); // cool neutral-blue
    }
    if (lane_lower.find("radar processing") != std::string::npos) {
        return ImVec4(0.86f, 0.82f, 0.76f, 1.00f); // warm neutral-sand
    }
    if (event.glyph == 'S' || event.glyph == 'P' || event.glyph == 'A') {
        const ImVec4 base = frame_base_color(event.frame_id);
        if (event.glyph == 'S') {
            return blend_color(base, ImVec4(1.00f, 1.00f, 1.00f, 1.00f), 0.32f); // light
        }
        if (event.glyph == 'P') {
            return base; // medium
        }
        return blend_color(base, ImVec4(0.00f, 0.00f, 0.00f, 1.00f), 0.35f); // dark
    }
    return glyph_color(event.glyph);
}

const adas::StageConfig* find_stage(const adas::PipelineConfig& cfg, const std::string& stage_id) {
    for (const auto& stage : cfg.stages) {
        if (stage.id == stage_id) {
            return &stage;
        }
    }
    return nullptr;
}

struct LaneDef {
    std::string key;
    std::string label;
    bool is_sensor = false;
    bool is_stage = false;
    bool is_system = false;
    std::string phase_id;
    std::string phase_name;
};

struct ComputeProfileDef {
    std::string id;
    std::string label;
    double      stage_delay_scale = 1.0;
    double      global_bandwidth_scale = 1.0;
    size_t      thread_pool_override = 0;
    double      central_loop_rate_hz_override = 0.0;
    std::string central_cycle_mode_override;
};

std::vector<ComputeProfileDef> default_compute_profiles() {
    return {
        {"eco", "Eco", 1.30, 0.90, 6},
        {"nominal", "Nominal", 1.00, 1.00, 0},
        {"performance", "Performance", 0.80, 1.10, 10},
    };
}

bool load_compute_profiles_from_json(const std::string& path,
                                     std::vector<ComputeProfileDef>& out_profiles,
                                     std::string& error) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        error = "profile file not found";
        return false;
    }

    nlohmann::json root;
    try {
        ifs >> root;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }

    if (!root.is_object() || !root.contains("profiles") || !root["profiles"].is_array()) {
        error = "missing 'profiles' array";
        return false;
    }

    std::vector<ComputeProfileDef> parsed;
    for (const auto& item : root["profiles"]) {
        if (!item.is_object()) {
            continue;
        }

        ComputeProfileDef profile;
        profile.id = item.value("id", std::string());
        profile.label = item.value("label", profile.id);
        profile.stage_delay_scale = item.value("stage_delay_scale", 1.0);
        profile.global_bandwidth_scale = item.value("global_bandwidth_scale", 1.0);
        profile.thread_pool_override = item.value("thread_pool_size", size_t(0));
        profile.central_loop_rate_hz_override = item.value("central_loop_rate_hz", 0.0);
        profile.central_cycle_mode_override = item.value("central_cycle_mode", std::string());

        if (profile.id.empty() || profile.label.empty() || profile.stage_delay_scale <= 0.0) {
            continue;
        }
        if (profile.global_bandwidth_scale <= 0.0) {
            profile.global_bandwidth_scale = 1.0;
        }
        parsed.push_back(std::move(profile));
    }

    if (parsed.empty()) {
        error = "no valid profiles found";
        return false;
    }

    out_profiles = std::move(parsed);
    error.clear();
    return true;
}

adas::PipelineConfig make_runtime_config(const adas::PipelineConfig& base_cfg,
                                         const std::map<std::string, bool>& sensor_enabled,
                                         const ComputeProfileDef& profile,
                                         const std::string& central_cycle_mode,
                                         const std::map<std::string, float>& stage_scales,
                                         const std::string& stage_timing_mode,
                                         bool stage_timing_sampled,
                                         uint32_t stage_spin_guard_us) {
    adas::PipelineConfig runtime_cfg = base_cfg;
    runtime_cfg.central_cycle_mode = profile.central_cycle_mode_override.empty()
        ? central_cycle_mode
        : profile.central_cycle_mode_override;
    runtime_cfg.stage_timing_mode = stage_timing_mode;
    runtime_cfg.stage_timing_sampled = stage_timing_sampled;
    runtime_cfg.stage_spin_guard_us = stage_spin_guard_us;

    runtime_cfg.sensors.erase(
        std::remove_if(runtime_cfg.sensors.begin(), runtime_cfg.sensors.end(),
                       [&](const adas::PipelineConfig::SensorCfg& sensor) {
                           auto it = sensor_enabled.find(sensor.name);
                           return it != sensor_enabled.end() && !it->second;
                       }),
        runtime_cfg.sensors.end());

    for (auto& stage : runtime_cfg.stages) {
        const float stage_scale = stage_scales.count(stage.id) ? stage_scales.at(stage.id) : 1.0f;
        const double total_scale = profile.stage_delay_scale * static_cast<double>(stage_scale);
        stage.delay_us_min = static_cast<uint32_t>(std::max(1.0, std::round(static_cast<double>(stage.delay_us_min) * total_scale)));
        stage.delay_us = static_cast<uint32_t>(std::max(1.0, std::round(static_cast<double>(stage.delay_us) * total_scale)));
        stage.delay_us_max = static_cast<uint32_t>(std::max(1.0, std::round(static_cast<double>(stage.delay_us_max) * total_scale)));
        if (stage.delay_us < stage.delay_us_min) stage.delay_us = stage.delay_us_min;
        if (stage.delay_us_max < stage.delay_us) stage.delay_us_max = stage.delay_us;
    }

    runtime_cfg.global_bandwidth_limit_mbps =
        std::max(1.0, runtime_cfg.global_bandwidth_limit_mbps * profile.global_bandwidth_scale);

    if (profile.thread_pool_override > 0) {
        runtime_cfg.thread_pool_size = profile.thread_pool_override;
    }

    if (profile.central_loop_rate_hz_override > 0.0) {
        runtime_cfg.central_loop_rate_hz = profile.central_loop_rate_hz_override;
    }

    return runtime_cfg;
}

const char* central_cycle_mode_label(const std::string& mode_id) {
    const std::string lowered = to_lower_copy(mode_id);
    if (lowered == "preempt_previous") {
        return "Preempt previous";
    }
    if (lowered == "skip_if_active") {
        return "Skip new if active";
    }
    return "Allow overlap";
}

int sensor_rank(const adas::PipelineConfig::SensorCfg& sensor) {
    if (sensor.type == "camera") return 0;
    if (sensor.type == "radar") return 1;
    if (sensor.type == "vehicle_state") return 2;
    return 3;
}

int stage_group_rank(const adas::StageConfig& stage) {
    (void)stage;
    return 0;
}

std::vector<LaneDef> build_ordered_lanes(const adas::PipelineConfig& cfg) {
    std::vector<LaneDef> lanes;

    std::vector<adas::PipelineConfig::SensorCfg> sensors = cfg.sensors;
    std::sort(sensors.begin(), sensors.end(), [](const auto& lhs, const auto& rhs) {
        const int lhs_rank = sensor_rank(lhs);
        const int rhs_rank = sensor_rank(rhs);
        if (lhs_rank != rhs_rank) {
            return lhs_rank < rhs_rank;
        }
        return lhs.name < rhs.name;
    });

    for (const auto& sensor : sensors) {
        LaneDef lane;
        lane.key = sensor.name;
        lane.label = sensor.short_name.empty() ? sensor.name : sensor.short_name;
        lane.is_sensor = true;
        lanes.push_back(std::move(lane));
    }

    for (const auto& phase : cfg.phases) {
        std::vector<const adas::StageConfig*> phase_stages;
        for (const auto& stage_id : phase.stage_ids) {
            const auto* stage = find_stage(cfg, stage_id);
            if (stage) {
                phase_stages.push_back(stage);
            }
        }

        std::stable_sort(phase_stages.begin(), phase_stages.end(), [](const auto* lhs, const auto* rhs) {
            const int lhs_rank = stage_group_rank(*lhs);
            const int rhs_rank = stage_group_rank(*rhs);
            if (lhs_rank != rhs_rank) {
                return lhs_rank < rhs_rank;
            }
            return lhs->name < rhs->name;
        });

        for (const auto* stage : phase_stages) {
            LaneDef lane;
            lane.key = stage->lane;
            lane.label = stage->name;
            lane.is_stage = true;
            lane.phase_id = stage->phase_id;
            lane.phase_name = stage->phase_name;
            lanes.push_back(std::move(lane));
        }
    }

    lanes.push_back({"BW-throttle", "BW-throttle", false, false, true, "system", "System"});
    lanes.push_back({"frame-drop", "frame-drop", false, false, true, "system", "System"});
    lanes.push_back({"cycle-miss", "cycle-miss", false, false, true, "system", "System"});
    lanes.push_back({"overlap", "overlap", false, false, true, "system", "System"});
    return lanes;
}

std::map<std::string, int> build_lane_thread_counts(const adas::PipelineConfig& cfg) {
    int camera_count = 0;
    int radar_count = 0;
    for (const auto& sensor : cfg.sensors) {
        if (sensor.type == "camera") {
            ++camera_count;
        } else if (sensor.type == "radar") {
            ++radar_count;
        }
    }

    std::map<std::string, int> lane_threads;
    for (const auto& sensor : cfg.sensors) {
        lane_threads[sensor.name] = 1;
    }

    for (const auto& stage : cfg.stages) {
        int threads = 1;
        if (stage.id == "sense_1_1_camera_processing") {
            threads = std::max(1, camera_count);
        } else if (stage.id == "sense_1_2_radar_processing") {
            threads = std::max(1, radar_count);
        }
        lane_threads[stage.lane] = threads;
    }

    lane_threads["BW-throttle"] = 1;
    lane_threads["frame-drop"] = 1;
    lane_threads["cycle-miss"] = 1;
    lane_threads["overlap"] = 1;
    return lane_threads;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/adas_pipeline_config.json";
    if (argc > 1) config_path = argv[1];

    adas::PipelineConfig base_cfg;
    try {
        base_cfg = adas::load_config(config_path);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "Failed to load config: %s\n", ex.what());
        return 1;
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1400, 900, "ADAS Sense-Plan-Act Viewer (ImGui + ImPlot)", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    float ui_scale = 1.55f;
    io.FontGlobalScale = ui_scale;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    std::vector<ComputeProfileDef> profiles = default_compute_profiles();
    int selected_profile_index = 1;
    std::string profile_status = "Using built-in profiles";
    std::string profile_json_path = "config/ecu_profiles.json";
    static const char* kCycleModeIds[] = {"allow_overlap", "skip_if_active", "preempt_previous"};
    static const char* kCycleModeLabels[] = {"Allow overlap", "Skip new if active", "Preempt previous"};
    int selected_cycle_mode = 0;
    {
        const std::string mode = to_lower_copy(base_cfg.central_cycle_mode);
        if (mode == "skip_if_active") {
            selected_cycle_mode = 1;
        } else if (mode == "preempt_previous") {
            selected_cycle_mode = 2;
        }
    }

    std::map<std::string, bool> sensor_enabled;
    for (const auto& sensor : base_cfg.sensors) {
        sensor_enabled[sensor.name] = true;
    }

    std::map<std::string, float> stage_scales;
    for (const auto& stage : base_cfg.stages) {
        stage_scales[stage.id] = 1.0f;
    }
    std::string stage_timing_mode = to_lower_copy(base_cfg.stage_timing_mode);
    if (stage_timing_mode != "sleep" && stage_timing_mode != "spin" && stage_timing_mode != "hybrid") {
        stage_timing_mode = "hybrid";
    }
    bool stage_timing_sampled = base_cfg.stage_timing_sampled;
    int stage_spin_guard_us = static_cast<int>(base_cfg.stage_spin_guard_us);

    adas::PipelineConfig runtime_cfg = make_runtime_config(
        base_cfg,
        sensor_enabled,
        profiles[static_cast<size_t>(selected_profile_index)],
        kCycleModeIds[selected_cycle_mode],
        stage_scales,
        stage_timing_mode,
        stage_timing_sampled,
        static_cast<uint32_t>(std::max(0, stage_spin_guard_us)));

    std::unique_ptr<adas::Pipeline> pipeline = std::make_unique<adas::Pipeline>(runtime_cfg);

    std::atomic<bool> sim_running{false};
    std::atomic<bool> sim_done{false};
    std::atomic<bool> sim_stop_requested{false};

    std::thread sim_thread;

    auto start_simulation = [&]() {
        sim_stop_requested = false;
        sim_done = false;
        sim_running = true;

        sim_thread = std::thread([&]() {
            pipeline->start();
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(runtime_cfg.run_duration_seconds);
            while (!sim_stop_requested && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            pipeline->stop();
            sim_running = false;
            sim_done = true;
        });
    };

    auto stop_simulation = [&]() {
        if (sim_running) {
            sim_stop_requested = true;
            pipeline->stop();
        }
        if (sim_thread.joinable()) {
            sim_thread.join();
        }
        sim_running = false;
    };

    start_simulation();

    float view_window_ms = 200.0f;
    float manual_view_start_ms = 0.0f;
    bool show_frame_numbers = true;
    std::map<std::string, bool> lane_visibility;
    std::vector<LaneDef> lane_defs = build_ordered_lanes(runtime_cfg);
    std::map<std::string, int> lane_thread_counts = build_lane_thread_counts(runtime_cfg);

    std::vector<double> throughput_t;
    std::vector<double> throughput_fps;
    auto hist_t0 = std::chrono::steady_clock::now();
    auto last_sample = hist_t0;
    size_t last_completed = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        auto snap = pipeline->visualizer().snapshot();
        auto sorted_events = snap.events;
        std::stable_sort(sorted_events.begin(), sorted_events.end(),
                         [](const adas::TimelineVisualizer::Event& lhs,
                            const adas::TimelineVisualizer::Event& rhs) {
                             const auto lhs_key = std::make_tuple(lhs.lane, lhs.start, lhs.frame_id);
                             const auto rhs_key = std::make_tuple(rhs.lane, rhs.start, rhs.frame_id);
                             return lhs_key < rhs_key;
                         });
        io.FontGlobalScale = ui_scale;

        double max_ms = 0.0;
        for (const auto& e : snap.events) {
            max_ms = std::max(max_ms, to_ms(e.end, snap.origin));
        }
        for (const auto& m : snap.markers) {
            max_ms = std::max(max_ms, to_ms(m.time, snap.origin));
        }
        if (max_ms < 1.0) max_ms = 1.0;

        size_t drops = 0;
        size_t throttles = 0;
        size_t missed_cycles = 0;
        size_t overlap_cycles = 0;

        struct LaneStats {
            int count = 0;
            double sum_ms = 0.0;
            double first_start_ms = -1.0;
            double last_start_ms = -1.0;
            void add(double start_ms, double dur_ms) {
                ++count;
                sum_ms += dur_ms;
                if (first_start_ms < 0.0 || start_ms < first_start_ms) first_start_ms = start_ms;
                if (last_start_ms < 0.0 || start_ms > last_start_ms) last_start_ms = start_ms;
            }
            double avg() const { return count > 0 ? sum_ms / static_cast<double>(count) : 0.0; }
            double freq_hz() const {
                if (count < 2 || last_start_ms <= first_start_ms) return 0.0;
                const double dt_s = (last_start_ms - first_start_ms) / 1000.0;
                return dt_s > 0.0 ? static_cast<double>(count - 1) / dt_s : 0.0;
            }
        };

        std::map<std::string, LaneStats> lane_stats;
        std::map<std::string, const LaneDef*> lane_lookup;
        for (const auto& lane_def : lane_defs) {
            lane_lookup[lane_def.key] = &lane_def;
            if (lane_visibility.find(lane_def.key) == lane_visibility.end()) {
                lane_visibility[lane_def.key] = true;
            }
        }

        for (const auto& e : sorted_events) {
            if (e.glyph == '~') ++throttles;
            const double start_ms = to_ms(e.start, snap.origin);
            const double dur_ms = std::max(0.0, to_ms(e.end, e.start));
            lane_stats[e.lane].add(start_ms, dur_ms);
        }
        for (const auto& m : snap.markers) {
            if (m.glyph == 'X') ++drops;
            if (m.glyph == '!') ++missed_cycles;
            if (m.glyph == '^') ++overlap_cycles;
        }

        const size_t cycle_count = pipeline->metrics().total_completed();
        const float max_timeline_start_ms = std::max(0.0f, static_cast<float>(max_ms) - view_window_ms);

        auto now = std::chrono::steady_clock::now();
        if (sim_running && now - last_sample >= std::chrono::seconds(1)) {
            const double tsec = std::chrono::duration<double>(now - hist_t0).count();
            const size_t curr = pipeline->metrics().total_completed();
            const double dt = std::chrono::duration<double>(now - last_sample).count();
            const double fps = dt > 0.0 ? static_cast<double>(curr - last_completed) / dt : 0.0;
            throughput_t.push_back(tsec);
            throughput_fps.push_back(fps);
            last_completed = curr;
            last_sample = now;
        }

        ImGuiViewport* vp = ImGui::GetMainViewport();
        const ImVec2 work_pos = vp->WorkPos;
        const ImVec2 work_size = vp->WorkSize;
        const float gap = 10.0f;

        const float left_w = 540.0f;
        const float top_h = 300.0f;
        const float right_w = work_size.x - left_w - gap;
        const float timeline_h = work_size.y - top_h - gap;

        manual_view_start_ms = std::clamp(manual_view_start_ms, 0.0f, max_timeline_start_ms);

        ImGui::SetNextWindowPos(ImVec2(work_pos.x, work_pos.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(left_w, work_size.y), ImGuiCond_Always);
        ImGui::Begin("Pipeline Inspector", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        // ── Simulator Controls (always first) ──────────────────────
        if (ImGui::TreeNodeEx("Simulator Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
            std::vector<const char*> profile_labels;
            profile_labels.reserve(profiles.size());
            for (const auto& profile : profiles) {
                profile_labels.push_back(profile.label.c_str());
            }
            if (!profile_labels.empty()) {
                ImGui::Combo("Compute profile", &selected_profile_index,
                             profile_labels.data(), static_cast<int>(profile_labels.size()));
                selected_profile_index = std::clamp(selected_profile_index, 0,
                                                    static_cast<int>(profile_labels.size()) - 1);
            }
            // Selected profile details
            {
                const auto& sel = profiles[static_cast<size_t>(selected_profile_index)];
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.85f, 1.00f, 1.00f));
                char thr_buf[32];
                if (sel.thread_pool_override > 0)
                    std::snprintf(thr_buf, sizeof(thr_buf), "%zu", sel.thread_pool_override);
                else
                    std::snprintf(thr_buf, sizeof(thr_buf), "default");
                char loop_buf[32];
                if (sel.central_loop_rate_hz_override > 0.0) {
                    std::snprintf(loop_buf, sizeof(loop_buf), "%.1f Hz", sel.central_loop_rate_hz_override);
                } else {
                    std::snprintf(loop_buf, sizeof(loop_buf), "default");
                }
                ImGui::Text("Delay x%.2f  |  BW x%.2f  |  Threads: %s  |  Loop: %s",
                            sel.stage_delay_scale, sel.global_bandwidth_scale, thr_buf, loop_buf);
                ImGui::Text("Central cycle policy: %s", central_cycle_mode_label(kCycleModeIds[selected_cycle_mode]));
                ImGui::Text("Stage timing: %s | sampled: %s | spin guard: %d us",
                            stage_timing_mode.c_str(), stage_timing_sampled ? "on" : "off",
                            stage_spin_guard_us);
                ImGui::PopStyleColor();
                ImGui::Unindent();
            }
            ImGui::Combo("Cycle policy", &selected_cycle_mode,
                         kCycleModeLabels, IM_ARRAYSIZE(kCycleModeLabels));
            ImGui::TextUnformatted("Policy applies when simulation is restarted.");

            static const char* kTimingModeLabels[] = {"sleep", "spin", "hybrid"};
            int timing_mode_index = 2;
            if (stage_timing_mode == "sleep") timing_mode_index = 0;
            else if (stage_timing_mode == "spin") timing_mode_index = 1;
            if (ImGui::Combo("Stage timing mode", &timing_mode_index,
                             kTimingModeLabels, IM_ARRAYSIZE(kTimingModeLabels))) {
                stage_timing_mode = kTimingModeLabels[timing_mode_index];
            }
            ImGui::Checkbox("Sample stage duration between min/avg/max", &stage_timing_sampled);
            ImGui::SliderInt("Hybrid spin guard (us)", &stage_spin_guard_us, 0, 3000);

            if (ImGui::TreeNodeEx("Per-stage timing scale", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (const auto& stage : base_cfg.stages) {
                    float& scale = stage_scales[stage.id];
                    ImGui::PushID(stage.id.c_str());
                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::SliderFloat("##scale", &scale, 0.20f, 3.00f, "x%.2f");
                    ImGui::SameLine();
                    ImGui::TextUnformatted(stage.name.c_str());
                    ImGui::PopID();
                }
                if (ImGui::Button("Reset stage scales")) {
                    for (auto& [id, scale] : stage_scales) {
                        (void)id;
                        scale = 1.0f;
                    }
                }
                ImGui::TreePop();
            }
            char profile_path_buf[512]{};
            std::snprintf(profile_path_buf, sizeof(profile_path_buf), "%s", profile_json_path.c_str());
            if (ImGui::InputText("Profiles JSON", profile_path_buf, sizeof(profile_path_buf))) {
                profile_json_path = profile_path_buf;
            }
            if (ImGui::Button("Load profile overrides")) {
                std::vector<ComputeProfileDef> loaded;
                std::string error;
                if (load_compute_profiles_from_json(profile_json_path, loaded, error)) {
                    profiles = std::move(loaded);
                    selected_profile_index = 0;
                    profile_status = "Loaded profile overrides from JSON";
                } else {
                    profiles = default_compute_profiles();
                    selected_profile_index = std::min(1, static_cast<int>(profiles.size()) - 1);
                    profile_status = "Profile override load failed: " + error + " (using built-ins)";
                }
            }
            ImGui::TextWrapped("%s", profile_status.c_str());
            ImGui::Separator();
            ImGui::TextUnformatted("Sensors (toggles apply on restart):");
            for (const auto& sensor : base_cfg.sensors) {
                std::string slabel = sensor.short_name.empty() ? sensor.name : sensor.short_name;
                bool enabled = sensor_enabled[sensor.name];
                if (ImGui::Checkbox(("##sensor_enabled_" + sensor.name).c_str(), &enabled)) {
                    sensor_enabled[sensor.name] = enabled;
                }
                ImGui::SameLine();
                ImGui::Text("%s (%s)", slabel.c_str(), sensor.type.c_str());
            }
            if (ImGui::Button("Restart simulation with selected settings")) {
                stop_simulation();
                const auto& active_profile =
                    profiles[static_cast<size_t>(std::clamp(selected_profile_index, 0,
                                                            static_cast<int>(profiles.size()) - 1))];
                runtime_cfg = make_runtime_config(base_cfg, sensor_enabled, active_profile,
                                                  kCycleModeIds[selected_cycle_mode],
                                                  stage_scales,
                                                  stage_timing_mode,
                                                  stage_timing_sampled,
                                                  static_cast<uint32_t>(std::max(0, stage_spin_guard_us)));
                pipeline = std::make_unique<adas::Pipeline>(runtime_cfg);
                lane_defs = build_ordered_lanes(runtime_cfg);
                lane_thread_counts = build_lane_thread_counts(runtime_cfg);
                lane_visibility.clear();
                for (const auto& lane_def : lane_defs) {
                    lane_visibility[lane_def.key] = true;
                }
                throughput_t.clear();
                throughput_fps.clear();
                hist_t0 = std::chrono::steady_clock::now();
                last_sample = hist_t0;
                last_completed = 0;
                manual_view_start_ms = 0.0f;
                start_simulation();
            }
            ImGui::TreePop();
        }

        ImGui::Separator();

        // ── Status ──────────────────────────────────────────────────
        ImGui::Text("Simulation: %s", sim_running ? "Running" : "Stopped");
        ImGui::Text("Cycle count: %zu", cycle_count);
        ImGui::Text("Dropped frames: %zu", drops);
        ImGui::Text("Throttle events: %zu", throttles);
        ImGui::Text("Missed cycles: %zu", missed_cycles);
        ImGui::Text("Overlapped cycles: %zu", overlap_cycles);
        ImGui::Separator();

        // ── View controls ────────────────────────────────────────────
        ImGui::SliderFloat("UI scale", &ui_scale, 1.10f, 2.20f, "%.2fx");
        ImGui::SliderFloat("View window (ms)", &view_window_ms, 10.0f, 5000.0f, "%.0f");
        const float page_step_ms = std::max(50.0f, view_window_ms * 0.25f);
        if (ImGui::Button("|<")) {
            manual_view_start_ms = 0.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("<")) {
            manual_view_start_ms = std::max(0.0f, manual_view_start_ms - page_step_ms);
        }
        ImGui::SameLine();
        if (ImGui::Button(">")) {
            manual_view_start_ms = std::min(max_timeline_start_ms, manual_view_start_ms + page_step_ms);
        }
        ImGui::SameLine();
        if (ImGui::Button(">|")) {
            manual_view_start_ms = max_timeline_start_ms;
        }
        ImGui::SliderFloat("Timeline position (ms)", &manual_view_start_ms, 0.0f,
                           std::max(0.0f, max_timeline_start_ms), "%.0f");
        ImGui::Checkbox("Show frame numbers", &show_frame_numbers);
        ImGui::TextUnformatted("Timeline: scroll to pan, Ctrl+scroll (pinch) to zoom window, drag to pan.");
        if (const auto* acq_stage = find_stage(runtime_cfg, "sense_1_3_sensor_acquisition");
            acq_stage != nullptr) {
            auto it = lane_stats.find(acq_stage->lane);
            if (it != lane_stats.end()) {
                ImGui::Text("Central loop freq: %.2f Hz", it->second.freq_hz());
            }
        }
        ImGui::Separator();

        // ── Stats row helper: label only, details available on hover ─
        auto draw_stats_row = [](const std::string& /*lane*/, const std::string& label) {
            ImGui::BulletText("%s", label.c_str());
        };

        // ── Sensors group ────────────────────────────────────────────
        {
            int sv_total = 0, sv_vis = 0;
            for (const auto& ld : lane_defs) {
                if (!ld.is_sensor) continue;
                ++sv_total;
                if (lane_visibility.count(ld.key) && lane_visibility[ld.key]) ++sv_vis;
            }
            bool sg = sv_total > 0 && sv_vis == sv_total;
            if (ImGui::Checkbox("##sensors_grp", &sg)) {
                for (auto& ld : lane_defs) {
                    if (ld.is_sensor) lane_visibility[ld.key] = sg;
                }
            }
            ImGui::SameLine();
            if (ImGui::TreeNodeEx("Sensors", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (const auto& lane_def : lane_defs) {
                    if (!lane_def.is_sensor) continue;
                    ImGui::Checkbox(("##vis_" + lane_def.key).c_str(), &lane_visibility[lane_def.key]);
                    ImGui::SameLine();
                    draw_stats_row(lane_def.key, lane_def.label);
                }
                ImGui::TreePop();
            }
        }

        // ── Central ECU Pipeline group ───────────────────────────────
        {
            int cv_total = 0, cv_vis = 0;
            for (const auto& ld : lane_defs) {
                if (!ld.is_stage) continue;
                ++cv_total;
                if (lane_visibility.count(ld.key) && lane_visibility[ld.key]) ++cv_vis;
            }
            bool cg = cv_total > 0 && cv_vis == cv_total;
            if (ImGui::Checkbox("##central_grp", &cg)) {
                for (auto& ld : lane_defs) {
                    if (ld.is_stage) lane_visibility[ld.key] = cg;
                }
            }
            ImGui::SameLine();
            if (ImGui::TreeNodeEx("Central ECU Pipeline", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (const auto& phase : runtime_cfg.phases) {
                    if (ImGui::TreeNodeEx(phase.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (const auto& stage_id : phase.stage_ids) {
                            const auto* stage = find_stage(runtime_cfg, stage_id);
                            if (!stage) {
                                continue;
                            }
                            ImGui::Checkbox(("##vis_" + stage->lane).c_str(), &lane_visibility[stage->lane]);
                            ImGui::SameLine();
                            draw_stats_row(stage->lane, stage->name);
                            if (!stage->substeps.empty()) {
                                std::string substeps = "sub-steps: ";
                                for (size_t index = 0; index < stage->substeps.size(); ++index) {
                                    if (index > 0) {
                                        substeps += " | ";
                                    }
                                    substeps += stage->substeps[index];
                                }
                                ImGui::TextWrapped("%s", substeps.c_str());
                            }
                        }
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
            }
        }

        // ── System Events group ──────────────────────────────────────
        {
            int syv_vis = 0;
            if (lane_visibility.count("BW-throttle") && lane_visibility["BW-throttle"]) ++syv_vis;
            if (lane_visibility.count("frame-drop") && lane_visibility["frame-drop"]) ++syv_vis;
            if (lane_visibility.count("cycle-miss") && lane_visibility["cycle-miss"]) ++syv_vis;
            if (lane_visibility.count("overlap") && lane_visibility["overlap"]) ++syv_vis;
            bool syg = syv_vis == 4;
            if (ImGui::Checkbox("##system_grp", &syg)) {
                lane_visibility["BW-throttle"] = syg;
                lane_visibility["frame-drop"] = syg;
                lane_visibility["cycle-miss"] = syg;
                lane_visibility["overlap"] = syg;
            }
            ImGui::SameLine();
            if (ImGui::TreeNodeEx("System Events", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("##vis_BW-throttle", &lane_visibility["BW-throttle"]);
                ImGui::SameLine();
                draw_stats_row("BW-throttle", "BW-throttle");
                ImGui::Checkbox("##vis_frame-drop", &lane_visibility["frame-drop"]);
                ImGui::SameLine();
                ImGui::BulletText("frame-drop markers : %zu", drops);
                ImGui::Checkbox("##vis_cycle-miss", &lane_visibility["cycle-miss"]);
                ImGui::SameLine();
                ImGui::BulletText("cycle-miss markers : %zu", missed_cycles);
                ImGui::Checkbox("##vis_overlap", &lane_visibility["overlap"]);
                ImGui::SameLine();
                ImGui::BulletText("overlap markers : %zu", overlap_cycles);
                ImGui::TreePop();
            }
        }

        if (sim_done) {
            ImGui::Separator();
            ImGui::Text("Run complete. You can inspect the timeline and close the window.");
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(work_pos.x + left_w + gap, work_pos.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(right_w, top_h), ImGuiCond_Always);
        ImGui::Begin("Throughput", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        if (ImPlot::BeginPlot("Completed FPS", ImVec2(-1, 220))) {
            ImPlot::SetupAxes("Time (s)", "Frames/s", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            if (!throughput_t.empty()) {
                ImPlot::PlotLine("fps", throughput_t.data(), throughput_fps.data(), static_cast<int>(throughput_t.size()));
            }
            ImPlot::EndPlot();
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(work_pos.x + left_w + gap, work_pos.y + top_h + gap), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(right_w, timeline_h), ImGuiCond_Always);
        ImGui::Begin("Pipeline Timeline", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        if (ImPlot::BeginPlot("Events", ImVec2(-1, -1))) {
            std::vector<double> y_ticks;
            std::vector<const char*> y_labels;
            std::map<std::string, int> lane_to_idx;
            std::vector<const LaneDef*> visible_lanes;
            for (const auto& lane_def : lane_defs) {
                if (!lane_visibility[lane_def.key]) {
                    continue;
                }
                visible_lanes.push_back(&lane_def);
            }

            const int lane_count = static_cast<int>(visible_lanes.size());
            for (int i = 0; i < lane_count; ++i) {
                const double y = static_cast<double>(lane_count - 1 - i);
                lane_to_idx[visible_lanes[i]->key] = static_cast<int>(y);
                y_ticks.push_back(y);
                y_labels.push_back(visible_lanes[i]->label.c_str());
            }

            const double x_start = manual_view_start_ms;
            const double x_end = x_start + static_cast<double>(view_window_ms);

            ImPlot::SetupAxes("Time (ms)", "Lane", ImPlotAxisFlags_None, ImPlotAxisFlags_NoGridLines);
            if (!y_ticks.empty()) {
                ImPlot::SetupAxisTicks(ImAxis_Y1, y_ticks.data(), static_cast<int>(y_ticks.size()), y_labels.data(), false);
            }
            ImPlot::SetupAxisLimits(ImAxis_X1, x_start, x_end, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -1.0, static_cast<double>(std::max(1, lane_count)), ImGuiCond_Always);

            // Assign overlapping events to stacked sub-rows within each lane.
            const double overlap_guard_ms = 0.01;
            const double slot_step = 0.16;
            const int max_slot_draw = 3;
            std::vector<int> event_slots(sorted_events.size(), 0);
            std::map<std::string, std::vector<double>> lane_slot_end_ms;
            for (size_t ev_idx = 0; ev_idx < sorted_events.size(); ++ev_idx) {
                const auto& e = sorted_events[ev_idx];
                if (!lane_visibility[e.lane]) continue;
                auto it_lane = lane_to_idx.find(e.lane);
                if (it_lane == lane_to_idx.end()) continue;

                const double s = to_ms(e.start, snap.origin);
                const double t = to_ms(e.end, snap.origin);
                if (t < x_start || s > x_end) continue;

                auto& slot_ends = lane_slot_end_ms[e.lane];
                int assigned_slot = static_cast<int>(slot_ends.size());
                for (int slot = 0; slot < static_cast<int>(slot_ends.size()); ++slot) {
                    if (s >= slot_ends[slot] + overlap_guard_ms) {
                        assigned_slot = slot;
                        slot_ends[slot] = t;
                        break;
                    }
                }
                if (assigned_slot == static_cast<int>(slot_ends.size())) {
                    slot_ends.push_back(t);
                }
                event_slots[ev_idx] = assigned_slot;
            }

            ImDrawList* draw_list = ImPlot::GetPlotDrawList();
            int idx = 0;
            for (size_t ev_idx = 0; ev_idx < sorted_events.size(); ++ev_idx) {
                const auto& e = sorted_events[ev_idx];
                if (!lane_visibility[e.lane]) continue;
                auto it = lane_to_idx.find(e.lane);
                if (it == lane_to_idx.end()) continue;

                const double s = to_ms(e.start, snap.origin);
                const double t = to_ms(e.end, snap.origin);
                if (t < x_start || s > x_end) continue;

                const int slot = std::min(event_slots[ev_idx], max_slot_draw);
                const double y = static_cast<double>(it->second) - slot_step * static_cast<double>(slot);
                double xs[2] = {s, t};
                double ys[2] = {y, y};
                const ImVec4 color = event_color(e);

                ImPlot::SetNextLineStyle(color, 5.0f);
                std::string id = "evt##" + std::to_string(idx++);
                ImPlot::PlotLine(id.c_str(), xs, ys, 2);

                // Draw a thin vertical start pin at the beginning of the execution span.
                double pin_x[2] = {s, s};
                double pin_y[2] = {y - 0.18, y + 0.18};
                ImPlot::SetNextLineStyle(color, 1.0f);
                std::string pin_id = "pin##" + std::to_string(idx++);
                ImPlot::PlotLine(pin_id.c_str(), pin_x, pin_y, 2);

                if (show_frame_numbers) {
                    const std::string frame_label = std::to_string(e.frame_id);
                    const ImVec2 pix = ImPlot::PlotToPixels(s, y + 0.34);
                    draw_list->AddText(ImVec2(pix.x + 2.0f, pix.y - 12.0f),
                                       ImGui::ColorConvertFloat4ToU32(color),
                                       frame_label.c_str());
                }
            }

            idx = 0;
            for (const auto& m : snap.markers) {
                if (!lane_visibility[m.lane]) continue;
                auto it = lane_to_idx.find(m.lane);
                if (it == lane_to_idx.end()) continue;
                const double x = to_ms(m.time, snap.origin);
                if (x < x_start || x > x_end) continue;
                const double y = static_cast<double>(it->second);
                double xs[1] = {x};
                double ys[1] = {y};
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 6.0f, glyph_color(m.glyph), 1.0f, glyph_color(m.glyph));
                std::string id = "mk##" + std::to_string(idx++);
                ImPlot::PlotScatter(id.c_str(), xs, ys, 1);
            }

            if (ImPlot::IsPlotHovered()) {
                const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
                const double y_tolerance = 0.16;
                const double marker_x_tolerance = std::max(0.8, static_cast<double>(view_window_ms) * 0.005);

                const adas::TimelineVisualizer::Event* hovered_event = nullptr;
                const adas::TimelineVisualizer::Marker* hovered_marker = nullptr;
                double hovered_event_dist = std::numeric_limits<double>::max();
                double hovered_marker_dist = std::numeric_limits<double>::max();

                for (size_t ev_idx = 0; ev_idx < sorted_events.size(); ++ev_idx) {
                    const auto& e = sorted_events[ev_idx];
                    if (!lane_visibility[e.lane]) continue;
                    auto it = lane_to_idx.find(e.lane);
                    if (it == lane_to_idx.end()) continue;

                    const int slot = std::min(event_slots[ev_idx], max_slot_draw);
                    const double y = static_cast<double>(it->second) - slot_step * static_cast<double>(slot);
                    if (std::abs(mouse.y - y) > y_tolerance) continue;

                    const double s = to_ms(e.start, snap.origin);
                    const double t = to_ms(e.end, snap.origin);
                    if (t < x_start || s > x_end) continue;
                    if (mouse.x < s || mouse.x > t) continue;

                    const double mid = 0.5 * (s + t);
                    const double dist = std::abs(mouse.x - mid);
                    if (dist < hovered_event_dist) {
                        hovered_event_dist = dist;
                        hovered_event = &e;
                    }
                }

                for (const auto& m : snap.markers) {
                    if (!lane_visibility[m.lane]) continue;
                    auto it = lane_to_idx.find(m.lane);
                    if (it == lane_to_idx.end()) continue;

                    const double y = static_cast<double>(it->second);
                    if (std::abs(mouse.y - y) > y_tolerance) continue;

                    const double x = to_ms(m.time, snap.origin);
                    if (x < x_start || x > x_end) continue;
                    const double dist = std::abs(mouse.x - x);
                    if (dist > marker_x_tolerance) continue;

                    if (dist < hovered_marker_dist) {
                        hovered_marker_dist = dist;
                        hovered_marker = &m;
                    }
                }

                if (hovered_event != nullptr) {
                    const std::string lane = hovered_event->lane;
                    const std::string lane_label = lane_lookup.count(lane) > 0
                        ? lane_lookup[lane]->label
                        : lane;
                    const double exec_ms = std::max(0.0, to_ms(hovered_event->end, hovered_event->start));
                    const ImVec4 hov_color = event_color(*hovered_event);

                    ImGui::BeginTooltip();
                    ImGui::PushStyleColor(ImGuiCol_Text, hov_color);
                    ImGui::Text("Lane: %s | Frame: %llu", lane_label.c_str(),
                                static_cast<unsigned long long>(hovered_event->frame_id));
                    ImGui::PopStyleColor();
                    ImGui::Separator();
                    ImGui::Text("Exec: %.3f ms", exec_ms);
                    ImGui::Text("Start: %.3f ms", to_ms(hovered_event->start, snap.origin));
                    ImGui::Text("End: %.3f ms", to_ms(hovered_event->end, snap.origin));
                    if (!hovered_event->source.empty()) {
                        ImGui::Text("Source: %s", hovered_event->source.c_str());
                    }

                    auto tt = lane_thread_counts.find(lane);
                    const int lane_threads = (tt != lane_thread_counts.end()) ? tt->second : 1;
                    ImGui::Text("Lane workers: %d", lane_threads);
                    ImGui::Text("Pool size: %zu", runtime_cfg.thread_pool_size);

                    auto it = lane_stats.find(lane);
                    if (it != lane_stats.end()) {
                        ImGui::Text("Cnt: %d", it->second.count);
                        ImGui::Text("Avg: %.3f ms", it->second.avg());
                        ImGui::Text("Freq: %.2f Hz", it->second.freq_hz());
                    }
                    ImGui::EndTooltip();
                } else if (hovered_marker != nullptr) {
                    const std::string lane = hovered_marker->lane;
                    const std::string lane_label = lane_lookup.count(lane) > 0
                        ? lane_lookup[lane]->label
                        : lane;
                    const double marker_ms = to_ms(hovered_marker->time, snap.origin);

                    ImGui::BeginTooltip();
                    ImGui::PushStyleColor(ImGuiCol_Text, glyph_color(hovered_marker->glyph));
                    ImGui::Text("Lane: %s | Marker: %c", lane_label.c_str(), hovered_marker->glyph);
                    ImGui::PopStyleColor();
                    ImGui::Separator();
                    ImGui::Text("Time: %.3f ms", marker_ms);

                    if (hovered_marker->glyph == 'X') {
                        ImGui::Text("Dropped frame id: %llu",
                                    static_cast<unsigned long long>(hovered_marker->frame_id));
                    } else if (hovered_marker->glyph == '!') {
                        ImGui::Text("Missed cycle id: %llu",
                                    static_cast<unsigned long long>(hovered_marker->frame_id));
                    } else if (hovered_marker->frame_id > 0) {
                        ImGui::Text("Frame id: %llu",
                                    static_cast<unsigned long long>(hovered_marker->frame_id));
                    }
                    if (!hovered_marker->source.empty()) {
                        ImGui::Text("Source: %s", hovered_marker->source.c_str());
                    }
                    if (!hovered_marker->detail.empty()) {
                        ImGui::TextWrapped("Reason: %s", hovered_marker->detail.c_str());
                    }

                    auto it = lane_stats.find(lane);
                    if (it != lane_stats.end()) {
                        ImGui::Text("Cnt: %d", it->second.count);
                    }
                    ImGui::EndTooltip();
                }
            }

            // Handle custom pan/zoom gestures after setup is locked;
            // apply to viewer state for the next frame.
            if (ImPlot::IsPlotHovered()) {
                const float pan_step = std::max(10.0f, view_window_ms * 0.10f);

                // Precision touchpad pinch is commonly delivered as Ctrl + wheel.
                if (io.KeyCtrl && io.MouseWheel != 0.0f) {
                    const float old_window_ms = view_window_ms;
                    const float zoom_factor = std::max(0.25f, 1.0f - io.MouseWheel * 0.12f);
                    view_window_ms = std::clamp(old_window_ms * zoom_factor, 10.0f, 5000.0f);

                    // Keep zoom centered on the current visible center.
                    const float center = manual_view_start_ms + old_window_ms * 0.5f;
                    const float new_max_start = std::max(0.0f, static_cast<float>(max_ms) - view_window_ms);
                    manual_view_start_ms = std::clamp(center - view_window_ms * 0.5f, 0.0f, new_max_start);
                } else {
                    if (io.MouseWheel != 0.0f) {
                        manual_view_start_ms -= io.MouseWheel * pan_step;
                    }
                    if (io.MouseWheelH != 0.0f) {
                        manual_view_start_ms -= io.MouseWheelH * pan_step;
                    }
                }
                manual_view_start_ms = std::clamp(manual_view_start_ms, 0.0f, max_timeline_start_ms);
            }

            ImPlot::EndPlot();
        }
        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    stop_simulation();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
