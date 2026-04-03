#include "pipeline/preprocess_stage.h"
#include <chrono>
#include <thread>

namespace adas {

PreprocessStage::PreprocessStage(uint32_t delay_us)
    : delay_us_(delay_us) {}

void PreprocessStage::process(std::shared_ptr<Frame>& /*frame*/) {
    // Simulate computational work
    std::this_thread::sleep_for(std::chrono::microseconds(delay_us_));
}

} // namespace adas
