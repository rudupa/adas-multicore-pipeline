#include "core/task_scheduler.h"
#include "core/config_loader.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <numeric>
#include <stdexcept>

namespace adas {

// ───────────────────────────────────────────────────────────────────
// TaskScheduler Implementation
// ───────────────────────────────────────────────────────────────────

TaskScheduler::TaskScheduler(size_t num_cores, double global_loop_rate_hz)
    : num_cores_(num_cores),
      global_loop_rate_hz_(global_loop_rate_hz),
      cycle_duration_us_(static_cast<uint32_t>(1e6 / global_loop_rate_hz)),
      cycle_start_time_us_(0) {
    
    // Initialize cores with default parameters
    for (size_t i = 0; i < num_cores; ++i) {
        CPUCoreState core;
        core.core_id = static_cast<int>(i);
        core.freq_ghz = 2.0;
        core.max_tasks = 4;
        cores_.push_back(core);
    }
}

void TaskScheduler::initialize_cores(const std::vector<struct CPUCore>& cores) {
    if (cores.empty()) {
        return;  // Keep defaults
    }
    
    cores_.clear();
    for (const auto& cpu_core : cores) {
        CPUCoreState core;
        core.core_id = cpu_core.core_id;
        core.freq_ghz = cpu_core.freq_ghz;
        core.max_tasks = cpu_core.max_tasks;
        cores_.push_back(core);
    }
    num_cores_ = cores_.size();
}

void TaskScheduler::enqueue_task(const TaskScheduleInfo& task) {
    ready_queue_.push(task);
}

std::vector<TaskScheduleInfo> TaskScheduler::schedule_tasks() {
    std::vector<TaskScheduleInfo> scheduled;
    
    // Clear per-core task lists for this scheduling round
    for (auto& core : cores_) {
        core.task_count = 0;
    }
    core_tasks_.clear();
    
    // Process all ready tasks
    while (!ready_queue_.empty()) {
        TaskScheduleInfo task = ready_queue_.top();
        ready_queue_.pop();
        
        // Select best core for this task
        int assigned_core = select_best_core(task);
        if (assigned_core < 0) {
            // No available core; task will be delayed
            continue;
        }
        
        task.assigned_core = assigned_core;
        task.expected_deadline_us = calculate_deadline(task);
        
        cores_[assigned_core].task_count++;
        core_tasks_[assigned_core].push_back(task);
        scheduled.push_back(task);
    }
    
    return scheduled;
}

void TaskScheduler::complete_task(const TaskScheduleInfo& task) {
    if (task.assigned_core < 0 || task.assigned_core >= static_cast<int>(cores_.size())) {
        return;
    }
    
    auto& core = cores_[task.assigned_core];
    core.total_exec_us += task.actual_execution_us;
    completed_tasks_.push_back(task);
    
    if (task.deadline_miss) {
        core.deadline_misses++;
        total_deadline_misses_++;
    }
}

void TaskScheduler::check_deadlines() {
    for (auto& task : completed_tasks_) {
        if (task.actual_execution_us > task.expected_deadline_us) {
            task.deadline_miss = true;
        }
    }
}

const CPUCoreState& TaskScheduler::get_core_state(int core_id) const {
    if (core_id < 0 || core_id >= static_cast<int>(cores_.size())) {
        throw std::out_of_range("Core ID out of range");
    }
    return cores_[core_id];
}

std::vector<double> TaskScheduler::get_core_utilization() const {
    std::vector<double> utilization;
    for (const auto& core : cores_) {
        // Utilization = (total_execution_time / cycle_duration) * 100
        double util = (static_cast<double>(core.total_exec_us) / cycle_duration_us_) * 100.0;
        utilization.push_back(std::min(100.0, util));
    }
    return utilization;
}

int TaskScheduler::get_deadline_miss_count() const {
    return total_deadline_misses_;
}

void TaskScheduler::reset_cycle() {
    cycle_start_time_us_ += cycle_duration_us_;
    total_deadline_misses_ = 0;
    
    for (auto& core : cores_) {
        core.task_count = 0;
        core.total_exec_us = 0;
        core.deadline_misses = 0;
        core.utilization = 0.0;
    }
    
    core_tasks_.clear();
    completed_tasks_.clear();
    
    // Clear ready queue
    while (!ready_queue_.empty()) {
        ready_queue_.pop();
    }
}

uint32_t TaskScheduler::current_cycle_time_us() const {
    return cycle_start_time_us_;
}

int TaskScheduler::select_best_core(const TaskScheduleInfo& task) {
    // If core affinity is specified, prefer that core
    if (task.preferred_core >= 0 && task.preferred_core < static_cast<int>(num_cores_)) {
        auto& core = cores_[task.preferred_core];
        if (core.task_count < core.max_tasks) {
            return task.preferred_core;
        }
    }
    
    // Otherwise, select core with lowest utilization
    int best_core = -1;
    int min_tasks = INT32_MAX;
    
    for (size_t i = 0; i < cores_.size(); ++i) {
        if (cores_[i].task_count < cores_[i].max_tasks &&
            cores_[i].task_count < min_tasks) {
            best_core = static_cast<int>(i);
            min_tasks = cores_[i].task_count;
        }
    }
    
    return best_core;
}

uint32_t TaskScheduler::calculate_deadline(const TaskScheduleInfo& task) const {
    // Deadline is the task's expected execution time
    // In a more sophisticated model, this would account for dependencies
    return task.expected_deadline_us;
}

// ───────────────────────────────────────────────────────────────────
// JitterSimulator Implementation
// ───────────────────────────────────────────────────────────────────

uint32_t JitterSimulator::apply_jitter(uint32_t base_time_us, double jitter_percentage) {
    if (jitter_percentage <= 0.0) {
        return base_time_us;
    }
    
    // Generate random jitter within ±jitter_percentage
    double jitter_amount_us = base_time_us * (jitter_percentage / 100.0);
    double applied_jitter = gaussian_jitter(0.0, jitter_amount_us / 3.0);  // ±3σ = ±percentage
    
    int32_t jittered = static_cast<int32_t>(base_time_us + applied_jitter);
    return std::max(1u, static_cast<uint32_t>(jittered));
}

double JitterSimulator::gaussian_jitter(double mean, double stddev) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::normal_distribution<> d(mean, stddev);
    return d(gen);
}

} // namespace adas
