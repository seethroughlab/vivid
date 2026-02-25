#ifndef VIVID_RUNTIME_AUDIO_ENGINE_H
#define VIVID_RUNTIME_AUDIO_ENGINE_H

#include "runtime/scheduler.h"
#include "runtime/graph.h"
#include "runtime/operator_registry.h"
#include "operator_api/audio_operator.h"
#include <array>
#include <atomic>
#include <vector>
#include <string>
#include <unordered_map>

struct ma_device;

namespace vivid {

struct AudioNodeState {
    std::string node_id;
    OperatorLoader* loader;
    void* instance;
    uint32_t input_port_count;
    uint32_t output_port_count;
    uint32_t param_count;
    std::vector<float> param_values;

    // Audio buffers: [port][sample]
    std::vector<std::vector<float>> input_buffers;
    std::vector<std::vector<float>> output_buffers;

    // Port name → index mappings
    std::unordered_map<std::string, uint32_t> input_port_indices;
    std::unordered_map<std::string, uint32_t> output_port_indices;
    std::unordered_map<std::string, uint32_t> param_indices;
};

// Wire within the audio subgraph (audio output → audio input)
struct AudioWire {
    uint32_t from_node_idx, from_port_idx;
    uint32_t to_node_idx, to_port_idx;
};

// Cross-domain wire: control output → audio param
struct CrossDomainWire {
    std::string control_node_id;
    uint32_t control_output_port_idx;
    uint32_t audio_node_idx;
    uint32_t audio_param_idx;
};

struct ParamSnapshot {
    std::vector<std::vector<float>> node_params;  // [audio_node_idx][param_idx]
};

struct AnalysisSnapshot {
    static constexpr uint32_t kWaveformSamples = 1024;
    std::vector<float> rms;   // [audio_node_idx]
    std::vector<float> peak;  // [audio_node_idx]
    std::vector<std::array<float, kWaveformSamples>> waveform; // [audio_node_idx]
};

struct AudioToControlMapping {
    uint32_t audio_engine_idx;    // index in AudioEngine::nodes_
    uint32_t scheduler_node_idx;  // index in Scheduler::nodes_
    uint32_t rms_port_idx;        // "rms" output port index in scheduler
    uint32_t peak_port_idx;       // "peak" output port index in scheduler
    uint32_t waveform_port_idx;   // "waveform" output port index in scheduler
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool build(const Graph& graph, OperatorRegistry& registry, const Scheduler& scheduler);
    bool start();
    void push_params(const Scheduler& scheduler);
    void inject_analysis(Scheduler& scheduler);
    void shutdown();

    // Read active analysis snapshot (call from main thread)
    const AnalysisSnapshot& analysis_read() const;
    // Map node_id to audio engine index (-1 if not found)
    int audio_node_index(const std::string& node_id) const;

    // Hot-reload support
    void pause();
    void resume();
    bool reload_operator(const std::string& type_name, OperatorRegistry& registry);

    static constexpr uint32_t kBufferSize = 256;
    static constexpr uint32_t kSampleRate = 48000;

private:
    // Called from the audio thread
    void audio_callback(float* output, uint32_t frame_count);
    static void ma_data_callback(struct ma_device* device, void* output, const void* input, unsigned int frame_count);

    std::vector<AudioNodeState> nodes_;
    std::vector<AudioWire> wires_;
    std::vector<CrossDomainWire> cross_wires_;

    // Double-buffered param bridge (control→audio)
    ParamSnapshot snapshots_[2];
    std::atomic<int> active_{0};

    // Double-buffered analysis bridge (audio→control)
    AnalysisSnapshot analysis_snapshots_[2];
    std::atomic<int> analysis_active_{0};
    std::vector<AudioToControlMapping> analysis_mappings_;

    // Node ID → audio engine index mapping (built during build())
    std::unordered_map<std::string, int> node_id_to_index_;

    // Per-node ring buffer for raw waveform sample accumulation
    std::vector<std::array<float, 1024>> waveform_rings_;
    std::vector<uint32_t> waveform_ring_pos_;

    // Audio time tracking
    uint64_t audio_frame_ = 0;

    // miniaudio device (opaque pointer to avoid including miniaudio.h in header)
    ma_device* device_ = nullptr;
    bool running_ = false;
};

} // namespace vivid

#endif // VIVID_RUNTIME_AUDIO_ENGINE_H
