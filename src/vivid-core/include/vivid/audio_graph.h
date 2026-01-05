#pragma once

/**
 * @file audio_graph.h
 * @brief Pull-based audio processing graph for real-time audio generation
 *
 * AudioGraph manages audio operators and processes them on the audio thread.
 * Events from the main thread are queued and processed at block boundaries
 * for thread-safe, glitch-free audio.
 */

#include <vivid/audio_event.h>
#include <vivid/audio_buffer.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <atomic>

namespace vivid {

// Forward declarations
class AudioOperator;
class Context;

/**
 * @brief Pull-based audio processing graph
 *
 * The AudioGraph owns the audio processing pipeline and is called from
 * the audio thread (miniaudio callback) to generate samples on-demand.
 *
 * Thread model:
 * - Main thread: queue events, modify parameters
 * - Audio thread: process events, generate samples
 *
 * @par Example Usage
 * @code
 * // Setup (main thread, audio stopped)
 * AudioGraph graph;
 * graph.addOperator("synth", &synth);
 * graph.addOperator("mixer", &mixer);
 * graph.setOutput(&mixer);
 * graph.buildExecutionOrder();
 *
 * // Runtime (audio callback)
 * void audioCallback(float* output, uint32_t frames) {
 *     graph.processBlock(output, frames);
 * }
 *
 * // Events (main thread)
 * graph.queueNoteOn(synthId, 440.0f);
 * @endcode
 */
class AudioGraph {
public:
    AudioGraph() = default;
    ~AudioGraph() = default;

    // Non-copyable
    AudioGraph(const AudioGraph&) = delete;
    AudioGraph& operator=(const AudioGraph&) = delete;

    // -------------------------------------------------------------------------
    /// @name Setup (call when audio is stopped)
    /// @{

    /**
     * @brief Add an operator to the graph
     * @param name Operator name for event routing
     * @param op Pointer to audio operator (graph does not own)
     * @return Operator ID for use in events
     */
    uint32_t addOperator(const std::string& name, AudioOperator* op);

    /**
     * @brief Get operator by name
     * @return Pointer to operator, or nullptr if not found
     */
    AudioOperator* getOperator(const std::string& name);

    /**
     * @brief Get operator ID by name
     * @return Operator ID, or UINT32_MAX if not found
     */
    uint32_t getOperatorId(const std::string& name) const;

    /**
     * @brief Set the output operator
     * @param op Output operator (must be in graph)
     */
    void setOutput(AudioOperator* op);

    /**
     * @brief Build execution order based on operator dependencies
     *
     * Call after all operators are added and connected.
     * Uses topological sort to ensure dependencies are processed first.
     */
    void buildExecutionOrder();

    /**
     * @brief Clear all operators from the graph
     */
    void clear();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Audio Thread Interface
    /// @{

    /**
     * @brief Process a block of audio (called from audio thread)
     *
     * This is the main entry point called from the miniaudio callback.
     * It processes queued events, generates audio for all operators,
     * and writes the output to the provided buffer.
     *
     * @param output Output buffer (interleaved stereo)
     * @param frameCount Number of frames to generate
     */
    void processBlock(float* output, uint32_t frameCount);

    /**
     * @brief Process queued events (called at start of processBlock)
     */
    void processEvents();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Main Thread Event Interface
    /// @{

    /**
     * @brief Queue a note-on event
     * @param operatorId Target operator ID
     * @param frequency Note frequency in Hz
     * @param velocity Note velocity (0-1, default 1)
     */
    void queueNoteOn(uint32_t operatorId, float frequency, float velocity = 1.0f);

    /**
     * @brief Queue a note-off event
     * @param operatorId Target operator ID
     */
    void queueNoteOff(uint32_t operatorId);

    /**
     * @brief Queue a trigger event (for drums, one-shots)
     * @param operatorId Target operator ID
     */
    void queueTrigger(uint32_t operatorId);

    /**
     * @brief Queue a parameter change
     * @param operatorId Target operator ID
     * @param paramId Parameter index
     * @param value New parameter value
     */
    void queueParamChange(uint32_t operatorId, uint32_t paramId, float value);

    /**
     * @brief Queue a reset event
     * @param operatorId Target operator ID
     */
    void queueReset(uint32_t operatorId);

    /// @}
    // -------------------------------------------------------------------------
    /// @name State
    /// @{

    /**
     * @brief Check if graph has any operators
     */
    bool empty() const { return m_operators.empty(); }

    /**
     * @brief Get number of operators
     */
    size_t operatorCount() const { return m_operators.size(); }

    /**
     * @brief Get the output operator
     */
    AudioOperator* output() const { return m_output; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Monitoring
    /// @{

    /**
     * @brief Get number of dropped events since last reset
     *
     * Events are dropped when the queue is full (typically during
     * rapid MIDI input or high-frequency parameter automation).
     */
    uint64_t droppedEventCount() const {
        return m_eventQueue.droppedCount();
    }

    /**
     * @brief Reset dropped event counter
     */
    void resetDroppedEventCount() {
        m_eventQueue.resetDroppedCount();
    }

    /**
     * @brief Get event queue fill level (0.0 - 1.0)
     *
     * Useful for monitoring queue pressure. High values indicate
     * risk of event drops.
     */
    float eventQueueFillLevel() const {
        return static_cast<float>(m_eventQueue.size()) /
               static_cast<float>(m_eventQueue.capacity());
    }

    /**
     * @brief Get current DSP load (0.0 - 1.0+)
     *
     * Ratio of processing time to buffer duration.
     * Values > 1.0 indicate overload (processing slower than real-time).
     */
    float dspLoad() const {
        return m_dspLoad.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get peak DSP load since last reset
     */
    float peakDspLoad() const {
        return m_peakDspLoad.load(std::memory_order_relaxed);
    }

    /**
     * @brief Reset peak DSP load counter
     */
    void resetPeakDspLoad() {
        m_peakDspLoad.store(0.0f, std::memory_order_relaxed);
    }

    /// @}

private:
    struct OperatorEntry {
        std::string name;
        AudioOperator* op = nullptr;
    };

    std::vector<OperatorEntry> m_operators;
    std::vector<AudioOperator*> m_executionOrder;
    std::unordered_map<std::string, uint32_t> m_nameToId;
    AudioOperator* m_output = nullptr;

    SPSCQueue<AudioEvent, 1024> m_eventQueue;

    // Temporary buffer for mixing (avoids allocation in audio thread)
    std::vector<float> m_mixBuffer;

    // DSP load monitoring
    std::atomic<float> m_dspLoad{0.0f};
    std::atomic<float> m_peakDspLoad{0.0f};
};

} // namespace vivid
