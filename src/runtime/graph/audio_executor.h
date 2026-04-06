#pragma once

#include "runtime/graph/compiled_graph.h"
#include "runtime/graph/lane_state.h"
#include "runtime/graph/snapshot_types.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <vector>

struct ma_device;

namespace vivid {

class AudioFrameBridge;
struct LiveMetronomeStateStore;

// ---------------------------------------------------------------------------
// AudioExecutor — processes audio-rate nodes on the audio thread.
//
// Replaces AudioEngine::audio_callback() and owns the miniaudio device,
// recording tap, and audio timing.  Operates on a CompiledGraph, iterating
// audio_order and routing audio buffers via audio_direct_edges.
// ---------------------------------------------------------------------------

class AudioExecutor {
public:
    AudioExecutor();
    ~AudioExecutor();

    // Build audio-specific state: detect sink, set up auto-dup groups, waveform rings.
    // Also stores bridge/graph references so process_audio_for_test() works before start().
    bool build(AudioFrameBridge& bridge, CompiledGraph& cg,
               const LiveMetronomeStateStore& metronome_store);

    // Start/stop audio device.
    bool start(bool use_null_device = false);
    void shutdown();
    void pause();
    void resume();

    // Recording tap
    void start_recording_tap();
    void stop_recording_tap();
    uint64_t available_recorded_samples() const;
    uint64_t pop_recorded_samples(float* dst, uint64_t max_samples);

    // Analysis toggle (main thread sets, audio thread reads)
    void set_analysis_enabled(bool enabled) { analysis_enabled_.store(enabled, std::memory_order_relaxed); }
    bool analysis_enabled() const { return analysis_enabled_.load(std::memory_order_relaxed); }

    // Diagnostics
    uint32_t underrun_count() const { return underrun_count_.load(std::memory_order_relaxed); }
    bool last_buffer_underrun() const { return last_buffer_underrun_.load(std::memory_order_relaxed); }
    float audio_load() const { return audio_load_.load(std::memory_order_relaxed); }

    // Test-only: run the audio callback directly
    void process_audio_for_test(float* output, uint32_t frame_count);

    static constexpr uint32_t kBufferSize = 256;
    static constexpr uint32_t kSampleRate = 48000;

    // Per-node lane state context (public for bridge callback access).
    struct NodeLaneCtx { LaneStateService* service; uint32_t node_idx; };

private:
    void audio_callback(float* output, uint32_t frame_count);
    static void ma_data_callback(ma_device* device, void* output,
                                  const void* input, unsigned int frame_count);

    AudioFrameBridge* bridge_ = nullptr;   // not owned
    CompiledGraph* graph_ = nullptr;       // not owned
    const LiveMetronomeStateStore* metronome_store_ = nullptr;  // not owned

    int sink_node_idx_ = -1;

    // Lane lift groups for pointwise operators in multi-lane chains
    std::vector<LaneLiftGroup> lane_lift_groups_;
    std::unordered_map<uint32_t, uint32_t> node_to_lift_group_;

    // Per-lane persistent state service (Phase 5)
    LaneStateService lane_state_;
    std::vector<NodeLaneCtx> node_lane_contexts_;  // indexed by audio_order position

    // Waveform ring buffers for analysis
    std::vector<std::array<float, 1024>> waveform_rings_;
    std::vector<uint32_t> waveform_ring_pos_;

    // miniaudio device
    ma_device* device_ = nullptr;
    bool running_ = false;

    // Audio time tracking
    uint64_t audio_frame_ = 0;

    // Analysis toggle (set from main thread, read from audio thread)
    std::atomic<bool> analysis_enabled_{true};

    // Diagnostics (atomic — read from main thread, written from audio thread)
    std::atomic<uint32_t> underrun_count_{0};
    std::atomic<bool> last_buffer_underrun_{false};
    std::atomic<float> audio_load_{0.0f};

    // Recording tap
    RecordingTap recording_tap_;
    uint32_t recording_overrun_count_ = 0;
};

} // namespace vivid
