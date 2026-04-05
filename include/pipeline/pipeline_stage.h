#pragma once

#include "core/types.h"
#include <memory>
#include <string>

namespace adas {

/// Abstract interface for a single processing stage in the pipeline.
class PipelineStage {
public:
    virtual ~PipelineStage() = default;

    /// Human-readable name of this stage (for metrics).
    virtual const std::string& name() const = 0;

    /// Single-character identifier used by the timeline viewer.
    virtual char glyph() const = 0;

    /// Process one frame.  The stage may modify the frame in-place
    /// (e.g. add annotations, update timestamps).
    virtual void process(std::shared_ptr<Frame>& frame) = 0;
};

} // namespace adas
