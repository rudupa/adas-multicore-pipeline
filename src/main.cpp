/// ADAS Multicore Pipeline Simulator
/// ==================================
/// A configurable simulation of a real-time ADAS compute pipeline with
/// multicore execution, bandwidth management, and performance profiling.
///
/// Usage:
///   adas_pipeline [config_path]
///
/// If no config path is given, it looks for config/pipeline_config.json
/// relative to the working directory.

#include "core/config_loader.h"
#include "pipeline/pipeline.h"

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char* argv[]) {
    std::string config_path = "config/pipeline_config.json";
    if (argc > 1) {
        config_path = argv[1];
    }

    std::printf("ADAS Multicore Pipeline Simulator\n");
    std::printf("──────────────────────────────────\n");
    std::printf("Config: %s\n\n", config_path.c_str());

    try {
        adas::PipelineConfig cfg = adas::load_config(config_path);

        std::printf("Sensors configured : %zu\n", cfg.sensors.size());
        for (auto& s : cfg.sensors) {
            std::printf("  - %-16s  type=%-8s  fps=%.0f  frame=%zu B  bw_limit=%.1f MB/s\n",
                        s.name.c_str(), s.type.c_str(), s.fps,
                        s.frame_size_bytes, s.bandwidth_limit_mbps);
        }
        std::printf("Global BW limit    : %.1f MB/s\n", cfg.global_bandwidth_limit_mbps);
        std::printf("Thread pool size   : %zu\n", cfg.thread_pool_size);
        std::printf("Scheduler          : %s\n", cfg.scheduler_type.c_str());
        std::printf("Run duration       : %u s\n\n", cfg.run_duration_seconds);

        adas::Pipeline pipeline(cfg);
        pipeline.run();

    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[error] %s\n", ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
