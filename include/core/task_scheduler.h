#pragma once


#include <cstdint>
#include <vector>
#include <queue>
#include <string>
#include <chrono>
#include <map>

namespace adas {

// Forward declarations
struct CPUCore;

// ───────────────────────────────────────────────────────────────────
// Task Scheduling & Deadline Tracking
// ───────────────────────────────────────────────────────────────────

/// Per-core CPU state tracking
struct CPUCoreState {
    int         core_id       = 0;
    double      freq_ghz      = 2.0;
    int         max_tasks     = 4;
    int         task_count    = 0;                  // Current active tasks
    uint32_t    total_exec_us = 0;                  // Total execution time this cycle
    double      utilization   = 0.0;                // Percentage (0-100)
    
    // Deadline tracking
    int         deadline_misses = 0;                // Cumulative in this cycle
    uint32_t    last_stage_deadline_us = 0;         // Expected finish time
};

/// Per-task scheduling state
struct TaskScheduleInfo {
    std::string     stage_id;
    std::string     stage_name;
    int             priority        = 50;           // 0-99 (higher = more important)
    int             preferred_core  = -1;           // -1 = no affinity
    std::string     accelerator     = "";           // "gpu", "npu", "dsp", or ""
    
    uint32_t        expected_deadline_us = 0;       // When stage should finish
    uint32_t        actual_execution_us  = 0;       // Actual execution time
    bool            deadline_miss   = false;        // Set if actual > expected
    
    uint32_t        arrival_time_us = 0;            // When task arrived (for jitter)
    uint32_t        start_time_us   = 0;            // When execution started
    uint32_t        end_time_us     = 0;            // When execution completed
    
    int             assigned_core   = -1;           // Actual core assigned
    double          jitter_us       = 0.0;          // Applied jitter amount
};

/// Task priority comparator (higher priority = lower value in priority_queue)
struct TaskComparator {
    bool operator()(const TaskScheduleInfo& a, const TaskScheduleInfo& b) const {
        // Higher priority value = runs first (min heap)
        if (a.priority != b.priority) {
            return a.priority < b.priority;  // Higher priority first
        }
        // Tiebreak: FIFO (arrival order)
        return a.arrival_time_us > b.arrival_time_us;
    }
};

/// Main task scheduler
class TaskScheduler {
public:
    TaskScheduler(size_t num_cores = 8, double global_loop_rate_hz = 50.0);
    
    /// Initialize core topology
    void initialize_cores(const std::vector<struct CPUCore>& cores);
    
    /// Queue a task for scheduling
    void enqueue_task(const TaskScheduleInfo& task);
    
    /// Schedule tasks: assign each to appropriate core based on priority and affinity
    /// Returns list of scheduled tasks in execution order
    std::vector<TaskScheduleInfo> schedule_tasks();
    
    /// Mark task as completed
    void complete_task(const TaskScheduleInfo& task);
    
    /// Detect deadline violations
    void check_deadlines();
    
    /// Get current core state
    const CPUCoreState& get_core_state(int core_id) const;
    
    /// Get per-core utilization (%)
    std::vector<double> get_core_utilization() const;
    
    /// Get total deadline misses this cycle
    int get_deadline_miss_count() const;
    
    /// Reset for next cycle
    void reset_cycle();
    
    /// Get current cycle time (microseconds)
    uint32_t current_cycle_time_us() const;
    
private:
    size_t      num_cores_;
    double      global_loop_rate_hz_;
    uint32_t    cycle_duration_us_;
    uint32_t    cycle_start_time_us_;
    
    std::vector<CPUCoreState>                           cores_;
    std::priority_queue<TaskScheduleInfo, 
                       std::vector<TaskScheduleInfo>, 
                       TaskComparator>                  ready_queue_;
    std::vector<TaskScheduleInfo>                       completed_tasks_;
    std::map<int, std::vector<TaskScheduleInfo>>        core_tasks_;  // Tasks per core
    
    // Scheduling statistics
    int         total_deadline_misses_ = 0;
    
    /// Select best core for task (respects affinity, minimizes contention)
    int select_best_core(const TaskScheduleInfo& task);
    
    /// Calculate task deadline based on expected execution time
    uint32_t calculate_deadline(const TaskScheduleInfo& task) const;
};

// ───────────────────────────────────────────────────────────────────
// Jitter Simulation
// ───────────────────────────────────────────────────────────────────

/// Adds timing variation to sensor arrivals and task execution
class JitterSimulator {
public:
    JitterSimulator() = default;
    
    /// Apply jitter to timestamp (percentage-based variation)
    static uint32_t apply_jitter(uint32_t base_time_us, double jitter_percentage);
    
    /// Generate Gaussian-distributed jitter
    static double gaussian_jitter(double mean, double stddev);
};

} // namespace adas
