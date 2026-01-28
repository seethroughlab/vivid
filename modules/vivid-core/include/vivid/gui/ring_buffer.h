#pragma once

/**
 * @file ring_buffer.h
 * @brief Fixed-capacity ring buffer for streaming time-series data
 *
 * Provides a simple, efficient circular buffer with statistical helpers
 * for use with graph widgets and performance monitoring.
 */

#include <cstddef>
#include <algorithm>
#include <numeric>
#include <limits>

namespace vivid {

/**
 * @brief Fixed-capacity ring buffer for streaming data
 *
 * Stores the most recent N values, automatically overwriting the oldest
 * when capacity is reached. Provides access to the underlying data with
 * offset information for correct iteration from oldest to newest.
 *
 * @tparam T Value type (typically float)
 * @tparam Capacity Maximum number of elements
 *
 * @par Example
 * @code
 * RingBuffer<float, 300> fpsHistory;  // ~5 seconds at 60fps
 *
 * // Each frame:
 * fpsHistory.push(currentFps);
 *
 * // Draw graph:
 * gui.graph("FPS", fpsHistory.data(), fpsHistory.size(), fpsHistory.offset());
 * @endcode
 */
template<typename T, size_t Capacity>
class RingBuffer {
public:
    RingBuffer() = default;

    /**
     * @brief Add a value to the buffer
     *
     * If the buffer is full, the oldest value is overwritten.
     */
    void push(T value) {
        m_data[m_writeIndex] = value;
        m_writeIndex = (m_writeIndex + 1) % Capacity;
        if (m_count < Capacity) {
            m_count++;
        }
    }

    /**
     * @brief Clear all values
     */
    void clear() {
        m_count = 0;
        m_writeIndex = 0;
    }

    /**
     * @brief Get pointer to underlying data array
     */
    const T* data() const { return m_data; }

    /**
     * @brief Get number of valid elements
     */
    size_t size() const { return m_count; }

    /**
     * @brief Get the buffer capacity
     */
    static constexpr size_t capacity() { return Capacity; }

    /**
     * @brief Get start index for correct iteration (oldest element)
     *
     * When the buffer is full, the oldest element is at writeIndex.
     * When not full, it's at index 0.
     */
    size_t offset() const {
        return (m_count < Capacity) ? 0 : m_writeIndex;
    }

    /**
     * @brief Get the most recently added value
     */
    T last() const {
        if (m_count == 0) return T{};
        size_t lastIdx = (m_writeIndex == 0) ? Capacity - 1 : m_writeIndex - 1;
        return m_data[lastIdx];
    }

    /**
     * @brief Get value at logical index (0 = oldest, size-1 = newest)
     */
    T at(size_t logicalIndex) const {
        if (logicalIndex >= m_count) return T{};
        size_t physicalIndex = (offset() + logicalIndex) % Capacity;
        return m_data[physicalIndex];
    }

    /**
     * @brief Get minimum value in buffer
     */
    T min() const {
        if (m_count == 0) return T{};
        T minVal = m_data[offset()];
        for (size_t i = 0; i < m_count; i++) {
            size_t idx = (offset() + i) % Capacity;
            if (m_data[idx] < minVal) minVal = m_data[idx];
        }
        return minVal;
    }

    /**
     * @brief Get maximum value in buffer
     */
    T max() const {
        if (m_count == 0) return T{};
        T maxVal = m_data[offset()];
        for (size_t i = 0; i < m_count; i++) {
            size_t idx = (offset() + i) % Capacity;
            if (m_data[idx] > maxVal) maxVal = m_data[idx];
        }
        return maxVal;
    }

    /**
     * @brief Get average value in buffer
     */
    T average() const {
        if (m_count == 0) return T{};
        T sum = T{};
        for (size_t i = 0; i < m_count; i++) {
            size_t idx = (offset() + i) % Capacity;
            sum += m_data[idx];
        }
        return sum / static_cast<T>(m_count);
    }

    /**
     * @brief Check if buffer is empty
     */
    bool empty() const { return m_count == 0; }

    /**
     * @brief Check if buffer is full
     */
    bool full() const { return m_count == Capacity; }

private:
    T m_data[Capacity] = {};
    size_t m_writeIndex = 0;
    size_t m_count = 0;
};

} // namespace vivid
