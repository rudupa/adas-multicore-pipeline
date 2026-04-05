#include "core/config_loader.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>

namespace adas {

namespace {

std::string to_lower_copy(const std::string& input) {
    std::string lowered = input;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

char glyph_for_phase(const std::string& phase_id, const std::string& phase_name) {
    const std::string lowered_id = to_lower_copy(phase_id);
    const std::string lowered_name = to_lower_copy(phase_name);
    if (lowered_id == "sense" || lowered_name == "sense") {
        return 'S';
    }
    if (lowered_id == "plan" || lowered_name == "plan") {
        return 'P';
    }
    if (lowered_id == "act" || lowered_name == "act") {
        return 'A';
    }
    return 'G';
}

std::string humanize_identifier(std::string value) {
    std::replace(value.begin(), value.end(), '_', ' ');
    if (!value.empty()) {
        value[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[0])));
    }
    return value;
}

std::string strip_phase_prefix(const std::string& value, const std::string& phase_id) {
    const std::string prefix = phase_id + "_";
    if (value.rfind(prefix, 0) == 0) {
        return value.substr(prefix.size());
    }
    return value;
}

uint32_t avg_ms_to_us(const nlohmann::json& node, uint32_t fallback_us = 1000) {
    if (node.contains("execution_times") && node["execution_times"].is_object()) {
        const auto& times = node["execution_times"];
        if (times.contains("avg_ms") && times["avg_ms"].is_number()) {
            const double avg_ms = times["avg_ms"].get<double>();
            return static_cast<uint32_t>(std::max(1.0, avg_ms * 1000.0));
        }
    }
    return fallback_us;
}

struct ExecutionTimesUs {
    uint32_t min_us;
    uint32_t avg_us;
    uint32_t max_us;
};

ExecutionTimesUs execution_times_to_us(const nlohmann::json& node, uint32_t fallback_us = 1000) {
    if (node.contains("execution_times") && node["execution_times"].is_object()) {
        const auto& times = node["execution_times"];
        const double min_ms = times.value("min_ms", times.value("avg_ms", fallback_us / 1000.0));
        const double avg_ms = times.value("avg_ms", times.value("min_ms", fallback_us / 1000.0));
        const double max_ms = times.value("max_ms", times.value("avg_ms", fallback_us / 1000.0));

        uint32_t min_us = static_cast<uint32_t>(std::max(1.0, min_ms * 1000.0));
        uint32_t avg_us = static_cast<uint32_t>(std::max(1.0, avg_ms * 1000.0));
        uint32_t max_us = static_cast<uint32_t>(std::max(1.0, max_ms * 1000.0));

        if (avg_us < min_us) avg_us = min_us;
        if (max_us < avg_us) max_us = avg_us;

        return {min_us, avg_us, max_us};
    }
    return {fallback_us, fallback_us, fallback_us};
}

void append_substep_names(std::vector<std::string>& out, const nlohmann::json& steps) {
    if (!steps.is_array()) {
        return;
    }
    for (const auto& step : steps) {
        if (!step.is_object()) {
            continue;
        }
        if (step.contains("name") && step["name"].is_string()) {
            out.push_back(step["name"].get<std::string>());
            continue;
        }
        if (step.contains("id") && step["id"].is_string()) {
            out.push_back(humanize_identifier(step["id"].get<std::string>()));
        }
    }
}

std::vector<std::string> load_execution_view_substeps(const nlohmann::json& root,
                                                      const std::string& phase_id,
                                                      const std::string& stage_id) {
    std::vector<std::string> substeps;
    if (!root.contains("real_time_execution_view") || !root["real_time_execution_view"].is_object()) {
        return substeps;
    }

    const auto& execution_view = root["real_time_execution_view"];
    if (!execution_view.contains(phase_id) || !execution_view[phase_id].is_object()) {
        return substeps;
    }

    const std::string step_key = strip_phase_prefix(stage_id, phase_id);
    const auto& phase_view = execution_view[phase_id];
    if (!phase_view.contains(step_key) || !phase_view[step_key].is_object()) {
        return substeps;
    }

    const auto& stage_view = phase_view[step_key];
    if (!stage_view.contains("sub_steps") || !stage_view["sub_steps"].is_array()) {
        return substeps;
    }

    for (const auto& substep : stage_view["sub_steps"]) {
        if (!substep.is_string()) {
            continue;
        }
        substeps.push_back(humanize_identifier(substep.get<std::string>()));
    }
    return substeps;
}

std::map<std::string, std::string> build_node_name_map(const nlohmann::json& root) {
    std::map<std::string, std::string> node_names;
    if (!root.contains("topology") || !root["topology"].is_object()) {
        return node_names;
    }

    const auto& topology = root["topology"];
    if (!topology.contains("nodes") || !topology["nodes"].is_array()) {
        return node_names;
    }

    for (const auto& node : topology["nodes"]) {
        if (!node.is_object()) {
            continue;
        }
        const std::string id = node.value("id", std::string());
        if (id.empty()) {
            continue;
        }
        node_names[id] = node.value("name", humanize_identifier(id));
    }
    return node_names;
}

void load_stage_nodes(StageConfig& stage,
                      const std::map<std::string, std::string>& node_names,
                      const nlohmann::json& step_node) {
    if (step_node.contains("mapped_nodes") && step_node["mapped_nodes"].is_array()) {
        for (const auto& mapped_node : step_node["mapped_nodes"]) {
            if (!mapped_node.is_string()) {
                continue;
            }
            stage.mapped_nodes.push_back(mapped_node.get<std::string>());
        }
    }

    if (!stage.mapped_nodes.empty()) {
        stage.primary_node_id = stage.mapped_nodes.front();
        auto it = node_names.find(stage.primary_node_id);
        stage.primary_node_name = it != node_names.end()
            ? it->second
            : humanize_identifier(stage.primary_node_id);
    }
}

void build_stage_config(PipelineConfig& cfg,
                        const std::map<std::string, std::string>& node_names,
                        const nlohmann::json& root,
                        const nlohmann::json& phase_node) {
    if (!phase_node.is_object()) {
        return;
    }

    PhaseConfig phase;
    phase.id = phase_node.value("id", std::string("phase"));
    phase.name = phase_node.value("name", humanize_identifier(phase.id));

    if (phase_node.contains("steps") && phase_node["steps"].is_array()) {
        for (const auto& step_node : phase_node["steps"]) {
            if (!step_node.is_object()) {
                continue;
            }

            StageConfig stage;
            stage.id = step_node.value("id", phase.id + "_stage_" + std::to_string(cfg.stages.size()));
            stage.name = step_node.value("name", humanize_identifier(stage.id));
            stage.phase_id = phase.id;
            stage.phase_name = phase.name;
            stage.lane = stage.name;
            const ExecutionTimesUs times = execution_times_to_us(step_node);
            stage.delay_us_min = times.min_us;
            stage.delay_us = times.avg_us;
            stage.delay_us_max = times.max_us;
            stage.glyph = glyph_for_phase(phase.id, phase.name);
            
            // ── Priority, affinity, accelerator ────────────────────
            stage.priority = step_node.value("priority", 50);
            stage.preferred_core = step_node.value("preferred_core", -1);
            stage.accelerator = step_node.value("accelerator", std::string(""));
            
            load_stage_nodes(stage, node_names, step_node);

            if (step_node.contains("steps") && step_node["steps"].is_array()) {
                append_substep_names(stage.substeps, step_node["steps"]);
            }
            if (stage.substeps.empty()) {
                stage.substeps = load_execution_view_substeps(root, phase.id, stage.id);
            }

            phase.stage_ids.push_back(stage.id);
            cfg.stages.push_back(std::move(stage));
        }
    }

    if (!phase.stage_ids.empty()) {
        cfg.phases.push_back(std::move(phase));
    }
}

void load_scenario_pipeline(PipelineConfig& cfg, const nlohmann::json& root) {
    if (!root.contains("scenarios") || !root["scenarios"].is_array() || root["scenarios"].empty()) {
        return;
    }

    const auto& scenario = root["scenarios"].front();
    if (!scenario.is_object() || !scenario.contains("pipeline") || !scenario["pipeline"].is_object()) {
        return;
    }

    const auto& pipeline = scenario["pipeline"];
    if (!pipeline.contains("steps") || !pipeline["steps"].is_array()) {
        return;
    }

    const auto node_names = build_node_name_map(root);

    for (const auto& phase_node : pipeline["steps"]) {
        build_stage_config(cfg, node_names, root, phase_node);
    }
}

void load_legacy_pipeline(PipelineConfig& cfg, const nlohmann::json& root) {
    if (!root.contains("pipeline") || !root["pipeline"].is_object()) {
        return;
    }

    const auto& pipeline = root["pipeline"];
    PhaseConfig phase;
    phase.id = "legacy";
    phase.name = "Legacy Pipeline";

    const struct LegacyStageDef {
        const char* id;
        const char* name;
        const char* key;
        char glyph;
    } legacy_stage_defs[] = {
        {"preprocess", "Preprocess", "preprocess_delay_us", 'S'},
        {"detection", "Detection", "detection_delay_us", 'P'},
        {"tracking", "Tracking", "tracking_delay_us", 'A'},
    };

    for (const auto& def : legacy_stage_defs) {
        if (!pipeline.contains(def.key) || !pipeline[def.key].is_number_unsigned()) {
            continue;
        }

        StageConfig stage;
        stage.id = def.id;
        stage.name = def.name;
        stage.lane = stage.name;
        stage.phase_id = phase.id;
        stage.phase_name = phase.name;
        stage.delay_us = pipeline.value(def.key, 1000u);
        stage.delay_us_min = stage.delay_us;
        stage.delay_us_max = stage.delay_us;
        stage.glyph = def.glyph;
        cfg.stages.push_back(stage);
        phase.stage_ids.push_back(stage.id);
    }

    if (!phase.stage_ids.empty()) {
        cfg.phases.push_back(std::move(phase));
    }
}

void append_sensor(PipelineConfig& cfg, const nlohmann::json& js, const std::string& fallback_type) {
    if (!js.is_object()) {
        return;
    }

    PipelineConfig::SensorCfg sc;
    sc.type = js.value("type", fallback_type);
    sc.name = js.value("name", "unnamed");
    sc.short_name = js.value("short_name", js.value("shortname", sc.name));
    sc.fps = js.value("fps", js.value("sampling_rate_hz", 30.0));

    if (js.contains("frame_size_bytes") && js["frame_size_bytes"].is_number()) {
        sc.frame_size_bytes = js["frame_size_bytes"].get<size_t>();
    } else if (js.contains("output") && js["output"].is_object()) {
        sc.frame_size_bytes = js["output"].value("frame_size_bytes", size_t(0));
    } else if (fallback_type == "vehicle_state") {
        sc.frame_size_bytes = 256;
    }

    sc.bandwidth_limit_mbps = js.value("bandwidth_limit_mbps", js.value("bandwidth_mbps", 0.0));
    cfg.sensors.push_back(std::move(sc));
}

} // namespace

PipelineConfig load_config(const std::string& json_path) {
    std::ifstream ifs(json_path);
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open config file: " + json_path);
    }

    nlohmann::json j;
    ifs >> j;

    PipelineConfig cfg;

    // ── Sensors ────────────────────────────────────────────────────
    if (j.contains("sensors")) {
        const auto& sensors = j["sensors"];
        if (sensors.is_array()) {
            for (const auto& js : sensors) {
                append_sensor(cfg, js, "camera");
            }
        } else if (sensors.is_object()) {
            if (sensors.contains("cameras") && sensors["cameras"].is_array()) {
                for (const auto& js : sensors["cameras"]) {
                    append_sensor(cfg, js, "camera");
                }
            }
            if (sensors.contains("radars") && sensors["radars"].is_array()) {
                for (const auto& js : sensors["radars"]) {
                    append_sensor(cfg, js, "radar");
                }
            }
            if (sensors.contains("vehicle_state_inputs") && sensors["vehicle_state_inputs"].is_array()) {
                for (const auto& js : sensors["vehicle_state_inputs"]) {
                    append_sensor(cfg, js, "vehicle_state");
                }
            }
        }
    }

    // ── Add jitter_percentage to sensors from sensor_jitter config ──
    if (j.contains("sensor_jitter") && j["sensor_jitter"].is_array()) {
        for (const auto& jitter_entry : j["sensor_jitter"]) {
            if (!jitter_entry.is_object()) continue;
            const std::string sensor_name = jitter_entry.value("sensor_name", std::string());
            const double jitter_pct = jitter_entry.value("jitter_percentage", 0.0);
            for (auto& sensor : cfg.sensors) {
                if (sensor.name == sensor_name) {
                    sensor.jitter_percentage = jitter_pct;
                    break;
                }
            }
        }
    }

    // ── CPU Cores ──────────────────────────────────────────────────
    if (j.contains("cpu_cores") && j["cpu_cores"].is_array()) {
        for (const auto& core_entry : j["cpu_cores"]) {
            if (!core_entry.is_object()) continue;
            CPUCore core;
            core.core_id = core_entry.value("core_id", 0);
            core.freq_ghz = core_entry.value("freq_ghz", 2.0);
            core.max_tasks = core_entry.value("max_tasks", 4);
            cfg.cpu_cores.push_back(core);
        }
    }

    // ── Accelerators ───────────────────────────────────────────────
    if (j.contains("accelerators") && j["accelerators"].is_array()) {
        for (const auto& accel_entry : j["accelerators"]) {
            if (!accel_entry.is_object()) continue;
            AcceleratorConfig accel;
            accel.name = accel_entry.value("name", "gpu");
            accel.inference_latency_us = accel_entry.value("inference_latency_us", 5000u);
            accel.max_queue_depth = accel_entry.value("max_queue_depth", 16);
            accel.bandwidth_mbps = accel_entry.value("bandwidth_mbps", 100.0);
            accel.scheduling = accel_entry.value("scheduling", "fifo");
            cfg.accelerators.push_back(accel);
        }
    }

    // ── Sensor Jitter Config ───────────────────────────────────────
    if (j.contains("sensor_jitter_config") && j["sensor_jitter_config"].is_array()) {
        for (const auto& jitter_cfg : j["sensor_jitter_config"]) {
            if (!jitter_cfg.is_object()) continue;
            SensorJitterCfg sj;
            sj.sensor_name = jitter_cfg.value("sensor_name", "");
            sj.jitter_percentage = jitter_cfg.value("jitter_percentage", 0.0);
            sj.enable_dma_interrupt = jitter_cfg.value("enable_dma_interrupt", false);
            sj.dma_arrival_jitter_us = jitter_cfg.value("dma_arrival_jitter_us", 0u);
            cfg.sensor_jitters.push_back(sj);
        }
    }

    // ── Bandwidth ──────────────────────────────────────────────────
    if (j.contains("bandwidth")) {
        auto& bw = j["bandwidth"];
        cfg.global_bandwidth_limit_mbps = bw.value("global_limit_mbps", 200.0);
        cfg.bandwidth_window_ms         = bw.value("window_duration_ms", 1000u);
    } else if (j.contains("bandwidth_budgets") && j["bandwidth_budgets"].is_object()) {
        const auto& bw = j["bandwidth_budgets"];
        cfg.global_bandwidth_limit_mbps = bw.value("compute_ecu_ingress_total_mbps", 200.0);
    }

    // ── Pipeline ───────────────────────────────────────────────────
    if (j.contains("pipeline") && j["pipeline"].is_object()) {
        auto& pl = j["pipeline"];
        cfg.queue_capacity = pl.value("queue_capacity", size_t(64));
    }
    load_scenario_pipeline(cfg, j);
    if (cfg.stages.empty()) {
        load_legacy_pipeline(cfg, j);
    }

    // ── Execution ──────────────────────────────────────────────────
    if (j.contains("execution")) {
        auto& ex = j["execution"];
        cfg.thread_pool_size     = ex.value("thread_pool_size", size_t(4));
        cfg.scheduler_type       = ex.value("scheduler", std::string("fifo"));
        cfg.central_cycle_mode   = ex.value("central_cycle_mode", std::string("allow_overlap"));
        cfg.stage_timing_mode    = ex.value("stage_timing_mode", std::string("hybrid"));
        cfg.stage_timing_sampled = ex.value("stage_timing_sampled", true);
        cfg.stage_spin_guard_us  = ex.value("stage_spin_guard_us", 300u);
        cfg.run_duration_seconds = ex.value("run_duration_seconds", 5u);
        cfg.queue_capacity       = ex.value("queue_capacity", cfg.queue_capacity);

        if (ex.contains("stage_timing") && ex["stage_timing"].is_object()) {
            const auto& st = ex["stage_timing"];
            cfg.stage_timing_mode = st.value("mode", cfg.stage_timing_mode);
            cfg.stage_timing_sampled = st.value("sampled", cfg.stage_timing_sampled);
            cfg.stage_spin_guard_us = st.value("spin_guard_us", cfg.stage_spin_guard_us);
        }

        // Metrics tracking
        if (ex.contains("metrics") && ex["metrics"].is_object()) {
            const auto& metrics = ex["metrics"];
            cfg.track_deadline_misses = metrics.value("track_deadline_misses", true);
            cfg.track_staleness = metrics.value("track_staleness", true);
            cfg.enable_fault_injection = metrics.value("enable_fault_injection", false);
            cfg.enable_thermal_throttling = metrics.value("enable_thermal_throttling", false);
        }
    } else if (j.contains("topology") && j["topology"].is_object()) {
        const auto& topo = j["topology"];
        if (topo.contains("nodes") && topo["nodes"].is_array()) {
            for (const auto& node : topo["nodes"]) {
                if (!node.is_object()) {
                    continue;
                }
                const std::string node_type = node.value("type", std::string());
                const std::string node_id = node.value("id", std::string());
                if (node_type == "multicore_ecu" || node_id == "compute_ecu_main") {
                    cfg.thread_pool_size = node.value("cores", size_t(4));
                    cfg.scheduler_type = node.value("scheduler", std::string("fifo"));
                    break;
                }
            }
        }
    }

    if (j.contains("timing") && j["timing"].is_object()) {
        const auto& timing = j["timing"];
        cfg.central_loop_rate_hz = timing.value(
            "central_loop_rate_hz",
            timing.value("prediction_planning_rate_hz", 50.0));
    }

    if (cfg.stages.empty()) {
        throw std::runtime_error("Config does not define any executable pipeline stages");
    }

    return cfg;
}
} // namespace adas
