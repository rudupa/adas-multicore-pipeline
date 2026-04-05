#pragma once

#include "pipeline/pipeline_stage.h"

#include <cstdint>
#include <string>

namespace adas {

/// Generic pipeline stage created from configuration data.
class ConfiguredStage : public PipelineStage {
public:
    ConfiguredStage(std::string name,
                    uint32_t delay_us,
                    uint32_t delay_us_min,
                    uint32_t delay_us_max,
                    char glyph,
                    std::string timing_mode,
                    bool sampled_timing,
                    uint32_t spin_guard_us);

    const std::string& name() const override { return name_; }
    char glyph() const override { return glyph_; }
    void process(std::shared_ptr<Frame>& frame) override;

private:
    std::string name_;
    uint32_t    delay_us_ = 0;
    uint32_t    delay_us_min_ = 0;
    uint32_t    delay_us_max_ = 0;
    char        glyph_ = '#';
    std::string timing_mode_ = "hybrid";
    bool        sampled_timing_ = true;
    uint32_t    spin_guard_us_ = 300;
};

} // namespace adas