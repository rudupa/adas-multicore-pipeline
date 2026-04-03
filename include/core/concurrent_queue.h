#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace adas {

/// Thread-safe bounded queue used between pipeline stages.
/// Supports blocking push/pop and non-blocking try variants.
template <typename T>
class ConcurrentQueue {
public:
    explicit ConcurrentQueue(size_t capacity = 128)
        : capacity_(capacity) {}

    // ── Blocking push (waits if full) ──────────────────────────────
    void push(T item) {
        std::unique_lock lock(mutex_);
        not_full_.wait(lock, [this] { return queue_.size() < capacity_ || closed_; });
        if (closed_) return;
        queue_.push_back(std::move(item));
        not_empty_.notify_one();
    }

    // ── Non-blocking try-push ──────────────────────────────────────
    bool try_push(T item) {
        std::lock_guard lock(mutex_);
        if (queue_.size() >= capacity_ || closed_) return false;
        queue_.push_back(std::move(item));
        not_empty_.notify_one();
        return true;
    }

    // ── Blocking pop (returns nullopt when closed & empty) ─────────
    std::optional<T> pop() {
        std::unique_lock lock(mutex_);
        not_empty_.wait(lock, [this] { return !queue_.empty() || closed_; });
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();
        return item;
    }

    // ── Non-blocking try-pop ───────────────────────────────────────
    std::optional<T> try_pop() {
        std::lock_guard lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();
        return item;
    }

    // ── Queue state ────────────────────────────────────────────────
    size_t size() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard lock(mutex_);
        return queue_.empty();
    }

    /// Signal all waiting threads to unblock.  After close(), no new
    /// items can be pushed. Pop will drain remaining items then return
    /// nullopt.
    void close() {
        std::lock_guard lock(mutex_);
        closed_ = true;
        not_empty_.notify_all();
        not_full_.notify_all();
    }

private:
    mutable std::mutex      mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::deque<T>           queue_;
    size_t                  capacity_;
    bool                    closed_ = false;
};

} // namespace adas
