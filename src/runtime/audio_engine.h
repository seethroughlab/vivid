#pragma once

#include "runtime/snapshot_types.h"
#include "runtime/operator_registry.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vivid {

class RuntimeCore;
struct CompiledGraph;
class CadenceBridge;
class AudioExecutor;

// ---------------------------------------------------------------------------
// AudioEngine — thin facade over AudioExecutor, CompiledGraph, and CadenceBridge.
//
// Manages audio device lifecycle and exposes analysis/recording/diagnostics
// to the main thread.  All real audio processing happens in AudioExecutor.
// ---------------------------------------------------------------------------

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool build(RuntimeCore& core);
    bool start(bool use_null_device = false);
    void shutdown();

    // Read active analysis snapshot (call from main thread)
    const AnalysisSnapshot& analysis_read() const;
    // Map node_id to audio engine index (-1 if not found)
    int audio_node_index(const std::string& node_id) const;

    // Hot-reload support
    void pause();
    void resume();
    // Two-phase hot reload: destroy old instances while old dylib is still loaded,
    // then create new instances after the dylib swap.
    // Call sequence: pre_reload → core.reload_operator → post_reload
    void pre_reload_operator(const std::string& type_name);
    bool post_reload_operator(const std::string& type_name, OperatorRegistry& registry);

    // Recording tap — capture the final stereo mix (call from main thread)
    void start_recording_tap();
    void stop_recording_tap();
    uint64_t available_recorded_samples() const;
    uint64_t pop_recorded_samples(float* dst, uint64_t max_samples);

    uint32_t underrun_count() const;
    bool last_buffer_underrun() const;
    float audio_load() const;
    uint32_t node_count() const;

    // Test-only accessors — expose internal state for white-box snapshot contract tests.
    float float_input_value_for_test(int node_idx, int port_idx) const;
    void  process_audio_for_test(float* output, uint32_t frame_count);

    static constexpr uint32_t kBufferSize = 256;
    static constexpr uint32_t kSampleRate = 48000;

private:
    // Saved state for two-phase reload (between pre_reload and post_reload)
    struct ReloadSavedNode {
        uint32_t node_idx;  // index into compiled_graph_->audio_order
        std::unordered_map<std::string, float> params;
    };
    std::vector<ReloadSavedNode> reload_saved_;

    // Cadence-aware runtime references (not owned)
    CompiledGraph* compiled_graph_ = nullptr;
    CadenceBridge* cadence_bridge_ = nullptr;
    std::unique_ptr<AudioExecutor> audio_executor_;

    // Fallback analysis snapshot (returned when no CadenceBridge available)
    AnalysisSnapshot empty_analysis_;
};

} // namespace vivid
