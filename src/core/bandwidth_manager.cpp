#include "core/bandwidth_manager.h"
#include <algorithm>
#include <thread>

namespace adas {

BandwidthManager::BandwidthManager(double global_limit_mbps, uint32_t window_ms)
    : global_limit_bytes_per_window_(
          global_limit_mbps * 1'000'000.0 / 8.0 * (window_ms / 1000.0)),
      window_ms_(window_ms),
      window_start_(Clock::now()) {}

uint64_t BandwidthManager::request(const std::string& /*sensor_name*/,
                                   size_t bytes,
                                   double per_sensor_limit_mbps) {
    std::lock_guard lock(mutex_);
    advance_window();

    // Check per-sensor limit (simplified: treat each call independently)
    double effective_limit = global_limit_bytes_per_window_;
    if (per_sensor_limit_mbps > 0.0) {
        double sensor_limit =
            per_sensor_limit_mbps * 1'000'000.0 / 8.0 * (window_ms_ / 1000.0);
        effective_limit = std::min(effective_limit, sensor_limit);
    }

    if (bytes_in_window_ + bytes > static_cast<size_t>(effective_limit)) {
        // Budget exhausted — compute delay until next window opens.
        auto now     = Clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - window_start_)
                           .count();
        uint64_t delay_ms = static_cast<uint64_t>(window_ms_) -
                            static_cast<uint64_t>(elapsed);
        if (delay_ms == 0) delay_ms = 1;
        return delay_ms * 1000; // return microseconds
    }

    bytes_in_window_ += bytes;
    return 0; // no throttle
}

void BandwidthManager::reset() {
    std::lock_guard lock(mutex_);
    bytes_in_window_ = 0;
    window_start_    = Clock::now();
}

size_t BandwidthManager::current_usage_bytes() const {
    std::lock_guard lock(mutex_);
    return bytes_in_window_;
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

} // namespace adas
