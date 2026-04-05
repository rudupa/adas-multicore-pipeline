#include "core/bandwidth_manager.h"
#include <algorithm>
#include <limits>
#include <thread>

namespace adas {

BandwidthManager::BandwidthManager(double global_limit_mbps, uint32_t window_ms)
    : global_limit_bytes_per_window_(
          global_limit_mbps * 1'000'000.0 / 8.0 * (window_ms / 1000.0)),
      window_ms_(window_ms),
      window_start_(Clock::now()) {}

uint64_t BandwidthManager::request(const std::string& sensor_name,
                                   size_t bytes,
                                   double per_sensor_limit_mbps) {
    std::lock_guard lock(mutex_);
    advance_window();

    const auto now = Clock::now();
    auto [it, inserted] = sensor_windows_.try_emplace(sensor_name, SensorWindowState{now, 0});
    (void)inserted;
    advance_sensor_window(it->second, now);

    const size_t global_limit = static_cast<size_t>(std::max(0.0, global_limit_bytes_per_window_));
    const bool global_exhausted = (bytes_in_window_ + bytes) > global_limit;

    size_t sensor_limit = std::numeric_limits<size_t>::max();
    bool sensor_exhausted = false;
    if (per_sensor_limit_mbps > 0.0) {
        const double sensor_limit_bytes =
            per_sensor_limit_mbps * 1'000'000.0 / 8.0 * (window_ms_ / 1000.0);
        sensor_limit = static_cast<size_t>(std::max(0.0, sensor_limit_bytes));
        sensor_exhausted = (it->second.bytes_in_window + bytes) > sensor_limit;
    }

    if (global_exhausted || sensor_exhausted) {
        uint64_t delay_us = 1;
        if (global_exhausted) {
            delay_us = std::max(delay_us, remaining_window_us(window_start_, now));
        }
        if (sensor_exhausted) {
            delay_us = std::max(delay_us, remaining_window_us(it->second.window_start, now));
        }
        return delay_us;
    }

    bytes_in_window_ += bytes;
    it->second.bytes_in_window += bytes;
    return 0; // no throttle
}

void BandwidthManager::reset() {
    std::lock_guard lock(mutex_);
    bytes_in_window_ = 0;
    window_start_    = Clock::now();
    sensor_windows_.clear();
}

size_t BandwidthManager::current_usage_bytes() const {
    std::lock_guard lock(mutex_);
    return bytes_in_window_;
}

size_t BandwidthManager::current_usage_bytes(const std::string& sensor_name) const {
    std::lock_guard lock(mutex_);
    auto it = sensor_windows_.find(sensor_name);
    if (it == sensor_windows_.end()) {
        return 0;
    }
    return it->second.bytes_in_window;
}

void BandwidthManager::advance_window() {
    auto now     = Clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - window_start_)
                       .count();
    if (elapsed >= window_ms_) {
        window_start_    = now;
        bytes_in_window_ = 0;
    }
}

void BandwidthManager::advance_sensor_window(SensorWindowState& state, TimePoint now) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now - state.window_start)
                             .count();
    if (elapsed >= window_ms_) {
        state.window_start = now;
        state.bytes_in_window = 0;
    }
}

uint64_t BandwidthManager::remaining_window_us(TimePoint window_start, TimePoint now) const {
    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                now - window_start)
                                .count();
    const uint64_t window_us = static_cast<uint64_t>(window_ms_) * 1000ULL;
    if (elapsed_us >= static_cast<int64_t>(window_us)) {
        return 1;
    }
    const uint64_t remaining = window_us - static_cast<uint64_t>(elapsed_us);
    return remaining == 0 ? 1 : remaining;
}

} // namespace adas
