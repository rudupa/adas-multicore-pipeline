#include "visualization/timeline_visualizer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace adas {

// ────────────────────────────────────────────────────────────────────
// Recording
// ────────────────────────────────────────────────────────────────────

void TimelineVisualizer::record_event(const std::string& lane,
                                      TimePoint start, TimePoint end,
                                      char glyph, uint64_t frame_id) {
    std::lock_guard lock(mutex_);
    ensure_lane(lane);
    events_.push_back({lane, start, end, glyph, frame_id});
}

void TimelineVisualizer::record_marker(const std::string& lane,
                                       TimePoint time, char glyph) {
    std::lock_guard lock(mutex_);
    ensure_lane(lane);
    markers_.push_back({lane, time, glyph});
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

// ────────────────────────────────────────────────────────────────────
// Rendering — full timeline
// ────────────────────────────────────────────────────────────────────

std::string TimelineVisualizer::render(int width_chars,
                                       double time_window_ms) const {
    std::lock_guard lock(mutex_);
    if (lane_order_.empty()) return "(no events recorded)\n";

    // Determine time range
    double max_ms = 0.0;
    for (auto& e : events_) {
        double t = std::chrono::duration<double, std::milli>(
                       e.end - origin_).count();
        max_ms = std::max(max_ms, t);
    }
    for (auto& m : markers_) {
        double t = std::chrono::duration<double, std::milli>(
                       m.time - origin_).count();
        max_ms = std::max(max_ms, t);
    }
    if (time_window_ms > 0.0) max_ms = time_window_ms;
    if (max_ms <= 0.0) max_ms = 1.0;

    double ms_per_col = max_ms / static_cast<double>(width_chars);

    // Label width
    size_t label_w = 4; // minimum
    for (auto& l : lane_order_)
        label_w = std::max(label_w, l.size());
    label_w += 2;

    // Build character grid: lane_order_.size() rows × width_chars cols
    size_t num_lanes = lane_order_.size();
    std::vector<std::string> grid(num_lanes, std::string(width_chars, ' '));

    // Rasterise span events
    for (auto& e : events_) {
        auto it = lane_index_.find(e.lane);
        if (it == lane_index_.end()) continue;
        int row = it->second;

        double s_ms = std::chrono::duration<double, std::milli>(
                          e.start - origin_).count();
        double e_ms = std::chrono::duration<double, std::milli>(
                          e.end - origin_).count();

        int col_start = static_cast<int>(s_ms / ms_per_col);
        int col_end   = static_cast<int>(e_ms / ms_per_col);
        col_start = std::clamp(col_start, 0, width_chars - 1);
        col_end   = std::clamp(col_end,   0, width_chars - 1);
        if (col_end == col_start) col_end = std::min(col_start + 1, width_chars - 1);

        for (int c = col_start; c <= col_end; ++c)
            grid[row][c] = e.glyph;
    }

    // Rasterise point markers (overwrite)
    for (auto& m : markers_) {
        auto it = lane_index_.find(m.lane);
        if (it == lane_index_.end()) continue;
        int row = it->second;
        double t_ms = std::chrono::duration<double, std::milli>(
                          m.time - origin_).count();
        int col = static_cast<int>(t_ms / ms_per_col);
        col = std::clamp(col, 0, width_chars - 1);
        grid[row][col] = m.glyph;
    }

    // Render to string
    std::ostringstream oss;
    oss << "\n";

    // ── Header: time axis ──────────────────────────────────────────
    oss << std::string(label_w, ' ');
    int num_ticks = 10;
    int tick_spacing = width_chars / num_ticks;
    for (int i = 0; i <= num_ticks; ++i) {
        int col = i * tick_spacing;
        if (col >= width_chars) break;
        double ms = col * ms_per_col;
        std::ostringstream tick;
        tick << std::fixed << std::setprecision(0) << ms;
        std::string ts = tick.str();
        // Place tick label
        if (col + static_cast<int>(ts.size()) <= width_chars)
            oss << ts;
        int pad = tick_spacing - static_cast<int>(ts.size());
        if (pad > 0) oss << std::string(pad, ' ');
    }
    oss << "  (ms)\n";

    // ── Separator ──────────────────────────────────────────────────
    oss << std::string(label_w, ' ')
        << std::string(width_chars, '-') << "\n";

    // ── Lanes ──────────────────────────────────────────────────────
    for (size_t i = 0; i < num_lanes; ++i) {
        std::string label = lane_order_[i];
        if (label.size() < label_w)
            label += std::string(label_w - label.size(), ' ');
        oss << label << "|" << grid[i] << "|\n";
    }

    // ── Footer separator ───────────────────────────────────────────
    oss << std::string(label_w, ' ')
        << std::string(width_chars, '-') << "\n";

    // ── Legend ──────────────────────────────────────────────────────
    oss << "\n"
        << "  Legend:  # = sensor generate   "
        << "P = preprocess   D = detection   T = tracking   "
        << "X = frame drop   ~ = BW throttle\n"
        << "  Scale:  1 col = " << std::fixed << std::setprecision(2)
        << ms_per_col << " ms   |   total = "
        << std::setprecision(1) << max_ms << " ms\n\n";

    return oss.str();
}

void TimelineVisualizer::print(int width_chars,
                               double time_window_ms) const {
    std::printf("%s", render(width_chars, time_window_ms).c_str());
}

// ────────────────────────────────────────────────────────────────────
// Rendering — per-frame waterfall
// ────────────────────────────────────────────────────────────────────

std::string TimelineVisualizer::render_waterfall(size_t max_frames,
                                                 int width_chars) const {
    std::lock_guard lock(mutex_);

    // Group events by frame_id — only show frames that have at least
    // one pipeline stage event (P, D, or T), not just sensor-only frames.
    std::map<uint64_t, std::vector<const Event*>> by_frame;
    for (auto& e : events_) {
        by_frame[e.frame_id].push_back(&e);
    }

    // Filter: keep only frames that have pipeline stage glyphs
    std::map<uint64_t, std::vector<const Event*>> completed;
    for (auto& [fid, evts] : by_frame) {
        bool has_stage = false;
        for (auto* ep : evts) {
            if (ep->glyph == 'P' || ep->glyph == 'D' || ep->glyph == 'T') {
                has_stage = true;
                break;
            }
        }
        if (has_stage) completed[fid] = evts;
    }

    if (completed.empty()) return "(no completed per-frame events)\n";

    std::ostringstream oss;
    oss << "\n── Per-Frame Waterfall (first " << max_frames << " frames) ──\n\n";

    // Header
    size_t label_w = 14;
    oss << std::left << std::setw(label_w) << "Frame"
        << "| Stage Timeline\n";
    oss << std::string(label_w, '-') << "|"
        << std::string(width_chars, '-') << "\n";

    size_t count = 0;
    for (auto& [fid, evts] : completed) {
        if (count >= max_frames) break;

        // Find per-frame time range
        TimePoint fmin = evts.front()->start;
        TimePoint fmax = evts.front()->end;
        for (auto* ep : evts) {
            if (ep->start < fmin) fmin = ep->start;
            if (ep->end   > fmax) fmax = ep->end;
        }
        double span_ms = std::chrono::duration<double, std::milli>(
                             fmax - fmin).count();
        if (span_ms <= 0.0) span_ms = 0.1;
        double ms_per_col = span_ms / static_cast<double>(width_chars);

        // Build single row
        std::string row(width_chars, ' ');
        for (auto* ep : evts) {
            double s = std::chrono::duration<double, std::milli>(
                           ep->start - fmin).count();
            double e = std::chrono::duration<double, std::milli>(
                           ep->end - fmin).count();
            int c0 = std::clamp(static_cast<int>(s / ms_per_col), 0, width_chars - 1);
            int c1 = std::clamp(static_cast<int>(e / ms_per_col), 0, width_chars - 1);
            if (c1 == c0) c1 = std::min(c0 + 1, width_chars - 1);
            for (int c = c0; c <= c1; ++c) row[c] = ep->glyph;
        }

        // Label
        std::ostringstream lbl;
        lbl << "F" << fid << " ("
            << std::fixed << std::setprecision(1) << span_ms << "ms)";
        oss << std::left << std::setw(label_w) << lbl.str()
            << "|" << row << "|\n";
        ++count;
    }

    oss << std::string(label_w, '-') << "|"
        << std::string(width_chars, '-') << "\n";

    oss << "\n  Glyphs:  # = sensor   P = preprocess   "
        << "D = detection   T = tracking\n\n";

    return oss.str();
}

void TimelineVisualizer::print_waterfall(size_t max_frames,
                                         int width_chars) const {
    std::printf("%s", render_waterfall(max_frames, width_chars).c_str());
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
