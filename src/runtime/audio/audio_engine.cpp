#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "runtime/audio/audio_engine.h"
#include "runtime/audio/audio_device_list.h"
#include "runtime/graph/audio_executor.h"
#include "runtime/core/runtime_core.h"
#include "runtime/core/settings.h"
#include "runtime/graph/graph_compiler.h"
#include "runtime/operators/builtin_operators.h"
#include <cstdio>

namespace vivid {

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::build(RuntimeCore& core) {
    runtime_core_ = &core;
    compiled_graph_ = core.compiled_graph();
    audio_frame_bridge_ = &core.audio_frame_bridge();
    if (!compiled_graph_ || compiled_graph_->audio_order.empty()) {
        std::fprintf(stderr, "[vivid] AudioEngine: no audio nodes\n");
        return false;
    }

    audio_executor_ = std::make_unique<AudioExecutor>();
    if (!audio_executor_->build(*audio_frame_bridge_, *compiled_graph_,
                                core.live_metronome_store(),
                                core.last_tick_time())) {
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
    if (!audio_executor_ || !audio_frame_bridge_ || !compiled_graph_) return false;
    bool ok = audio_executor_->start(use_null_device);
    if (ok) {
        std::fprintf(stderr, "[vivid] AudioEngine: started (%u Hz, %u frames/buffer, %zu audio nodes)\n",
                     sample_rate(), buffer_size(), compiled_graph_->audio_order.size());
        // If the device opened at a rate that differs from the graph's
        // compiled rate, signal main.cpp to recompile. Skip when the
        // backend doesn't report a rate (null backend used in tests).
        if (!use_null_device && runtime_core_) {
            uint32_t actual = audio_executor_->actual_device_rate();
            if (actual != 0 && actual != runtime_core_->audio_sample_rate()) {
                pending_session_sample_rate_ = actual;
            }
        }
    }
    return ok;
}

// Resolve the audio_out node's `device` param: returns the param index
// within CompiledNode::param_values, or -1 if not found. Also outputs the
// CompiledNode pointer so callers can read or write the value in place.
static int find_audio_out_device_param(CompiledGraph* cg, CompiledNode** cn_out) {
    if (!cg) return -1;
    for (uint32_t idx : cg->audio_order) {
        auto& cn = cg->nodes[idx];
        if (cn.type_name != "audio_out") continue;
        auto it = cn.param_indices.find("device");
        if (it == cn.param_indices.end() || it->second >= cn.param_values.size()) return -1;
        if (cn_out) *cn_out = &cn;
        return static_cast<int>(it->second);
    }
    return -1;
}

void AudioEngine::tick() {
    if (!audio_executor_ || !compiled_graph_) return;

    // -- Drain miniaudio's device-notification slot.
    int notif = audio_executor_->consume_device_notification();
    bool need_check_disappearance = false;
    if (notif >= 0) {
        const auto type = static_cast<ma_device_notification_type>(notif);
        if (type == ma_device_notification_type_rerouted ||
            type == ma_device_notification_type_stopped) {
            std::fprintf(stderr, "[vivid] AudioEngine: device notification (%s)\n",
                         type == ma_device_notification_type_rerouted ? "rerouted" : "stopped");
            need_check_disappearance = true;
        }
        // interruption_began/ended/unlocked are noisy on macOS and don't
        // need a list refresh; ignore beyond the consume.
    }

    // -- Throttled poll-refresh of the device list (~1 Hz at 60 fps).
    constexpr uint32_t kRefreshEveryFrames = 60;
    bool list_changed = false;
    if (++tick_frame_counter_ >= kRefreshEveryFrames) {
        tick_frame_counter_ = 0;
        list_changed = AudioDeviceList::instance().refresh();
        if (list_changed) {
            std::fprintf(stderr, "[vivid] AudioEngine: device list changed (%u entries)\n",
                         AudioDeviceList::instance().count());
            sync_audio_out_device_choices();
            need_check_disappearance = true;
        }
    }
    if (need_check_disappearance && !list_changed) {
        // A notification fired but we haven't refreshed yet — do it now so
        // disappearance detection sees the post-event list.
        if (AudioDeviceList::instance().refresh()) {
            sync_audio_out_device_choices();
        }
    }

    // -- Disappearance check: if the device we're using is gone, fall back
    //    to Default. This is also what "rerouted" wants in practice — the
    //    user's chosen device went away or the system default flipped.
    if (need_check_disappearance) {
        const void* id_bytes = audio_executor_->applied_device_id_bytes();
        size_t id_len = audio_executor_->applied_device_id_bytes_len();
        bool active_is_default = (id_bytes == nullptr);
        if (!active_is_default) {
            int new_idx = AudioDeviceList::instance()
                            .find_index_by_id_bytes(id_bytes, id_len);
            if (new_idx < 0) {
                // Device unplugged. Reset the audio_out param to 0 so the
                // switch path below picks Default on this same tick.
                CompiledNode* cn = nullptr;
                int pidx = find_audio_out_device_param(compiled_graph_, &cn);
                if (pidx >= 0 && cn) {
                    cn->param_values[pidx] = 0.0f;
                    std::fprintf(stderr,
                        "[vivid] AudioEngine: active device disappeared, falling back to Default\n");
                }
            }
        }
    }

    // -- Switch device when the user's selection differs from what's open.
    int desired = -1;
    {
        CompiledNode* cn = nullptr;
        int pidx = find_audio_out_device_param(compiled_graph_, &cn);
        if (pidx >= 0 && cn) desired = static_cast<int>(cn->param_values[pidx]);
    }
    if (desired < 0) return;
    if (desired == audio_executor_->applied_device_index()) return;

    if (!audio_executor_->restart_device()) {
        std::fprintf(stderr, "[vivid] AudioEngine: failed to switch to device index %d\n", desired);
    } else {
        std::fprintf(stderr, "[vivid] AudioEngine: switched playback device (index %d)\n", desired);
        // Detect rate mismatch against the session rate; main.cpp will
        // drain this and trigger a graph recompile at the new rate.
        if (runtime_core_) {
            uint32_t actual = audio_executor_->actual_device_rate();
            if (actual != 0 && actual != runtime_core_->audio_sample_rate()) {
                pending_session_sample_rate_ = actual;
            }
        }
    }
}

uint32_t AudioEngine::consume_pending_session_sample_rate() {
    uint32_t v = pending_session_sample_rate_;
    pending_session_sample_rate_ = 0;
    return v;
}

void AudioEngine::shutdown() {
    if (audio_executor_) {
        audio_executor_->shutdown();
        audio_executor_.reset();
    }
    compiled_graph_ = nullptr;
    audio_frame_bridge_ = nullptr;
    runtime_core_ = nullptr;

    std::fprintf(stderr, "[vivid] AudioEngine: shutdown\n");
}

const AnalysisSnapshot& AudioEngine::analysis_read() const {
    if (audio_frame_bridge_) return audio_frame_bridge_->active_analysis();
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

void AudioEngine::audio_node_output_port(const std::string& node_id,
                                          const std::string& port_name,
                                          int* port_idx_out,
                                          bool* is_lane_array_out) const {
    if (port_idx_out) *port_idx_out = -1;
    if (is_lane_array_out) *is_lane_array_out = false;
    if (!compiled_graph_) return;
    int idx = audio_node_index(node_id);
    if (idx < 0) return;
    const auto& cn = compiled_graph_->nodes[compiled_graph_->audio_order[idx]];
    auto it = cn.output_port_indices.find(port_name);
    if (it == cn.output_port_indices.end()) return;
    if (port_idx_out) *port_idx_out = static_cast<int>(it->second);
    if (is_lane_array_out && cn.loader && cn.loader->descriptor()) {
        const auto* desc = cn.loader->descriptor();
        for (uint32_t i = 0; i < desc->port_count; ++i) {
            if (desc->ports[i].direction == VIVID_PORT_OUTPUT &&
                desc->ports[i].name && port_name == desc->ports[i].name) {
                *is_lane_array_out = (desc->ports[i].type == VIVID_PORT_SCALAR &&
                                      desc->ports[i].multiplicity == VIVID_MULTIPLICITY_MANY);
                break;
            }
        }
    }
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

        // Destroy primary instance using the still-valid old loader.
        // For audio-only operators graph_compiler.cpp:115 aliases
        // cn.audio_instance = cn.instance (same pointer). If we don't clear
        // the alias here, GraphCompiler::reload_operator's subsequent cleanup
        // will try to destroy_instance() it a second time (UAF / double-free).
        if (cn.instance) {
            void* dead = cn.instance;
            cn.loader->destroy_instance(cn.instance);
            cn.instance = nullptr;
            if (cn.audio_instance == dead) cn.audio_instance = nullptr;
        }
    }
    // Note: audio remains paused until post_reload_operator
}

// Phase 2: Create new instances from the new (already-swapped) loader.
bool AudioEngine::post_reload_operator(const std::string& type_name, OperatorRegistry& registry) {
    OperatorLoader* new_loader = registry.find_loaded(type_name);
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
        GraphCompiler::init_audio_state(cn, new_desc, buffer_size());

        // Restore saved param values
        for (const auto& [pname, pval] : saved.params) {
            auto pi = cn.param_indices.find(pname);
            if (pi != cn.param_indices.end())
                cn.param_values[pi->second] = pval;
        }

        cn.errored = false;
        cn.error_message.clear();
        if (cn.audio) cn.audio->error_message[0] = '\0';
    }

    reload_saved_.clear();

    // Rebuild AudioExecutor (auto-dup groups may change with new descriptor)
    if (audio_executor_ && compiled_graph_ && audio_frame_bridge_ && runtime_core_) {
        audio_executor_->shutdown();
        audio_executor_->build(*audio_frame_bridge_, *compiled_graph_,
                               runtime_core_->live_metronome_store(),
                               runtime_core_->last_tick_time());
        audio_frame_bridge_->build(*compiled_graph_);
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

uint32_t AudioEngine::recording_overrun_count() const {
    if (audio_executor_) return audio_executor_->recording_overrun_count();
    return 0;
}

void AudioEngine::set_analysis_enabled(bool enabled) {
    if (audio_executor_) audio_executor_->set_analysis_enabled(enabled);
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

uint32_t AudioEngine::late_delivery_count() const {
    if (audio_executor_) return audio_executor_->late_delivery_count();
    return 0;
}

uint32_t AudioEngine::max_delivery_gap_us() const {
    if (audio_executor_) return audio_executor_->max_delivery_gap_us();
    return 0;
}

uint32_t AudioEngine::node_count() const {
    if (compiled_graph_) return static_cast<uint32_t>(compiled_graph_->audio_order.size());
    return 0;
}

uint32_t AudioEngine::buffer_size() const {
    if (audio_executor_) return audio_executor_->buffer_size();
    if (compiled_graph_) return compiled_graph_->audio_buffer_size;
    return vivid::kDefaultAudioBufferSize;
}

uint32_t AudioEngine::sample_rate() const {
    if (audio_executor_) return audio_executor_->sample_rate();
    if (compiled_graph_) return compiled_graph_->audio_sample_rate;
    return kSampleRate;
}

bool AudioEngine::running() const {
    return audio_executor_ && audio_executor_->running();
}

void AudioEngine::process_audio_for_test(float* output, uint32_t frame_count) {
    if (audio_executor_) {
        audio_executor_->process_audio_for_test(output, frame_count);
    } else {
        std::memset(output, 0, frame_count * 2 * sizeof(float));
    }
}

} // namespace vivid
