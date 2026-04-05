/// ADAS Multicore Pipeline Simulator
/// ==================================
/// A configurable simulation of a real-time ADAS compute pipeline with
/// multicore execution, bandwidth management, and performance profiling.
///
/// Usage:
///   adas_pipeline [config_path]
///
/// If no config path is given, it looks for config/adas_pipeline_config.json
/// relative to the working directory.

#include "core/config_loader.h"
#include "pipeline/pipeline.h"

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char* argv[]) {
    std::string config_path = "config/adas_pipeline_config.json";
    if (argc > 1) {
        config_path = argv[1];
    }

    std::printf("ADAS Sense-Plan-Act Simulator\n");
    std::printf("──────────────────────────────────\n");
    std::printf("Config: %s\n\n", config_path.c_str());

    try {
        adas::PipelineConfig cfg = adas::load_config(config_path);

        std::printf("Sensors configured : %zu\n", cfg.sensors.size());
        for (auto& s : cfg.sensors) {
            std::printf("  - %-20s  type=%-13s  fps=%.0f  frame=%zu B  bw_limit=%.1f Mbps\n",
                        s.name.c_str(), s.type.c_str(), s.fps,
                        s.frame_size_bytes, s.bandwidth_limit_mbps);
        }
        std::printf("Global BW limit    : %.1f Mbps\n", cfg.global_bandwidth_limit_mbps);
        std::printf("Thread pool size   : %zu\n", cfg.thread_pool_size);
        std::printf("Scheduler          : %s\n", cfg.scheduler_type.c_str());
        std::printf("Run duration       : %u s\n", cfg.run_duration_seconds);
        std::printf("Pipeline phases    : %zu\n", cfg.phases.size());
        std::printf("Executable stages  : %zu\n", cfg.stages.size());
        for (const auto& phase : cfg.phases) {
            std::printf("  [%s] %s\n", phase.id.c_str(), phase.name.c_str());
            for (const auto& stage : cfg.stages) {
                if (stage.phase_id != phase.id) {
                    continue;
                }
                std::printf("    - %-32s  %.3f ms\n",
                            stage.name.c_str(),
                            static_cast<double>(stage.delay_us) / 1000.0);
            }
        }
        std::printf("\n");

        adas::Pipeline pipeline(cfg);
        pipeline.run();

    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[error] %s\n", ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
