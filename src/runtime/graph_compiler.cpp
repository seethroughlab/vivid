#include "runtime/graph_compiler.h"
#include "runtime/domain.h"
#include "runtime/crash_guard.h"
#include "runtime/shared_handle_registry.h"
#include "common/topo_sort.h"
#include "operator_api/type_id.h"
#include "operator_api/port_type_registry.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace vivid {

static constexpr uint32_t kMaxSpreadCapacity = 1024;

// Compute a linear scale equivalent from ConnectionDef remap fields.
static float remap_to_scale(const ConnectionDef& c) {
    float range = c.from_max - c.from_min;
    return (range != 0.0f) ? (c.to_max - c.to_min) / range : 1.0f;
}

// ---------------------------------------------------------------------------
// Node initialization helpers
// ---------------------------------------------------------------------------

// Initialize the frame-side state on a CompiledNode (ports, params, spreads,
// strings, custom ports, file params, GPU resources).  This corresponds to
// the logic in Scheduler::init_node_state().
static void init_frame_state(CompiledNode& cn,
                             const VividOperatorDescriptor* desc,
                             const std::unordered_map<std::string, float>* param_overrides,
                             const std::unordered_map<std::string, std::string>* string_overrides,
                             const std::filesystem::path& graph_base_dir) {
    // Count and index ports
    cn.input_port_count = 0;
    cn.output_port_count = 0;
    cn.input_port_indices.clear();
    cn.output_port_indices.clear();
    cn.param_indices.clear();
    cn.input_port_types.clear();
    cn.output_port_types.clear();

    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_INPUT) {
            cn.input_port_indices[desc->ports[i].name] = cn.input_port_count++;
            cn.input_port_types.push_back(desc->ports[i].type);
        } else {
            cn.output_port_indices[desc->ports[i].name] = cn.output_port_count++;
            cn.output_port_types.push_back(desc->ports[i].type);
        }
    }

    cn.input_values.assign(cn.input_port_count, 0.0f);
    cn.output_values.assign(cn.output_port_count, 0.0f);
    cn.input_string_values.assign(cn.input_port_count, "");
    cn.output_string_values.assign(cn.output_port_count, "");
    cn.c_input_string_values.assign(cn.input_port_count, nullptr);
    cn.c_output_string_values.assign(cn.output_port_count, nullptr);
    cn.input_spreads.resize(cn.input_port_count);
    cn.output_spreads.resize(cn.output_port_count);
    cn.input_string_spreads.resize(cn.input_port_count);
    cn.output_string_spreads.resize(cn.output_port_count);

    // Params
    cn.param_values.resize(desc->param_count);
    cn.param_lock_flags.assign(desc->param_count, 0);
    for (uint32_t i = 0; i < desc->param_count; ++i) {
        cn.param_values[i] = desc->params[i].default_value;
        cn.param_indices[desc->params[i].name] = i;
    }
    if (param_overrides) {
        for (const auto& [pname, pval] : *param_overrides) {
            auto pi = cn.param_indices.find(pname);
            if (pi != cn.param_indices.end())
                cn.param_values[pi->second] = pval;
        }
    }

    // Domain/cadence flags
    cn.time_dependent = desc->time_dependent != 0;
    cn.is_gpu = (desc->execution_env == VIVID_ENV_GPU);
    cn.prev_output_values.assign(cn.output_port_count, 0.0f);

    // Implicit analysis ports for audio-cadence nodes
    if (cn.active_cadence == Cadence::Audio) {
        cn.analysis_output_port_indices["rms"]      = cn.output_port_count++;
        cn.analysis_output_port_indices["peak"]     = cn.output_port_count++;
        cn.analysis_output_port_indices["waveform"] = cn.output_port_count++;
        cn.output_values.resize(cn.output_port_count, 0.0f);
        cn.prev_output_values.resize(cn.output_port_count, 0.0f);
        cn.output_spreads.resize(cn.output_port_count);
    }

    // Spread port staging buffers
    cn.c_in_spreads.resize(cn.input_port_count);
    cn.c_out_spreads.resize(cn.output_port_count);
    cn.out_spread_buf.resize(cn.output_port_count);
    cn.c_in_string_spreads.resize(cn.input_port_count);
    cn.c_out_string_spreads.resize(cn.output_port_count);
    cn.in_string_spread_ptrs.resize(cn.input_port_count);
    cn.out_string_spread_ptr_buf.resize(cn.output_port_count);
    for (uint32_t p = 0; p < cn.output_port_count; ++p) {
        cn.out_spread_buf[p].resize(kMaxSpreadCapacity, 0.0f);
        cn.out_string_spread_ptr_buf[p].resize(kMaxSpreadCapacity, nullptr);
    }
    for (uint32_t p = 0; p < cn.input_port_count; ++p) {
        cn.in_string_spread_ptrs[p].resize(kMaxSpreadCapacity, nullptr);
    }

    // File params
    cn.file_param_storage.clear();
    cn.file_param_ptrs.clear();
    cn.file_param_indices.clear();
    cn.file_param_is_path.clear();
    for (uint32_t i = 0; i < desc->param_count; ++i) {
        if (desc->params[i].type == VIVID_PARAM_FILE ||
            desc->params[i].type == VIVID_PARAM_TEXT) {
            uint32_t fidx = static_cast<uint32_t>(cn.file_param_storage.size());
            cn.file_param_indices[desc->params[i].name] = fidx;
            const char* def = desc->params[i].default_string;
            cn.file_param_storage.push_back(def ? def : "");
            cn.file_param_is_path.push_back(desc->params[i].type == VIVID_PARAM_FILE ? 1 : 0);
        }
    }
    if (string_overrides) {
        for (const auto& [pname, pval] : *string_overrides) {
            auto fi = cn.file_param_indices.find(pname);
            if (fi != cn.file_param_indices.end())
                cn.file_param_storage[fi->second] = pval;
        }
    }
    // Resolve relative file paths
    if (!graph_base_dir.empty()) {
        for (size_t i = 0; i < cn.file_param_storage.size(); ++i) {
            if (!cn.file_param_is_path.empty() && !cn.file_param_is_path[i]) continue;
            auto& val = cn.file_param_storage[i];
            if (!val.empty() && std::filesystem::path(val).is_relative()) {
                auto resolved = graph_base_dir / val;
                if (std::filesystem::exists(resolved))
                    val = std::filesystem::canonical(resolved).string();
                else
                    val = resolved.lexically_normal().string();
            }
        }
    }
    cn.file_param_ptrs.resize(cn.file_param_storage.size());
    for (size_t i = 0; i < cn.file_param_storage.size(); ++i)
        cn.file_param_ptrs[i] = cn.file_param_storage[i].c_str();

    // Identify special port indices
    cn.texture_input_port_indices.clear();
    cn.custom_input_port_indices.clear();
    cn.custom_output_port_indices.clear();
    cn.string_input_port_indices.clear();
    cn.string_spread_input_port_indices.clear();
    cn.is_gpu_sink = false;
    cn.has_texture_output = false;
    cn.has_string_output = false;
    cn.has_string_spread_output = false;
    cn.aux_texture_output_port_indices.clear();
    cn.aux_gpu_textures.clear();
    cn.aux_gpu_texture_views.clear();
    uint32_t input_idx = 0, out_idx = 0, gpu_tex_out_count = 0;
    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_INPUT) {
            switch (desc->ports[i].type) {
                case VIVID_PORT_TEXTURE:
                    cn.texture_input_port_indices.push_back(input_idx); break;
                case VIVID_PORT_STRING:
                    cn.string_input_port_indices.push_back(input_idx); break;
                case VIVID_PORT_STRING_SPREAD:
                    cn.string_spread_input_port_indices.push_back(input_idx); break;
                default:
                    if (vivid_is_custom_port_type(desc->ports[i].type))
                        cn.custom_input_port_indices.push_back(input_idx);
                    break;
            }
            input_idx++;
        } else {
            switch (desc->ports[i].type) {
                case VIVID_PORT_TEXTURE:
                    cn.has_texture_output = true;
                    if (gpu_tex_out_count > 0) {
                        cn.aux_texture_output_port_indices.push_back(static_cast<int32_t>(out_idx));
                        cn.aux_gpu_textures.push_back(nullptr);
                        cn.aux_gpu_texture_views.push_back(nullptr);
                    }
                    ++gpu_tex_out_count;
                    break;
                case VIVID_PORT_STRING:
                    cn.has_string_output = true; break;
                case VIVID_PORT_STRING_SPREAD:
                    cn.has_string_spread_output = true; break;
                default:
                    if (vivid_is_custom_port_type(desc->ports[i].type))
                        cn.custom_output_port_indices.push_back(out_idx);
                    break;
            }
            out_idx++;
        }
    }
    if (cn.is_gpu) {
        cn.is_gpu_sink = !cn.texture_input_port_indices.empty()
                      && !cn.has_texture_output
                      && cn.custom_output_port_indices.empty();
    }

    cn.custom_output_buf.assign(cn.custom_output_port_indices.size(), nullptr);
    cn.custom_outputs.assign(cn.custom_output_port_indices.size(), nullptr);
    cn.resolved_custom_inputs.assign(cn.custom_input_port_indices.size(), nullptr);
}

// Initialize audio-specific state on a CompiledNode that has Cadence::Audio.
// Corresponds to AudioEngine::init_audio_node_state().
static void init_audio_state(CompiledNode& cn,
                             const VividOperatorDescriptor* desc,
                             uint32_t buffer_size) {
    // Channel descriptors
    cn.descriptor_input_channels.clear();
    cn.descriptor_output_channels.clear();
    cn.has_spread_ports = false;
    cn.has_string_input_ports = false;
    cn.has_custom_input_ports = false;
    cn.has_custom_output_ports_audio = false;

    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_INPUT) {
            cn.descriptor_input_channels.push_back(desc->ports[i].channels);
            if (desc->ports[i].type == VIVID_PORT_SPREAD)
                cn.has_spread_ports = true;
            if (desc->ports[i].type == VIVID_PORT_STRING)
                cn.has_string_input_ports = true;
            if (vivid_is_custom_port_type(desc->ports[i].type))
                cn.has_custom_input_ports = true;
        } else {
            cn.descriptor_output_channels.push_back(desc->ports[i].channels);
            if (vivid_is_custom_port_type(desc->ports[i].type))
                cn.has_custom_output_ports_audio = true;
        }
    }

    // Channel counts default to 1; channel negotiation (Pass 4) will override
    cn.input_channel_counts.assign(cn.input_port_count, 1);
    cn.output_channel_counts.assign(cn.output_port_count, 1);
    cn.is_mono_autodup = false;

    // Audio buffers (will be resized during channel negotiation)
    cn.audio_buffers_in.resize(cn.input_port_count,
                               std::vector<float>(buffer_size, 0.0f));
    cn.audio_buffers_out.resize(cn.output_port_count,
                                std::vector<float>(buffer_size, 0.0f));
    cn.audio_in_ptrs.resize(cn.input_port_count);
    cn.audio_out_ptrs.resize(cn.output_port_count);

    // Float CV inputs (from descriptor defaults for SIGNAL input ports)
    cn.float_input_defaults.clear();
    cn.float_input_count = 0;
    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_INPUT &&
            desc->ports[i].type == VIVID_PORT_SIGNAL) {
            cn.float_input_defaults.push_back(desc->ports[i].default_value);
            cn.float_input_count++;
        }
    }
    cn.float_input_values = cn.float_input_defaults;

    // Float/SIGNAL outputs + auto-extraction mappings
    cn.float_output_values.clear();
    cn.float_output_count = 0;
    cn.signal_output_extractions.clear();
    {
        uint32_t oi = 0;
        for (uint32_t i = 0; i < desc->port_count; ++i) {
            if (desc->ports[i].direction == VIVID_PORT_OUTPUT) {
                if (desc->ports[i].type == VIVID_PORT_SIGNAL) {
                    cn.signal_output_extractions.push_back({oi, cn.float_output_count});
                    cn.float_output_count++;
                }
                oi++;
            }
        }
    }
    cn.float_output_values.resize(cn.float_output_count, 0.0f);

    // Custom output ptrs
    cn.custom_output_ptrs.clear();
    cn.custom_output_count_audio = 0;
    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_OUTPUT &&
            vivid_is_custom_port_type(desc->ports[i].type))
            cn.custom_output_count_audio++;
    }
    cn.custom_output_ptrs.resize(cn.custom_output_count_audio, nullptr);
}

// ---------------------------------------------------------------------------
// GraphCompiler::compile()
// ---------------------------------------------------------------------------

std::unique_ptr<CompiledGraph> GraphCompiler::compile(
    const Graph& graph,
    OperatorRegistry& registry,
    const Options& options)
{
    auto cg = std::make_unique<CompiledGraph>();
    std::filesystem::path graph_base_dir;
    if (!graph.source_path().empty()) {
        graph_base_dir = std::filesystem::path(graph.source_path()).parent_path();
    }

    // Collect incoming/outgoing port names for missing-operator placeholders
    std::unordered_map<std::string, std::vector<std::string>> incoming_ports;
    std::unordered_map<std::string, std::vector<std::string>> outgoing_ports;
    auto push_unique = [](std::vector<std::string>& v, const std::string& s) {
        if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
    };
    for (const auto& conn : graph.connections()) {
        push_unique(outgoing_ports[conn.from_node], conn.from_port);
        push_unique(incoming_ports[conn.to_node], conn.to_port);
    }

    // ===================================================================
    // Pass 1: Create CompiledNodes
    // ===================================================================

    std::unordered_map<std::string, uint32_t> node_index;

    for (const auto& ndef : graph.nodes()) {
        OperatorLoader* loader = registry.find(ndef.type);
        std::unique_ptr<OperatorLoader> owned;

        // WGSLFilter / preset handling
        if (!loader && registry.is_wgsl_preset(ndef.type)) {
            auto* cfg = registry.wgsl_config(ndef.type);
            if (cfg) {
                owned = std::make_unique<OperatorLoader>();
                owned->init_data_driven(*cfg);
                loader = owned.get();
            }
        } else if (loader && ndef.type == "WGSLFilter") {
            auto it = ndef.string_params.find("filter");
            if (it != ndef.string_params.end()) {
                auto* cfg = registry.wgsl_config(it->second);
                if (cfg) {
                    owned = std::make_unique<OperatorLoader>();
                    owned->init_data_driven(*cfg);
                    loader = owned.get();
                }
            }
        }

        const VividOperatorDescriptor* desc = loader ? loader->descriptor() : nullptr;

        CompiledNode cn;
        cn.node_id = ndef.id;
        cn.type_name = ndef.type;
        cn.loader = loader;
        cn.owned_loader = std::move(owned);
        cn.generation = 0;

        if (loader && desc) {
            cn.instance = loader->create_instance();

            // Determine cadence from descriptor.
            // Check execution_env first (new API), fall back to legacy domain field
            // for built-in operators that use static descriptors without the new fields.
            if (desc->execution_env == VIVID_ENV_GPU || desc->has_process_gpu ||
                desc->domain == 2u /*GPU*/) {
                cn.active_cadence = Cadence::Frame;
                cn.is_gpu = true;
            } else if (desc->execution_env == VIVID_ENV_AUDIO ||
                       (desc->has_process_audio && !desc->has_process_frame) ||
                       desc->domain == 1u /*AUDIO*/) {
                cn.active_cadence = Cadence::Audio;
            } else if (desc->cadence_capability == VIVID_CADENCE_AUDIO_CAPABLE) {
                // Audio-capable operators default to audio-cadence for backward
                // compatibility. The cadence report's target model has them default
                // to frame-rate, but switching that requires explicit cadence
                // selection UI. For now, keep them in the audio world.
                cn.active_cadence = Cadence::Audio;
            } else {
                cn.active_cadence = Cadence::Frame;
            }
            cn.cadence_capability = desc->cadence_capability;

            // Initialize frame-side state (all nodes get this)
            init_frame_state(cn, desc, &ndef.params,
                             ndef.string_params.empty() ? nullptr : &ndef.string_params,
                             graph_base_dir);

            // Initialize audio-specific state (audio-cadence nodes only)
            if (cn.active_cadence == Cadence::Audio) {
                init_audio_state(cn, desc, options.audio_buffer_size);
            }

            // Param lock flags
            for (const auto& [pname, flags] : ndef.param_lock_flags) {
                auto pi = cn.param_indices.find(pname);
                if (pi != cn.param_indices.end())
                    cn.param_lock_flags[pi->second] = flags;
            }

            // Per-node GPU texture resolution
            if (cn.is_gpu) {
                cn.gpu_tex_width  = ndef.tex_width;
                cn.gpu_tex_height = ndef.tex_height;
            }
        } else {
            // Missing operator placeholder
            cn.missing_operator = true;
            cn.instance = nullptr;
            cn.time_dependent = false;
            cn.is_gpu = false;
            cn.active_cadence = Cadence::Frame;
            cn.is_gpu_sink = false;
            cn.has_texture_output = false;

            const auto& in_names = incoming_ports[ndef.id];
            const auto& out_names = outgoing_ports[ndef.id];
            cn.input_port_count = static_cast<uint32_t>(in_names.size());
            cn.output_port_count = static_cast<uint32_t>(out_names.size());
            for (uint32_t i = 0; i < cn.input_port_count; ++i)
                cn.input_port_indices[in_names[i]] = i;
            for (uint32_t i = 0; i < cn.output_port_count; ++i)
                cn.output_port_indices[out_names[i]] = i;
            cn.input_port_types.assign(cn.input_port_count, VIVID_PORT_SIGNAL);
            cn.output_port_types.assign(cn.output_port_count, VIVID_PORT_SIGNAL);
            cn.input_values.assign(cn.input_port_count, 0.0f);
            cn.output_values.assign(cn.output_port_count, 0.0f);
            cn.input_string_values.assign(cn.input_port_count, "");
            cn.output_string_values.assign(cn.output_port_count, "");
            cn.c_input_string_values.assign(cn.input_port_count, nullptr);
            cn.c_output_string_values.assign(cn.output_port_count, nullptr);
            cn.prev_output_values.assign(cn.output_port_count, 0.0f);
            cn.input_spreads.resize(cn.input_port_count);
            cn.output_spreads.resize(cn.output_port_count);
            cn.input_string_spreads.resize(cn.input_port_count);
            cn.output_string_spreads.resize(cn.output_port_count);

            uint32_t pidx = 0;
            for (const auto& [pname, pval] : ndef.params) {
                cn.param_indices[pname] = pidx++;
                cn.param_values.push_back(pval);
            }
            cn.param_lock_flags.assign(cn.param_values.size(), 0);
            for (const auto& [pname, flags] : ndef.param_lock_flags) {
                auto pi = cn.param_indices.find(pname);
                if (pi != cn.param_indices.end())
                    cn.param_lock_flags[pi->second] = flags;
            }

            cn.c_in_spreads.resize(cn.input_port_count);
            cn.c_out_spreads.resize(cn.output_port_count);
            cn.out_spread_buf.resize(cn.output_port_count);
            cn.c_in_string_spreads.resize(cn.input_port_count);
            cn.c_out_string_spreads.resize(cn.output_port_count);
            cn.in_string_spread_ptrs.resize(cn.input_port_count);
            cn.out_string_spread_ptr_buf.resize(cn.output_port_count);
            for (uint32_t p = 0; p < cn.output_port_count; ++p)
                cn.out_spread_buf[p].resize(kMaxSpreadCapacity, 0.0f);
            for (uint32_t p = 0; p < cn.input_port_count; ++p)
                cn.in_string_spread_ptrs[p].resize(kMaxSpreadCapacity, nullptr);
            for (uint32_t p = 0; p < cn.output_port_count; ++p)
                cn.out_string_spread_ptr_buf[p].resize(kMaxSpreadCapacity, nullptr);

            std::fprintf(stderr, "[vivid] GraphCompiler: missing operator '%s' (node '%s') — placeholder\n",
                         ndef.type.c_str(), ndef.id.c_str());
        }

        node_index[ndef.id] = static_cast<uint32_t>(cg->nodes.size());
        cg->nodes.push_back(std::move(cn));
    }

    // ===================================================================
    // Pass 2: Resolve connections into CompiledEdges
    // ===================================================================

    uint32_t n = static_cast<uint32_t>(cg->nodes.size());
    std::vector<std::vector<uint32_t>> adj(n);
    std::vector<uint32_t> in_degree(n, 0);
    std::vector<std::vector<uint32_t>> string_in_fanin(n);
    for (uint32_t i = 0; i < n; ++i)
        string_in_fanin[i].assign(cg->nodes[i].input_port_count, 0);

    for (const auto& conn : graph.connections()) {
        auto from_it = node_index.find(conn.from_node);
        auto to_it   = node_index.find(conn.to_node);
        if (from_it == node_index.end() || to_it == node_index.end()) continue;

        uint32_t fi = from_it->second;
        uint32_t ti = to_it->second;
        auto& from_cn = cg->nodes[fi];
        auto& to_cn   = cg->nodes[ti];

        // Determine source port
        VividPortType from_port_type = VIVID_PORT_SIGNAL;
        bool source_is_param = false;
        uint32_t from_port_idx = 0;
        auto fp_it = from_cn.output_port_indices.find(conn.from_port);
        if (fp_it != from_cn.output_port_indices.end()) {
            from_port_idx = fp_it->second;
            if (from_cn.loader && from_cn.loader->descriptor()) {
                const auto* from_desc = from_cn.loader->descriptor();
                uint32_t oi = 0;
                for (uint32_t pi = 0; pi < from_desc->port_count; ++pi) {
                    if (from_desc->ports[pi].direction == VIVID_PORT_OUTPUT) {
                        if (oi == fp_it->second) {
                            from_port_type = from_desc->ports[pi].type;
                            break;
                        }
                        oi++;
                    }
                }
            }
        } else {
            auto pp_it = from_cn.param_indices.find(conn.from_port);
            if (pp_it == from_cn.param_indices.end()) continue;
            from_port_idx = pp_it->second;
            source_is_param = true;
            auto fp_src_it = from_cn.file_param_indices.find(conn.from_port);
            from_port_type = (fp_src_it != from_cn.file_param_indices.end())
                ? VIVID_PORT_STRING : VIVID_PORT_SIGNAL;
        }

        CompiledEdge e;
        e.from_node = fi;
        e.from_port = from_port_idx;
        e.sources_param = source_is_param;
        if (source_is_param && from_port_type == VIVID_PORT_STRING) {
            auto fp_src_it2 = from_cn.file_param_indices.find(conn.from_port);
            e.sources_file_param = true;
            e.from_file_param_idx = fp_src_it2->second;
        }
        e.to_node = ti;
        e.data_type = from_port_type;
        e.from_min = conn.from_min;
        e.from_max = conn.from_max;
        e.to_min   = conn.to_min;
        e.to_max   = conn.to_max;
        e.clamp    = conn.clamp;

        // Determine destination port
        auto tp_it = to_cn.input_port_indices.find(conn.to_port);
        if (tp_it != to_cn.input_port_indices.end()) {
            e.to_port = tp_it->second;
            e.targets_param = false;

            VividPortType to_port_type = VIVID_PORT_SIGNAL;
            if (to_cn.loader && to_cn.loader->descriptor()) {
                const auto* to_desc = to_cn.loader->descriptor();
                uint32_t inp_idx = 0;
                for (uint32_t pi = 0; pi < to_desc->port_count; ++pi) {
                    if (to_desc->ports[pi].direction == VIVID_PORT_INPUT) {
                        if (inp_idx == tp_it->second) {
                            to_port_type = to_desc->ports[pi].type;
                            break;
                        }
                        inp_idx++;
                    }
                }
            }

            // Type validation
            if (!from_cn.missing_operator && !to_cn.missing_operator) {
                if (from_port_type == VIVID_PORT_STRING &&
                    to_port_type == VIVID_PORT_STRING) {
                    e.data_type = VIVID_PORT_STRING;
                    string_in_fanin[ti][e.to_port]++;
                } else if (from_port_type == VIVID_PORT_STRING_SPREAD &&
                           to_port_type == VIVID_PORT_STRING_SPREAD) {
                    e.data_type = VIVID_PORT_STRING_SPREAD;
                } else if (from_port_type == VIVID_PORT_TEXTURE &&
                           to_port_type == VIVID_PORT_TEXTURE) {
                    e.data_type = VIVID_PORT_TEXTURE;
                } else if (from_port_type == VIVID_PORT_SPREAD ||
                           to_port_type == VIVID_PORT_SPREAD) {
                    // SPREAD on either end → treat as spread edge
                    // (SIGNAL↔SPREAD is compatible for control types)
                    e.data_type = VIVID_PORT_SPREAD;
                } else if (vivid_is_custom_port_type(from_port_type) &&
                           vivid_is_custom_port_type(to_port_type) &&
                           from_port_type == to_port_type) {
                    e.data_type = from_port_type;
                    e.custom_type_id = from_port_type;
                } else if ((from_port_type == VIVID_PORT_SIGNAL || from_port_type == VIVID_PORT_AUDIO) &&
                           (to_port_type == VIVID_PORT_SIGNAL || to_port_type == VIVID_PORT_AUDIO)) {
                    // SIGNAL/AUDIO interop
                    e.data_type = (from_port_type == VIVID_PORT_AUDIO || to_port_type == VIVID_PORT_AUDIO)
                        ? VIVID_PORT_AUDIO : VIVID_PORT_SIGNAL;
                } else if (from_port_type == VIVID_PORT_TEXTURE || to_port_type == VIVID_PORT_TEXTURE) {
                    std::fprintf(stderr, "[vivid] GraphCompiler: texture type mismatch %s/%s -> %s/%s\n",
                                 conn.from_node.c_str(), conn.from_port.c_str(),
                                 conn.to_node.c_str(), conn.to_port.c_str());
                    continue;
                }
            }
        } else {
            // Try file/text param target
            auto fp_it2 = to_cn.file_param_indices.find(conn.to_port);
            if (fp_it2 != to_cn.file_param_indices.end()) {
                e.targets_file_param = true;
                e.to_file_param_idx = fp_it2->second;
                e.data_type = VIVID_PORT_STRING;
                auto pp_it = to_cn.param_indices.find(conn.to_port);
                e.to_port = (pp_it != to_cn.param_indices.end()) ? pp_it->second : 0;
                e.targets_param = true;
            } else {
                auto pp_it = to_cn.param_indices.find(conn.to_port);
                if (pp_it == to_cn.param_indices.end()) continue;
                e.to_port = pp_it->second;
                e.targets_param = true;
            }
        }

        // Determine transport from cadence mismatch
        if (from_cn.active_cadence == to_cn.active_cadence) {
            e.transport = EdgeTransport::Direct;
        } else {
            e.transport = EdgeTransport::Snapshot;
        }

        cg->edges.push_back(e);

        // Build adjacency for topological sort (Direct edges only)
        if (e.transport == EdgeTransport::Direct) {
            adj[fi].push_back(ti);
            in_degree[ti]++;
        }
    }

    // Validate string fan-in
    for (uint32_t ni = 0; ni < n; ++ni) {
        for (uint32_t pi = 0; pi < cg->nodes[ni].input_port_count; ++pi) {
            if (string_in_fanin[ni][pi] > 1) {
                std::fprintf(stderr, "[vivid] GraphCompiler: string fan-in > 1 on '%s' port %u\n",
                             cg->nodes[ni].node_id.c_str(), pi);
                return nullptr;
            }
        }
    }

    // ===================================================================
    // Pass 3: Topological sort
    // ===================================================================
    // We sort the entire graph together (all cadences). The frame_order and
    // audio_order are then extracted as subsets of the global topo order.

    auto sorted_order = kahn_sort(n, adj, in_degree);
    if (sorted_order.empty() && n > 0) {
        std::fprintf(stderr, "[vivid] GraphCompiler: cycle detected in graph\n");
        return nullptr;
    }

    // Reindex nodes to sorted order
    std::vector<uint32_t> old_to_new(n);
    for (uint32_t i = 0; i < n; ++i)
        old_to_new[sorted_order[i]] = i;

    std::vector<CompiledNode> sorted_nodes(n);
    for (uint32_t i = 0; i < n; ++i)
        sorted_nodes[old_to_new[i]] = std::move(cg->nodes[i]);
    cg->nodes = std::move(sorted_nodes);

    // Remap edges
    for (auto& e : cg->edges) {
        e.from_node = old_to_new[e.from_node];
        e.to_node   = old_to_new[e.to_node];
    }

    // Fix owned_loader pointers after move
    for (auto& cn : cg->nodes) {
        if (cn.owned_loader) cn.loader = cn.owned_loader.get();
    }

    // Build upstream_nodes for generation tracking
    for (auto& cn : cg->nodes)
        cn.upstream_nodes.clear();
    for (const auto& e : cg->edges) {
        auto& ups = cg->nodes[e.to_node].upstream_nodes;
        bool found = false;
        for (auto idx : ups) {
            if (idx == e.from_node) { found = true; break; }
        }
        if (!found) ups.push_back(e.from_node);
    }

    // Build frame_order and audio_order from sorted nodes
    for (uint32_t i = 0; i < n; ++i) {
        if (cg->nodes[i].active_cadence == Cadence::Audio)
            cg->audio_order.push_back(i);
        else
            cg->frame_order.push_back(i);
    }

    // ===================================================================
    // Pass 4: Audio channel negotiation
    // ===================================================================
    // Three-pass algorithm ported from AudioEngine::build()

    // Pass 4a: Set explicit channel counts from descriptors
    for (uint32_t idx : cg->audio_order) {
        auto& cn = cg->nodes[idx];
        for (uint32_t p = 0; p < cn.input_port_count; ++p) {
            if (p < cn.descriptor_input_channels.size() &&
                cn.descriptor_input_channels[p] > 0)
                cn.input_channel_counts[p] = cn.descriptor_input_channels[p];
        }
        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
            if (p < cn.descriptor_output_channels.size() &&
                cn.descriptor_output_channels[p] > 0)
                cn.output_channel_counts[p] = cn.descriptor_output_channels[p];
        }
    }

    // Pass 4b: Propagate via audio Direct edges in topo order
    for (uint32_t idx : cg->audio_order) {
        auto& cn = cg->nodes[idx];
        // Auto outputs inherit max of inputs
        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
            if (p < cn.descriptor_output_channels.size() &&
                cn.descriptor_output_channels[p] == 0 &&
                p < cn.output_port_types.size() &&
                cn.output_port_types[p] == VIVID_PORT_AUDIO) {
                uint8_t max_in = 1;
                for (uint32_t ip = 0; ip < cn.input_port_count; ++ip) {
                    if (cn.input_channel_counts[ip] > max_in)
                        max_in = cn.input_channel_counts[ip];
                }
                cn.output_channel_counts[p] = max_in;
            }
        }
        // Propagate to downstream via edges
        for (const auto& e : cg->edges) {
            if (e.from_node == idx && e.transport == EdgeTransport::Direct &&
                cg->nodes[e.to_node].active_cadence == Cadence::Audio &&
                !e.targets_param) {
                auto& to_cn = cg->nodes[e.to_node];
                uint8_t src_ch = 1;
                if (e.from_port < cn.output_channel_counts.size())
                    src_ch = cn.output_channel_counts[e.from_port];
                if (e.to_port < to_cn.input_channel_counts.size() &&
                    e.to_port < to_cn.descriptor_input_channels.size() &&
                    to_cn.descriptor_input_channels[e.to_port] == 0 &&
                    src_ch > to_cn.input_channel_counts[e.to_port]) {
                    to_cn.input_channel_counts[e.to_port] = src_ch;
                }
            }
        }
    }

    // Pass 4c: Detect mono auto-dup candidates
    for (uint32_t idx : cg->audio_order) {
        auto& cn = cg->nodes[idx];
        bool all_mono = true;
        for (uint32_t p = 0; p < cn.input_port_count && all_mono; ++p) {
            if (p < cn.descriptor_input_channels.size() &&
                cn.input_port_types[p] == VIVID_PORT_AUDIO &&
                cn.descriptor_input_channels[p] > 1)
                all_mono = false;
        }
        for (uint32_t p = 0; p < cn.output_port_count && all_mono; ++p) {
            if (p < cn.descriptor_output_channels.size() &&
                cn.output_port_types[p] == VIVID_PORT_AUDIO &&
                cn.descriptor_output_channels[p] > 1)
                all_mono = false;
        }
        if (!all_mono) continue;

        uint8_t max_wire_ch = 1;
        for (const auto& e : cg->edges) {
            if (e.to_node == idx && e.transport == EdgeTransport::Direct &&
                !e.targets_param) {
                uint8_t src_ch = 1;
                if (e.from_port < cg->nodes[e.from_node].output_channel_counts.size())
                    src_ch = cg->nodes[e.from_node].output_channel_counts[e.from_port];
                if (src_ch > max_wire_ch) max_wire_ch = src_ch;
            }
        }
        if (max_wire_ch > 1) {
            cn.is_mono_autodup = true;
            for (auto& ch : cn.input_channel_counts) ch = 1;
            for (auto& ch : cn.output_channel_counts) ch = 1;
        }
    }

    // ===================================================================
    // Pass 5: Audio buffer allocation
    // ===================================================================

    for (uint32_t idx : cg->audio_order) {
        auto& cn = cg->nodes[idx];
        uint32_t bs = options.audio_buffer_size;

        if (cn.is_mono_autodup) {
            // Find max incoming wire channel count for buffer sizing
            uint8_t wire_ch = 1;
            for (const auto& e : cg->edges) {
                if (e.to_node == idx && e.transport == EdgeTransport::Direct && !e.targets_param) {
                    uint8_t src_ch = 1;
                    if (e.from_port < cg->nodes[e.from_node].output_channel_counts.size())
                        src_ch = cg->nodes[e.from_node].output_channel_counts[e.from_port];
                    if (src_ch > wire_ch) wire_ch = src_ch;
                }
            }
            for (uint32_t p = 0; p < cn.input_port_count; ++p)
                cn.audio_buffers_in[p].resize(wire_ch * bs, 0.0f);
            for (uint32_t p = 0; p < cn.output_port_count; ++p)
                cn.audio_buffers_out[p].resize(wire_ch * bs, 0.0f);
        } else {
            for (uint32_t p = 0; p < cn.input_port_count; ++p)
                cn.audio_buffers_in[p].resize(cn.input_channel_counts[p] * bs, 0.0f);
            for (uint32_t p = 0; p < cn.output_port_count; ++p)
                cn.audio_buffers_out[p].resize(cn.output_channel_counts[p] * bs, 0.0f);
        }

        // Set audio buffer pointers
        for (uint32_t p = 0; p < cn.input_port_count; ++p)
            cn.audio_in_ptrs[p] = cn.audio_buffers_in[p].data();
        for (uint32_t p = 0; p < cn.output_port_count; ++p)
            cn.audio_out_ptrs[p] = cn.audio_buffers_out[p].data();
    }

    // Set from_channels/to_channels on audio Direct edges
    for (auto& e : cg->edges) {
        if (e.transport != EdgeTransport::Direct) continue;
        if (cg->nodes[e.from_node].active_cadence != Cadence::Audio) continue;
        if (e.targets_param) continue;

        auto& from_cn = cg->nodes[e.from_node];
        auto& to_cn = cg->nodes[e.to_node];

        if (from_cn.is_mono_autodup) {
            // Trace back to find upstream channel count
            uint8_t ch = 1;
            for (const auto& ue : cg->edges) {
                if (ue.to_node == e.from_node && ue.transport == EdgeTransport::Direct && !ue.targets_param) {
                    uint8_t src_ch = 1;
                    if (ue.from_port < cg->nodes[ue.from_node].output_channel_counts.size())
                        src_ch = cg->nodes[ue.from_node].output_channel_counts[ue.from_port];
                    if (src_ch > ch) ch = src_ch;
                }
            }
            e.from_channels = ch;
        } else if (e.from_port < from_cn.output_channel_counts.size()) {
            e.from_channels = from_cn.output_channel_counts[e.from_port];
        }

        if (to_cn.is_mono_autodup) {
            e.to_channels = e.from_channels;  // auto-dup matches source
        } else if (e.to_port < to_cn.input_channel_counts.size()) {
            e.to_channels = to_cn.input_channel_counts[e.to_port];
        }
    }

    // ===================================================================
    // Pass 6: Partition edges
    // ===================================================================

    for (uint32_t ei = 0; ei < static_cast<uint32_t>(cg->edges.size()); ++ei) {
        const auto& e = cg->edges[ei];
        if (e.transport == EdgeTransport::Direct) {
            if (cg->nodes[e.from_node].active_cadence == Cadence::Audio)
                cg->audio_direct_edges.push_back(ei);
            else
                cg->frame_direct_edges.push_back(ei);
        } else {
            // Snapshot edge — determine direction
            if (cg->nodes[e.from_node].active_cadence == Cadence::Audio)
                cg->audio_to_frame_edges.push_back(ei);
            else
                cg->frame_to_audio_edges.push_back(ei);
        }
    }

    // ===================================================================
    // Pass 7: Finalize
    // ===================================================================

    // Build node_id_to_index
    for (uint32_t i = 0; i < n; ++i)
        cg->node_id_to_index[cg->nodes[i].node_id] = i;

    if (std::getenv("VIVID_VERBOSE")) {
        std::fprintf(stderr, "[vivid] GraphCompiler: %u nodes (%zu frame, %zu audio), %zu edges (%zu snapshot)\n",
                     n,
                     cg->frame_order.size(), cg->audio_order.size(),
                     cg->edges.size(),
                     cg->frame_to_audio_edges.size() + cg->audio_to_frame_edges.size());
    }

    return cg;
}

} // namespace vivid
