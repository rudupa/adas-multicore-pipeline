#pragma once

#include "pipeline/pipeline_stage.h"
#include <cstdint>

namespace adas {

/// Second pipeline stage — simulates object detection (e.g. neural-net inference).
class DetectionStage : public PipelineStage {
public:
    explicit DetectionStage(uint32_t delay_us = 2000);

    const std::string& name() const override { return name_; }
    void process(std::shared_ptr<Frame>& frame) override;

private:
    std::string name_{"detection"};
    uint32_t    delay_us_;
};

} // namespace adas
