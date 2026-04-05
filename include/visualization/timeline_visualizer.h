#pragma once

#include "core/types.h"
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace adas {

/// Records timestamped events across the pipeline for visualization.
///
/// This buffer is consumed by the desktop GUI viewer (ImGui + ImPlot).
/// Each "lane" is a named row (sensor, stage, bandwidth, drop).
class TimelineVisualizer {
public:
    /// One span of activity on a named lane.
    struct Event {
        std::string lane;          // row label
        TimePoint   start;
        TimePoint   end;
        char        glyph = '#';   // character used to render the span
        uint64_t    frame_id = 0;  // optional association
        std::string source;        // originating sensor/stream
    };

    /// Instantaneous marker (drop, throttle hit, etc.).
    struct Marker {
        std::string lane;
        TimePoint   time;
        char        glyph = 'X';
        uint64_t    frame_id = 0;
        std::string source;
        std::string detail;
    };

    /// Immutable copy of current visualization buffers.
    struct Snapshot {
        std::vector<Event>       events;
        std::vector<Marker>      markers;
        std::vector<std::string> lane_order;
        TimePoint                origin;
    };

    // ── Recording (thread-safe) ────────────────────────────────────

    /// Record a span event (e.g. stage processing, sensor generation).
    void record_event(const std::string& lane, TimePoint start,
                      TimePoint end, char glyph = '#',
                      uint64_t frame_id = 0,
                      const std::string& source = {});

    /// Record an instantaneous marker (e.g. frame drop).
    void record_marker(const std::string& lane, TimePoint time,
                       char glyph = 'X', uint64_t frame_id = 0,
                       const std::string& source = {},
                       const std::string& detail = {});

    /// Set the global reference time (usually pipeline start).
    void set_origin(TimePoint origin);

    /// Return a consistent point-in-time copy of all lanes/events.
    Snapshot snapshot() const;

    /// Reset all recorded data.
    void reset();

private:
    mutable std::mutex          mutex_;
    std::vector<Event>          events_;
    std::vector<Marker>         markers_;
    TimePoint                   origin_ = Clock::now();

    // Ordered lane list (preserves insertion order for display)
    std::vector<std::string>    lane_order_;
    std::map<std::string, int>  lane_index_;

    int ensure_lane(const std::string& lane);
};

} // namespace adas
