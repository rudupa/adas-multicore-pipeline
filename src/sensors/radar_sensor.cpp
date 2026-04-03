#include "sensors/radar_sensor.h"
#include <chrono>
#include <thread>

namespace adas {

RadarSensor::RadarSensor(const std::string& name,
                         double fps,
                         size_t frame_size_bytes,
                         double bandwidth_limit_mbps)
    : name_(name),
      fps_(fps),
      frame_size_(frame_size_bytes),
      bw_limit_(bandwidth_limit_mbps) {}

void RadarSensor::start() {
    running_ = true;
    frame_counter_ = 0;
}

void RadarSensor::stop() {
    running_ = false;
}

std::shared_ptr<Frame> RadarSensor::generateFrame() {
    if (!running_) return nullptr;

    // Simulate sensor timing — sleep for one frame period
    auto period = std::chrono::microseconds(
        static_cast<int64_t>(1'000'000.0 / fps_));
    std::this_thread::sleep_for(period);

    if (!running_) return nullptr;

    auto frame          = std::make_shared<Frame>();
    frame->frame_id     = frame_counter_++;
    frame->sensor_type  = SensorType::Radar;
    frame->sensor_name  = name_;
    frame->data_size    = frame_size_;
    frame->created_at   = Clock::now();
    return frame;
}

} // namespace adas
