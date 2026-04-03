#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace adas {

/// POD holding the full simulation configuration loaded from JSON.
struct PipelineConfig {
    // ── Per-sensor config ──────────────────────────────────────────
    struct SensorCfg {
        std::string type;                    // "camera", "radar", "lidar"
        std::string name;                    // logical name
        double      fps             = 30.0;
        size_t      frame_size_bytes= 0;
        double      bandwidth_limit_mbps = 0.0;  // 0 = no per-sensor cap
    };
    std::vector<SensorCfg> sensors;

    // ── Bandwidth ──────────────────────────────────────────────────
    double   global_bandwidth_limit_mbps = 200.0;
    uint32_t bandwidth_window_ms         = 1000;

    // ── Pipeline stages ────────────────────────────────────────────
    uint32_t preprocess_delay_us = 500;
    uint32_t detection_delay_us  = 2000;
    uint32_t tracking_delay_us   = 1000;
    size_t   queue_capacity      = 64;

    // ── Execution ──────────────────────────────────────────────────
    size_t      thread_pool_size     = 4;
    std::string scheduler_type       = "fifo";
    uint32_t    run_duration_seconds = 5;
};

/// Load a PipelineConfig from a JSON file.
/// Throws std::runtime_error on parse failure.
PipelineConfig load_config(const std::string& json_path);

} // namespace adas
