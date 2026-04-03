#include "pipeline/detection_stage.h"
#include <chrono>
#include <thread>

namespace adas {

DetectionStage::DetectionStage(uint32_t delay_us)
    : delay_us_(delay_us) {}

void DetectionStage::process(std::shared_ptr<Frame>& /*frame*/) {
    // Simulate neural-net inference delay
    std::this_thread::sleep_for(std::chrono::microseconds(delay_us_));
}

} // namespace adas
