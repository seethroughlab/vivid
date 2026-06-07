#include "runtime/graph/graph_compiler.h"
#include "runtime/graph/graph_compiler_internal.h"
#include "runtime/graph/value_output_adapter.h"  // make_value_output (Phase 4a)

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace vivid {

namespace graph_compiler_internal {

void warm_up_instance_assets(CompiledNode& cn) {
    if (!cn.loader || !cn.instance || !cn.loader->has_prepare_instance_assets()) return;
    cn.loader->prepare_instance_assets(
        cn.instance,
        cn.param_values.empty() ? nullptr : cn.param_values.data(),
        cn.file_param_ptrs.empty() ? nullptr : cn.file_param_ptrs.data(),
        static_cast<uint32_t>(cn.file_param_ptrs.size()));
    if (cn.audio_instance && cn.audio_instance != cn.instance) {
        cn.loader->prepare_instance_assets(
            cn.audio_instance,
            cn.param_values.empty() ? nullptr : cn.param_values.data(),
            cn.file_param_ptrs.empty() ? nullptr : cn.file_param_ptrs.data(),
            static_cast<uint32_t>(cn.file_param_ptrs.size()));
    }
}

} // namespace graph_compiler_internal

void GraphCompiler::init_frame_state(CompiledNode& cn,
                                     const VividOperatorDescriptor* desc,
                                     const std::unordered_map<std::string, float>* param_overrides,
                                     const std::unordered_map<std::string, std::string>* string_overrides,
                                     const std::filesystem::path& graph_base_dir) {
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
            cn.input_port_multiplicities.push_back(
                graph_compiler_internal::port_declared_multiplicity(desc->ports[i]));
        } else {
            cn.output_port_indices[desc->ports[i].name] = cn.output_port_count++;
            cn.output_port_types.push_back(desc->ports[i].type);
            cn.output_port_multiplicities.push_back(
                graph_compiler_internal::port_declared_multiplicity(desc->ports[i]));
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
    cn.input_lanes.resize(cn.input_port_count);
    cn.output_lanes.resize(cn.output_port_count);
    cn.input_string_lanes.resize(cn.input_port_count);
    cn.output_string_lanes.resize(cn.output_port_count);

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

    cn.time_dependent = desc->time_dependent != 0;
    bool node_is_gpu = (desc->has_process_gpu != 0);

    if (cn.audio) {
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

    // Collect output ports tagged VIVID_PORT_DISPLAY_ADVANCED. The inspector
    // hides these on the node body unless connected — used by synth voice_*
    // breakouts and similar power-user surfaces. Populated for every node
    // kind because this is purely a UI affordance.
    cn.advanced_output_port_indices.clear();
    for (uint32_t i = 0; i < desc->port_count; ++i) {
        const auto& p = desc->ports[i];
        if (p.direction == VIVID_PORT_OUTPUT &&
            p.display_hint == VIVID_PORT_DISPLAY_ADVANCED) {
            auto it = cn.output_port_indices.find(p.name);
            if (it != cn.output_port_indices.end())
                cn.advanced_output_port_indices[p.name] = it->second;
        }
    }

    if (node_is_gpu && cn.gpu) {
        auto inject = [&](const char* name) -> uint32_t {
            uint32_t idx = cn.output_port_count++;
            cn.output_port_indices[name] = idx;
            cn.output_port_types.push_back(VIVID_PORT_SCALAR);
            cn.output_port_multiplicities.push_back(VIVID_MULTIPLICITY_SCALAR);
            return idx;
        };
        cn.gpu->analysis_frame_hash_idx = inject("frame_hash");
        cn.gpu->analysis_brightness_idx = inject("brightness");
        cn.gpu->analysis_contrast_idx = inject("contrast");
        cn.gpu->analysis_dominant_hue_idx = inject("dominant_hue");

        cn.output_values.resize(cn.output_port_count, 0.0f);
        cn.output_string_values.resize(cn.output_port_count, "");
        cn.c_output_string_values.resize(cn.output_port_count, nullptr);
        cn.output_lanes.resize(cn.output_port_count);
        cn.output_string_lanes.resize(cn.output_port_count);
    }

    cn.input_lane_sets.resize(cn.input_port_count);
    cn.output_lane_sets.resize(cn.output_port_count);

    cn.input_lane_refs.resize(cn.input_port_count);
    cn.output_lane_refs.resize(cn.output_port_count);

    cn.c_in_lane_views.resize(cn.input_port_count, VividLaneView{});

    // Legacy lane output buffers — kept allocated as the 7a shim source for the
    // bridge (output readback copies out_value_bufs → out_lane_bufs → output_lane_refs).
    cn.out_lane_bufs.clear();
    cn.out_lane_bufs.reserve(cn.output_port_count);
    for (uint32_t p = 0; p < cn.output_port_count; ++p)
        cn.out_lane_bufs.emplace_back(graph_compiler_internal::kDefaultLaneCapacity);

    // Native value transport (lane-value clean-break Phase 7a). Node-local
    // out_value_bufs (pool_owned=false → ensure() grows on the frame thread).
    cn.input_value_refs.resize(cn.input_port_count);
    cn.output_value_refs.resize(cn.output_port_count);
    cn.out_value_bufs.clear();
    cn.out_value_bufs.reserve(cn.output_port_count);
    for (uint32_t p = 0; p < cn.output_port_count; ++p)
        cn.out_value_bufs.emplace_back(VIVID_VALUE_FLOAT,
                                       graph_compiler_internal::kDefaultLaneCapacity);

    // Output adapters back onto out_value_bufs for ALL nodes (Phase 7b — both the
    // frame and audio executors now run on the value substrate). Audio-block output
    // ports are re-pointed per-tick by populate_audio_value_views to the runtime
    // output_buffers; lane-array/scalar outputs flow through out_value_bufs.
    cn.c_out_lane_outputs.resize(cn.output_port_count);
    for (uint32_t p = 0; p < cn.output_port_count; ++p)
        cn.c_out_lane_outputs[p] = make_lane_output(&cn.out_value_bufs[p]);

    // Value-model staging (Phase 4a/4b). Inputs populated per-tick; outputs
    // backed by the SAME transport the lane path uses (float→out_lane_bufs,
    // string→out_string_lane_bufs) so value-API + lane-API operators interoperate.
    cn.c_in_value_views.resize(cn.input_port_count, VividValueView{});
    cn.c_out_value_outputs.resize(cn.output_port_count);

    cn.c_in_string_lane_views.resize(cn.input_port_count, VividStringLaneView{});
    cn.in_string_lane_ptrs.resize(cn.input_port_count);
    for (uint32_t p = 0; p < cn.input_port_count; ++p)
        cn.in_string_lane_ptrs[p].resize(graph_compiler_internal::kDefaultLaneCapacity, nullptr);
    cn.out_string_lane_bufs.resize(cn.output_port_count, StringLaneBuffer(graph_compiler_internal::kDefaultLaneCapacity));
    cn.c_out_string_lane_outputs.resize(cn.output_port_count);
    for (uint32_t p = 0; p < cn.output_port_count; ++p)
        cn.c_out_string_lane_outputs[p] = make_string_lane_output(&cn.out_string_lane_bufs[p]);

    // Per-port value-output backing (needs out_string_lane_bufs to exist first).
    for (uint32_t p = 0; p < cn.output_port_count; ++p) {
        const VividPortType t = (p < cn.output_port_types.size())
            ? cn.output_port_types[p] : VIVID_PORT_SCALAR;
        cn.c_out_value_outputs[p] =
            (t == VIVID_PORT_STRING || t == VIVID_PORT_STRING_LANES)
                ? make_string_value_output(&cn.out_string_lane_bufs[p])
                : make_value_output(&cn.out_value_bufs[p]);
    }

    cn.file_param_storage.clear();
    cn.file_param_ptrs.clear();
    cn.file_param_write_ptrs.clear();
    cn.file_param_indices.clear();
    cn.file_param_is_path.clear();
    cn.file_param_persist.clear();
    for (uint32_t i = 0; i < desc->param_count; ++i) {
        if (desc->params[i].type == VIVID_PARAM_FILE ||
            desc->params[i].type == VIVID_PARAM_TEXT) {
            uint32_t fidx = static_cast<uint32_t>(cn.file_param_storage.size());
            cn.file_param_indices[desc->params[i].name] = fidx;
            const char* def = desc->params[i].default_string;
            cn.file_param_storage.push_back(def ? def : "");
            cn.file_param_is_path.push_back(desc->params[i].type == VIVID_PARAM_FILE ? 1 : 0);
            // Transient params (runtime-computed catalogs/scratch) are not saved.
            cn.file_param_persist.push_back(
                desc->params[i].display_hint == VIVID_DISPLAY_TRANSIENT ? 0 : 1);
        }
    }
    if (string_overrides) {
        for (const auto& [pname, pval] : *string_overrides) {
            auto fi = cn.file_param_indices.find(pname);
            if (fi != cn.file_param_indices.end())
                cn.file_param_storage[fi->second] = pval;
        }
    }
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
    cn.file_param_write_ptrs.resize(cn.file_param_storage.size());
    for (size_t i = 0; i < cn.file_param_storage.size(); ++i) {
        cn.file_param_ptrs[i]       = cn.file_param_storage[i].c_str();
        cn.file_param_write_ptrs[i] = &cn.file_param_storage[i];
    }

    cn.custom_input_port_indices.clear();
    cn.custom_output_port_indices.clear();
    cn.string_input_port_indices.clear();
    cn.string_lane_input_port_indices.clear();
    cn.has_string_output = false;
    cn.has_string_lane_output = false;

    if (node_is_gpu) {
        if (!cn.gpu) cn.gpu = std::make_unique<GpuNodeState>();
        cn.gpu->texture_input_port_indices.clear();
        cn.gpu->is_sink = false;
        cn.gpu->has_texture_output = false;
        cn.gpu->aux_texture_output_port_indices.clear();
        cn.gpu->aux_gpu_textures.clear();
        cn.gpu->aux_gpu_texture_views.clear();
        cn.gpu->aux_texture_format_hints.clear();
    }

    uint32_t input_idx = 0, out_idx = 0, gpu_tex_out_count = 0;
    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_INPUT) {
            switch (desc->ports[i].type) {
                case VIVID_PORT_TEXTURE:
                    if (cn.gpu) cn.gpu->texture_input_port_indices.push_back(input_idx);
                    break;
                case VIVID_PORT_STRING:
                    cn.string_input_port_indices.push_back(input_idx);
                    break;
                case VIVID_PORT_STRING_LANES:
                    cn.string_lane_input_port_indices.push_back(input_idx);
                    break;
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
                            cn.gpu->aux_texture_format_hints.push_back(desc->ports[i].gpu_texture_format);
                        }
                        ++gpu_tex_out_count;
                    }
                    break;
                case VIVID_PORT_STRING:
                    cn.has_string_output = true;
                    break;
                case VIVID_PORT_STRING_LANES:
                    cn.has_string_lane_output = true;
                    break;
                default:
                    if (vivid_is_custom_port_type(desc->ports[i].type))
                        cn.custom_output_port_indices.push_back(out_idx);
                    break;
            }
            out_idx++;
        }
    }
    if (cn.gpu) {
        cn.gpu->is_sink = !cn.gpu->texture_input_port_indices.empty() &&
                          !cn.gpu->has_texture_output &&
                          cn.custom_output_port_indices.empty();
    }
    cn.custom_output_buf.assign(cn.custom_output_port_indices.size(), nullptr);
    cn.custom_outputs.assign(cn.custom_output_port_indices.size(), nullptr);
    cn.resolved_custom_inputs.assign(cn.custom_input_port_indices.size(), nullptr);
}

void GraphCompiler::init_audio_state(CompiledNode& cn,
                                     const VividOperatorDescriptor* desc,
                                     uint32_t buffer_size) {
    auto& a = *cn.audio;

    a.descriptor_input_channels.clear();
    a.descriptor_output_channels.clear();
    a.has_lane_ports = false;
    a.has_string_input_ports = false;
    a.has_custom_input_ports = false;
    a.has_custom_output_ports = false;

    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_INPUT) {
            a.descriptor_input_channels.push_back(desc->ports[i].channels);
            if (desc->ports[i].type == VIVID_PORT_LANE_ARRAY) a.has_lane_ports = true;
            if (desc->ports[i].type == VIVID_PORT_STRING) a.has_string_input_ports = true;
            if (vivid_is_custom_port_type(desc->ports[i].type)) a.has_custom_input_ports = true;
        } else {
            a.descriptor_output_channels.push_back(desc->ports[i].channels);
            if (vivid_is_custom_port_type(desc->ports[i].type)) a.has_custom_output_ports = true;
        }
    }

    a.input_channel_counts.assign(cn.input_port_count, 1);
    a.output_channel_counts.assign(cn.output_port_count, 1);
    a.debug_input_channel_counts.assign(cn.input_port_count, 1);
    a.debug_output_channel_counts.assign(cn.output_port_count, 1);
    a.execution_strategy = LaneExecutionStrategy::Scalar;
    a.lane_lift_count = 0;
    a.lane_lift_set_id = 0;

    a.buffers_in.resize(cn.input_port_count, std::vector<float>(buffer_size, 0.0f));
    a.buffers_out.resize(cn.output_port_count, std::vector<float>(buffer_size, 0.0f));
    a.in_ptrs.resize(cn.input_port_count);
    a.out_ptrs.resize(cn.output_port_count);
    a.input_port_debug = std::make_unique<AudioNodeState::AudioPortDebugTelemetry[]>(cn.input_port_count);
    a.output_port_debug = std::make_unique<AudioNodeState::AudioPortDebugTelemetry[]>(cn.output_port_count);

    a.input_port_defaults.resize(cn.input_port_count, 0.0f);
    for (uint32_t i = 0, inp = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_INPUT) {
            a.input_port_defaults[inp] = desc->ports[i].default_value;
            ++inp;
        }
    }

    a.custom_output_ptrs.clear();
    a.custom_output_count = 0;
    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_OUTPUT &&
            vivid_is_custom_port_type(desc->ports[i].type)) {
            a.custom_output_count++;
        }
    }
    a.custom_output_ptrs.resize(a.custom_output_count, nullptr);

    // Pre-allocate audio-thread-local param buffer (mirrors cn.param_values).
    a.audio_local_params = cn.param_values;
}

} // namespace vivid
