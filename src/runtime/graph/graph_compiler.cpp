#include "runtime/graph/graph_compiler.h"
#include "runtime/graph/graph_compiler_internal.h"
#include "runtime/core/crash_guard.h"
#include "runtime/core/shared_handle_registry.h"
#include "common/topo_sort.h"
#include "operator_api/type_id.h"
#include "operator_api/port_type_registry.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace vivid {

// ---------------------------------------------------------------------------
// GraphCompiler::compile()
//
// Transforms a Graph (pure data model) + OperatorRegistry into a CompiledGraph
// (live execution state) via 7 passes:
//
//   Pass 1:   Create CompiledNodes — instantiate operators, determine cadence
//   Pass 2:   Resolve edges — connections become CompiledEdges with port indices
//   Pass 2.6: Lane-set propagation — walk topo order assigning lane provenance
//   Pass 3:   Topological sort — produce frame_order and audio_order
//   Pass 4:   Audio channel negotiation — explicit, propagated, then planner
//   Pass 5:   Audio buffer allocation — pre-allocate per-node planar buffers
//   Pass 6:   Partition edges — separate into frame Direct, audio Direct, Snapshot
//   Pass 7:   Finalize — error summary, diagnostics
//
// The resulting CompiledGraph is shared read-only by FrameExecutor and
// AudioExecutor. It is never mutated during execution — any topology change
// triggers a full recompile.
// ---------------------------------------------------------------------------

std::unique_ptr<CompiledGraph> GraphCompiler::compile(
    const Graph& graph,
    OperatorRegistry& registry,
    const Options& options)
{
    auto cg = std::make_unique<CompiledGraph>();
    cg->max_loop_lanes = options.max_loop_lanes;
    cg->audio_buffer_size = options.audio_buffer_size;
    cg->audio_sample_rate = options.audio_sample_rate;
    cg->metronome = graph.metronome();
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
        // Safe-mode / crash-recovery: skip loader lookup for disabled or
        // quarantined nodes so the placeholder path below runs with the right
        // reason.  "disabled" wins over "quarantined" when a type is in both.
        const bool is_disabled =
            options.disabled_node_ids.count(ndef.id) != 0 ||
            options.disabled_types.count(ndef.type)  != 0;
        const bool is_quarantined =
            !is_disabled &&
            options.quarantined_types.count(ndef.type) != 0;

        OperatorLoader* loader = (is_disabled || is_quarantined)
                                     ? nullptr
                                     : registry.find(ndef.type);
        const VividOperatorDescriptor* desc = loader ? loader->descriptor() : nullptr;

        CompiledNode cn;
        cn.node_id = ndef.id;
        cn.type_name = ndef.type;
        cn.subgraph_owner = ndef.subgraph_owner;
        cn.subgraph_type = ndef.subgraph_type;
        cn.loader = loader;
        cn.owned_loader = nullptr;

        if (loader && desc) {
            cn.instance = loader->create_instance();

            // Determine cadence from descriptor.
            const bool has_audio = desc->has_process_audio != 0;
            const bool has_gpu = desc->has_process_gpu != 0;
            const bool has_frame = desc->has_process_frame != 0;
            const bool mixed_audio_frame = has_audio && (has_gpu || has_frame);

            if (has_gpu) {
                cn.active_cadence = Cadence::Frame;
                cn.gpu = std::make_unique<GpuNodeState>();
            } else if (has_audio && !has_frame) {
                cn.active_cadence = Cadence::Audio;
            } else {
                cn.active_cadence = Cadence::Frame;
            }
            cn.lane_behavior = static_cast<LaneBehavior>(desc->lane_behavior);
            cn.operator_kind = vivid_operator_kind(desc);

            // Allocate audio sub-struct before init_frame_state (which uses it
            // for analysis port indices), but defer full audio init until after
            // init_frame_state sets up port counts.
            if (has_audio) {
                cn.audio = std::make_unique<AudioNodeState>();
                cn.audio_instance = mixed_audio_frame ? loader->create_instance() : cn.instance;
            }

            // Initialize frame-side state (all nodes get this — sets port counts)
            init_frame_state(cn, desc, &ndef.params,
                             ndef.string_params.empty() ? nullptr : &ndef.string_params,
                             graph_base_dir);

            // Initialize audio-specific state (after frame state sets port counts)
            if (cn.audio) {
                init_audio_state(cn, desc, options.audio_buffer_size);
            }

            graph_compiler_internal::warm_up_instance_assets(cn);

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

            // Classify why the operator is missing.  "disabled" and
            // "quarantined" both win over provenance so explicit safe-mode
            // intent or crash-history evidence is not masked by a pre-existing
            // package-load failure.  Disabled is checked first so it wins over
            // quarantined when a type happens to be in both sets.
            if (is_disabled) {
                cn.missing_operator_reason = "disabled";
                cn.missing_operator_detail = "Disabled by safe mode (crash recovery)";
            } else if (is_quarantined) {
                cn.missing_operator_reason = "quarantined";
                cn.missing_operator_detail = "Quarantined after repeated crashes";
            } else if (const auto* prov = registry.operator_provenance(ndef.type)) {
                if (!prov->package_built) {
                    cn.missing_operator_reason = "not_built";
                    cn.missing_operator_detail = "Package '" + prov->package_name +
                        "' found but not built. Run 'vivid rebuild " + prov->package_name + "'.";
                } else if (prov->abi_mismatch) {
                    cn.missing_operator_reason = "abi_mismatch";
                    cn.missing_operator_detail = prov->failure_detail;
                } else if (prov->load_failed) {
                    cn.missing_operator_reason = "load_failed";
                    cn.missing_operator_detail = prov->failure_detail;
                } else {
                    cn.missing_operator_reason = "load_failed";
                    cn.missing_operator_detail = "Package '" + prov->package_name +
                        "' is installed but operator '" + ndef.type + "' failed to load.";
                }
            } else {
                cn.missing_operator_reason = "not_found";
                cn.missing_operator_detail = "No installed package provides operator '" + ndef.type + "'.";
            }

            const auto& in_names = incoming_ports[ndef.id];
            const auto& out_names = outgoing_ports[ndef.id];
            cn.input_port_count = static_cast<uint32_t>(in_names.size());
            cn.output_port_count = static_cast<uint32_t>(out_names.size());
            for (uint32_t i = 0; i < cn.input_port_count; ++i)
                cn.input_port_indices[in_names[i]] = i;
            for (uint32_t i = 0; i < cn.output_port_count; ++i)
                cn.output_port_indices[out_names[i]] = i;
            cn.input_port_types.assign(cn.input_port_count, VIVID_PORT_SCALAR);
            cn.output_port_types.assign(cn.output_port_count, VIVID_PORT_SCALAR);
            cn.input_values.assign(cn.input_port_count, 0.0f);
            cn.bridge_input_values.assign(cn.input_port_count, 0.0f);
            cn.bridge_input_dirty.assign(cn.input_port_count, 0);
            cn.input_connected.assign(cn.input_port_count, 0);
            cn.output_values.assign(cn.output_port_count, 0.0f);
            cn.input_string_values.assign(cn.input_port_count, "");
            cn.output_string_values.assign(cn.output_port_count, "");
            cn.c_input_string_values.assign(cn.input_port_count, nullptr);
            cn.c_output_string_values.assign(cn.output_port_count, nullptr);
            cn.input_lanes.resize(cn.input_port_count);
            cn.output_lanes.resize(cn.output_port_count);
            cn.input_string_lanes.resize(cn.input_port_count);
            cn.output_string_lanes.resize(cn.output_port_count);
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

            cn.input_lane_refs.resize(cn.input_port_count);
            cn.output_lane_refs.resize(cn.output_port_count);

            cn.c_in_lane_views.resize(cn.input_port_count, VividLaneView{});
            cn.out_lane_bufs.clear();
            cn.out_lane_bufs.reserve(cn.output_port_count);
            for (uint32_t p = 0; p < cn.output_port_count; ++p)
                cn.out_lane_bufs.emplace_back(graph_compiler_internal::kDefaultLaneCapacity);
            cn.c_out_lane_outputs.resize(cn.output_port_count);
            for (uint32_t p = 0; p < cn.output_port_count; ++p)
                cn.c_out_lane_outputs[p] = make_lane_output(&cn.out_lane_bufs[p]);

            cn.c_in_string_lane_views.resize(cn.input_port_count, VividStringLaneView{});
            cn.in_string_lane_ptrs.resize(cn.input_port_count);
            for (uint32_t p = 0; p < cn.input_port_count; ++p)
                cn.in_string_lane_ptrs[p].resize(graph_compiler_internal::kDefaultLaneCapacity, nullptr);
            cn.out_string_lane_bufs.resize(cn.output_port_count, StringLaneBuffer(graph_compiler_internal::kDefaultLaneCapacity));
            cn.c_out_string_lane_outputs.resize(cn.output_port_count);
            for (uint32_t p = 0; p < cn.output_port_count; ++p)
                cn.c_out_string_lane_outputs[p] = make_string_lane_output(&cn.out_string_lane_bufs[p]);

            if (is_disabled) {
                std::fprintf(stderr,
                             "[vivid] GraphCompiler: node '%s' (type '%s') disabled by safe mode — placeholder\n",
                             ndef.id.c_str(), ndef.type.c_str());
            } else if (is_quarantined) {
                std::fprintf(stderr,
                             "[vivid] GraphCompiler: node '%s' (type '%s') quarantined after repeated crashes — placeholder\n",
                             ndef.id.c_str(), ndef.type.c_str());
            } else {
                std::fprintf(stderr,
                             "[vivid] GraphCompiler: missing operator '%s' (node '%s') — placeholder\n",
                             ndef.type.c_str(), ndef.id.c_str());
            }
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
        VividPortType from_port_type = VIVID_PORT_SCALAR;
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
                ? VIVID_PORT_STRING : VIVID_PORT_SCALAR;
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
        e.curve    = static_cast<RemapCurve>(conn.curve);
        e.bridge_kind = graph_compiler_internal::parse_bridge_kind(conn.bridge);
        if (!conn.bridge.empty() && e.bridge_kind == BridgeKind::None) {
            std::fprintf(stderr, "[vivid] warning: unknown bridge kind '%s' on connection %s/%s → %s/%s\n",
                         conn.bridge.c_str(),
                         conn.from_node.c_str(), conn.from_port.c_str(),
                         conn.to_node.c_str(), conn.to_port.c_str());
        }

        // Determine destination port
        auto tp_it = to_cn.input_port_indices.find(conn.to_port);
        if (tp_it != to_cn.input_port_indices.end()) {
            e.to_port = tp_it->second;
            e.targets_param = false;

            VividPortType to_port_type = VIVID_PORT_SCALAR;
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
                } else if (from_port_type == VIVID_PORT_STRING_LANES &&
                           to_port_type == VIVID_PORT_STRING_LANES) {
                    e.data_type = VIVID_PORT_STRING_LANES;
                } else if (from_port_type == VIVID_PORT_TEXTURE &&
                           to_port_type == VIVID_PORT_TEXTURE) {
                    e.data_type = VIVID_PORT_TEXTURE;
                } else if (from_port_type == VIVID_PORT_LANE_ARRAY ||
                           to_port_type == VIVID_PORT_LANE_ARRAY) {
                    // LANE_ARRAY on either end → treat as lane edge
                    // (SIGNAL↔SPREAD is compatible for control types)
                    e.data_type = VIVID_PORT_LANE_ARRAY;
                } else if (vivid_is_custom_port_type(from_port_type) &&
                           vivid_is_custom_port_type(to_port_type) &&
                           from_port_type == to_port_type) {
                    e.data_type = from_port_type;
                    e.custom_type_id = from_port_type;
                    e.port_transport = from_port_transport;
                    e.custom_payload_size = from_payload_size;
                } else if (from_port_type == VIVID_PORT_STRING ||
                           from_port_type == VIVID_PORT_STRING_LANES ||
                           to_port_type == VIVID_PORT_STRING ||
                           to_port_type == VIVID_PORT_STRING_LANES) {
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
        const bool audio_edge = e.data_type == VIVID_PORT_AUDIO_BUFFER;
        const bool direct_audio_edge = audio_edge && from_cn.audio && to_cn.audio;
        const bool direct_frame_edge = !audio_edge && from_cn.active_cadence == to_cn.active_cadence;
        if (direct_audio_edge || direct_frame_edge) {
            e.transport = EdgeTransport::Direct;
        } else {
            e.transport = EdgeTransport::Snapshot;
        }

        // Enforce explicit bridge rules (use raw bridge string to catch typos too)
        bool has_bridge = conn.has_bridge();
        if (e.transport == EdgeTransport::Snapshot && !has_bridge) {
            drop_connection(conn, "cross-cadence connection requires explicit bridge");
            continue;
        }
        if (e.transport == EdgeTransport::Direct && has_bridge) {
            drop_connection(conn, "same-cadence connection must not have bridge");
            continue;
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
                std::string resolved_src_node;
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
                        resolved_src_node     = cg->nodes[e.from_node].node_id;
                    } else if (src_ls.lane_set_id != resolved_lane_set_id) {
                        lane_mismatch = true;
                        if (mismatch_src_a.empty())
                            mismatch_src_a = resolved_src_node;
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
        if (cg->nodes[i].audio) {
            cg->audio_order.push_back(i);
        }
        if (cg->nodes[i].active_cadence != Cadence::Audio) {
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
        // Auto outputs inherit max of inputs.
        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
            if (p < a.descriptor_output_channels.size() &&
                a.descriptor_output_channels[p] == 0 &&
                p < cn.output_port_types.size() &&
                cn.output_port_types[p] == VIVID_PORT_AUDIO_BUFFER) {
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
                cg->nodes[e.to_node].audio &&
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

    // Pass 4c: Apply audio lane execution strategy via planner.
    for (uint32_t idx : cg->audio_order) {
        auto& a = *cg->nodes[idx].audio;
        auto& cn = cg->nodes[idx];

        auto plan = graph_compiler_internal::plan_audio_lane_strategy(cn, a, *cg, idx);
        a.execution_strategy = plan.strategy;
        a.lane_lift_count = plan.lane_lift_count;
        a.lane_lift_set_id = plan.lane_lift_set_id;
        a.lane_id_port = plan.lane_id_port;
        if (plan.override_channel_counts) {
            for (auto& ch : a.input_channel_counts) ch = 1;
            for (auto& ch : a.output_channel_counts) ch = 1;
        }
    }

    // Pass 4c.1: Re-propagate effective wire width after lane execution planning.
    //
    // InstancePerLane and LoopBased nodes process one lane at a time internally,
    // but their direct audio edges still represent a multi-lane stream on the
    // wire. Rebuild auto-channel counts from that effective wire width so
    // downstream reduction consumers (for example VoiceMixer) see the full lane
    // bundle instead of the mono-per-lane processing width.
    for (uint32_t idx : cg->audio_order) {
        auto& cn = cg->nodes[idx];
        auto& a = *cn.audio;

        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
            if (p < a.descriptor_output_channels.size() &&
                a.descriptor_output_channels[p] == 0 &&
                p < cn.output_port_types.size() &&
                cn.output_port_types[p] == VIVID_PORT_AUDIO_BUFFER) {
                uint8_t max_in = 1;
                for (uint32_t ip = 0; ip < cn.input_port_count; ++ip) {
                    if (a.input_channel_counts[ip] > max_in)
                        max_in = a.input_channel_counts[ip];
                }
                a.output_channel_counts[p] = max_in;
            }
        }

        for (const auto& e : cg->edges) {
            if (e.from_node != idx || e.transport != EdgeTransport::Direct ||
                e.targets_param || !cg->nodes[e.to_node].audio) {
                continue;
            }

            auto& to_cn = cg->nodes[e.to_node];
            if (!to_cn.audio) continue;
            auto& to_a = *to_cn.audio;

            uint8_t src_ch = graph_compiler_internal::effective_audio_output_channels(
                cn, a, e.from_port, options.max_loop_lanes);
            if (e.to_port < to_a.input_channel_counts.size() &&
                e.to_port < to_a.descriptor_input_channels.size() &&
                to_a.descriptor_input_channels[e.to_port] == 0 &&
                src_ch > to_a.input_channel_counts[e.to_port]) {
                to_a.input_channel_counts[e.to_port] = src_ch;
            }
        }
    }

    // Pass 4d: Apply frame lane execution strategy via planner.
    for (uint32_t idx : cg->frame_order) {
        auto& cn = cg->nodes[idx];
        auto plan = graph_compiler_internal::plan_frame_lane_strategy(cn);
        cn.frame_execution_strategy = plan.strategy;
        cn.frame_lane_id_port = plan.lane_id_port;
    }

    // Pass 4e: GPU lane promotion analysis (Phase 4).
    graph_compiler_internal::plan_gpu_lane_promotion(*cg);

    // ===================================================================
    // Pass 5: Audio buffer allocation
    // ===================================================================

    for (uint32_t idx : cg->audio_order) {
        auto& cn = cg->nodes[idx];
        auto& a = *cn.audio;

        for (uint32_t p = 0; p < cn.input_port_count; ++p) {
            a.debug_input_channel_counts[p] =
                graph_compiler_internal::effective_audio_input_channels(
                    cn, a, p, options.max_loop_lanes);
        }
        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
            a.debug_output_channel_counts[p] =
                graph_compiler_internal::effective_audio_output_channels(
                    cn, a, p, options.max_loop_lanes);
        }

        uint32_t bs = options.audio_buffer_size;

        if (a.execution_strategy == LaneExecutionStrategy::InstancePerLane) {
            // Instance-per-lane: allocate multi-lane buffers (one mono buffer per lane).
            uint32_t lanes = a.lane_lift_count;
            for (uint32_t p = 0; p < cn.input_port_count; ++p)
                a.buffers_in[p].resize(lanes * bs, 0.0f);
            for (uint32_t p = 0; p < cn.output_port_count; ++p)
                a.buffers_out[p].resize(lanes * bs, 0.0f);
        } else if (a.execution_strategy == LaneExecutionStrategy::LoopBased) {
            // LoopBased: pre-allocate at max lane capacity to avoid audio-thread allocation.
            uint32_t max_ll = options.max_loop_lanes;
            for (uint32_t p = 0; p < cn.input_port_count; ++p)
                a.buffers_in[p].resize(max_ll * bs, 0.0f);
            for (uint32_t p = 0; p < cn.output_port_count; ++p)
                a.buffers_out[p].resize(max_ll * bs, 0.0f);
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
        if (e.data_type != VIVID_PORT_AUDIO_BUFFER || !cg->nodes[e.from_node].audio) continue;
        if (e.targets_param) continue;

        auto& from_a = *cg->nodes[e.from_node].audio;
        auto& to_cn = cg->nodes[e.to_node];

        e.from_channels = graph_compiler_internal::effective_audio_output_channels(
            cg->nodes[e.from_node], from_a, e.from_port, options.max_loop_lanes);

        if (to_cn.audio) {
            e.to_channels = graph_compiler_internal::effective_audio_input_channels(
                to_cn, *to_cn.audio, e.to_port, options.max_loop_lanes);
        }
    }

    // ===================================================================
    // Pass 6: Partition edges
    // ===================================================================

    for (uint32_t ei = 0; ei < static_cast<uint32_t>(cg->edges.size()); ++ei) {
        const auto& e = cg->edges[ei];
        if (e.transport == EdgeTransport::Direct) {
            if (!e.sources_param && !e.targets_param &&
                cg->nodes[e.from_node].audio && cg->nodes[e.to_node].audio)
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

} // namespace vivid
