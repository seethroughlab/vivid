#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "runtime/audio_engine.h"
#include <algorithm>
#include <queue>
#include <cstdio>
#include <cstring>

namespace vivid {

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::build(const Graph& graph, OperatorRegistry& registry, const Scheduler& scheduler) {
    nodes_.clear();
    wires_.clear();
    cross_wires_.clear();

    // Map node id → audio node index
    std::unordered_map<std::string, uint32_t> audio_node_index;

    // 1. Extract audio-domain nodes from the graph
    for (const auto& ndef : graph.nodes()) {
        OperatorLoader* loader = registry.find(ndef.type);
        if (!loader) continue;

        const VividOperatorDescriptor* desc = loader->descriptor();
        if (desc->domain != VIVID_DOMAIN_AUDIO) continue;

        AudioNodeState ns;
        ns.node_id = ndef.id;
        ns.loader = loader;
        ns.instance = loader->create_instance();
        ns.input_port_count = 0;
        ns.output_port_count = 0;
        ns.param_count = desc->param_count;

        // Count and index ports by direction
        for (uint32_t i = 0; i < desc->port_count; ++i) {
            if (desc->ports[i].direction == VIVID_PORT_INPUT) {
                ns.input_port_indices[desc->ports[i].name] = ns.input_port_count;
                ns.input_port_count++;
            } else {
                ns.output_port_indices[desc->ports[i].name] = ns.output_port_count;
                ns.output_port_count++;
            }
        }

        // Allocate audio buffers
        ns.input_buffers.resize(ns.input_port_count, std::vector<float>(kBufferSize, 0.0f));
        ns.output_buffers.resize(ns.output_port_count, std::vector<float>(kBufferSize, 0.0f));

        // Init params from descriptor defaults, then override from JSON
        ns.param_values.resize(desc->param_count);
        for (uint32_t i = 0; i < desc->param_count; ++i) {
            ns.param_values[i] = desc->params[i].default_value;
            ns.param_indices[desc->params[i].name] = i;
        }
        for (const auto& [pname, pval] : ndef.params) {
            auto pi = ns.param_indices.find(pname);
            if (pi != ns.param_indices.end()) {
                ns.param_values[pi->second] = pval;
            }
        }

        audio_node_index[ndef.id] = static_cast<uint32_t>(nodes_.size());
        nodes_.push_back(std::move(ns));
    }

    if (nodes_.empty()) return false;

    // 2. Resolve connections
    uint32_t n = static_cast<uint32_t>(nodes_.size());
    std::vector<std::vector<uint32_t>> adj(n);
    std::vector<uint32_t> in_degree(n, 0);

    // Build a lookup for control scheduler nodes by id
    std::unordered_map<std::string, size_t> control_node_map;
    for (size_t i = 0; i < scheduler.nodes().size(); ++i) {
        control_node_map[scheduler.nodes()[i].node_id] = i;
    }

    for (const auto& conn : graph.connections()) {
        auto from_audio = audio_node_index.find(conn.from_node);
        auto to_audio = audio_node_index.find(conn.to_node);

        if (from_audio != audio_node_index.end() && to_audio != audio_node_index.end()) {
            // Audio → Audio wire
            uint32_t fi = from_audio->second;
            uint32_t ti = to_audio->second;
            auto& from_ns = nodes_[fi];
            auto& to_ns = nodes_[ti];

            auto fp_it = from_ns.output_port_indices.find(conn.from_port);
            auto tp_it = to_ns.input_port_indices.find(conn.to_port);
            if (fp_it != from_ns.output_port_indices.end() &&
                tp_it != to_ns.input_port_indices.end()) {
                AudioWire w;
                w.from_node_idx = fi;
                w.from_port_idx = fp_it->second;
                w.to_node_idx = ti;
                w.to_port_idx = tp_it->second;
                wires_.push_back(w);

                adj[fi].push_back(ti);
                in_degree[ti]++;
            }
        } else if (from_audio == audio_node_index.end() && to_audio != audio_node_index.end()) {
            // Control → Audio cross-domain wire (control output → audio param)
            uint32_t ti = to_audio->second;
            auto& to_ns = nodes_[ti];

            auto pp_it = to_ns.param_indices.find(conn.to_port);
            if (pp_it == to_ns.param_indices.end()) continue;

            // Find the control node's output port index
            auto ctrl_it = control_node_map.find(conn.from_node);
            if (ctrl_it == control_node_map.end()) continue;

            const auto& ctrl_ns = scheduler.nodes()[ctrl_it->second];
            auto cp_it = ctrl_ns.output_port_indices.find(conn.from_port);
            if (cp_it == ctrl_ns.output_port_indices.end()) continue;

            CrossDomainWire cw;
            cw.control_node_id = conn.from_node;
            cw.control_output_port_idx = cp_it->second;
            cw.audio_node_idx = ti;
            cw.audio_param_idx = pp_it->second;
            cross_wires_.push_back(cw);
        }
    }

    // 3. Topological sort (Kahn's algorithm)
    std::queue<uint32_t> q;
    for (uint32_t i = 0; i < n; ++i) {
        if (in_degree[i] == 0)
            q.push(i);
    }

    std::vector<uint32_t> sorted_order;
    sorted_order.reserve(n);
    while (!q.empty()) {
        uint32_t cur = q.front();
        q.pop();
        sorted_order.push_back(cur);
        for (uint32_t next : adj[cur]) {
            if (--in_degree[next] == 0)
                q.push(next);
        }
    }

    if (sorted_order.size() != n) {
        std::fprintf(stderr, "[vivid] AudioEngine: cycle detected in audio subgraph\n");
        return false;
    }

    // 4. Reorder nodes to sorted order
    std::vector<uint32_t> old_to_new(n);
    for (uint32_t i = 0; i < n; ++i) {
        old_to_new[sorted_order[i]] = i;
    }

    std::vector<AudioNodeState> sorted_nodes(n);
    for (uint32_t i = 0; i < n; ++i) {
        sorted_nodes[old_to_new[i]] = std::move(nodes_[i]);
    }
    nodes_ = std::move(sorted_nodes);

    for (auto& w : wires_) {
        w.from_node_idx = old_to_new[w.from_node_idx];
        w.to_node_idx = old_to_new[w.to_node_idx];
    }
    for (auto& cw : cross_wires_) {
        cw.audio_node_idx = old_to_new[cw.audio_node_idx];
    }

    // Initialize param snapshots
    for (auto& snap : snapshots_) {
        snap.node_params.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            snap.node_params[i] = nodes_[i].param_values;
        }
    }

    std::fprintf(stderr, "[vivid] Audio evaluation order:");
    for (uint32_t i = 0; i < n; ++i) {
        std::fprintf(stderr, "%s%s", (i == 0 ? " " : " -> "), nodes_[i].node_id.c_str());
    }
    std::fprintf(stderr, "\n");

    return true;
}

bool AudioEngine::start() {
    if (nodes_.empty()) return false;

    device_ = new ma_device;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate = kSampleRate;
    config.periodSizeInFrames = kBufferSize;
    config.dataCallback = &AudioEngine::ma_data_callback;
    config.pUserData = this;

    if (ma_device_init(nullptr, &config, device_) != MA_SUCCESS) {
        std::fprintf(stderr, "[vivid] AudioEngine: failed to init miniaudio device\n");
        delete device_;
        device_ = nullptr;
        return false;
    }

    if (ma_device_start(device_) != MA_SUCCESS) {
        std::fprintf(stderr, "[vivid] AudioEngine: failed to start miniaudio device\n");
        ma_device_uninit(device_);
        delete device_;
        device_ = nullptr;
        return false;
    }

    running_ = true;
    std::fprintf(stderr, "[vivid] AudioEngine: started (%u Hz, %u frames/buffer, %zu audio nodes)\n",
        kSampleRate, kBufferSize, nodes_.size());
    return true;
}

void AudioEngine::push_params(const Scheduler& scheduler) {
    int write_idx = 1 - active_.load(std::memory_order_acquire);
    auto& snap = snapshots_[write_idx];

    // Copy current param values from audio nodes
    for (size_t i = 0; i < nodes_.size(); ++i) {
        snap.node_params[i] = nodes_[i].param_values;
    }

    // Apply cross-domain wires: read control outputs → write audio params
    for (const auto& cw : cross_wires_) {
        for (const auto& ctrl_ns : scheduler.nodes()) {
            if (ctrl_ns.node_id == cw.control_node_id) {
                float val = ctrl_ns.output_values[cw.control_output_port_idx];
                snap.node_params[cw.audio_node_idx][cw.audio_param_idx] = val;
                break;
            }
        }
    }

    active_.store(write_idx, std::memory_order_release);
}

void AudioEngine::shutdown() {
    if (device_) {
        if (running_) {
            ma_device_stop(device_);
            running_ = false;
        }
        ma_device_uninit(device_);
        delete device_;
        device_ = nullptr;
    }

    for (auto& ns : nodes_) {
        if (ns.instance) {
            ns.loader->destroy_instance(ns.instance);
            ns.instance = nullptr;
        }
    }
    nodes_.clear();
    wires_.clear();
    cross_wires_.clear();

    std::fprintf(stderr, "[vivid] AudioEngine: shutdown\n");
}

void AudioEngine::ma_data_callback(ma_device* device_ptr, void* output, const void* /*input*/, ma_uint32 frame_count) {
    auto* engine = static_cast<AudioEngine*>(device_ptr->pUserData);
    engine->audio_callback(static_cast<float*>(output), frame_count);
}

void AudioEngine::audio_callback(float* output, uint32_t frame_count) {
    // Read params from the active snapshot (lock-free)
    const auto& snap = snapshots_[active_.load(std::memory_order_acquire)];

    // Apply param snapshot to audio nodes
    for (size_t i = 0; i < nodes_.size(); ++i) {
        auto& ns = nodes_[i];
        if (i < snap.node_params.size()) {
            for (size_t p = 0; p < ns.param_values.size() && p < snap.node_params[i].size(); ++p) {
                ns.param_values[p] = snap.node_params[i][p];
            }
        }
    }

    // Process in chunks of kBufferSize
    uint32_t frames_written = 0;
    while (frames_written < frame_count) {
        uint32_t chunk = std::min(kBufferSize, frame_count - frames_written);

        // Process each audio node in topological order
        for (uint32_t ni = 0; ni < static_cast<uint32_t>(nodes_.size()); ++ni) {
            auto& ns = nodes_[ni];

            // Zero input buffers
            for (auto& buf : ns.input_buffers)
                std::memset(buf.data(), 0, chunk * sizeof(float));

            // Copy upstream audio outputs into this node's inputs
            for (const auto& w : wires_) {
                if (w.to_node_idx == ni) {
                    const float* src = nodes_[w.from_node_idx].output_buffers[w.from_port_idx].data();
                    float* dst = ns.input_buffers[w.to_port_idx].data();
                    for (uint32_t s = 0; s < chunk; ++s)
                        dst[s] += src[s];  // additive mixing for multiple sources
                }
            }

            // Build pointer arrays for VividAudioState
            std::vector<float*> in_ptrs(ns.input_port_count);
            std::vector<float*> out_ptrs(ns.output_port_count);
            for (uint32_t p = 0; p < ns.input_port_count; ++p)
                in_ptrs[p] = ns.input_buffers[p].data();
            for (uint32_t p = 0; p < ns.output_port_count; ++p)
                out_ptrs[p] = ns.output_buffers[p].data();

            VividAudioState audio_state{};
            audio_state.input_buffers = in_ptrs.data();
            audio_state.output_buffers = out_ptrs.data();
            audio_state.buffer_size = chunk;
            audio_state.sample_rate = kSampleRate;

            double time = static_cast<double>(audio_frame_ + frames_written) / kSampleRate;

            VividProcessContext ctx{};
            ctx.time = time;
            ctx.delta_time = static_cast<double>(chunk) / kSampleRate;
            ctx.frame = audio_frame_ + frames_written;
            ctx.param_values = ns.param_values.data();
            ctx.input_values = nullptr;
            ctx.output_values = nullptr;
            ctx.gpu = nullptr;
            ctx.audio = &audio_state;

            ns.loader->process(ns.instance, &ctx);
        }

        // Copy last node's output to device buffer (it's the sink)
        if (!nodes_.empty()) {
            auto& sink = nodes_.back();
            if (sink.output_port_count > 0) {
                const float* src = sink.output_buffers[0].data();
                std::memcpy(output + frames_written, src, chunk * sizeof(float));
            } else {
                std::memset(output + frames_written, 0, chunk * sizeof(float));
            }
        } else {
            std::memset(output + frames_written, 0, chunk * sizeof(float));
        }

        frames_written += chunk;
    }

    audio_frame_ += frame_count;
}

} // namespace vivid
