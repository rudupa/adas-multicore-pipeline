#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace adas {

struct StageConfig {
    std::string              id;
    std::string              name;
    std::string              lane;
    std::string              phase_id;
    std::string              phase_name;
    std::string              primary_node_id;
    std::string              primary_node_name;
    std::vector<std::string> mapped_nodes;
    std::vector<std::string> substeps;
    uint32_t                 delay_us = 1000;
    uint32_t                 delay_us_min = 1000;
    uint32_t                 delay_us_max = 1000;
    char                     glyph = 'S';
};

struct PhaseConfig {
    std::string              id;
    std::string              name;
    std::vector<std::string> stage_ids;
};

/// POD holding the full simulation configuration loaded from JSON.
struct PipelineConfig {
    // ── Per-sensor config ──────────────────────────────────────────
    struct SensorCfg {
        std::string type;                    // "camera", "radar", "lidar"
        std::string name;                    // logical name
        std::string short_name;              // display label in viewer
        double      fps             = 30.0;
        size_t      frame_size_bytes= 0;
        double      bandwidth_limit_mbps = 0.0;  // 0 = no per-sensor cap
    };
    std::vector<SensorCfg> sensors;

    // ── Bandwidth ──────────────────────────────────────────────────
    double   global_bandwidth_limit_mbps = 200.0;
    uint32_t bandwidth_window_ms         = 1000;

    // ── Pipeline stages ────────────────────────────────────────────
    std::vector<PhaseConfig> phases;
    std::vector<StageConfig> stages;
    size_t   queue_capacity      = 64;

    // ── Execution ──────────────────────────────────────────────────
    size_t      thread_pool_size     = 4;
    std::string scheduler_type       = "fifo";
    std::string central_cycle_mode   = "allow_overlap";
    std::string stage_timing_mode    = "hybrid"; // sleep | spin | hybrid
    bool        stage_timing_sampled = true;      // sample between min/avg/max
    uint32_t    stage_spin_guard_us  = 300;       // hybrid: spin this tail
    uint32_t    run_duration_seconds = 5;
    double      central_loop_rate_hz = 50.0;
};

/// Load a PipelineConfig from a JSON file.
/// Throws std::runtime_error on parse failure.
PipelineConfig load_config(const std::string& json_path);

} // namespace adas
