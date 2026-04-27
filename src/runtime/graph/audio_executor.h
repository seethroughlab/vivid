#pragma once

#include "runtime/graph/compiled_graph.h"
#include "runtime/graph/executor_common.h"
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
// audio_order and routing audio-cadence direct edges via audio_direct_edges.
// ---------------------------------------------------------------------------

class AudioExecutor {
public:
    AudioExecutor();
    ~AudioExecutor();

    // Build audio-specific state: detect sink, set up auto-dup groups, waveform rings.
    // Also stores bridge/graph references so process_audio_for_test() works before start().
    // wall_time is the frame-tick time at the moment of build, used to align the audio
    // sample-counter time domain with the metronome's wall-clock anchor.
    bool build(AudioFrameBridge& bridge, CompiledGraph& cg,
               const LiveMetronomeStateStore& metronome_store,
               double wall_time = 0.0);

    // Start/stop audio device.
    bool start(bool use_null_device = false);
    void shutdown();
    void pause();
    void resume();

    // Re-init the playback device after the audio_out node's `device` param
    // has changed. Only touches the ma_device — leaves bridge wiring, lane
    // state, lift groups, and audio-time tracking intact.
    bool restart_device();

    // Index currently passed to ma_device_init (matches AudioDeviceList).
    // 0 == "Default". Records the most recent attempt regardless of outcome
    // so the AudioEngine watcher won't busy-retry a failing index.
    int applied_device_index() const;

    // Drain the most recent miniaudio device-notification event posted by
    // the device-thread callback. Returns -1 if nothing pending; otherwise
    // returns the `ma_device_notification_type` integer and clears the slot.
    int consume_device_notification();

    // Returns a pointer to the byte buffer that holds the ma_device_id we
    // last passed to ma_device_init, or nullptr if the active device is
    // "Default" (system default — no explicit id). The buffer lives inside
    // AudioExecutor; it's valid until the next start()/restart_device()/
    // shutdown(). Length is `applied_id_bytes_len()`.
    const void* applied_device_id_bytes() const;
    size_t applied_device_id_bytes_len() const;

    // Sample rate the device is actually running at after ma_device_init,
    // as reported by miniaudio's `internalSampleRate`. Returns 0 if there is
    // no device or the backend doesn't report (null backend in tests).
    uint32_t actual_device_rate() const;

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
    uint32_t buffer_size() const { return buffer_size_; }
    uint32_t sample_rate() const { return sample_rate_; }
    bool running() const { return running_; }

    // Test-only: run the audio callback directly
    void process_audio_for_test(float* output, uint32_t frame_count);

    static constexpr uint32_t kSampleRate = 48000;

    // Per-node lane state context (public for bridge callback access).
    using NodeLaneCtx = ExecutorLaneCtx;

private:
    void audio_callback(float* output, uint32_t frame_count);
    static void ma_data_callback(ma_device* device, void* output,
                                  const void* input, unsigned int frame_count);

    // Reads the audio_out node's `device` int param from the compiled graph.
    // Falls back to 0 ("Default") if the param or sink node is missing.
    int audio_out_device_index() const;

public:
    // Posted from miniaudio's internal thread by the notification trampoline.
    // Public only because the trampoline (a free function in the .cpp) needs
    // it; not part of the intended user-facing API.
    void post_device_notification(int type) {
        device_notification_pending_.store(type, std::memory_order_release);
    }
private:

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

    // Waveform ring buffers for analysis (per-channel)
    static constexpr uint32_t kMaxWaveformChannels = AnalysisSnapshot::kMaxWaveformChannels;
    std::vector<std::array<std::array<float, 1024>, kMaxWaveformChannels>> waveform_rings_;
    std::vector<std::array<uint32_t, kMaxWaveformChannels>> waveform_ring_pos_;

    // miniaudio device
    ma_device* device_ = nullptr;
    bool running_ = false;
    int applied_device_index_ = 0;

    // Byte copy of the ma_device_id last passed to ma_device_init (empty
    // for "Default"). Used by AudioEngine to detect, after an
    // AudioDeviceList refresh, whether the device we're using has been
    // unplugged. Stored as raw bytes so this header doesn't need
    // <miniaudio.h>.
    std::vector<uint8_t> applied_device_id_bytes_;

    // Posted from the miniaudio internal thread by ma_notification_callback;
    // drained on the main thread via consume_device_notification(). Holds
    // the most recent ma_device_notification_type, or -1 if nothing pending.
    std::atomic<int> device_notification_pending_{-1};

    // Audio time tracking
    uint64_t audio_frame_ = 0;
    double audio_start_wall_time_ = 0.0;  // wall time when audio_frame_ was 0
    uint32_t buffer_size_ = 256;
    uint32_t sample_rate_ = kSampleRate;

    // Analysis toggle (set from main thread, read from audio thread)
    std::atomic<bool> analysis_enabled_{true};

    // Diagnostics (atomic — read from main thread, written from audio thread)
    std::atomic<uint32_t> underrun_count_{0};
    std::atomic<bool> last_buffer_underrun_{false};
    std::atomic<float> audio_load_{0.0f};

    // Pre-allocated scratch for LoopBased lane processing (avoids audio-thread allocation).
    std::vector<uint32_t> loop_lane_ids_scratch_;
    std::vector<float*>   loop_in_ptrs_scratch_;
    std::vector<float*>   loop_out_ptrs_scratch_;

    // Recording tap
    RecordingTap recording_tap_;
    uint32_t recording_overrun_count_ = 0;
};

} // namespace vivid
