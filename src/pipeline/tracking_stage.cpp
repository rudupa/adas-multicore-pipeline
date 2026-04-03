#include "pipeline/tracking_stage.h"
#include <chrono>
#include <thread>

namespace adas {

TrackingStage::TrackingStage(uint32_t delay_us)
    : delay_us_(delay_us) {}

void TrackingStage::process(std::shared_ptr<Frame>& /*frame*/) {
    // Simulate multi-object tracking computation
    std::this_thread::sleep_for(std::chrono::microseconds(delay_us_));
}

} // namespace adas
