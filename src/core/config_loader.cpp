#include "core/config_loader.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace adas {

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
        for (auto& js : j["sensors"]) {
            PipelineConfig::SensorCfg sc;
            sc.type               = js.value("type", "camera");
            sc.name               = js.value("name", "unnamed");
            sc.fps                = js.value("fps", 30.0);
            sc.frame_size_bytes   = js.value("frame_size_bytes", size_t(0));
            sc.bandwidth_limit_mbps = js.value("bandwidth_limit_mbps", 0.0);
            cfg.sensors.push_back(std::move(sc));
        }
    }

    // ── Bandwidth ──────────────────────────────────────────────────
    if (j.contains("bandwidth")) {
        auto& bw = j["bandwidth"];
        cfg.global_bandwidth_limit_mbps = bw.value("global_limit_mbps", 200.0);
        cfg.bandwidth_window_ms         = bw.value("window_duration_ms", 1000u);
    }

    // ── Pipeline ───────────────────────────────────────────────────
    if (j.contains("pipeline")) {
        auto& pl = j["pipeline"];
        cfg.preprocess_delay_us = pl.value("preprocess_delay_us", 500u);
        cfg.detection_delay_us  = pl.value("detection_delay_us", 2000u);
        cfg.tracking_delay_us   = pl.value("tracking_delay_us", 1000u);
        cfg.queue_capacity      = pl.value("queue_capacity", size_t(64));
    }

    // ── Execution ──────────────────────────────────────────────────
    if (j.contains("execution")) {
        auto& ex = j["execution"];
        cfg.thread_pool_size     = ex.value("thread_pool_size", size_t(4));
        cfg.scheduler_type       = ex.value("scheduler", std::string("fifo"));
        cfg.run_duration_seconds = ex.value("run_duration_seconds", 5u);
    }

    return cfg;
}

} // namespace adas
