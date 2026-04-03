#pragma once

#include "pipeline/pipeline_stage.h"
#include <cstdint>

namespace adas {

/// Third pipeline stage — simulates multi-object tracking.
class TrackingStage : public PipelineStage {
public:
    explicit TrackingStage(uint32_t delay_us = 1000);

    const std::string& name() const override { return name_; }
    void process(std::shared_ptr<Frame>& frame) override;

private:
    std::string name_{"tracking"};
    uint32_t    delay_us_;
};

} // namespace adas
