#pragma once

#include "core/types.h"
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>

namespace adas {

/// Tracks aggregate data throughput and enforces bandwidth caps.
/// Sensors call `request()` before delivering a frame.  If the
/// budget is exhausted the call returns the delay (in microseconds)
/// the caller should sleep before transmitting.
class BandwidthManager {
public:
    /// @param global_limit_mbps  Total bandwidth cap across all sensors.
    /// @param window_ms          Sliding window size for accounting.
    explicit BandwidthManager(double global_limit_mbps = 200.0,
                              uint32_t window_ms       = 1000);

    /// Request permission to transmit `bytes` of data for the named
    /// sensor.  Returns the number of microseconds the caller should
    /// delay (0 = no throttle).
    uint64_t request(const std::string& sensor_name,
                     size_t bytes,
                     double per_sensor_limit_mbps = 0.0);

    /// Reset counters (e.g. between test runs).
    void reset();

    /// Query current utilisation (bytes transferred in current window).
    size_t current_usage_bytes() const;

    /// Query per-sensor utilisation (bytes transferred in sensor window).
    size_t current_usage_bytes(const std::string& sensor_name) const;

private:
    struct SensorWindowState {
        TimePoint window_start;
        size_t    bytes_in_window = 0;
    };

    void advance_window();
    void advance_sensor_window(SensorWindowState& state, TimePoint now);
    uint64_t remaining_window_us(TimePoint window_start, TimePoint now) const;

    mutable std::mutex mutex_;
    double             global_limit_bytes_per_window_;
    uint32_t           window_ms_;
    TimePoint          window_start_;
    size_t             bytes_in_window_ = 0;
    std::map<std::string, SensorWindowState> sensor_windows_;
};

} // namespace adas
