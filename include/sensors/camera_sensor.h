#pragma once

#include "sensors/sensor.h"
#include <atomic>

namespace adas {

class CameraSensor : public Sensor {
public:
    CameraSensor(const std::string& name,
                 double fps,
                 size_t frame_size_bytes,
                 double bandwidth_limit_mbps = 0.0);

    void start() override;
    void stop()  override;
    std::shared_ptr<Frame> generateFrame() override;

    const std::string& name() const override { return name_; }
    SensorType         type() const override { return SensorType::Camera; }
    double             fps()  const override { return fps_; }
    size_t     frame_size_bytes()  const override { return frame_size_; }
    double  bandwidth_limit_mbps() const override { return bw_limit_; }

private:
    std::string       name_;
    double            fps_;
    size_t            frame_size_;
    double            bw_limit_;
    std::atomic<bool> running_{false};
    uint64_t          frame_counter_ = 0;
};

} // namespace adas
