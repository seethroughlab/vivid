#pragma once

/**
 * @file audio_event.h
 * @brief Thread-safe audio event types for main thread to audio thread communication
 *
 * Events are queued from the main thread and processed on the audio thread
 * at the start of each audio block for sample-accurate timing.
 */

#include <cstdint>
#include <string>
#include <array>
#include <atomic>

namespace vivid {

/**
 * @brief Event types for audio thread communication
 */
enum class AudioEventType {
    NoteOn,       ///< Trigger a note with frequency
    NoteOff,      ///< Release a note
    Trigger,      ///< One-shot trigger (drums, envelopes)
    ParamChange,  ///< Parameter value change
    Reset,        ///< Reset operator state
};

/**
 * @brief Audio event for thread-safe communication
 *
 * Events are queued from the main thread and processed on the audio thread.
 * The operator ID is used to route events to the correct operator.
 */
struct AudioEvent {
    AudioEventType type = AudioEventType::Trigger;
    uint32_t operatorId = 0;     ///< Target operator (index in execution order)
    uint32_t paramId = 0;        ///< Parameter index (for ParamChange)
    float value1 = 0.0f;         ///< Primary value (frequency, parameter value)
    float value2 = 0.0f;         ///< Secondary value (velocity, etc.)
};

/**
 * @brief Lock-free single-producer single-consumer queue for audio events
 *
 * This is a simple ring buffer implementation that allows the main thread
 * to queue events without blocking the audio thread.
 *
 * Thread safety:
 * - Main thread: calls push() only
 * - Audio thread: calls pop() only
 * - No locks required due to SPSC pattern
 */
template<typename T, size_t Capacity = 256>
class SPSCQueue {
public:
    SPSCQueue() : m_head(0), m_tail(0), m_droppedCount(0) {}

    /**
     * @brief Push an item to the queue (main thread only)
     * @return true if successful, false if queue is full
     */
    bool push(const T& item) {
        size_t head = m_head.load(std::memory_order_relaxed);
        size_t next = (head + 1) % Capacity;

        if (next == m_tail.load(std::memory_order_acquire)) {
            m_droppedCount.fetch_add(1, std::memory_order_relaxed);
            return false;  // Queue full
        }

        m_buffer[head] = item;
        m_head.store(next, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pop an item from the queue (audio thread only)
     * @return true if successful, false if queue is empty
     */
    bool pop(T& item) {
        size_t tail = m_tail.load(std::memory_order_relaxed);

        if (tail == m_head.load(std::memory_order_acquire)) {
            return false;  // Queue empty
        }

        item = m_buffer[tail];
        m_tail.store((tail + 1) % Capacity, std::memory_order_release);
        return true;
    }

    /**
     * @brief Check if queue is empty
     */
    bool empty() const {
        return m_tail.load(std::memory_order_acquire) ==
               m_head.load(std::memory_order_acquire);
    }

    /**
     * @brief Get number of dropped events since last reset
     */
    uint64_t droppedCount() const {
        return m_droppedCount.load(std::memory_order_relaxed);
    }

    /**
     * @brief Reset dropped event counter
     */
    void resetDroppedCount() {
        m_droppedCount.store(0, std::memory_order_relaxed);
    }

    /**
     * @brief Get current queue size (approximate, for monitoring)
     */
    size_t size() const {
        size_t head = m_head.load(std::memory_order_relaxed);
        size_t tail = m_tail.load(std::memory_order_relaxed);
        return (head >= tail) ? (head - tail) : (Capacity - tail + head);
    }

    /**
     * @brief Get maximum capacity
     */
    static constexpr size_t capacity() { return Capacity; }

private:
    std::array<T, Capacity> m_buffer;
    std::atomic<size_t> m_head;           ///< Write position (main thread)
    std::atomic<size_t> m_tail;           ///< Read position (audio thread)
    std::atomic<uint64_t> m_droppedCount; ///< Events dropped due to full queue
};

} // namespace vivid
