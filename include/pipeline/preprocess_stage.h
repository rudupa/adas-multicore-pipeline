#pragma once

#include "pipeline/pipeline_stage.h"
#include <cstdint>

namespace adas {

/// First pipeline stage — simulates image preprocessing / normalization.
class PreprocessStage : public PipelineStage {
public:
    explicit PreprocessStage(uint32_t delay_us = 500);

    const std::string& name() const override { return name_; }
    void process(std::shared_ptr<Frame>& frame) override;

private:
    std::string name_{"preprocess"};
    uint32_t    delay_us_;
};

} // namespace adas
