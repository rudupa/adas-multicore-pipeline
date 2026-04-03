#pragma once

#include "core/types.h"
#include <memory>
#include <string>

namespace adas {

/// Abstract sensor interface.
/// All sensor implementations must derive from this.
class Sensor {
public:
    virtual ~Sensor() = default;

    /// Begin generating frames at the configured rate.
    virtual void start() = 0;

    /// Stop frame generation.
    virtual void stop() = 0;

    /// Produce the next frame (blocking for the configured interval).
    /// Returns nullptr if the sensor has been stopped.
    virtual std::shared_ptr<Frame> generateFrame() = 0;

    /// Sensor metadata
    virtual const std::string& name() const = 0;
    virtual SensorType         type() const = 0;
    virtual double             fps()  const = 0;
    virtual size_t     frame_size_bytes() const = 0;
    virtual double  bandwidth_limit_mbps() const = 0;
};

} // namespace adas
