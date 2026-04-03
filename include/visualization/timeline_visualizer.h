#pragma once

#include "core/types.h"
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace adas {

/// Records timestamped events across the pipeline and renders them as
/// text-based timing diagrams.
///
/// Each "lane" is a named row in the diagram (sensor, stage, thread).
/// Events are spans with a start/end time, rendered as character blocks.
///
/// Example output:
///
///   Time(ms)        0    5   10   15   20   25   30   35   40
///   front_camera    ████░░░░████░░░░████░░░░████░░░░████░░░░
///   front_radar     ██████░░░░░░██████░░░░░░██████░░░░░░████
///   preprocess      ░░██░░██░░██░░██░░██░░██░░██░░██░░██░░██
///   detection       ░░░░████░░░░████░░░░████░░░░████░░░░████
///   tracking        ░░░░░░██░░░░░░██░░░░░░██░░░░░░██░░░░░░██
///   BW-throttle     ░░░░░░░░░░░░░░░░░░▓▓░░░░░░░░░░░░░░▓▓░░
///   frame-drop      ░░░░░░░░░░░░░░░░░░░░X░░░░░░░░░░░░░░X░░
///
class TimelineVisualizer {
public:
    /// One span of activity on a named lane.
    struct Event {
        std::string lane;          // row label
        TimePoint   start;
        TimePoint   end;
        char        glyph = '#';   // character used to render the span
        uint64_t    frame_id = 0;  // optional association
    };

    /// Instantaneous marker (drop, throttle hit, etc.).
    struct Marker {
        std::string lane;
        TimePoint   time;
        char        glyph = 'X';
    };

    // ── Recording (thread-safe) ────────────────────────────────────

    /// Record a span event (e.g. stage processing, sensor generation).
    void record_event(const std::string& lane, TimePoint start,
                      TimePoint end, char glyph = '#',
                      uint64_t frame_id = 0);

    /// Record an instantaneous marker (e.g. frame drop).
    void record_marker(const std::string& lane, TimePoint time,
                       char glyph = 'X');

    /// Set the global reference time (usually pipeline start).
    void set_origin(TimePoint origin);

    // ── Rendering ──────────────────────────────────────────────────

    /// Render the full timeline to a string.
    /// @param width_chars  Total width of the diagram body (default 120).
    /// @param time_window_ms  How many ms to render (0 = auto-fit).
    std::string render(int width_chars = 120,
                       double time_window_ms = 0.0) const;

    /// Render and print to stdout.
    void print(int width_chars = 120,
               double time_window_ms = 0.0) const;

    /// Render the first N frames as a compact per-frame waterfall.
    std::string render_waterfall(size_t max_frames = 20,
                                 int width_chars = 100) const;

    void print_waterfall(size_t max_frames = 20,
                         int width_chars = 100) const;

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
