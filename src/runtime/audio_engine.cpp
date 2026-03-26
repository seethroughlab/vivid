#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "runtime/audio_engine.h"
#include "runtime/audio_executor.h"
#include "runtime/cadence_bridge.h"
#include "runtime/compiled_graph.h"
#include "runtime/graph_compiler.h"
#include "runtime/scheduler.h"
#include <cstdio>

namespace vivid {

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::build(const Graph& /*graph*/, OperatorRegistry& /*registry*/,
                         const Scheduler& scheduler) {
    compiled_graph_ = const_cast<CompiledGraph*>(scheduler.compiled_graph());
    cadence_bridge_ = &const_cast<Scheduler&>(scheduler).cadence_bridge();
    if (!compiled_graph_ || compiled_graph_->audio_order.empty()) {
        std::fprintf(stderr, "[vivid] AudioEngine: no audio nodes\n");
        return false;
    }

    audio_executor_ = std::make_unique<AudioExecutor>();
    if (!audio_executor_->build(*cadence_bridge_, *compiled_graph_)) {
        audio_executor_.reset();
        return false;
    }

    if (std::getenv("VIVID_VERBOSE")) {
        std::fprintf(stderr, "[vivid] Audio evaluation order:");
        for (uint32_t i = 0; i < compiled_graph_->audio_order.size(); ++i) {
            uint32_t ni = compiled_graph_->audio_order[i];
            std::fprintf(stderr, "%s%s", (i == 0 ? " " : " -> "),
                         compiled_graph_->nodes[ni].node_id.c_str());
        }
        std::fprintf(stderr, "\n");
    }

    return true;
}

bool AudioEngine::start(bool use_null_device) {
    if (!audio_executor_ || !cadence_bridge_ || !compiled_graph_) return false;
    bool ok = audio_executor_->start(use_null_device);
    if (ok) {
        std::fprintf(stderr, "[vivid] AudioEngine: started (%u Hz, %u frames/buffer, %zu audio nodes)\n",
                     kSampleRate, kBufferSize, compiled_graph_->audio_order.size());
    }
    return ok;
}

void AudioEngine::shutdown() {
    if (audio_executor_) {
        audio_executor_->shutdown();
        audio_executor_.reset();
    }
    compiled_graph_ = nullptr;
    cadence_bridge_ = nullptr;

    std::fprintf(stderr, "[vivid] AudioEngine: shutdown\n");
}

const AnalysisSnapshot& AudioEngine::analysis_read() const {
    if (cadence_bridge_) return cadence_bridge_->active_analysis();
    return empty_analysis_;
}

int AudioEngine::audio_node_index(const std::string& node_id) const {
    if (!compiled_graph_) return -1;
    for (uint32_t i = 0; i < static_cast<uint32_t>(compiled_graph_->audio_order.size()); ++i) {
        if (compiled_graph_->nodes[compiled_graph_->audio_order[i]].node_id == node_id)
            return static_cast<int>(i);
    }
    return -1;
}

void AudioEngine::pause() {
    if (audio_executor_) audio_executor_->pause();
}

void AudioEngine::resume() {
    if (audio_executor_) audio_executor_->resume();
}

// Phase 1: Destroy old instances while the old dylib is still loaded.
void AudioEngine::pre_reload_operator(const std::string& type_name) {
    pause();
    reload_saved_.clear();

    if (!compiled_graph_) return;

    for (uint32_t i = 0; i < static_cast<uint32_t>(compiled_graph_->audio_order.size()); ++i) {
        uint32_t ni = compiled_graph_->audio_order[i];
        auto& cn = compiled_graph_->nodes[ni];
        if (cn.active_cadence != Cadence::Audio) continue;
        if (!cn.loader) continue;
        const auto* desc = cn.loader->descriptor();
        if (!desc || std::string(desc->name) != type_name) continue;

        // Save param values by name
        ReloadSavedNode saved;
        saved.node_idx = ni;
        for (const auto& [name, idx] : cn.param_indices)
            saved.params[name] = cn.param_values[idx];
        reload_saved_.push_back(std::move(saved));

        // Destroy auto-dup extra instances via AudioExecutor
        // (AudioExecutor owns them; they'll be recreated on rebuild)

        // Destroy primary instance using the still-valid old loader
        if (cn.instance) {
            cn.loader->destroy_instance(cn.instance);
            cn.instance = nullptr;
        }
    }
    // Note: audio remains paused until post_reload_operator
}

// Phase 2: Create new instances from the new (already-swapped) loader.
bool AudioEngine::post_reload_operator(const std::string& type_name, OperatorRegistry& registry) {
    OperatorLoader* new_loader = registry.find(type_name);
    if (!new_loader || !new_loader->descriptor()) {
        resume();
        reload_saved_.clear();
        return false;
    }
    const auto* new_desc = new_loader->descriptor();

    for (const auto& saved : reload_saved_) {
        auto& cn = compiled_graph_->nodes[saved.node_idx];
        cn.loader = new_loader;
        cn.instance = new_loader->create_instance();
        if (!cn.instance) {
            std::fprintf(stderr, "[vivid] AudioEngine: failed to create replacement instance for '%s'\n",
                         type_name.c_str());
            resume();
            reload_saved_.clear();
            return false;
        }

        // Reinitialize node state with new descriptor
        GraphCompiler::init_frame_state(cn, new_desc, &saved.params, nullptr, {});
        GraphCompiler::init_audio_state(cn, new_desc, kBufferSize);

        // Restore saved param values
        for (const auto& [pname, pval] : saved.params) {
            auto pi = cn.param_indices.find(pname);
            if (pi != cn.param_indices.end())
                cn.param_values[pi->second] = pval;
        }

        cn.errored = false;
        cn.error_message.clear();
        cn.audio_error_message[0] = '\0';
    }

    reload_saved_.clear();

    // Rebuild AudioExecutor (auto-dup groups may change with new descriptor)
    if (audio_executor_ && compiled_graph_ && cadence_bridge_) {
        audio_executor_->shutdown();
        audio_executor_->build(*cadence_bridge_, *compiled_graph_);
        cadence_bridge_->build(*compiled_graph_);
        audio_executor_->start(false);
    }

    resume();
    return true;
}

void AudioEngine::start_recording_tap() {
    if (audio_executor_) audio_executor_->start_recording_tap();
}

void AudioEngine::stop_recording_tap() {
    if (audio_executor_) audio_executor_->stop_recording_tap();
}

uint64_t AudioEngine::available_recorded_samples() const {
    if (audio_executor_) return audio_executor_->available_recorded_samples();
    return 0;
}

uint64_t AudioEngine::pop_recorded_samples(float* dst, uint64_t max_samples) {
    if (audio_executor_) return audio_executor_->pop_recorded_samples(dst, max_samples);
    return 0;
}

uint32_t AudioEngine::underrun_count() const {
    if (audio_executor_) return audio_executor_->underrun_count();
    return 0;
}

bool AudioEngine::last_buffer_underrun() const {
    if (audio_executor_) return audio_executor_->last_buffer_underrun();
    return false;
}

float AudioEngine::audio_load() const {
    if (audio_executor_) return audio_executor_->audio_load();
    return 0.0f;
}

uint32_t AudioEngine::node_count() const {
    if (compiled_graph_) return static_cast<uint32_t>(compiled_graph_->audio_order.size());
    return 0;
}

float AudioEngine::float_input_value_for_test(int node_idx, int port_idx) const {
    if (!compiled_graph_ || node_idx < 0 ||
        node_idx >= static_cast<int>(compiled_graph_->audio_order.size()))
        return 0.0f;
    uint32_t ni = compiled_graph_->audio_order[node_idx];
    const auto& cn = compiled_graph_->nodes[ni];
    if (port_idx < 0 || port_idx >= static_cast<int>(cn.float_input_values.size()))
        return 0.0f;
    return cn.float_input_values[port_idx];
}

void AudioEngine::process_audio_for_test(float* output, uint32_t frame_count) {
    if (audio_executor_) {
        audio_executor_->process_audio_for_test(output, frame_count);
    } else {
        std::memset(output, 0, frame_count * 2 * sizeof(float));
    }
}

} // namespace vivid
