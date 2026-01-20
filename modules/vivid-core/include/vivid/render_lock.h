#pragma once

/**
 * @file render_lock.h
 * @brief Global render lock for synchronization between modules
 *
 * This provides a way for video playback to block rendering during
 * critical transitions (like video loops) to avoid GPU state conflicts.
 */

#include <atomic>

namespace vivid {

/**
 * @brief Global render lock singleton
 *
 * Call lock() before critical video operations that might conflict with rendering.
 * Call unlock() after the operation is complete.
 * The render system should check isLocked() and skip rendering if true.
 */
class RenderLock {
public:
    static RenderLock& instance() {
        static RenderLock instance;
        return instance;
    }

    void lock() { locked_.store(true); }
    void unlock() { locked_.store(false); }
    bool isLocked() const { return locked_.load(); }

private:
    RenderLock() = default;
    std::atomic<bool> locked_{false};
};

} // namespace vivid
