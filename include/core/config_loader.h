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
    // ── Real-time and multi-core scheduling ──────────────────────
    int                      priority = 50;              // 0-99, higher = more important
    int                      preferred_core = -1;       // -1 = no affinity, 0..n = pin to core
    std::string              accelerator = "";           // "gpu", "npu", "dsp", or "" for CPU
};

struct PhaseConfig {
    std::string              id;
    std::string              name;
    std::vector<std::string> stage_ids;
};

// ── CPU Core Configuration ─────────────────────────────────────────
struct CPUCore {
    int    core_id = 0;
    double freq_ghz = 2.0;          // Clock frequency
    int    max_tasks = 4;           // Maximum concurrent tasks (hyperthreads)
};

// ── Accelerator Configuration ──────────────────────────────────────
struct AcceleratorConfig {
    std::string name;               // "gpu", "npu", "dsp"
    uint32_t    inference_latency_us = 5000;  // DNN inference time
    uint32_t    max_queue_depth = 8;          // Task queue depth
    double      bandwidth_mbps = 100.0;       // Memory bandwidth
    std::string scheduling = "fifo";          // "fifo" or "priority"
};

// ── Sensor Jitter & Sporadic Task Configuration ────────────────────
struct SensorJitterCfg {
    std::string sensor_name;
    double      jitter_percentage = 0.0;      // e.g., 5% = 5% jitter
    bool        enable_dma_interrupt = false; // Sporadic DMA completion event
    uint32_t    dma_arrival_jitter_us = 0;
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
        double      jitter_percentage = 0.0;     // Frame timing jitter (%)
    };
    std::vector<SensorCfg> sensors;

    // ── CPU & Accelerator Configuration ────────────────────────────
    std::vector<CPUCore> cpu_cores;                // CPU cores with frequencies/limits
    std::vector<AcceleratorConfig> accelerators;   // GPU, NPU, DSP configs
    std::vector<SensorJitterCfg> sensor_jitters;   // Sensor-specific jitter

    // ── Bandwidth ──────────────────────────────────────────────────
    double   global_bandwidth_limit_mbps = 200.0;
    uint32_t bandwidth_window_ms         = 1000;

    // ── Pipeline stages ────────────────────────────────────────────
    std::vector<PhaseConfig> phases;
    std::vector<StageConfig> stages;
    size_t   queue_capacity      = 64;

    // ── Execution ──────────────────────────────────────────────────
    size_t      thread_pool_size     = 4;
    std::string scheduler_type       = "fifo";      // "fifo", "priority", "deadline"
    std::string central_cycle_mode   = "allow_overlap";
    std::string stage_timing_mode    = "hybrid";    // sleep | spin | hybrid
    bool        stage_timing_sampled = true;        // sample between min/avg/max
    uint32_t    stage_spin_guard_us  = 300;         // hybrid: spin this tail
    uint32_t    run_duration_seconds = 5;
    double      central_loop_rate_hz = 50.0;

    // ── Metrics & Monitoring ───────────────────────────────────────
    bool        track_deadline_misses = true;       // Deadline miss detection
    bool        track_staleness = true;             // Track data age at each stage
    bool        enable_fault_injection = false;     // Fault injection testing
    bool        enable_thermal_throttling = false;  // Frequency scaling
};

/// Load a PipelineConfig from a JSON file.
/// Throws std::runtime_error on parse failure.
PipelineConfig load_config(const std::string& json_path);

} // namespace adas
