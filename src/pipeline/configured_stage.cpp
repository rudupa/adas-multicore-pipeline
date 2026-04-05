#include "pipeline/configured_stage.h"

#include <chrono>
#include <random>
#include <thread>

namespace adas {

namespace {

uint32_t sample_triangular_us(uint32_t min_us, uint32_t mode_us, uint32_t max_us) {
    if (min_us >= max_us) {
        return mode_us;
    }
    const double a = static_cast<double>(min_us);
    const double b = static_cast<double>(mode_us);
    const double c = static_cast<double>(max_us);
    const double f = (b - a) / (c - a);

    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    const double u = dist(rng);

    const double sampled = (u < f)
        ? a + std::sqrt(u * (b - a) * (c - a))
        : c - std::sqrt((1.0 - u) * (c - b) * (c - a));
    return static_cast<uint32_t>(std::max(1.0, sampled));
}

void wait_us(uint32_t duration_us, const std::string& timing_mode, uint32_t spin_guard_us) {
    if (timing_mode == "spin") {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::microseconds(duration_us);
        while (std::chrono::steady_clock::now() < deadline) {
        }
        return;
    }

    if (timing_mode == "hybrid") {
        if (duration_us > spin_guard_us) {
            std::this_thread::sleep_for(std::chrono::microseconds(duration_us - spin_guard_us));
        }
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::microseconds(std::min(duration_us, spin_guard_us));
        while (std::chrono::steady_clock::now() < deadline) {
        }
        return;
    }

    std::this_thread::sleep_for(std::chrono::microseconds(duration_us));
}

} // namespace

ConfiguredStage::ConfiguredStage(std::string name,
                                 uint32_t delay_us,
                                 uint32_t delay_us_min,
                                 uint32_t delay_us_max,
                                 char glyph,
                                 std::string timing_mode,
                                 bool sampled_timing,
                                 uint32_t spin_guard_us)
    : name_(std::move(name)),
      delay_us_(delay_us),
      delay_us_min_(delay_us_min),
      delay_us_max_(delay_us_max),
      glyph_(glyph),
      timing_mode_(std::move(timing_mode)),
      sampled_timing_(sampled_timing),
      spin_guard_us_(spin_guard_us) {}

void ConfiguredStage::process(std::shared_ptr<Frame>& /*frame*/) {
    const uint32_t duration_us = sampled_timing_
        ? sample_triangular_us(delay_us_min_, delay_us_, delay_us_max_)
        : delay_us_;
    wait_us(duration_us, timing_mode_, spin_guard_us_);
}

} // namespace adas