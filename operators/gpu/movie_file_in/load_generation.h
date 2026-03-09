#pragma once

#include <cstdint>
#include <atomic>
#include <memory>
#include <mutex>

struct MovieLoadGenerationTracker {
    uint64_t requested = 0;
    uint64_t applied = 0;

    uint64_t next() { return ++requested; }
    bool should_apply(uint64_t generation) const {
        return generation == requested && generation > applied;
    }
    void mark_applied(uint64_t generation) {
        if (generation > applied) applied = generation;
    }
};

class MovieLoadCoordinator {
public:
    uint64_t request_next() {
        std::lock_guard<std::mutex> lock(mu_);
        cancel_locked();
        return generations_.next();
    }

    uint64_t cancel_pending() {
        std::lock_guard<std::mutex> lock(mu_);
        uint64_t generation = generations_.next();
        cancel_locked();
        return generation;
    }

    std::shared_ptr<std::atomic<bool>> begin_active() {
        std::lock_guard<std::mutex> lock(mu_);
        auto token = std::make_shared<std::atomic<bool>>(false);
        active_cancel_ = token;
        return token;
    }

    void clear_active(const std::shared_ptr<std::atomic<bool>>& token) {
        std::lock_guard<std::mutex> lock(mu_);
        if (active_cancel_ == token) active_cancel_.reset();
    }

    bool should_apply(uint64_t generation) const {
        std::lock_guard<std::mutex> lock(mu_);
        return generations_.should_apply(generation);
    }

    void mark_applied(uint64_t generation) {
        std::lock_guard<std::mutex> lock(mu_);
        generations_.mark_applied(generation);
    }

    void cancel_all() {
        std::lock_guard<std::mutex> lock(mu_);
        cancel_locked();
    }

    ~MovieLoadCoordinator() { cancel_all(); }

private:
    void cancel_locked() {
        if (active_cancel_) active_cancel_->store(true, std::memory_order_release);
    }

    mutable std::mutex mu_;
    MovieLoadGenerationTracker generations_{};
    std::shared_ptr<std::atomic<bool>> active_cancel_{};
};
