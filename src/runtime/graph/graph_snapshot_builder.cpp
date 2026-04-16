#include "runtime/graph/graph_snapshot_builder.h"
#include "runtime/operators/operator_info_cache.h"
#include "runtime/graph/subgraph_module.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/audio/system_midi.h"
#include "runtime/control/runtime_api.h"
#include "runtime/debug/capture_coordinator.h"
#include "runtime/control/control_server.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/audio/audio_frame_bridge.h"
#include "common/string_util.h"
#include <filesystem>
#include <algorithm>

namespace vivid {

using vivid::format_float;

vivid::ui::GraphSnapshot build_graph_snapshot(
        const vivid::Graph& graph,
        const vivid::RuntimeCore& runtime,
        vivid::AudioEngine* audio_engine,
        vivid::OperatorRegistry& registry,
        OperatorInfoCache& op_cache,
        vivid::SystemMidiListener* system_midi,
        const vivid::RuntimeAPI* runtime_api,
        vivid::CaptureCoordinator* capture_coordinator,
        const vivid::ControlServer* control_server,
        const vivid::SubgraphModuleRegistry* subgraph_modules) {
    vivid::ui::GraphSnapshot snap;

    const auto* cg = runtime.compiled_graph();
    if (!cg) return snap;
    const auto& compiled_nodes = cg->nodes;
    const auto& conns = graph.connections();

    // Nodes
    snap.nodes.resize(compiled_nodes.size());
    for (size_t i = 0; i < compiled_nodes.size(); ++i) {
        const auto& cn = compiled_nodes[i];
        auto& sn = snap.nodes[i];
        sn.node_id = cn.node_id;
        sn.type_name = runtime.type_name(static_cast<uint32_t>(i));
        sn.subgraph_owner = cn.subgraph_owner;
        sn.subgraph_type = cn.subgraph_type;
        sn.is_subgraph_member = !cn.subgraph_owner.empty();
        sn.active_cadence = cn.active_cadence;
        sn.is_gpu = cn.is_gpu();
        sn.is_gpu_sink = cn.is_gpu_sink();
        sn.is_generator = cn.gpu ? cn.gpu->texture_input_port_indices.empty() && !cn.is_gpu_sink() : true;
        sn.lane_behavior = static_cast<uint8_t>(cn.lane_behavior);
        sn.input_port_indices = cn.input_port_indices;
        sn.output_port_indices = cn.output_port_indices;
        sn.analysis_output_port_indices = cn.audio ? cn.audio->analysis_output_port_indices
                                                   : std::unordered_map<std::string, uint32_t>{};
        sn.param_indices = cn.param_indices;
        sn.param_values = cn.param_values;
        sn.param_lock_flags = cn.param_lock_flags;
        sn.output_values = cn.output_values;
        sn.output_lanes.resize(cn.output_port_count);
        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
            if (p < cn.output_lane_refs.size() && cn.output_lane_refs[p])
                sn.output_lanes[p].assign(cn.output_lane_refs[p].data(),
                                           cn.output_lane_refs[p].data() + cn.output_lane_refs[p].length());
            else
                sn.output_lanes[p].clear();
        }
        sn.output_string_values = cn.output_string_values;
        sn.output_string_lanes = cn.output_string_lanes;
        for (const auto& [name, idx] : cn.file_param_indices) {
            if (idx < cn.file_param_storage.size())
                sn.file_param_values[name] = cn.file_param_storage[idx];
        }
        sn.gpu_tex_width = cn.gpu ? cn.gpu->tex_width : 0;
        sn.gpu_tex_height = cn.gpu ? cn.gpu->tex_height : 0;
        sn.gpu_tex_inherited = cn.gpu ? cn.gpu->tex_inherited : false;
        sn.errored       = cn.errored || (cn.gpu && cn.gpu->shader_error);
        sn.error_message = cn.errored                        ? cn.error_message
                         : (cn.gpu && cn.gpu->shader_error) ? cn.gpu->shader_error_msg
                         : cn.error_message;   // compile/build error (node still running)
        sn.missing_operator = cn.missing_operator;
        if (cn.missing_operator) {
            // Try to find the specific package with an ABI mismatch for this operator
            std::string pkg_name;
            for (const auto& d : registry.abi_mismatch_diagnostics()) {
                if (d.plugin_name == sn.type_name) { pkg_name = d.package_name; break; }
            }
            if (!pkg_name.empty()) {
                sn.error_message = "\"" + sn.type_name + "\" failed to load (ABI mismatch).\n"
                    "Package '" + pkg_name + "' may need rebuild.\n"
                    "Run: vivid rebuild " + pkg_name;
            } else if (registry.has_abi_mismatch_diagnostics()) {
                sn.error_message = "Operator \"" + sn.type_name + "\" not found.\n"
                    "ABI mismatch detected \xe2\x80\x94 plugins were built against a different Vivid version.\n"
                    "Run 'vivid rebuild <package>' to recompile, then reload.";
            } else {
                sn.error_message = "Operator \"" + sn.type_name + "\" not found.\n"
                    "The package providing this operator may not be installed or linked.\n"
                    "Install/link the package, then reload the graph.";
            }
        }

        // Solo state
        if (runtime.is_solo_active()) {
            const auto& solo_set = runtime.solo_active_set();
            std::string solo_id = (runtime.solo_node_idx() >= 0 &&
                                   runtime.solo_node_idx() < static_cast<int>(compiled_nodes.size()))
                                  ? compiled_nodes[runtime.solo_node_idx()].node_id : "";
            sn.soloed = (cn.node_id == solo_id);
            sn.solo_dimmed = (i < solo_set.size() && !solo_set[i]);
        }

        // Layout from graph
        const auto* ndef = graph.find_node(cn.node_id);
        if (ndef && ndef->has_layout()) {
            sn.layout_x = ndef->layout_x;
            sn.layout_y = ndef->layout_y;
            sn.has_layout = true;
        }

        // Operator info (cached)
        sn.op_info = op_cache.get(sn.type_name, registry, cn.loader);

        // Per-operator presets
        sn.preset_names = graph.list_presets(cn.node_id);
        sn.factory_preset_names = registry.factory_preset_names(sn.type_name);
        if (sn.factory_preset_names.empty() && subgraph_modules) {
            const auto* mod = subgraph_modules->find(sn.type_name);
            if (mod) {
                for (const auto& p : mod->presets)
                    sn.factory_preset_names.push_back(p.name);
            }
        }
        if (runtime_api)
            sn.active_preset = runtime_api->active_preset(cn.node_id);

        // State-preset mappings (for StateMachine nodes)
        const auto* spm = graph.find_state_mapping(cn.node_id);
        if (spm)
            sn.state_preset_map = spm->state_presets;

        // Index
        snap.node_index[cn.node_id] = i;
    }

    // -----------------------------------------------------------------------
    // Synthesize module instance nodes from authored graph
    // -----------------------------------------------------------------------
    if (subgraph_modules) {
        for (const auto& ndef : graph.nodes()) {
            const auto* mod = subgraph_modules->find(ndef.type);
            if (!mod) continue;
            // This is a module instance in the authored graph — synthesize a snapshot
            vivid::ui::NodeSnapshot msn;
            msn.node_id = ndef.id;
            msn.type_name = mod->name;
            msn.is_module_instance = true;

            // Layout from authored node
            if (ndef.has_layout()) {
                msn.layout_x = ndef.layout_x;
                msn.layout_y = ndef.layout_y;
                msn.has_layout = true;
            }

            // Operator info (rich metadata from make_operator_info)
            if (!snap.operator_catalog.count(mod->name)) {
                snap.operator_catalog[mod->name] = vivid::make_operator_info(*mod);
            }
            msn.op_info = snap.operator_catalog[mod->name];

            // Build param_indices and gather live param_values from internal nodes
            for (size_t pi = 0; pi < mod->params.size(); ++pi) {
                const auto& pb = mod->params[pi];
                msn.param_indices[pb.name] = static_cast<uint32_t>(pi);

                float live_val = 0.0f;
                std::string flat_id = ndef.id + ".__" + pb.internal_node;
                const auto* icn = cg->find_node(flat_id);

                // Check if this param targets a modulated destination.
                // If so, use the authored value (the user's base value)
                // rather than the wire-driven compiled node value.
                bool is_modulated = false;
                for (const auto& rec : runtime.modulation_records()) {
                    if (rec.instance_id == ndef.id && rec.exposed_param == pb.name) {
                        is_modulated = true;
                        break;
                    }
                }

                if (is_modulated) {
                    auto ait = ndef.params.find(pb.name);
                    if (ait != ndef.params.end()) live_val = ait->second;
                } else if (icn) {
                    auto iit = icn->param_indices.find(pb.internal_param);
                    if (iit != icn->param_indices.end() && iit->second < icn->param_values.size())
                        live_val = icn->param_values[iit->second];
                } else {
                    auto ait = ndef.params.find(pb.name);
                    if (ait != ndef.params.end()) live_val = ait->second;
                }
                msn.param_values.push_back(live_val);

                // Lock flags: read from internal compiled node, fall back to authored node
                uint8_t lock = 0;
                if (icn) {
                    auto iit = icn->param_indices.find(pb.internal_param);
                    if (iit != icn->param_indices.end() && iit->second < icn->param_lock_flags.size())
                        lock = icn->param_lock_flags[iit->second];
                }
                if (!lock) {
                    auto lit = ndef.param_lock_flags.find(pb.name);
                    if (lit != ndef.param_lock_flags.end())
                        lock = lit->second;
                }
                msn.param_lock_flags.push_back(lock);
            }

            // Gather file/string param values from internal nodes
            for (const auto& pb : mod->params) {
                std::string flat_id = ndef.id + ".__" + pb.internal_node;
                const auto* icn = cg->find_node(flat_id);
                if (icn) {
                    auto fi = icn->file_param_indices.find(pb.internal_param);
                    if (fi != icn->file_param_indices.end() && fi->second < icn->file_param_storage.size()) {
                        msn.file_param_values[pb.name] = icn->file_param_storage[fi->second];
                    }
                }
            }

            // Build port indices from module port bindings
            uint32_t in_idx = 0, out_idx = 0;
            for (const auto& port : mod->ports) {
                if (port.direction == VIVID_PORT_INPUT)
                    msn.input_port_indices[port.name] = in_idx++;
                else
                    msn.output_port_indices[port.name] = out_idx++;
            }

            // Presets
            msn.preset_names = graph.list_presets(ndef.id);
            for (const auto& p : mod->presets)
                msn.factory_preset_names.push_back(p.name);
            if (runtime_api)
                msn.active_preset = runtime_api->active_preset(ndef.id);

            // Modulation sources, destinations, and assignments
            for (const auto& s : mod->mod_sources)
                msn.mod_sources.push_back({s.name, s.description, s.shape, s.polarity, s.group});
            for (const auto& d : mod->mod_destinations)
                msn.mod_destinations.push_back({d.name, d.description, d.shape, d.group});
            if (const auto* assigns = graph.find_mod_assignments(ndef.id)) {
                for (const auto& a : *assigns)
                    msn.mod_assignments.push_back({a.source, a.destination, a.amount, a.polarity, a.curve});
            }

            snap.node_index[ndef.id] = snap.nodes.size();
            snap.nodes.push_back(std::move(msn));
        }

        // Filter out internal subgraph-member nodes (they still execute but are hidden)
        std::vector<vivid::ui::NodeSnapshot> visible;
        visible.reserve(snap.nodes.size());
        snap.node_index.clear();
        for (auto& sn : snap.nodes) {
            if (sn.is_subgraph_member) continue;
            snap.node_index[sn.node_id] = visible.size();
            visible.push_back(std::move(sn));
        }
        snap.nodes = std::move(visible);
    }

    // Connections — preserve graph truth even when an endpoint no longer resolves.
    snap.connections.reserve(conns.size());
    for (size_t i = 0; i < conns.size(); ++i) {
        bool from_is_param = false;
        bool to_is_param   = false;
        bool invalid = false;
        bool from_endpoint_missing = false;
        bool to_endpoint_missing = false;
        std::string invalid_reason;

        // Determine if source is a param (not an output port)
        auto ni_it = snap.node_index.find(conns[i].from_node);
        if (ni_it != snap.node_index.end()) {
            const auto& src = snap.nodes[ni_it->second];
            if (src.output_port_indices.count(conns[i].from_port) == 0 &&
                src.analysis_output_port_indices.count(conns[i].from_port) == 0) {
                if (src.param_indices.count(conns[i].from_port) == 0) {
                    invalid = true;
                    from_endpoint_missing = true;
                    invalid_reason = "missing source endpoint";
                } else {
                    from_is_param = true;
                    {
                        static std::unordered_set<std::string> warned;
                        std::string key = conns[i].from_node + "/" + conns[i].from_port
                                        + "->" + conns[i].to_node + "/" + conns[i].to_port + ":src";
                        if (warned.insert(key).second)
                            std::fprintf(stderr, "[vivid] snapshot: %s/%s -> %s/%s: "
                                "source '%s' not in output ports, resolved as param\n",
                                conns[i].from_node.c_str(), conns[i].from_port.c_str(),
                                conns[i].to_node.c_str(), conns[i].to_port.c_str(),
                                conns[i].from_port.c_str());
                    }
                }
            }
        } else {
            invalid = true;
            from_endpoint_missing = true;
            invalid_reason = "missing source node";
        }

        // Determine if destination is a param (not an input port)
        auto dest_it = snap.node_index.find(conns[i].to_node);
        if (dest_it != snap.node_index.end()) {
            const auto& dest = snap.nodes[dest_it->second];
            if (dest.input_port_indices.count(conns[i].to_port) == 0) {
                if (dest.param_indices.count(conns[i].to_port) == 0) {
                    invalid = true;
                    to_endpoint_missing = true;
                    if (invalid_reason.empty())
                        invalid_reason = "missing destination endpoint";
                    else
                        invalid_reason += "; missing destination endpoint";
                } else {
                    to_is_param = true;
                    {
                        static std::unordered_set<std::string> warned;
                        std::string key = conns[i].from_node + "/" + conns[i].from_port
                                        + "->" + conns[i].to_node + "/" + conns[i].to_port + ":dst";
                        if (warned.insert(key).second)
                            std::fprintf(stderr, "[vivid] snapshot: %s/%s -> %s/%s: "
                                "destination '%s' not in input ports, resolved as param\n",
                                conns[i].from_node.c_str(), conns[i].from_port.c_str(),
                                conns[i].to_node.c_str(), conns[i].to_port.c_str(),
                                conns[i].to_port.c_str());
                    }
                }
            }
        } else {
            invalid = true;
            to_endpoint_missing = true;
            if (invalid_reason.empty())
                invalid_reason = "missing destination node";
            else
                invalid_reason += "; missing destination node";
        }

        auto& c = snap.connections.emplace_back();
        c.from_node    = conns[i].from_node;
        c.from_port    = conns[i].from_port;
        c.to_node      = conns[i].to_node;
        c.to_port      = conns[i].to_port;
        c.from_min     = conns[i].from_min;
        c.from_max     = conns[i].from_max;
        c.to_min       = conns[i].to_min;
        c.to_max       = conns[i].to_max;
        c.clamp        = conns[i].clamp;
        c.from_is_param = from_is_param;
        c.to_is_param   = to_is_param;
        c.invalid = invalid;
        c.from_endpoint_missing = from_endpoint_missing;
        c.to_endpoint_missing = to_endpoint_missing;
        c.invalid_reason = invalid_reason;

        // Lane metadata from compiled edge (match by node + port)
        if (cg) {
            for (const auto& e : cg->edges) {
                if (e.from_node >= cg->nodes.size() || e.to_node >= cg->nodes.size())
                    continue;
                const auto& fn = cg->nodes[e.from_node];
                const auto& tn = cg->nodes[e.to_node];
                if (fn.node_id != conns[i].from_node || tn.node_id != conns[i].to_node)
                    continue;
                bool from_ok = false, to_ok = false;
                for (const auto& [name, idx] : fn.output_port_indices)
                    if (idx == e.from_port && name == conns[i].from_port) { from_ok = true; break; }
                if (!from_ok) {
                    for (const auto& [name, idx] : fn.param_indices)
                        if (idx == e.from_port && name == conns[i].from_port) { from_ok = true; break; }
                }
                if (e.targets_param) {
                    for (const auto& [name, idx] : tn.param_indices)
                        if (idx == e.to_port && name == conns[i].to_port) { to_ok = true; break; }
                } else {
                    for (const auto& [name, idx] : tn.input_port_indices)
                        if (idx == e.to_port && name == conns[i].to_port) { to_ok = true; break; }
                }
                if (from_ok && to_ok) {
                    c.lane_set_id = e.lane_set_id;
                    c.lane_count  = e.lane_count;
                    c.data_type   = e.data_type;
                    break;
                }
            }
        }
    }

    // Mark connections that the compiler dropped
    for (const auto& dc : cg->dropped_connections) {
        for (auto& sc : snap.connections) {
            if (sc.from_node == dc.from_node && sc.from_port == dc.from_port &&
                sc.to_node == dc.to_node && sc.to_port == dc.to_port) {
                sc.dropped = true;
                sc.invalid = true;
                sc.invalid_reason = dc.reason;
                break;
            }
        }
    }

    // Audio analysis
    if (audio_engine) {
        const auto& analysis = audio_engine->analysis_read();
        for (const auto& ns : compiled_nodes) {
            int ae_idx = audio_engine->audio_node_index(ns.node_id);
            if (ae_idx >= 0) {
                snap.audio_index[ns.node_id] = ae_idx;
            }
        }
        snap.audio_analysis.resize(analysis.waveform.size());
        for (size_t i = 0; i < analysis.waveform.size(); ++i) {
            auto& dst = snap.audio_analysis[i];
            dst.channel_count = (i < analysis.channel_counts.size())
                ? analysis.channel_counts[i] : static_cast<uint8_t>(1);
            dst.peak = (i < analysis.peak.size()) ? analysis.peak[i] : decltype(dst.peak){};
            dst.waveform = analysis.waveform[i];
        }

        snap.audio_underrun_count = audio_engine->underrun_count();
        snap.audio_underrun_active = audio_engine->last_buffer_underrun();
        snap.audio_load = audio_engine->audio_load();
        snap.audio_sample_rate = audio_engine->sample_rate();
        snap.audio_buffer_size = audio_engine->buffer_size();
        snap.audio_node_count = audio_engine->node_count();
    }

    struct AudioHotNodeRow {
        std::string node_id;
        std::string type_name;
        uint32_t last_block_total_us = 0;
        uint32_t last_process_us = 0;
        uint32_t ema_block_us = 0;
        float last_block_budget_pct = 0.0f;
        uint32_t last_lane_count = 0;
        uint32_t lane_state_entries = 0;
    };
    std::vector<AudioHotNodeRow> audio_rows;
    audio_rows.reserve(cg->audio_order.size());
    for (uint32_t idx : cg->audio_order) {
        const auto& ns = compiled_nodes[idx];
        if (!ns.audio) continue;
        auto dbg = read_audio_node_debug(*ns.audio);
        if (!dbg.valid) continue;
        audio_rows.push_back({
            ns.node_id,
            ns.type_name,
            dbg.last_block_total_us,
            dbg.last_process_us,
            dbg.ema_block_us,
            dbg.last_block_budget_pct,
            dbg.last_lane_count,
            dbg.lane_state_entries,
        });
    }
    auto copy_row = [](const AudioHotNodeRow& row) {
        vivid::ui::AudioHotNodeSnapshot snap_row;
        snap_row.node_id = row.node_id;
        snap_row.type_name = row.type_name;
        snap_row.last_block_total_us = row.last_block_total_us;
        snap_row.last_process_us = row.last_process_us;
        snap_row.ema_block_us = row.ema_block_us;
        snap_row.last_block_budget_pct = row.last_block_budget_pct;
        snap_row.last_lane_count = row.last_lane_count;
        snap_row.lane_state_entries = row.lane_state_entries;
        return snap_row;
    };
    auto by_hotness = audio_rows;
    std::sort(by_hotness.begin(), by_hotness.end(), [](const AudioHotNodeRow& a,
                                                       const AudioHotNodeRow& b) {
        if (a.ema_block_us != b.ema_block_us) return a.ema_block_us > b.ema_block_us;
        if (a.last_block_total_us != b.last_block_total_us) return a.last_block_total_us > b.last_block_total_us;
        return a.node_id < b.node_id;
    });
    for (size_t i = 0; i < by_hotness.size() && i < 5; ++i)
        snap.audio_top_nodes.push_back(copy_row(by_hotness[i]));

    auto by_lane_state = audio_rows;
    std::sort(by_lane_state.begin(), by_lane_state.end(), [](const AudioHotNodeRow& a,
                                                             const AudioHotNodeRow& b) {
        if (a.lane_state_entries != b.lane_state_entries) return a.lane_state_entries > b.lane_state_entries;
        if (a.last_lane_count != b.last_lane_count) return a.last_lane_count > b.last_lane_count;
        return a.node_id < b.node_id;
    });
    for (size_t i = 0; i < by_lane_state.size() && i < 5; ++i)
        snap.audio_top_lane_state_nodes.push_back(copy_row(by_lane_state[i]));

    // Operator catalog
    snap.operator_types = registry.type_names();
    // Include subgraph module types in the catalog
    if (subgraph_modules) {
        for (const auto& mt : subgraph_modules->type_names())
            snap.operator_types.push_back(mt);
    }
    std::sort(snap.operator_types.begin(), snap.operator_types.end());
    for (const auto& tn : snap.operator_types) {
        // Try operator registry first, then subgraph modules
        auto info = op_cache.get(tn, registry);
        if (info) {
            snap.operator_catalog[tn] = info;
        } else if (subgraph_modules) {
            const auto* mod = subgraph_modules->find(tn);
            if (mod) snap.operator_catalog[tn] = vivid::make_operator_info(*mod);
        }
    }

    // MIDI mappings
    const auto& mappings = graph.midi_mappings();
    snap.midi_mappings.resize(mappings.size());
    for (size_t i = 0; i < mappings.size(); ++i) {
        auto& sm = snap.midi_mappings[i];
        const auto& gm = mappings[i];
        sm.node_id = gm.node_id;
        sm.param_name = gm.param_name;
        sm.cc_number = gm.cc_number;
        sm.channel = gm.channel;
        sm.range_min = gm.range_min;
        sm.range_max = gm.range_max;
        snap.midi_mapping_index[gm.node_id + "\t" + gm.param_name] = i;
    }

    // Pending CC events from system MIDI listener
    if (system_midi) {
        const auto& events = system_midi->last_drained_events();
        snap.pending_cc_events.resize(events.size());
        for (size_t i = 0; i < events.size(); ++i) {
            snap.pending_cc_events[i] = {events[i].channel, events[i].cc_number, events[i].value};
        }
    }

    // Variations
    const auto& vars = graph.variations();
    snap.variations.resize(vars.size());
    for (size_t i = 0; i < vars.size(); ++i) {
        snap.variations[i].name = vars[i].name;
    }
    snap.active_variation = graph.active_variation();
    snap.quantize_clock_node = graph.quantize_clock_node();
    const auto metronome = runtime_api
        ? runtime_api->current_metronome_sample()
        : sample_graph_metronome(graph.metronome(), runtime.last_tick_time());
    snap.metronome_bpm = metronome.bpm;
    snap.metronome_beats_per_bar = metronome.beats_per_bar;
    snap.metronome_beat_phase = metronome.beat_phase;
    snap.metronome_bar_phase = metronome.bar_phase;
    snap.metronome_beat_ms = metronome.beat_ms;

    // Sticky notes
    const auto& sticky = graph.sticky_notes();
    snap.sticky_notes.resize(sticky.size());
    for (size_t i = 0; i < sticky.size(); ++i) {
        auto& ss = snap.sticky_notes[i];
        const auto& gs = sticky[i];
        ss.id = gs.id;
        ss.text = gs.text;
        ss.x = gs.x;
        ss.y = gs.y;
        ss.width = gs.width;
        ss.height = gs.height;
        ss.color = gs.color;
    }
    if (runtime_api) {
        snap.variation_dirty = runtime_api->variation_dirty();
        snap.graph_dirty = runtime_api->graph_dirty();
        snap.queued_variation = runtime_api->pending_variation_idx();
    }

    // Solo state
    if (runtime.is_solo_active() && runtime_api)
        snap.solo_node_id = runtime_api->solo_node_id();

    // Recording state
    if (capture_coordinator) {
        snap.is_recording = capture_coordinator->is_recording();
        if (snap.is_recording) {
            snap.recording_frame_count = capture_coordinator->recording_frame_count();
            snap.recording_duration_sec = capture_coordinator->recording_duration_sec();
        }
    }

    // MCP server ping timestamps
    if (control_server) {
        snap.mcp_main_last_ping_ms  = control_server->mcp_last_ping_ms("vivid");
        snap.mcp_opdev_last_ping_ms = control_server->mcp_last_ping_ms("opdev");
    }

    return snap;
}

} // namespace vivid
