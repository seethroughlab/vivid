#include "runtime/graph_compiler.h"
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
// strings, custom ports, file params, GPU resources).
void GraphCompiler::init_frame_state(CompiledNode& cn,
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
    cn.bridge_input_values.assign(cn.input_port_count, 0.0f);
    cn.bridge_input_dirty.assign(cn.input_port_count, 0);
    cn.input_connected.assign(cn.input_port_count, 0);
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

    // Execution flags
    cn.time_dependent = desc->time_dependent != 0;
    bool node_is_gpu = (desc->has_process_gpu != 0);

    // Discover analysis ports from descriptor (tagged with semantic_tag "analysis")
    if (cn.active_cadence == Cadence::Audio) {
        for (uint32_t i = 0; i < desc->port_count; ++i) {
            const auto& p = desc->ports[i];
            if (p.direction == VIVID_PORT_OUTPUT && p.semantic_tag &&
                std::strcmp(p.semantic_tag, "analysis") == 0) {
                auto it = cn.output_port_indices.find(p.name);
                if (it != cn.output_port_indices.end())
                    cn.audio->analysis_output_port_indices[p.name] = it->second;
            }
        }
    }

    // Auto-inject GPU analysis output ports (frame_hash, brightness, contrast, dominant_hue).
    // These are owned by the runtime — operators don't declare them.
    if (node_is_gpu && cn.gpu) {
        auto inject = [&](const char* name) -> uint32_t {
            uint32_t idx = cn.output_port_count++;
            cn.output_port_indices[name] = idx;
            cn.output_port_types.push_back(VIVID_PORT_SIGNAL);
            return idx;
        };
        cn.gpu->analysis_frame_hash_idx   = inject("frame_hash");
        cn.gpu->analysis_brightness_idx   = inject("brightness");
        cn.gpu->analysis_contrast_idx     = inject("contrast");
        cn.gpu->analysis_dominant_hue_idx = inject("dominant_hue");

        // Resize output arrays to accommodate the injected ports.
        cn.output_values.resize(cn.output_port_count, 0.0f);
        cn.output_string_values.resize(cn.output_port_count, "");
        cn.c_output_string_values.resize(cn.output_port_count, nullptr);
        cn.output_spreads.resize(cn.output_port_count);
        cn.output_string_spreads.resize(cn.output_port_count);
    }

    // Lane metadata (sized to port count, populated by Pass 2.6).
    cn.input_lane_sets.resize(cn.input_port_count);
    cn.output_lane_sets.resize(cn.output_port_count);

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
    cn.custom_input_port_indices.clear();
    cn.custom_output_port_indices.clear();
    cn.string_input_port_indices.clear();
    cn.string_spread_input_port_indices.clear();
    cn.has_string_output = false;
    cn.has_string_spread_output = false;

    // GPU-specific port scanning
    if (node_is_gpu) {
        if (!cn.gpu) cn.gpu = std::make_unique<GpuNodeState>();
        cn.gpu->texture_input_port_indices.clear();
        cn.gpu->is_sink = false;
        cn.gpu->has_texture_output = false;
        cn.gpu->aux_texture_output_port_indices.clear();
        cn.gpu->aux_gpu_textures.clear();
        cn.gpu->aux_gpu_texture_views.clear();
    }

    uint32_t input_idx = 0, out_idx = 0, gpu_tex_out_count = 0;
    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_INPUT) {
            switch (desc->ports[i].type) {
                case VIVID_PORT_TEXTURE:
                    if (cn.gpu) cn.gpu->texture_input_port_indices.push_back(input_idx);
                    break;
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
                    if (cn.gpu) {
                        cn.gpu->has_texture_output = true;
                        if (gpu_tex_out_count > 0) {
                            cn.gpu->aux_texture_output_port_indices.push_back(static_cast<int32_t>(out_idx));
                            cn.gpu->aux_gpu_textures.push_back(nullptr);
                            cn.gpu->aux_gpu_texture_views.push_back(nullptr);
                        }
                        ++gpu_tex_out_count;
                    }
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
    if (cn.gpu) {
        cn.gpu->is_sink = !cn.gpu->texture_input_port_indices.empty()
                       && !cn.gpu->has_texture_output
                       && cn.custom_output_port_indices.empty();
    }

    cn.custom_output_buf.assign(cn.custom_output_port_indices.size(), nullptr);
    cn.custom_outputs.assign(cn.custom_output_port_indices.size(), nullptr);
    cn.resolved_custom_inputs.assign(cn.custom_input_port_indices.size(), nullptr);
}

// Initialize audio-specific state on a CompiledNode that has Cadence::Audio.
void GraphCompiler::init_audio_state(CompiledNode& cn,
                                     const VividOperatorDescriptor* desc,
                                     uint32_t buffer_size) {
    // cn.audio must already be allocated by the caller
    auto& a = *cn.audio;

    // Channel descriptors
    a.descriptor_input_channels.clear();
    a.descriptor_output_channels.clear();
    a.has_spread_ports = false;
    a.has_string_input_ports = false;
    a.has_custom_input_ports = false;
    a.has_custom_output_ports = false;

    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_INPUT) {
            a.descriptor_input_channels.push_back(desc->ports[i].channels);
            if (desc->ports[i].type == VIVID_PORT_SPREAD)
                a.has_spread_ports = true;
            if (desc->ports[i].type == VIVID_PORT_STRING)
                a.has_string_input_ports = true;
            if (vivid_is_custom_port_type(desc->ports[i].type))
                a.has_custom_input_ports = true;
        } else {
            a.descriptor_output_channels.push_back(desc->ports[i].channels);
            if (vivid_is_custom_port_type(desc->ports[i].type))
                a.has_custom_output_ports = true;
        }
    }

    // Channel counts default to 1; channel negotiation (Pass 4) will override
    a.input_channel_counts.assign(cn.input_port_count, 1);
    a.output_channel_counts.assign(cn.output_port_count, 1);
    a.lane_lift_count = 0;
    a.lane_lift_set_id = 0;

    // Audio buffers (will be resized during channel negotiation)
    a.buffers_in.resize(cn.input_port_count,
                               std::vector<float>(buffer_size, 0.0f));
    a.buffers_out.resize(cn.output_port_count,
                                std::vector<float>(buffer_size, 0.0f));
    a.in_ptrs.resize(cn.input_port_count);
    a.out_ptrs.resize(cn.output_port_count);

    // Float CV inputs (from descriptor defaults for SIGNAL input ports)
    a.float_input_defaults.clear();
    a.float_input_count = 0;
    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_INPUT &&
            desc->ports[i].type == VIVID_PORT_SIGNAL) {
            a.float_input_defaults.push_back(desc->ports[i].default_value);
            a.float_input_count++;
        }
    }
    a.float_input_values = a.float_input_defaults;

    // Float/SIGNAL outputs + auto-extraction mappings
    a.float_output_values.clear();
    a.float_output_count = 0;
    a.signal_output_extractions.clear();
    {
        uint32_t oi = 0;
        for (uint32_t i = 0; i < desc->port_count; ++i) {
            if (desc->ports[i].direction == VIVID_PORT_OUTPUT) {
                if (desc->ports[i].type == VIVID_PORT_SIGNAL) {
                    a.signal_output_extractions.push_back({oi, a.float_output_count});
                    a.float_output_count++;
                }
                oi++;
            }
        }
    }
    a.float_output_values.resize(a.float_output_count, 0.0f);

    // Custom output ptrs
    a.custom_output_ptrs.clear();
    a.custom_output_count = 0;
    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_OUTPUT &&
            vivid_is_custom_port_type(desc->ports[i].type))
            a.custom_output_count++;
    }
    a.custom_output_ptrs.resize(a.custom_output_count, nullptr);
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
        cn.subgraph_owner = ndef.subgraph_owner;
        cn.subgraph_type = ndef.subgraph_type;
        cn.loader = loader;
        cn.owned_loader = std::move(owned);

        if (loader && desc) {
            cn.instance = loader->create_instance();

            // Determine cadence from descriptor + per-node override.
            // Fixed-env operators (GPU, audio-native) ignore overrides.
            if (desc->has_process_gpu) {
                cn.active_cadence = Cadence::Frame;
                cn.gpu = std::make_unique<GpuNodeState>();
            } else if (desc->has_process_audio && !desc->has_process_frame) {
                cn.active_cadence = Cadence::Audio;
            } else if (ndef.cadence_override == CadenceOverride::Audio &&
                       desc->cadence_capability == VIVID_CADENCE_AUDIO_CAPABLE) {
                cn.active_cadence = Cadence::Audio;
            } else if (ndef.cadence_override == CadenceOverride::Frame) {
                cn.active_cadence = Cadence::Frame;
            } else {
                // Audio-capable operators default to frame cadence unless
                // explicitly promoted via CadenceOverride::Audio.
                cn.active_cadence = Cadence::Frame;
            }
            cn.cadence_capability = desc->cadence_capability;
            cn.lane_behavior = static_cast<LaneBehavior>(desc->lane_behavior);
            cn.operator_kind = vivid_operator_kind(desc);
            cn.original_cadence_override = ndef.cadence_override;

            // Allocate audio sub-struct before init_frame_state (which uses it
            // for analysis port indices), but defer full audio init until after
            // init_frame_state sets up port counts.
            if (cn.active_cadence == Cadence::Audio) {
                cn.audio = std::make_unique<AudioNodeState>();
            }

            // Initialize frame-side state (all nodes get this — sets port counts)
            init_frame_state(cn, desc, &ndef.params,
                             ndef.string_params.empty() ? nullptr : &ndef.string_params,
                             graph_base_dir);

            // Initialize audio-specific state (after frame state sets port counts)
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
            if (cn.gpu) {
                cn.gpu->tex_width  = ndef.tex_width;
                cn.gpu->tex_height = ndef.tex_height;
            }
        } else {
            // Missing operator placeholder
            cn.missing_operator = true;
            cn.instance = nullptr;
            cn.time_dependent = false;
            cn.active_cadence = Cadence::Frame;

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
            cn.bridge_input_values.assign(cn.input_port_count, 0.0f);
            cn.bridge_input_dirty.assign(cn.input_port_count, 0);
            cn.input_connected.assign(cn.input_port_count, 0);
            cn.output_values.assign(cn.output_port_count, 0.0f);
            cn.input_string_values.assign(cn.input_port_count, "");
            cn.output_string_values.assign(cn.output_port_count, "");
            cn.c_input_string_values.assign(cn.input_port_count, nullptr);
            cn.c_output_string_values.assign(cn.output_port_count, nullptr);
            cn.input_spreads.resize(cn.input_port_count);
            cn.output_spreads.resize(cn.output_port_count);
            cn.input_string_spreads.resize(cn.input_port_count);
            cn.output_string_spreads.resize(cn.output_port_count);
            cn.input_lane_sets.resize(cn.input_port_count);
            cn.output_lane_sets.resize(cn.output_port_count);

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

    auto drop_connection = [&](const auto& conn, const std::string& reason) {
        std::fprintf(stderr, "[vivid] warning: dropped connection %s/%s → %s/%s: %s\n",
                     conn.from_node.c_str(), conn.from_port.c_str(),
                     conn.to_node.c_str(), conn.to_port.c_str(), reason.c_str());
        cg->dropped_connections.push_back({conn.from_node, conn.from_port,
                                           conn.to_node, conn.to_port, reason});
    };

    for (const auto& conn : graph.connections()) {
        auto from_it = node_index.find(conn.from_node);
        auto to_it   = node_index.find(conn.to_node);
        if (from_it == node_index.end() || to_it == node_index.end()) {
            std::string reason;
            if (from_it == node_index.end()) reason = "node '" + conn.from_node + "' not found";
            if (to_it == node_index.end()) {
                if (!reason.empty()) reason += "; ";
                reason += "node '" + conn.to_node + "' not found";
            }
            drop_connection(conn, reason);
            continue;
        }

        uint32_t fi = from_it->second;
        uint32_t ti = to_it->second;
        auto& from_cn = cg->nodes[fi];
        auto& to_cn   = cg->nodes[ti];

        // Determine source port
        VividPortType from_port_type = VIVID_PORT_SIGNAL;
        VividPortTransport from_port_transport = VIVID_PORT_TRANSPORT_SIGNAL;
        uint32_t from_payload_size = 0;
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
                            from_port_transport = from_desc->ports[pi].transport;
                            from_payload_size = from_desc->ports[pi].payload_size;
                            break;
                        }
                        oi++;
                    }
                }
            }
        } else {
            auto pp_it = from_cn.param_indices.find(conn.from_port);
            if (pp_it == from_cn.param_indices.end()) {
                std::string avail;
                for (const auto& [k, _] : from_cn.output_port_indices)
                    avail += (avail.empty() ? "" : ", ") + k;
                for (const auto& [k, _] : from_cn.param_indices)
                    avail += (avail.empty() ? "" : ", ") + k;
                drop_connection(conn, "'" + conn.from_port + "' not found on node '" +
                    conn.from_node + "' (" + from_cn.type_name + "). Available: " + avail);
                continue;
            }
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
                    e.port_transport = from_port_transport;
                    e.custom_payload_size = from_payload_size;
                } else if ((from_port_type == VIVID_PORT_SIGNAL || from_port_type == VIVID_PORT_AUDIO) &&
                           (to_port_type == VIVID_PORT_SIGNAL || to_port_type == VIVID_PORT_AUDIO)) {
                    // SIGNAL/AUDIO interop
                    e.data_type = (from_port_type == VIVID_PORT_AUDIO || to_port_type == VIVID_PORT_AUDIO)
                        ? VIVID_PORT_AUDIO : VIVID_PORT_SIGNAL;
                } else if (from_port_type == VIVID_PORT_STRING ||
                           from_port_type == VIVID_PORT_STRING_SPREAD ||
                           to_port_type == VIVID_PORT_STRING ||
                           to_port_type == VIVID_PORT_STRING_SPREAD) {
                    std::fprintf(stderr, "[vivid] GraphCompiler: string type mismatch %s/%s -> %s/%s "
                        "(string port types must match exactly)\n",
                        conn.from_node.c_str(), conn.from_port.c_str(),
                        conn.to_node.c_str(), conn.to_port.c_str());
                    return nullptr;
                } else if (from_port_type == VIVID_PORT_TEXTURE || to_port_type == VIVID_PORT_TEXTURE) {
                    drop_connection(conn, "texture type mismatch");
                    continue;
                } else if (vivid_is_custom_port_type(from_port_type) != vivid_is_custom_port_type(to_port_type)) {
                    drop_connection(conn, "custom port type mismatch");
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
                if (pp_it == to_cn.param_indices.end()) {
                    std::string avail;
                    for (const auto& [k, _] : to_cn.input_port_indices)
                        avail += (avail.empty() ? "" : ", ") + k;
                    for (const auto& [k, _] : to_cn.param_indices)
                        avail += (avail.empty() ? "" : ", ") + k;
                    drop_connection(conn, "'" + conn.to_port + "' not found on node '" +
                        conn.to_node + "' (" + to_cn.type_name + "). Available: " + avail);
                    continue;
                }
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

        // Precompute SIGNAL ordinals for cross-cadence and audio-direct paths.
        // The ordinal is the index of this port among VIVID_PORT_SIGNAL ports only.
        if (e.data_type == VIVID_PORT_SIGNAL) {
            if (!e.sources_param) {
                uint32_t ord = 0;
                for (uint32_t p = 0; p < e.from_port && p < from_cn.output_port_types.size(); ++p)
                    if (from_cn.output_port_types[p] == VIVID_PORT_SIGNAL) ord++;
                e.from_signal_ordinal = ord;
            }
            if (!e.targets_param) {
                uint32_t ord = 0;
                for (uint32_t p = 0; p < e.to_port && p < to_cn.input_port_types.size(); ++p)
                    if (to_cn.input_port_types[p] == VIVID_PORT_SIGNAL) ord++;
                e.to_signal_ordinal = ord;
            }
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
    // Pass 2.5: Cadence inference
    // ===================================================================
    // Promote Auto audio-capable nodes to Audio cadence when connected to
    // audio-cadence neighbours in either direction:
    //   - Downstream pull: a Frame supplier feeds an Audio consumer
    //   - Upstream push:   an Audio supplier feeds a Frame consumer
    // Iterate to a fixed point so chains of dual-cadence nodes all promote.
    {
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& e : cg->edges) {
                auto& from_cn = cg->nodes[e.from_node];
                auto& to_cn = cg->nodes[e.to_node];

                // Downstream pull: promote supplier to match audio consumer
                if (to_cn.active_cadence == Cadence::Audio &&
                    from_cn.active_cadence == Cadence::Frame &&
                    from_cn.cadence_capability == VIVID_CADENCE_AUDIO_CAPABLE &&
                    from_cn.original_cadence_override == CadenceOverride::Auto) {
                    from_cn.active_cadence = Cadence::Audio;
                    from_cn.audio = std::make_unique<AudioNodeState>();
                    init_audio_state(from_cn, from_cn.loader->descriptor(),
                                     options.audio_buffer_size);
                    changed = true;
                }

                // Upstream push: promote consumer to match audio supplier
                if (from_cn.active_cadence == Cadence::Audio &&
                    to_cn.active_cadence == Cadence::Frame &&
                    to_cn.cadence_capability == VIVID_CADENCE_AUDIO_CAPABLE &&
                    to_cn.original_cadence_override == CadenceOverride::Auto) {
                    to_cn.active_cadence = Cadence::Audio;
                    to_cn.audio = std::make_unique<AudioNodeState>();
                    init_audio_state(to_cn, to_cn.loader->descriptor(),
                                     options.audio_buffer_size);
                    changed = true;
                }
            }
        }

        // Reclassify edge transport after promotions
        for (auto& e : cg->edges) {
            if (cg->nodes[e.from_node].active_cadence == cg->nodes[e.to_node].active_cadence)
                e.transport = EdgeTransport::Direct;
            else
                e.transport = EdgeTransport::Snapshot;
        }

        // Rebuild adjacency for topo sort (Direct edges only)
        for (auto& a : adj) a.clear();
        std::fill(in_degree.begin(), in_degree.end(), 0);
        for (const auto& e : cg->edges) {
            if (e.transport == EdgeTransport::Direct) {
                adj[e.from_node].push_back(e.to_node);
                in_degree[e.to_node]++;
            }
        }
    }

    // ===================================================================
    // Pass 2.6: Lane-set propagation
    // ===================================================================
    // Walk nodes in topological order and propagate lane-set metadata.
    // Enforces legality: pointwise nodes may not receive inputs from
    // different non-scalar lane sets. Structural nodes allocate fresh
    // lane sets. Reductions emit scalar output.
    //
    // In Phase 2A all operators default to Pointwise, so this pass
    // populates metadata but does not reject any existing graphs.
    {
        // We need a topo order for propagation. Use a temporary sort
        // from the current adjacency (rebuilt at end of Pass 2.5).
        auto lane_order = kahn_sort(n, adj, in_degree);
        // If cycle detected, skip lane propagation — Pass 3 will catch it.
        if (!lane_order.empty() || n == 0) {
            for (uint32_t idx : lane_order) {
                auto& cn = cg->nodes[idx];

                // Collect non-scalar input lane sets from incoming Direct edges.
                uint32_t resolved_lane_set_id = 0;
                uint32_t resolved_lane_count  = 1;
                bool     resolved_identity    = false;
                bool     has_multi_lane       = false;
                bool     lane_mismatch        = false;
                std::string mismatch_src_a, mismatch_src_b;

                for (const auto& e : cg->edges) {
                    if (e.to_node != idx || e.transport != EdgeTransport::Direct || e.targets_param)
                        continue;

                    const auto& from_cn = cg->nodes[e.from_node];
                    if (e.from_port >= from_cn.output_lane_sets.size())
                        continue;

                    const auto& src_ls = from_cn.output_lane_sets[e.from_port];
                    if (src_ls.is_scalar())
                        continue;

                    if (!has_multi_lane) {
                        // First non-scalar input — adopt it.
                        resolved_lane_set_id = src_ls.lane_set_id;
                        resolved_lane_count  = src_ls.lane_count;
                        resolved_identity    = src_ls.identity_bearing;
                        has_multi_lane       = true;
                    } else if (src_ls.lane_set_id != resolved_lane_set_id) {
                        lane_mismatch = true;
                        if (mismatch_src_a.empty())
                            mismatch_src_a = cg->nodes[e.from_node].node_id;
                        mismatch_src_b = cg->nodes[e.from_node].node_id;
                    } else {
                        // Same lane_set_id — take the max count.
                        if (src_ls.lane_count > resolved_lane_count)
                            resolved_lane_count = src_ls.lane_count;
                    }
                }

                // Enforce legality for Pointwise nodes: mismatched non-scalar
                // lane sets are a hard compile failure.
                if (lane_mismatch && cn.lane_behavior == LaneBehavior::Pointwise) {
                    std::fprintf(stderr,
                        "[vivid] GraphCompiler: lane-set mismatch at pointwise node '%s' "
                        "(conflicting sources: '%s', '%s')\n",
                        cn.node_id.c_str(), mismatch_src_a.c_str(),
                        mismatch_src_b.c_str());
                    return nullptr;
                }

                // Build the resolved input lane set.
                LaneSet resolved;
                resolved.lane_set_id     = resolved_lane_set_id;
                resolved.lane_count      = resolved_lane_count;
                resolved.identity_bearing = resolved_identity;

                // Store per-input-port lane sets.
                for (const auto& e : cg->edges) {
                    if (e.to_node != idx || e.transport != EdgeTransport::Direct || e.targets_param)
                        continue;
                    if (e.to_port < cn.input_lane_sets.size()) {
                        const auto& from_cn = cg->nodes[e.from_node];
                        if (e.from_port < from_cn.output_lane_sets.size()) {
                            const auto& src_ls = from_cn.output_lane_sets[e.from_port];
                            if (src_ls.is_scalar()) {
                                // Scalar broadcasts into the resolved lane set.
                                cn.input_lane_sets[e.to_port] = resolved;
                            } else {
                                cn.input_lane_sets[e.to_port] = src_ls;
                            }
                        }
                    }
                }

                // Set output lane sets based on lane behavior.
                LaneSet output_ls;
                switch (cn.lane_behavior) {
                    case LaneBehavior::Pointwise:
                    case LaneBehavior::Kernel:
                        output_ls = resolved;
                        break;
                    case LaneBehavior::Structural:
                        output_ls.lane_set_id = cg->next_lane_set_id++;
                        output_ls.lane_count  = 1;  // runtime will set actual count
                        output_ls.identity_bearing = false;
                        break;
                    case LaneBehavior::Reduction:
                        output_ls.lane_set_id     = 0;
                        output_ls.lane_count      = 1;
                        output_ls.identity_bearing = false;
                        break;
                }

                for (auto& ols : cn.output_lane_sets)
                    ols = output_ls;

                // Populate edge lane metadata for outgoing edges.
                for (auto& e : cg->edges) {
                    if (e.from_node != idx)
                        continue;
                    if (e.from_port < cn.output_lane_sets.size()) {
                        const auto& ols = cn.output_lane_sets[e.from_port];
                        e.lane_set_id = ols.lane_set_id;
                        e.lane_count  = ols.lane_count;
                    }
                }
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

    // Build upstream_nodes for skip-logic dirty propagation
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
        if (cg->nodes[i].active_cadence == Cadence::Audio) {
            cg->audio_order.push_back(i);
        } else {
            cg->frame_order.push_back(i);
        }
    }

    // ===================================================================
    // Pass 4: Audio channel negotiation
    // ===================================================================
    // Three-pass algorithm ported from AudioEngine::build()

    // Pass 4a: Set explicit channel counts from descriptors
    for (uint32_t idx : cg->audio_order) {
        auto& a = *cg->nodes[idx].audio;
        for (uint32_t p = 0; p < cg->nodes[idx].input_port_count; ++p) {
            if (p < a.descriptor_input_channels.size() &&
                a.descriptor_input_channels[p] > 0)
                a.input_channel_counts[p] = a.descriptor_input_channels[p];
        }
        for (uint32_t p = 0; p < cg->nodes[idx].output_port_count; ++p) {
            if (p < a.descriptor_output_channels.size() &&
                a.descriptor_output_channels[p] > 0)
                a.output_channel_counts[p] = a.descriptor_output_channels[p];
        }
    }

    // Pass 4b: Propagate via audio Direct edges in topo order
    for (uint32_t idx : cg->audio_order) {
        auto& cn = cg->nodes[idx];
        auto& a = *cn.audio;
        // Auto outputs inherit max of inputs
        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
            if (p < a.descriptor_output_channels.size() &&
                a.descriptor_output_channels[p] == 0 &&
                p < cn.output_port_types.size() &&
                cn.output_port_types[p] == VIVID_PORT_AUDIO) {
                uint8_t max_in = 1;
                for (uint32_t ip = 0; ip < cn.input_port_count; ++ip) {
                    if (a.input_channel_counts[ip] > max_in)
                        max_in = a.input_channel_counts[ip];
                }
                a.output_channel_counts[p] = max_in;
            }
        }
        // Propagate to downstream via edges
        for (const auto& e : cg->edges) {
            if (e.from_node == idx && e.transport == EdgeTransport::Direct &&
                cg->nodes[e.to_node].active_cadence == Cadence::Audio &&
                !e.targets_param) {
                auto& to_a = *cg->nodes[e.to_node].audio;
                uint8_t src_ch = 1;
                if (e.from_port < a.output_channel_counts.size())
                    src_ch = a.output_channel_counts[e.from_port];
                if (e.to_port < to_a.input_channel_counts.size() &&
                    e.to_port < to_a.descriptor_input_channels.size() &&
                    to_a.descriptor_input_channels[e.to_port] == 0 &&
                    src_ch > to_a.input_channel_counts[e.to_port]) {
                    to_a.input_channel_counts[e.to_port] = src_ch;
                }
            }
        }
    }

    // Pass 4c: Detect lane-liftable pointwise audio operators.
    // A pointwise audio operator with all-mono audio ports that receives
    // multi-channel audio input (or multi-lane spread input) is lane-lifted:
    // the runtime creates N instances and processes each lane independently.
    for (uint32_t idx : cg->audio_order) {
        auto& a = *cg->nodes[idx].audio;
        auto& cn = cg->nodes[idx];

        // Only pointwise operators are lane-lifted.
        if (cn.lane_behavior != LaneBehavior::Pointwise) continue;

        // Check that all audio ports are mono (descriptor says 1 channel).
        bool all_mono = true;
        for (uint32_t p = 0; p < cn.input_port_count && all_mono; ++p) {
            if (p < a.descriptor_input_channels.size() &&
                cn.input_port_types[p] == VIVID_PORT_AUDIO &&
                a.descriptor_input_channels[p] > 1)
                all_mono = false;
        }
        for (uint32_t p = 0; p < cn.output_port_count && all_mono; ++p) {
            if (p < a.descriptor_output_channels.size() &&
                cn.output_port_types[p] == VIVID_PORT_AUDIO &&
                a.descriptor_output_channels[p] > 1)
                all_mono = false;
        }
        if (!all_mono) continue;

        // Find max incoming audio channel count (lane lifting for stereo/multichannel).
        uint8_t max_wire_ch = 1;
        for (const auto& e : cg->edges) {
            if (e.to_node == idx && e.transport == EdgeTransport::Direct &&
                !e.targets_param) {
                uint8_t src_ch = 1;
                auto& from_a = cg->nodes[e.from_node].audio;
                if (from_a && e.from_port < from_a->output_channel_counts.size())
                    src_ch = from_a->output_channel_counts[e.from_port];
                if (src_ch > max_wire_ch) max_wire_ch = src_ch;
            }
        }

        if (max_wire_ch > 1) {
            a.lane_lift_count = max_wire_ch;
            // Use the lane_set_id from the node's resolved input lane sets if available,
            // otherwise use 0 (positional channel-based lifting).
            a.lane_lift_set_id = 0;
            for (const auto& ils : cn.input_lane_sets) {
                if (!ils.is_scalar()) { a.lane_lift_set_id = ils.lane_set_id; break; }
            }
            for (auto& ch : a.input_channel_counts) ch = 1;
            for (auto& ch : a.output_channel_counts) ch = 1;
        }

        // NOTE: Non-audio lane-bearing inputs (spread/control from structural
        // operators) do NOT yet trigger audio lane lifting. Structural outputs
        // have runtime-dynamic lane counts that can't be pre-allocated at
        // compile time. Runtime-dynamic lane lifting requires the per-lane
        // state service from Phase 5.
    }

    // ===================================================================
    // Pass 5: Audio buffer allocation
    // ===================================================================

    for (uint32_t idx : cg->audio_order) {
        auto& cn = cg->nodes[idx];
        auto& a = *cn.audio;
        uint32_t bs = options.audio_buffer_size;

        if (a.lane_lift_count > 0) {
            // Lane-lifted: allocate multi-lane buffers (one mono buffer per lane).
            uint32_t lanes = a.lane_lift_count;
            for (uint32_t p = 0; p < cn.input_port_count; ++p)
                a.buffers_in[p].resize(lanes * bs, 0.0f);
            for (uint32_t p = 0; p < cn.output_port_count; ++p)
                a.buffers_out[p].resize(lanes * bs, 0.0f);
        } else {
            for (uint32_t p = 0; p < cn.input_port_count; ++p)
                a.buffers_in[p].resize(a.input_channel_counts[p] * bs, 0.0f);
            for (uint32_t p = 0; p < cn.output_port_count; ++p)
                a.buffers_out[p].resize(a.output_channel_counts[p] * bs, 0.0f);
        }

        // Set audio buffer pointers
        for (uint32_t p = 0; p < cn.input_port_count; ++p)
            a.in_ptrs[p] = a.buffers_in[p].data();
        for (uint32_t p = 0; p < cn.output_port_count; ++p)
            a.out_ptrs[p] = a.buffers_out[p].data();
    }

    // Set from_channels/to_channels on audio Direct edges
    for (auto& e : cg->edges) {
        if (e.transport != EdgeTransport::Direct) continue;
        if (cg->nodes[e.from_node].active_cadence != Cadence::Audio) continue;
        if (e.targets_param) continue;

        auto& from_a = *cg->nodes[e.from_node].audio;
        auto& to_cn = cg->nodes[e.to_node];

        if (from_a.lane_lift_count > 0) {
            // Lane-lifted: trace back to find upstream channel count
            uint8_t ch = 1;
            for (const auto& ue : cg->edges) {
                if (ue.to_node == e.from_node && ue.transport == EdgeTransport::Direct && !ue.targets_param) {
                    uint8_t src_ch = 1;
                    auto& ue_from_a = cg->nodes[ue.from_node].audio;
                    if (ue_from_a && ue.from_port < ue_from_a->output_channel_counts.size())
                        src_ch = ue_from_a->output_channel_counts[ue.from_port];
                    if (src_ch > ch) ch = src_ch;
                }
            }
            e.from_channels = ch;
        } else if (e.from_port < from_a.output_channel_counts.size()) {
            e.from_channels = from_a.output_channel_counts[e.from_port];
        }

        if (to_cn.audio && to_cn.audio->lane_lift_count > 0) {
            e.to_channels = e.from_channels;  // lane-lifted: matches source
        } else if (to_cn.audio && e.to_port < to_cn.audio->input_channel_counts.size()) {
            e.to_channels = to_cn.audio->input_channel_counts[e.to_port];
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

    // Mark which input ports have incoming edges (for connection metadata).
    for (const auto& e : cg->edges) {
        if (!e.targets_param && e.to_node < cg->nodes.size() &&
            e.to_port < cg->nodes[e.to_node].input_connected.size())
            cg->nodes[e.to_node].input_connected[e.to_port] = 1;
    }

    if (std::getenv("VIVID_VERBOSE")) {
        std::fprintf(stderr, "[vivid] GraphCompiler: %u nodes (%zu frame, %zu audio), %zu edges (%zu snapshot)\n",
                     n,
                     cg->frame_order.size(), cg->audio_order.size(),
                     cg->edges.size(),
                     cg->frame_to_audio_edges.size() + cg->audio_to_frame_edges.size());
    }

    return cg;
}

// ---------------------------------------------------------------------------
// reload_operator — hot-reload a single operator type in-place
// ---------------------------------------------------------------------------

bool GraphCompiler::reload_operator(CompiledGraph& cg,
                                    const std::string& type_name,
                                    OperatorRegistry& registry,
                                    const std::string& new_dylib_path,
                                    const std::filesystem::path& graph_base_dir) {
    // 1. Find all CompiledNodes of this type and save their param values by name
    struct SavedParams {
        uint32_t node_idx;
        std::unordered_map<std::string, float> values;
        std::unordered_map<std::string, std::string> string_values;
        std::unordered_map<std::string, uint8_t> lock_flags;
    };
    std::vector<SavedParams> saved;

    for (uint32_t i = 0; i < static_cast<uint32_t>(cg.nodes.size()); ++i) {
        auto& cn = cg.nodes[i];
        if (!cn.loader) continue;
        const auto* desc = cn.loader->descriptor();
        if (!desc || std::string(desc->name) != type_name) continue;

        SavedParams sp;
        sp.node_idx = i;
        for (const auto& [name, idx] : cn.param_indices) {
            sp.values[name] = cn.param_values[idx];
            if (cn.param_lock_flags[idx] != PARAM_LOCK_NONE)
                sp.lock_flags[name] = cn.param_lock_flags[idx];
        }
        for (const auto& [name, idx] : cn.file_param_indices) {
            sp.string_values[name] = cn.file_param_storage[idx];
        }
        saved.push_back(std::move(sp));
    }

    if (saved.empty()) return true;  // no instances to reload

    // 2. Destroy old instances while the old dylib is still loaded
    for (const auto& sp : saved) {
        auto& cn = cg.nodes[sp.node_idx];
        if (cn.instance) {
            cn.loader->destroy_instance(cn.instance);
            cn.instance = nullptr;
        }
    }

    // 3. Reload the dylib
    if (!registry.reload_operator(type_name, new_dylib_path)) {
        std::fprintf(stderr, "[vivid] GraphCompiler: dylib reload failed for '%s'\n", type_name.c_str());
        // Old dylib is still loaded. Recreate instances using old loader so nodes keep running.
        OperatorLoader* old_loader = registry.find(type_name);
        if (old_loader && old_loader->is_loaded()) {
            const auto* old_desc = old_loader->descriptor();
            if (old_desc) {
                for (const auto& sp : saved) {
                    auto& cn = cg.nodes[sp.node_idx];
                    cn.instance = old_loader->create_instance();
                    init_frame_state(cn, old_desc, &sp.values,
                                     sp.string_values.empty() ? nullptr : &sp.string_values,
                                     graph_base_dir);
                    for (const auto& [pname, flags] : sp.lock_flags) {
                        auto pi = cn.param_indices.find(pname);
                        if (pi != cn.param_indices.end())
                            cn.param_lock_flags[pi->second] = flags;
                    }
                    cn.dirty = true;
                }
            }
        }
        return false;
    }

    // 4. Update loader pointer and recreate instances with param reconciliation
    OperatorLoader* new_loader = registry.find(type_name);
    if (!new_loader) return false;
    const auto* new_desc = new_loader->descriptor();
    if (!new_desc) return false;

    for (const auto& sp : saved) {
        auto& cn = cg.nodes[sp.node_idx];
        cn.loader = new_loader;
        cn.instance = new_loader->create_instance();
        init_frame_state(cn, new_desc, &sp.values,
                         sp.string_values.empty() ? nullptr : &sp.string_values,
                         graph_base_dir);

        // Restore lock flags
        for (const auto& [pname, flags] : sp.lock_flags) {
            auto pi = cn.param_indices.find(pname);
            if (pi != cn.param_indices.end())
                cn.param_lock_flags[pi->second] = flags;
        }

        // Clear error state on successful reload
        cn.errored = false;
        cn.error_message.clear();

        // Force downstream recompute
        cn.dirty = true;
    }

    return true;
}

} // namespace vivid
