#include "visualization/timeline_visualizer.h"

namespace adas {

// ────────────────────────────────────────────────────────────────────
// Recording
// ────────────────────────────────────────────────────────────────────

void TimelineVisualizer::record_event(const std::string& lane,
                                      TimePoint start, TimePoint end,
                                      char glyph, uint64_t frame_id,
                                      const std::string& source) {
    std::lock_guard lock(mutex_);
    ensure_lane(lane);
    events_.push_back({lane, start, end, glyph, frame_id, source});
}

void TimelineVisualizer::record_marker(const std::string& lane,
                                       TimePoint time, char glyph,
                                       uint64_t frame_id,
                                       const std::string& source,
                                       const std::string& detail) {
    std::lock_guard lock(mutex_);
    ensure_lane(lane);
    markers_.push_back({lane, time, glyph, frame_id, source, detail});
}

void TimelineVisualizer::set_origin(TimePoint origin) {
    std::lock_guard lock(mutex_);
    origin_ = origin;
}

void TimelineVisualizer::reset() {
    std::lock_guard lock(mutex_);
    events_.clear();
    markers_.clear();
    lane_order_.clear();
    lane_index_.clear();
}

int TimelineVisualizer::ensure_lane(const std::string& lane) {
    auto it = lane_index_.find(lane);
    if (it != lane_index_.end()) return it->second;
    int idx = static_cast<int>(lane_order_.size());
    lane_order_.push_back(lane);
    lane_index_[lane] = idx;
    return idx;
}

TimelineVisualizer::Snapshot TimelineVisualizer::snapshot() const {
    std::lock_guard lock(mutex_);
    Snapshot s;
    s.events     = events_;
    s.markers    = markers_;
    s.lane_order = lane_order_;
    s.origin     = origin_;
    return s;
}

} // namespace adas
