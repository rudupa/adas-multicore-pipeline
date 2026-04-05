#include "metrics/metrics_collector.h"

#include <algorithm>
#include <cstdio>
#include <numeric>

namespace adas {

void MetricsCollector::record_pipeline_enter(uint64_t frame_id,
                                             const std::string& sensor,
                                             TimePoint enter_time) {
    std::lock_guard lock(mutex_);
    auto& r       = frames_[frame_id];
    r.sensor      = sensor;
    r.enter_time  = enter_time;
}

void MetricsCollector::record_pipeline_exit(uint64_t frame_id,
                                            TimePoint exit_time) {
    std::lock_guard lock(mutex_);
    auto it = frames_.find(frame_id);
    if (it == frames_.end()) return;
    it->second.exit_time  = exit_time;
    it->second.completed  = true;
    ++total_completed_;
}

void MetricsCollector::record_stage_time(uint64_t frame_id,
                                         const std::string& stage_name,
                                         Duration processing_time) {
    std::lock_guard lock(mutex_);
    frames_[frame_id].stage_us[stage_name] = to_us(processing_time);
}

void MetricsCollector::record_queue_wait(uint64_t frame_id,
                                         const std::string& stage_name,
                                         Duration wait_time) {
    std::lock_guard lock(mutex_);
    frames_[frame_id].queue_us[stage_name] = to_us(wait_time);
}

void MetricsCollector::record_frame_drop(const std::string& sensor) {
    std::lock_guard lock(mutex_);
    drops_[sensor]++;
}

void MetricsCollector::record_deadline_miss(uint64_t frame_id, 
                                           const std::string& stage_name,
                                           uint32_t actual_us,
                                           uint32_t expected_us) {
    std::lock_guard lock(mutex_);
    frames_[frame_id].deadline_miss[stage_name] = true;
    ++total_deadline_misses_;
}

void MetricsCollector::print_periodic_update() const {
    std::lock_guard lock(mutex_);
    uint64_t total_drops = 0;
    for (auto& [s, c] : drops_) total_drops += c;
    std::printf("[metrics] completed: %llu | dropped: %llu | tracked: %zu\n",
                static_cast<unsigned long long>(total_completed_),
                static_cast<unsigned long long>(total_drops),
                frames_.size());
}

uint64_t MetricsCollector::total_completed() const {
    std::lock_guard lock(mutex_);
    return total_completed_;
}

uint64_t MetricsCollector::total_drops() const {
    std::lock_guard lock(mutex_);
    uint64_t total_drops = 0;
    for (const auto& [sensor, count] : drops_) {
        total_drops += count;
    }
    return total_drops;
}

uint64_t MetricsCollector::total_deadline_misses() const {
    std::lock_guard lock(mutex_);
    return total_deadline_misses_;
}

void MetricsCollector::print_summary() const {
    std::lock_guard lock(mutex_);

    std::printf("\n========== PIPELINE METRICS SUMMARY ==========\n");
    std::printf("Total frames tracked : %zu\n", frames_.size());
    std::printf("Frames completed     : %llu\n",
                static_cast<unsigned long long>(total_completed_));

    // Dropped frames
    uint64_t total_drops = 0;
    for (auto& [sensor, count] : drops_) {
        std::printf("Frames dropped [%-16s]: %llu\n",
                    sensor.c_str(),
                    static_cast<unsigned long long>(count));
        total_drops += count;
    }
    std::printf("Total frames dropped : %llu\n",
                static_cast<unsigned long long>(total_drops));

    // Deadline misses
    if (total_deadline_misses_ > 0) {
        std::printf("Deadline misses      : %llu\n",
                    static_cast<unsigned long long>(total_deadline_misses_));
    }

    // End-to-end latency
    std::vector<double> e2e_us;
    for (auto& [id, r] : frames_) {
        if (!r.completed) continue;
        auto dur = std::chrono::duration_cast<Duration>(r.exit_time - r.enter_time);
        e2e_us.push_back(to_us(dur));
    }

    if (!e2e_us.empty()) {
        std::sort(e2e_us.begin(), e2e_us.end());
        double sum = std::accumulate(e2e_us.begin(), e2e_us.end(), 0.0);
        double avg = sum / static_cast<double>(e2e_us.size());
        double med = e2e_us[e2e_us.size() / 2];
        double p99 = e2e_us[static_cast<size_t>(e2e_us.size() * 0.99)];

        std::printf("\n── End-to-End Latency (us) ──\n");
        std::printf("  min   : %10.1f\n", e2e_us.front());
        std::printf("  avg   : %10.1f\n", avg);
        std::printf("  median: %10.1f\n", med);
        std::printf("  p99   : %10.1f\n", p99);
        std::printf("  max   : %10.1f\n", e2e_us.back());
    }

    // Per-stage averages
    std::map<std::string, std::vector<double>> stage_times;
    std::map<std::string, std::vector<double>> queue_waits;
    for (auto& [id, r] : frames_) {
        for (auto& [sname, us] : r.stage_us) stage_times[sname].push_back(us);
        for (auto& [sname, us] : r.queue_us) queue_waits[sname].push_back(us);
    }

    if (!stage_times.empty()) {
        std::printf("\n── Per-Stage Processing Time (us, avg) ──\n");
        for (auto& [sname, vals] : stage_times) {
            double avg = std::accumulate(vals.begin(), vals.end(), 0.0) /
                         static_cast<double>(vals.size());
            std::printf("  %-16s: %10.1f  (n=%zu)\n",
                        sname.c_str(), avg, vals.size());
        }
    }

    if (!queue_waits.empty()) {
        std::printf("\n── Per-Stage Queue Wait Time (us, avg) ──\n");
        for (auto& [sname, vals] : queue_waits) {
            double avg = std::accumulate(vals.begin(), vals.end(), 0.0) /
                         static_cast<double>(vals.size());
            std::printf("  %-16s: %10.1f  (n=%zu)\n",
                        sname.c_str(), avg, vals.size());
        }
    }

    std::printf("==============================================\n\n");
}

void MetricsCollector::reset() {
    std::lock_guard lock(mutex_);
    frames_.clear();
    drops_.clear();
    total_completed_ = 0;
    total_deadline_misses_ = 0;
}

} // namespace adas
