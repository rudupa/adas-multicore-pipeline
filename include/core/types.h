#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace adas {

// ─── Time helpers ──────────────────────────────────────────────────
using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration  = std::chrono::microseconds;

inline double to_ms(Duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
}

inline double to_us(Duration d) {
    return static_cast<double>(d.count());
}

// ─── Sensor types ──────────────────────────────────────────────────
enum class SensorType : uint8_t {
    Camera = 0,
    Radar,
    VehicleState,
    Lidar,   // TODO: implement LidarSensor
    Count
};

inline const char* sensor_type_name(SensorType t) {
    switch (t) {
        case SensorType::Camera: return "camera";
        case SensorType::Radar:  return "radar";
        case SensorType::VehicleState: return "vehicle_state";
        case SensorType::Lidar:  return "lidar";
        default:                 return "unknown";
    }
}

// ─── Frame ─────────────────────────────────────────────────────────
// Represents a single data frame produced by a sensor and consumed by
// the pipeline.  We do NOT allocate real pixel data — only metadata
// needed for scheduling and metrics.
struct Frame {
    uint64_t    frame_id       = 0;
    SensorType  sensor_type    = SensorType::Camera;
    std::string sensor_name;
    size_t      data_size      = 0;          // bytes (simulated)

    // Timestamps for metrics
    TimePoint   created_at     = Clock::now();
    TimePoint   pipeline_enter = {};         // when queued into stage 1
    TimePoint   pipeline_exit  = {};         // when last stage finishes
};

// ─── Task priority (for future scheduler extensions) ───────────────
enum class TaskPriority : uint8_t {
    Low    = 0,
    Normal = 1,
    High   = 2,
};

} // namespace adas
