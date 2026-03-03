#include "runtime/gpu_context.h"
#include "runtime/fullscreen_blit.h"
#include "ui/thumbnail_cache.h"
#include "ui/thumbnail_renderer.h"
#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/audio_engine.h"
#include "runtime/file_watcher.h"
#include "runtime/hot_reload.h"
#include "runtime/runtime_api.h"
#include "ui/renderer_2d.h"
#include "ui/node_graph.h"
#include "ui/graph_snapshot.h"
#include "ui/ui_command_sink.h"
#include "runtime/builtin_operators.h"
#include "runtime/control_server.h"
#include "runtime/capture_coordinator.h"
#include "runtime/system_midi.h"
#include "runtime/settings.h"
#include "runtime/editor_detect.h"
#include "runtime/operator_info_cache.h"
#include "runtime/runtime_command_sink.h"
#include "runtime/crash_guard.h"
#include "ui/ui_style.h"
#include "ui/theme_loader.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/data_driven_filter.h"
#include "operator_api/types.h"
#include "operator_api/input_state.h"
#include "common/gpu_util.h"
#include "export/export_pipeline.h"
#include "runtime/package_compiler.h"
#include "runtime/package_manager.h"
#include "runtime/package_catalog.h"
#include <fstream>
#include <sstream>
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <GLFW/glfw3.h>
#include <stb_image_write.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <algorithm>
#include <memory>
#include <unordered_set>
#include <vector>
#include <CLI/CLI.hpp>

#ifdef __APPLE__
#include "runtime/macos_frame_timer.h"
#include "runtime/macos_menu.h"
#include "ui/file_dialog.h"
#endif

// #16191D in sRGB → linear: pow(x/255, 2.2)
static constexpr double kClearLinear[4]  = { 0.00699, 0.00821, 0.01041, 1.0 };
// #16191D as raw unorm (no gamma conversion)
static constexpr double kClearRaw[4]     = { 0.0863, 0.0980, 0.1137, 1.0 };

// Thumbnail size: node width × 16:10 aspect
static constexpr uint32_t kThumbW = 140;
static constexpr uint32_t kThumbH = 88;

// Default GPU texture resolution for nodes without explicit size
static constexpr uint32_t kDefaultTexW = 800;
static constexpr uint32_t kDefaultTexH = 600;

static bool is_srgb_format(WGPUTextureFormat fmt) {
    switch (fmt) {
        case WGPUTextureFormat_RGBA8UnormSrgb:
        case WGPUTextureFormat_BGRA8UnormSrgb:
            return true;
        default:
            return false;
    }
}

using vivid::to_sv;


// ---------------------------------------------------------------------------
// build_graph_snapshot — produces a GraphSnapshot from runtime state
// ---------------------------------------------------------------------------
static vivid::ui::GraphSnapshot build_graph_snapshot(
        const vivid::Graph& graph,
        const vivid::Scheduler& scheduler,
        vivid::AudioEngine* audio_engine,
        vivid::OperatorRegistry& registry,
        OperatorInfoCache& op_cache,
        vivid::SystemMidiListener* system_midi = nullptr,
        const vivid::RuntimeAPI* runtime_api = nullptr,
        vivid::CaptureCoordinator* capture_coordinator = nullptr) {
    vivid::ui::GraphSnapshot snap;

    const auto& sched_nodes = scheduler.nodes();
    const auto& conns = graph.connections();

    // Nodes
    snap.nodes.resize(sched_nodes.size());
    for (size_t i = 0; i < sched_nodes.size(); ++i) {
        const auto& ns = sched_nodes[i];
        auto& sn = snap.nodes[i];
        sn.node_id = ns.node_id;
        sn.type_name = scheduler.type_name(static_cast<uint32_t>(i));
        sn.domain = ns.loader->descriptor()->domain;
        sn.is_gpu = ns.is_gpu;
        sn.is_audio = ns.is_audio;
        sn.is_gpu_sink = ns.is_gpu_sink;
        sn.is_generator = ns.texture_input_port_indices.empty() && !ns.is_gpu_sink;
        sn.input_port_indices = ns.input_port_indices;
        sn.output_port_indices = ns.output_port_indices;
        sn.param_indices = ns.param_indices;
        sn.param_values = ns.param_values;
        sn.param_lock_flags = ns.param_lock_flags;
        sn.output_values = ns.output_values;
        sn.output_spreads = ns.output_spreads;
        for (const auto& [name, idx] : ns.file_param_indices)
            sn.file_param_values[name] = ns.file_param_storage[idx];
        sn.gpu_tex_width = ns.gpu_tex_width;
        sn.gpu_tex_height = ns.gpu_tex_height;
        sn.errored = ns.errored;
        sn.error_message = ns.error_message;

        // Layout from graph
        const auto* ndef = graph.find_node(ns.node_id);
        if (ndef && ndef->has_layout()) {
            sn.layout_x = ndef->layout_x;
            sn.layout_y = ndef->layout_y;
            sn.has_layout = true;
        }

        // Operator info (cached; pass per-instance loader as fallback for WGSLFilter nodes)
        sn.op_info = op_cache.get(sn.type_name, registry, ns.loader);

        // Per-operator presets
        sn.preset_names = graph.list_presets(ns.node_id);
        sn.factory_preset_names = registry.factory_preset_names(sn.type_name);
        if (runtime_api)
            sn.active_preset = runtime_api->active_preset(ns.node_id);

        // State-preset mappings (for StateMachine nodes)
        const auto* spm = graph.find_state_mapping(ns.node_id);
        if (spm)
            sn.state_preset_map = spm->state_presets;

        // Index
        snap.node_index[ns.node_id] = i;
    }

    // Connections
    snap.connections.resize(conns.size());
    for (size_t i = 0; i < conns.size(); ++i) {
        snap.connections[i].from_node = conns[i].from_node;
        snap.connections[i].from_port = conns[i].from_port;
        snap.connections[i].to_node   = conns[i].to_node;
        snap.connections[i].to_port   = conns[i].to_port;
        snap.connections[i].from_min  = conns[i].from_min;
        snap.connections[i].from_max  = conns[i].from_max;
        snap.connections[i].to_min    = conns[i].to_min;
        snap.connections[i].to_max    = conns[i].to_max;
        snap.connections[i].clamp     = conns[i].clamp;
        // Determine if source is a param (not an output port)
        auto ni_it = snap.node_index.find(conns[i].from_node);
        if (ni_it != snap.node_index.end()) {
            const auto& src = snap.nodes[ni_it->second];
            snap.connections[i].from_is_param =
                (src.output_port_indices.count(conns[i].from_port) == 0);
        }
    }

    // Audio analysis
    if (audio_engine) {
        const auto& analysis = audio_engine->analysis_read();
        for (const auto& ns : sched_nodes) {
            int ae_idx = audio_engine->audio_node_index(ns.node_id);
            if (ae_idx >= 0) {
                snap.audio_index[ns.node_id] = ae_idx;
            }
        }
        snap.audio_analysis.resize(analysis.waveform.size());
        for (size_t i = 0; i < analysis.waveform.size(); ++i) {
            snap.audio_analysis[i].peak = (i < analysis.peak.size()) ? analysis.peak[i] : 0.0f;
            snap.audio_analysis[i].waveform = analysis.waveform[i];
        }

        snap.audio_underrun_count = audio_engine->underrun_count();
        snap.audio_underrun_active = audio_engine->last_buffer_underrun();
    }

    // Operator catalog
    snap.operator_types = registry.type_names();
    std::sort(snap.operator_types.begin(), snap.operator_types.end());
    for (const auto& tn : snap.operator_types) {
        auto info = op_cache.get(tn, registry);
        if (info) snap.operator_catalog[tn] = info;
    }

    // WGSL preset names (for filter selector UI)
    snap.wgsl_preset_names = registry.wgsl_preset_names();

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
    if (runtime_api) {
        snap.variation_dirty = runtime_api->variation_dirty();
        snap.queued_variation = runtime_api->pending_variation_idx();
    }

    // Recording state
    if (capture_coordinator) {
        snap.is_recording = capture_coordinator->is_recording();
        if (snap.is_recording) {
            snap.recording_frame_count = capture_coordinator->recording_frame_count();
            snap.recording_duration_sec = capture_coordinator->recording_duration_sec();
        }
    }

    return snap;
}

static void emit_clear_pass(WGPUCommandEncoder encoder, WGPUTextureView view, const double clear[4]) {
    WGPURenderPassColorAttachment color_att{};
    color_att.view = view;
    color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_att.resolveTarget = nullptr;
    color_att.loadOp = WGPULoadOp_Clear;
    color_att.storeOp = WGPUStoreOp_Store;
    color_att.clearValue = { clear[0], clear[1], clear[2], clear[3] };
    WGPURenderPassDescriptor rp_desc{};
    rp_desc.label = to_sv("Clear Pass");
    rp_desc.colorAttachmentCount = 1;
    rp_desc.colorAttachments = &color_att;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp_desc);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

static void poll_hot_reload(vivid::FileWatcher& fw, vivid::HotReloader& hr,
                            vivid::Scheduler& scheduler, vivid::OperatorRegistry& registry,
                            vivid::AudioEngine& audio_engine, bool has_audio,
                            OperatorInfoCache* op_cache = nullptr,
                            const std::string& operators_dir = {}) {
    auto changes = fw.poll_changes();
    for (const auto& change : changes) {
        hr.queue_rebuild(change.target_name);
    }

    auto ready = hr.poll_ready();
    for (const auto& result : ready) {
        if (!result.success) continue;

        const std::string* type_name_ptr = registry.type_name_for_target(result.target_name);
        if (!type_name_ptr) {
            // New operator (just scaffolded) — load its dylib into the registry
            if (registry.register_loaded_operator(result.staged_dylib_path)) {
                // Register file watch for the new operator's source files
                if (!operators_dir.empty()) {
                    // Scan all domain subdirs for the target directory
                    for (const char* domain : {"control", "audio", "gpu"}) {
                        std::string cpp_path = operators_dir + "/" + domain + "/" +
                                               result.target_name + "/" + result.target_name + ".cpp";
                        if (std::filesystem::exists(cpp_path)) {
                            fw.add_watch(cpp_path, result.target_name);
                            break;
                        }
                    }
                }
                std::fprintf(stderr, "[vivid] New operator '%s' loaded\n",
                    result.target_name.c_str());
            } else {
                std::fprintf(stderr, "[vivid] Hot-reload: failed to load new target '%s'\n",
                    result.target_name.c_str());
            }
            continue;
        }
        const std::string& tn = *type_name_ptr;

        std::fprintf(stderr, "[vivid] Hot-reload: reloading %s...\n", tn.c_str());

        bool is_audio_op = scheduler.is_audio_type(tn);

        if (is_audio_op && has_audio) {
            audio_engine.pause();
        }

        if (scheduler.reload_operator(tn, registry, result.staged_dylib_path)) {
            if (is_audio_op && has_audio) {
                audio_engine.reload_operator(tn, registry);
            }
            if (op_cache) op_cache->invalidate(tn);
            std::fprintf(stderr, "[vivid] Hot-reload: %s reloaded successfully\n", tn.c_str());
        } else {
            std::fprintf(stderr, "[vivid] Hot-reload: %s reload FAILED\n", tn.c_str());
        }

        if (is_audio_op && has_audio) {
            audio_engine.resume();
        }
    }
}

static void draw_custom_thumbnails(const vivid::Scheduler& scheduler,
                                   vivid::ui::ThumbnailCache& cache, vivid::ui::NodeGraphUI& graph_ui,
                                   double time, uint32_t thumb_w, uint32_t thumb_h) {
    std::vector<uint8_t> thumb_pixels(thumb_w * thumb_h * 4);
    std::unordered_set<std::string> custom_thumb_ids;
    for (const auto& ns : scheduler.nodes()) {
        if (!ns.loader->has_draw_thumbnail()) continue;
        VividThumbnailContext tctx{};
        tctx.pixels = thumb_pixels.data();
        tctx.width = thumb_w;
        tctx.height = thumb_h;
        tctx.stride = thumb_w * 4;
        tctx.time = time;
        tctx.output_values = const_cast<float*>(ns.output_values.data());
        tctx.output_count = ns.output_port_count;
        tctx.param_values = const_cast<float*>(ns.param_values.data());
        tctx.param_count = static_cast<uint32_t>(ns.param_values.size());
        std::memset(thumb_pixels.data(), 0, thumb_pixels.size());
        ns.loader->draw_thumbnail(ns.instance, &tctx);
        cache.upload_cpu(ns.node_id, thumb_pixels.data());
        custom_thumb_ids.insert(ns.node_id);
    }
    graph_ui.set_custom_thumbnail_nodes(std::move(custom_thumb_ids));
}

static bool try_capture_screenshot(const std::string& path, vivid::GpuContext& gpu,
                                   vivid::FrameState& frame, int fb_w, int fb_h,
                                   uint64_t frame_count, int delay, GLFWwindow* window) {
    if (path.empty() || !gpu.surface_supports_copy_src()
        || static_cast<int>(frame_count) < delay) {
        return false;
    }

    const uint32_t ss_w = static_cast<uint32_t>(fb_w);
    const uint32_t ss_h = static_cast<uint32_t>(fb_h);
    const uint32_t bpp = 4;
    const uint32_t unpadded_row = ss_w * bpp;
    static constexpr uint32_t kGpuRowAlignment = 256;
    const uint32_t aligned_row = (unpadded_row + kGpuRowAlignment - 1) & ~(kGpuRowAlignment - 1);
    const uint64_t buf_size = static_cast<uint64_t>(aligned_row) * ss_h;

    WGPUBufferDescriptor staging_desc{};
    staging_desc.label = to_sv("Screenshot Staging");
    staging_desc.size = buf_size;
    staging_desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    staging_desc.mappedAtCreation = false;
    WGPUBuffer staging = wgpuDeviceCreateBuffer(gpu.device(), &staging_desc);

    WGPUTexelCopyTextureInfo src{};
    src.texture = frame.texture;
    src.mipLevel = 0;
    src.origin = { 0, 0, 0 };
    src.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo dst{};
    dst.buffer = staging;
    dst.layout.offset = 0;
    dst.layout.bytesPerRow = aligned_row;
    dst.layout.rowsPerImage = ss_h;

    WGPUExtent3D copy_size = { ss_w, ss_h, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(frame.encoder, &src, &dst, &copy_size);

    gpu.end_frame(frame);

    // Wait for GPU work to complete
    {
        bool work_done = false;
        WGPUQueueWorkDoneCallbackInfo work_cb{};
        work_cb.mode = WGPUCallbackMode_AllowSpontaneous;
        work_cb.callback = [](WGPUQueueWorkDoneStatus, void* ud1, void*) {
            *static_cast<bool*>(ud1) = true;
        };
        work_cb.userdata1 = &work_done;
        wgpuQueueOnSubmittedWorkDone(gpu.queue(), work_cb);
        while (!work_done)
            wgpuDevicePoll(gpu.device(), true, nullptr);
    }

    bool map_done = false;
    WGPUBufferMapCallbackInfo map_cb{};
    map_cb.mode = WGPUCallbackMode_AllowSpontaneous;
    map_cb.callback = [](WGPUMapAsyncStatus, WGPUStringView, void* ud1, void*) {
        *static_cast<bool*>(ud1) = true;
    };
    map_cb.userdata1 = &map_done;
    wgpuBufferMapAsync(staging, WGPUMapMode_Read, 0, buf_size, map_cb);
    while (!map_done)
        wgpuDevicePoll(gpu.device(), true, nullptr);

    const uint8_t* mapped = static_cast<const uint8_t*>(
        wgpuBufferGetConstMappedRange(staging, 0, buf_size));

    std::vector<uint8_t> pixels(ss_w * ss_h * bpp);
    for (uint32_t y = 0; y < ss_h; ++y) {
        const uint8_t* src_row = mapped + y * aligned_row;
        uint8_t* dst_row = pixels.data() + y * unpadded_row;
        for (uint32_t x = 0; x < ss_w; ++x) {
            dst_row[x * 4 + 0] = src_row[x * 4 + 2]; // R <- B
            dst_row[x * 4 + 1] = src_row[x * 4 + 1]; // G <- G
            dst_row[x * 4 + 2] = src_row[x * 4 + 0]; // B <- R
            dst_row[x * 4 + 3] = src_row[x * 4 + 3]; // A <- A
        }
    }

    wgpuBufferUnmap(staging);
    wgpuBufferRelease(staging);

    if (stbi_write_png(path.c_str(), ss_w, ss_h, 4, pixels.data(), ss_w * bpp)) {
        std::fprintf(stderr, "[vivid] Screenshot saved: %s\n", path.c_str());
    } else {
        std::fprintf(stderr, "[vivid] Screenshot FAILED: %s\n", path.c_str());
    }

    glfwSetWindowShouldClose(window, GLFW_TRUE);
    return true;  // frame already submitted
}

// GLFW callback trampolines
struct WindowUserData {
    vivid::ui::NodeGraphUI* graph_ui = nullptr;
    vivid::RuntimeAPI* runtime_api = nullptr;
    vivid::Graph* graph = nullptr;
    std::string working_filters_dir;
    vivid::Settings* settings = nullptr;

    // Input forwarding to operators (when UI hidden)
    std::vector<VividInputEvent> pending_events;
    double raw_mouse_x = 0.0, raw_mouse_y = 0.0;  // window coords
    int buttons_held = 0;   // bitmask: bit 0=left, 1=right, 2=middle
    int current_mods = 0;

    // Drag-and-drop graph loading
    std::string pending_drop_path;
};

static void char_callback(GLFWwindow* w, unsigned int codepoint) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud) return;
    if (ud->graph_ui && ud->graph_ui->visible()) {
        if (ud->graph_ui->wants_keyboard())
            ud->graph_ui->on_char(codepoint);
    } else {
        VividInputEvent ev{};
        ev.type = VIVID_INPUT_CHAR;
        ev.codepoint = codepoint;
        ev.mouse_x = static_cast<float>(ud->raw_mouse_x);
        ev.mouse_y = static_cast<float>(ud->raw_mouse_y);
        ev.modifiers = ud->current_mods;
        ud->pending_events.push_back(ev);
    }
}

static void key_callback(GLFWwindow* w, int key, int scancode, int action, int mods) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud) return;

    ud->current_mods = mods;

    // Tilde toggles graph UI visibility (intercept before any dispatch)
    if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS && mods == 0) {
        if (ud->graph_ui) ud->graph_ui->toggle_visible();
        return;
    }

    // Cmd+S and Cmd+, are handled by the native macOS menu bar (macos_menu.mm).
    // On non-Apple platforms, fall through to the graph UI key handler.

    if (ud->graph_ui && ud->graph_ui->visible()) {
        ud->graph_ui->on_key(key, action, mods);
    } else {
        VividInputEvent ev{};
        ev.type = VIVID_INPUT_KEY;
        ev.key = key;
        ev.scancode = scancode;
        ev.action = action;
        ev.modifiers = mods;
        ev.mouse_x = static_cast<float>(ud->raw_mouse_x);
        ev.mouse_y = static_cast<float>(ud->raw_mouse_y);
        ud->pending_events.push_back(ev);
    }
}

static void cursor_pos_callback(GLFWwindow* w, double xpos, double ypos) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud) return;
    ud->raw_mouse_x = xpos;
    ud->raw_mouse_y = ypos;
    if (ud->graph_ui && ud->graph_ui->visible()) {
        ud->graph_ui->on_mouse_move(static_cast<float>(xpos), static_cast<float>(ypos));
    } else {
        VividInputEvent ev{};
        ev.type = VIVID_INPUT_MOUSE_MOVE;
        ev.mouse_x = static_cast<float>(xpos);  // will be normalized later
        ev.mouse_y = static_cast<float>(ypos);
        ev.modifiers = ud->current_mods;
        ev.button = -1;
        ud->pending_events.push_back(ev);
    }
}

static void mouse_button_callback(GLFWwindow* w, int button, int action, int mods) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud) return;
    ud->current_mods = mods;
    // Track button state
    if (button >= 0 && button <= 2) {
        if (action == GLFW_PRESS)
            ud->buttons_held |= (1 << button);
        else if (action == GLFW_RELEASE)
            ud->buttons_held &= ~(1 << button);
    }
    if (ud->graph_ui && ud->graph_ui->visible()) {
        ud->graph_ui->on_mouse_button(button, action, mods);
    } else {
        VividInputEvent ev{};
        ev.type = VIVID_INPUT_MOUSE_BUTTON;
        ev.button = button;
        ev.action = action;
        ev.modifiers = mods;
        ev.mouse_x = static_cast<float>(ud->raw_mouse_x);
        ev.mouse_y = static_cast<float>(ud->raw_mouse_y);
        ud->pending_events.push_back(ev);
    }
}

static void scroll_callback(GLFWwindow* w, double xoffset, double yoffset) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud) return;
    if (ud->graph_ui && ud->graph_ui->visible()) {
        int mods = 0;
        if (glfwGetKey(w, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
            glfwGetKey(w, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS)
            mods |= GLFW_MOD_SUPER;
        ud->graph_ui->on_scroll(
            static_cast<float>(xoffset), static_cast<float>(yoffset), mods);
    } else {
        VividInputEvent ev{};
        ev.type = VIVID_INPUT_MOUSE_SCROLL;
        ev.scroll_dx = static_cast<float>(xoffset);
        ev.scroll_dy = static_cast<float>(yoffset);
        ev.mouse_x = static_cast<float>(ud->raw_mouse_x);
        ev.mouse_y = static_cast<float>(ud->raw_mouse_y);
        ev.modifiers = ud->current_mods;
        ud->pending_events.push_back(ev);
    }
}

static void drop_callback(GLFWwindow* w, int count, const char** paths) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud || count < 1) return;
    for (int i = 0; i < count; ++i) {
        std::string_view p(paths[i]);
        if (p.size() > 5 && p.substr(p.size() - 5) == ".json") {
            ud->pending_drop_path = paths[i];
            return;
        }
    }
}

// --- Build/source directory discovery (shared by export, packages, hot-reload) ---
namespace fs = std::filesystem;

struct BuildPaths { std::string source_dir, build_dir; };

static BuildPaths discover_build_paths(const fs::path& exe_dir,
                                       const fs::path& resources_dir,
                                       const std::string& user_src_dir) {
    BuildPaths p;
    // Prefer compile-time build dir (set by CMake on Apple)
#ifdef VIVID_BUILD_DIR
    p.build_dir = VIVID_BUILD_DIR;
    if (!fs::is_directory(p.build_dir))
#endif
    {
#ifdef __APPLE__
        // In a bundle: exe_dir is Contents/MacOS/, build dir is 3 levels up
        p.build_dir = exe_dir.parent_path().parent_path().parent_path().string();
#else
        p.build_dir = exe_dir.string();
#endif
    }

    // Walk up from build_dir looking for source root
    auto c = fs::path(p.build_dir);
    for (int i = 0; i < 3; ++i) {
        if (fs::exists(c / "CMakeLists.txt") && fs::exists(c / "src" / "runtime")) {
            p.source_dir = c.string();
            break;
        }
        c = c.parent_path();
    }
    // Fallback: bundle SDK (Contents/Resources/sdk/)
#ifdef __APPLE__
    if (p.source_dir.empty()) {
        auto sdk_dir = resources_dir / "sdk";
        if (fs::is_directory(sdk_dir / "src" / "operator_api"))
            p.source_dir = sdk_dir.string();
    }
#endif
    if (p.source_dir.empty())
        p.source_dir = user_src_dir;
    return p;
}

int main(int argc, char* argv[]) {
    vivid::install_crash_handlers();

    // Derive exe directory so resource lookup works from any CWD
    auto exe_path = std::filesystem::canonical(std::filesystem::path(argv[0]));
    auto exe_dir = exe_path.parent_path();

    // Resources dir: Contents/Resources/ in a macOS bundle, else same as exe_dir
#ifdef __APPLE__
    auto resources_dir = exe_dir.parent_path() / "Resources";
    auto plugins_dir = exe_dir.parent_path() / "PlugIns";
#else
    auto resources_dir = exe_dir;
#endif

    // --- CLI argument parsing ---
    std::string graph_file = "graph.json";
    std::string screenshot_path;
    int screenshot_delay = 5;
    bool headless = false;
    std::string src_dir;

    CLI::App app{"Vivid - Real-time audio-visual graph engine\n\n"
                 "Loads a JSON graph file and runs it in real-time.\n"
                 "Control server listens on http://127.0.0.1:9876 for live manipulation."};

    app.add_option("graph", graph_file, "Graph file to load")->type_name("FILE");
    app.add_option("--screenshot", screenshot_path, "Capture a screenshot to PNG and exit")->type_name("FILE");
    app.add_option("--screenshot-delay", screenshot_delay, "Frames to wait before capture (default: 5)");
    app.add_flag("--headless", headless, "Run without displaying a window");
    app.add_option("--src-dir", src_dir, "Source directory for operator hot-reload")->type_name("PATH");

    // --- Export subcommand ---
    std::string export_graph_path;
    std::string export_output;
    std::string export_output_dir;
    bool export_headless = false;
    bool export_control_server = false;
    std::vector<std::string> export_extra_ops;

    auto* export_cmd = app.add_subcommand("export", "Export graph as a standalone binary");
    export_cmd->add_option("--graph", export_graph_path, "Graph file to export")
        ->required()->type_name("FILE");
    export_cmd->add_option("--output", export_output, "Output binary name")
        ->required()->type_name("NAME");
    export_cmd->add_option("--output-dir", export_output_dir, "Export build directory")->type_name("PATH");
    export_cmd->add_flag("--headless", export_headless, "Build headless (no window)");
    export_cmd->add_flag("--control-server", export_control_server, "Include HTTP control server");
    export_cmd->add_option("--extra-operators", export_extra_ops,
        "Additional operator types to include (comma-separated)")->delimiter(',');

    // --- Package management subcommands ---
    std::string install_url;
    std::string uninstall_name;

    auto* install_cmd = app.add_subcommand("install", "Install an operator package");
    install_cmd->add_option("url", install_url, "Git URL or local path")->required();

    auto* uninstall_cmd = app.add_subcommand("uninstall", "Uninstall an operator package");
    uninstall_cmd->add_option("name", uninstall_name, "Package name")->required();

    auto* list_pkg_cmd = app.add_subcommand("list-packages", "List installed operator packages");

    std::string link_path;
    auto* link_cmd = app.add_subcommand("link", "Link a local package for development");
    link_cmd->add_option("path", link_path, "Path to package directory")->required();

    std::string unlink_name;
    auto* unlink_cmd = app.add_subcommand("unlink", "Unlink a linked package");
    unlink_cmd->add_option("name", unlink_name, "Package name")->required();

    std::string rebuild_name;
    auto* rebuild_cmd = app.add_subcommand("rebuild", "Recompile operators for a package");
    rebuild_cmd->add_option("name", rebuild_name, "Package name")->required();

    app.require_subcommand(0, 1);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    // Resolve build/source directories once (used by export, packages, hot-reload)
    auto build_paths = discover_build_paths(exe_dir, resources_dir, src_dir);

    // --- Handle export subcommand (early exit, no GLFW) ---
    if (export_cmd->parsed()) {
        if (build_paths.source_dir.empty()) {
            std::fprintf(stderr, "[vivid] Cannot determine source directory. "
                         "Use --src-dir or run from a build directory.\n");
            return 1;
        }

        // Build registry to get type→target mappings
        vivid::OperatorRegistry registry;
#ifdef __APPLE__
        registry.scan_deferred(plugins_dir.string().c_str());
#else
        registry.scan_deferred(exe_dir.string().c_str());
#endif
        register_builtin_operators(registry);
        std::string filters_dir = (resources_dir / "filters").string();
        registry.scan_wgsl_presets(filters_dir);

        vivid::ExportOptions opts;
        opts.graph_path = export_graph_path;
        opts.output_name = export_output;
        opts.output_dir = export_output_dir;
        opts.headless = export_headless;
        opts.control_server = export_control_server;
        opts.extra_operators = export_extra_ops;

        vivid::ExportPipeline pipeline(build_paths.source_dir, build_paths.build_dir);
        if (!pipeline.run(opts, registry)) {
            std::fprintf(stderr, "[vivid] Export failed\n");
            return 1;
        }
        return 0;
    }

    // --- Handle package management subcommands (early exit, no GLFW) ---
    if (install_cmd->parsed() || uninstall_cmd->parsed() || list_pkg_cmd->parsed() ||
        link_cmd->parsed() || unlink_cmd->parsed() || rebuild_cmd->parsed()) {
        vivid::OperatorRegistry registry;
#ifdef __APPLE__
        registry.scan_deferred(plugins_dir.string().c_str());
#else
        registry.scan_deferred(exe_dir.string().c_str());
#endif
        register_builtin_operators(registry);

        vivid::PackageCompiler compiler(build_paths.source_dir, build_paths.build_dir);
        vivid::PackageManager pm(compiler, registry);

        if (install_cmd->parsed()) {
            auto result = pm.install(install_url);
            if (result.success) {
                std::fprintf(stderr, "Installed %s v%s (%zu operators)\n",
                             result.info.name.c_str(), result.info.version.c_str(),
                             result.info.operators.size() + result.info.gpu_operators.size());
                return 0;
            } else {
                std::fprintf(stderr, "Install failed: %s\n", result.error.c_str());
                for (const auto& cr : result.compile_results) {
                    if (!cr.success)
                        std::fprintf(stderr, "  %s: %s\n", cr.operator_name.c_str(),
                                     cr.error_output.c_str());
                }
                return 1;
            }
        } else if (uninstall_cmd->parsed()) {
            if (pm.uninstall(uninstall_name)) {
                std::fprintf(stderr, "Uninstalled %s\n", uninstall_name.c_str());
                return 0;
            } else {
                std::fprintf(stderr, "Failed to uninstall %s\n", uninstall_name.c_str());
                return 1;
            }
        } else if (list_pkg_cmd->parsed()) {
            auto packages = pm.list();
            if (packages.empty()) {
                std::printf("No packages installed.\n");
            } else {
                for (const auto& pkg : packages) {
                    std::printf("%s v%s  (%zu operators)%s\n",
                                pkg.name.c_str(), pkg.version.c_str(),
                                pkg.operators.size() + pkg.gpu_operators.size(),
                                pkg.linked ? "  [linked]" : "");
                    if (!pkg.description.empty())
                        std::printf("  %s\n", pkg.description.c_str());
                    for (const auto& op : pkg.operators)
                        std::printf("    %s\n", op.c_str());
                    for (const auto& op : pkg.gpu_operators)
                        std::printf("    %s (gpu)\n", op.c_str());
                }
            }
            return 0;
        } else if (link_cmd->parsed()) {
            auto result = pm.link(link_path);
            if (result.success) {
                std::fprintf(stderr, "Linked %s v%s (%zu operators)\n",
                             result.info.name.c_str(), result.info.version.c_str(),
                             result.info.operators.size() + result.info.gpu_operators.size());
                return 0;
            } else {
                std::fprintf(stderr, "Link failed: %s\n", result.error.c_str());
                for (const auto& cr : result.compile_results) {
                    if (!cr.success)
                        std::fprintf(stderr, "  %s: %s\n", cr.operator_name.c_str(),
                                     cr.error_output.c_str());
                }
                return 1;
            }
        } else if (unlink_cmd->parsed()) {
            if (pm.unlink(unlink_name)) {
                std::fprintf(stderr, "Unlinked %s\n", unlink_name.c_str());
                return 0;
            } else {
                std::fprintf(stderr, "Failed to unlink %s\n", unlink_name.c_str());
                return 1;
            }
        } else if (rebuild_cmd->parsed()) {
            auto result = pm.rebuild(rebuild_name);
            if (result.success) {
                std::fprintf(stderr, "Rebuilt %s (%zu operators)\n",
                             result.info.name.c_str(),
                             result.info.operators.size() + result.info.gpu_operators.size());
                return 0;
            } else {
                std::fprintf(stderr, "Rebuild failed: %s\n", result.error.c_str());
                for (const auto& cr : result.compile_results) {
                    if (!cr.success)
                        std::fprintf(stderr, "  %s: %s\n", cr.operator_name.c_str(),
                                     cr.error_output.c_str());
                }
                return 1;
            }
        }
    }

    // --- GLFW ---
    if (!glfwInit()) {
        std::fprintf(stderr, "[vivid] Failed to init GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    if (headless) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    vivid::Settings settings = vivid::load_settings();

    // Clamp saved window size to fit the primary monitor's work area
    {
        GLFWmonitor* primary = glfwGetPrimaryMonitor();
        if (primary) {
            int mx, my, mw, mh;
            glfwGetMonitorWorkarea(primary, &mx, &my, &mw, &mh);
            if (settings.window_width > mw) settings.window_width = mw;
            if (settings.window_height > mh) settings.window_height = mh;
        }
    }

    GLFWwindow* window = glfwCreateWindow(settings.window_width, settings.window_height,
                                           "Vivid", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[vivid] Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    // Restore saved window position, validating it's on a visible monitor
    if (settings.window_x != -1 && settings.window_y != -1) {
        bool on_screen = false;
        int mon_count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&mon_count);
        for (int i = 0; i < mon_count; i++) {
            int mx, my, mw, mh;
            glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
            // Check that at least a 100x100 corner of the window is visible
            if (settings.window_x + 100 > mx && settings.window_x < mx + mw &&
                settings.window_y + 100 > my && settings.window_y < my + mh) {
                on_screen = true;
                break;
            }
        }
        if (on_screen) {
            glfwSetWindowPos(window, settings.window_x, settings.window_y);
        }
    }

    // --- Query physical framebuffer size and DPI scale ---
    int fb_width, fb_height;
    glfwGetFramebufferSize(window, &fb_width, &fb_height);
    float xscale, yscale;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    float dpi_scale = xscale; // on macOS, xscale == yscale
    std::fprintf(stderr, "[vivid] Framebuffer: %dx%d, DPI scale: %.1f\n",
                 fb_width, fb_height, dpi_scale);

    // --- GPU ---
    vivid::GpuContext gpu;
    if (!gpu.init(window, fb_width, fb_height)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    const double* clear = is_srgb_format(gpu.surface_format()) ? kClearLinear : kClearRaw;

    // --- Offscreen texture format (used by per-node GPU textures) ---
    static constexpr WGPUTextureFormat kOffscreenFormat = WGPUTextureFormat_RGBA16Float;

    // --- Fullscreen blit (per-node texture → surface) ---
    vivid::FullscreenBlit blit;
    if (!blit.init(gpu.device(), gpu.surface_format())) {
        std::fprintf(stderr, "[vivid] Failed to init FullscreenBlit\n");
        gpu.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // --- Thumbnail cache + renderer ---
    vivid::ui::ThumbnailCache thumb_cache;
    thumb_cache.init(gpu.device(), gpu.queue(), kThumbW, kThumbH);

    // Separate blit pipeline for offscreen→thumbnail (targets RGBA16Float, not surface format)
    vivid::FullscreenBlit thumb_blit;
    if (!thumb_blit.init(gpu.device(), kOffscreenFormat)) {
        std::fprintf(stderr, "[vivid] Failed to init thumbnail blit\n");
    }

    vivid::ui::ThumbnailRenderer thumb_renderer;
    bool thumb_renderer_ok = thumb_renderer.init(gpu.device(), gpu.queue(), gpu.surface_format());

    // --- Load operator plugins ---
    vivid::OperatorRegistry registry;
#ifdef __APPLE__
    registry.scan_deferred(plugins_dir.string().c_str());
#else
    registry.scan_deferred(exe_dir.string().c_str());
#endif
    register_builtin_operators(registry);

    // --- Load self-describing .wgsl filter presets ---
    std::string filters_dir = (resources_dir / "filters").string();
    registry.scan_wgsl_presets(filters_dir);

    // --- Load factory presets for operators ---
    std::string factory_presets_dir = (resources_dir / "factory_presets").string();
    registry.scan_factory_presets(factory_presets_dir);

    // --- Package management (needs to outlive main loop for catalog/install) ---
    vivid::PackageCompiler pkg_compiler(build_paths.source_dir, build_paths.build_dir);
    vivid::PackageManager pkg_manager(pkg_compiler, registry);
    pkg_manager.scan_installed();
    vivid::PackageCatalog pkg_catalog(pkg_manager);
    pkg_manager.set_resolver([&pkg_catalog](const std::string& name) -> std::string {
        for (const auto& e : pkg_catalog.entries())
            if (e.name == name) return e.url;
        return "";
    });

    // --- Load graph ---
    vivid::Graph graph;
    vivid::Scheduler scheduler;
    bool graph_loaded = false;

    // Working directory for user filter shaders: {graph_dir}/{graph_stem}_filters/
    std::string working_filters_dir;

    if (graph.load(graph_file.c_str())) {
        // Register user filters from graph before building the scheduler
        if (!graph.filters().empty()) {
            auto gp = std::filesystem::path(graph.source_path());
            auto graph_dir = gp.parent_path();
            auto graph_stem = gp.stem();
            working_filters_dir = (graph_dir / (graph_stem.string() + "_filters")).string();
            std::filesystem::create_directories(working_filters_dir);

            for (const auto& fd : graph.filters()) {
                // Write shader source to working file
                std::string working_path = working_filters_dir + "/" + fd.name + ".wgsl";
                {
                    std::ofstream ofs(working_path);
                    ofs << fd.shader;
                }

                // Build DataDrivenFilterConfig
                auto config = std::make_shared<vivid::DataDrivenFilterConfig>();
                config->name = fd.name;
                config->shader_path = working_path;
                config->source_builtin = fd.source;
                config->time_dependent = fd.time_dependent;
                for (const auto& pd : fd.params) {
                    vivid::DataDrivenFilterConfig::ParamDef cpd;
                    cpd.name = pd.name;
                    cpd.default_value = pd.default_value;
                    cpd.min_value = pd.min_value;
                    cpd.max_value = pd.max_value;
                    config->params.push_back(std::move(cpd));
                }
                registry.register_user_filter(fd.name, config);
            }
        }

        // Load only the operators this graph actually uses
        registry.load_for_graph(graph);

        if (scheduler.build(graph, registry)) {
            graph_loaded = true;
        } else {
            std::fprintf(stderr, "[vivid] Scheduler build failed (non-fatal, continuing)\n");
        }
    } else {
        std::fprintf(stderr, "[vivid] Graph load failed (non-fatal, continuing)\n");
    }

    bool has_gpu_ops = graph_loaded && scheduler.has_gpu_operators();

    // Allocate per-node GPU textures
    if (has_gpu_ops) {
        scheduler.allocate_gpu_textures(gpu.device(), kDefaultTexW, kDefaultTexH, kOffscreenFormat);
    }
    int video_out_idx = has_gpu_ops ? scheduler.find_gpu_sink() : -1;

    // --- Audio engine ---
    vivid::AudioEngine audio_engine;
    bool has_audio = false;
    if (graph_loaded && scheduler.has_audio_operators()) {
        if (audio_engine.build(graph, registry, scheduler)) {
            if (audio_engine.start()) {
                has_audio = true;
            }
        }
    }

    // --- System MIDI listener (for MIDI mapping) ---
    vivid::SystemMidiListener system_midi;
    system_midi.open_all();  // listen on all available MIDI ports

    // --- RuntimeAPI ---
    vivid::RuntimeAPI runtime_api(graph, scheduler, audio_engine, registry, &system_midi);

    // --- Control server (MCP HTTP bridge) ---
    vivid::CaptureCoordinator capture_coordinator;
    if (has_audio) capture_coordinator.set_audio_engine(&audio_engine);
    vivid::ControlServer control_server;
    control_server.set_capture_coordinator(&capture_coordinator);
    control_server.set_package_manager(&pkg_manager);
    control_server.set_package_compiler(&pkg_compiler);
    control_server.set_package_catalog(&pkg_catalog);
    if (!control_server.start(9876)) {
        std::fprintf(stderr, "[vivid] Control server unavailable (port 9876 in use?)\n");
    }
    if (!src_dir.empty())
        control_server.set_src_dir(src_dir);

    vivid::ui::Renderer2D text_renderer;
    bool text_renderer_ok = false;
    {
        // Look for font next to executable, or in source tree
        std::string font_path = (resources_dir / "JetBrainsMono-Regular.ttf").string();
        if (!std::filesystem::exists(font_path)) {
            auto alt = exe_dir.parent_path() / "fonts" / "JetBrainsMono-Regular.ttf";
            if (std::filesystem::exists(alt)) font_path = alt.string();
        }
        if (text_renderer.init(gpu.device(), gpu.surface_format(), font_path.c_str(), 16.0f, dpi_scale)) {
            text_renderer_ok = true;
        } else {
            std::fprintf(stderr, "[vivid] Text renderer disabled (font not found)\n");
        }
    }

    RuntimeCommandSink command_sink(runtime_api);
    OperatorInfoCache op_info_cache;
    command_sink.set_registry(&registry);
    command_sink.set_graph(&graph);
    command_sink.set_op_cache(&op_info_cache);
    command_sink.set_working_filters_dir(working_filters_dir);
    command_sink.set_settings(&settings);
    command_sink.set_capture_coordinator(&capture_coordinator);
    vivid::ui::NodeGraphUI graph_ui(command_sink);
    graph_ui.set_dpi_scale(dpi_scale);
    graph_ui.set_bezier_wires(settings.bezier_wires);
    graph_ui.set_package_catalog(&pkg_catalog);
    if (graph.has_viewport())
        graph_ui.set_viewport(graph.viewport_pan_x, graph.viewport_pan_y, graph.viewport_zoom);

    // Detect available text editors and set up style options
    {
        auto detected = vivid::detect_editors();
        std::vector<std::string> editor_names, editor_ids;
        int editor_sel = 0;
        for (size_t i = 0; i < detected.size(); ++i) {
            editor_names.push_back(detected[i].name);
            editor_ids.push_back(detected[i].app_id);
            if (detected[i].app_id == settings.editor)
                editor_sel = static_cast<int>(i);
        }
        // If editor is "custom", select that
        if (settings.editor == "custom") {
            for (size_t i = 0; i < editor_ids.size(); ++i) {
                if (editor_ids[i] == "custom") { editor_sel = static_cast<int>(i); break; }
            }
        }
        graph_ui.set_editor_options(std::move(editor_names), std::move(editor_ids),
                                    editor_sel, settings.editor_command);

        vivid::ui::ensure_default_themes();
        auto themes = vivid::ui::discover_themes();
        auto styles = vivid::ui::load_all_themes(themes);
        int style_sel = 0;
        for (size_t i = 0; i < styles.size(); ++i) {
            if (styles[i].id == settings.style_id)
                style_sel = static_cast<int>(i);
        }
        graph_ui.set_style_options(std::move(styles), style_sel, std::move(themes));
    }

    // Set up GLFW input callbacks
    WindowUserData window_user_data;
    window_user_data.graph_ui = &graph_ui;
    window_user_data.runtime_api = &runtime_api;
    window_user_data.graph = &graph;
    window_user_data.working_filters_dir = working_filters_dir;
    window_user_data.settings = &settings;
    glfwSetWindowUserPointer(window, &window_user_data);
    glfwSetCharCallback(window, char_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetDropCallback(window, drop_callback);

    // --- Hot-reload ---
    vivid::FileWatcher file_watcher;
    vivid::HotReloader hot_reloader;
    bool hot_reload_enabled = false;
    {
        if (src_dir.empty()) {
            auto probe = exe_dir;
            for (int i = 0; i < 5 && probe.has_parent_path(); ++i) {
                probe = probe.parent_path();
                if (std::filesystem::exists(probe / "operators")) {
                    src_dir = probe.string();
                    break;
                }
            }
        }

        if (!src_dir.empty()) {
            std::string operators_dir = src_dir + "/operators";
            scheduler.set_operators_src_dir(operators_dir);
            command_sink.set_operators_dir(operators_dir);
            command_sink.set_filters_dir(filters_dir);
            command_sink.set_build_dir(build_paths.build_dir);
            op_info_cache.set_operators_dir(operators_dir);
            // Set working filters dir if not already determined from graph
            if (working_filters_dir.empty() && !graph.source_path().empty()) {
                auto gp = std::filesystem::path(graph.source_path());
                working_filters_dir = (gp.parent_path() / (gp.stem().string() + "_filters")).string();
                command_sink.set_working_filters_dir(working_filters_dir);
            }
            if (file_watcher.start(operators_dir) && hot_reloader.start(build_paths.build_dir)) {
                hot_reload_enabled = true;
                control_server.set_hot_reloader(&hot_reloader);
                command_sink.set_hot_reloader(&hot_reloader);
                std::fprintf(stderr, "[vivid] Hot-reload enabled (watching %s)\n", operators_dir.c_str());

                // Also watch installed package operator directories
                std::string pkgs_dir = vivid::PackageManager::packages_dir();
                file_watcher.add_package_watches(pkgs_dir);

                // Set up package compile callback for hot-reloader
                std::string pkg_src_dir = src_dir;
                std::string pkg_build_dir = build_paths.build_dir;
                hot_reloader.set_package_compiler(
                    [pkgs_dir, pkg_src_dir, pkg_build_dir](const std::string& target) -> vivid::ReloadResult {
                        // Parse "pkg:<package_name>:<operator_name>"
                        vivid::ReloadResult result;
                        result.target_name = target;

                        auto first_colon = target.find(':');
                        auto second_colon = target.find(':', first_colon + 1);
                        if (first_colon == std::string::npos || second_colon == std::string::npos) {
                            result.success = false;
                            result.error_output = "Invalid package target: " + target;
                            return result;
                        }

                        std::string pkg_name = target.substr(first_colon + 1, second_colon - first_colon - 1);
                        std::string op_name = target.substr(second_colon + 1);
                        std::string pkg_dir = pkgs_dir + "/" + pkg_name;

                        vivid::PackageCompiler compiler(pkg_src_dir, pkg_build_dir);

                        // Find the operator's relative path from the manifest
                        // or try common pattern: look for it under operators/
                        std::string op_rel;
                        for (const auto& domain : {"audio", "control", "gpu"}) {
                            std::string candidate = pkg_dir + "/operators/" +
                                domain + "/" + op_name + "/" + op_name + ".cpp";
                            if (std::filesystem::exists(candidate)) {
                                op_rel = std::string(domain) + "/" + op_name;
                                break;
                            }
                        }
                        if (op_rel.empty()) {
                            result.success = false;
                            result.error_output = "Cannot find operator source for " + op_name + " in " + pkg_dir;
                            return result;
                        }

                        auto cr = compiler.compile_operator(pkg_dir, op_rel, false);
                        result.success = cr.success;
                        result.staged_dylib_path = cr.dylib_path;
                        result.error_output = cr.error_output;
                        return result;
                    });
            }
        } else {
            std::fprintf(stderr, "[vivid] Hot-reload disabled (operators/ not found; use --src-dir)\n");
        }
    }

    // --- macOS native menu bar ---
#ifdef __APPLE__
    {
        vivid::MenuCallbacks menu_cbs;

        menu_cbs.on_preferences = [&]() {
            graph_ui.toggle_preferences();
        };

        menu_cbs.on_save = [&]() {
            // Capture viewport before saving
            if (graph_ui.visible())
                graph.set_viewport(graph_ui.pan_x(), graph_ui.pan_y(), graph_ui.zoom());
            // Read back working filter shaders before saving
            if (!working_filters_dir.empty()) {
                for (const auto& fd : graph.filters()) {
                    std::string wpath = working_filters_dir + "/" + fd.name + ".wgsl";
                    std::ifstream ifs(wpath);
                    if (ifs) {
                        std::ostringstream ss;
                        ss << ifs.rdbuf();
                        graph.update_filter_shader(fd.name, ss.str());
                    }
                }
            }
            auto result = runtime_api.save();
            std::fprintf(stderr, "[vivid] Save: %s\n", result.message.c_str());
        };

        menu_cbs.on_open = [&]() {
            std::string path = vivid::ui::open_file_dialog();
            if (path.empty()) return;

            // Load the graph file (sets source_path internally)
            if (!graph.load(path.c_str())) {
                std::fprintf(stderr, "[vivid] Failed to load %s\n", path.c_str());
                return;
            }

            // Ensure operators used by this graph are fully loaded
            registry.load_for_graph(graph);

            // Rebuild via reload (re-reads from graph.source_path())
            auto result = runtime_api.reload(has_gpu_ops, has_audio);
            if (result.ok) graph_loaded = true;
            std::fprintf(stderr, "[vivid] Open: %s\n", result.message.c_str());
        };

        menu_cbs.on_export = [&]() {
            if (graph.source_path().empty()) {
                std::fprintf(stderr, "[vivid] Export: no graph loaded\n");
                return;
            }

            std::string output_path = vivid::ui::save_file_dialog("my_app");
            if (output_path.empty()) return;

            auto out = std::filesystem::path(output_path);
            std::string output_name = out.stem().string();
            std::string output_dir = (out.parent_path() / (output_name + "_export")).string();

            if (build_paths.source_dir.empty()) {
                std::fprintf(stderr, "[vivid] Export: cannot determine source directory\n");
                return;
            }

            vivid::ExportOptions opts;
            opts.graph_path = graph.source_path();
            opts.output_name = output_name;
            opts.output_dir = output_dir;

            vivid::ExportPipeline pipeline(build_paths.source_dir, build_paths.build_dir);
            if (pipeline.run(opts, registry)) {
                std::fprintf(stderr, "[vivid] Export succeeded: %s\n", output_name.c_str());
            } else {
                std::fprintf(stderr, "[vivid] Export failed\n");
            }
        };

        menu_cbs.on_browse_packages = [&]() {
            graph_ui.toggle_package_browser();
        };

        // Edit menu
        menu_cbs.on_delete_selected = [&]() { graph_ui.delete_selected(); };

        // View menu
        menu_cbs.on_toggle_ui = [&]() { graph_ui.toggle_visible(); };
        menu_cbs.on_toggle_bezier_wires = [&]() { graph_ui.set_bezier_wires(!graph_ui.bezier_wires()); };
        menu_cbs.on_toggle_session_grid = [&]() { graph_ui.toggle_session_grid(); };
        menu_cbs.on_toggle_midi_map = [&]() { graph_ui.toggle_midi_map_mode(); };

        // Insert menu
        menu_cbs.on_add_node = [&]() { graph_ui.open_chooser(); };

        // State queries for checkmarks / enable states
        menu_cbs.is_ui_visible = [&]() { return graph_ui.visible(); };
        menu_cbs.is_bezier_wires = [&]() { return graph_ui.bezier_wires(); };
        menu_cbs.is_session_grid_open = [&]() { return graph_ui.session_grid_open(); };
        menu_cbs.is_midi_map_mode = [&]() { return graph_ui.midi_map_mode(); };
        menu_cbs.has_selection = [&]() { return graph_ui.has_selection(); };

        vivid::macos_setup_menu(menu_cbs);
    }
#endif

    double prev_time = glfwGetTime();
    uint64_t frame_count = 0;

    // --- Main loop ---
    auto tick_frame = [&]() -> bool {
        // Close button may fire during macOS tracking (resize/menus).
        if (glfwWindowShouldClose(window)) return false;

        int win_w, win_h;
        glfwGetWindowSize(window, &win_w, &win_h);
        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);

        // Skip frame if minimized
        if (fb_w == 0 || fb_h == 0) return true;

        // Handle drag-and-drop graph loading
        if (!window_user_data.pending_drop_path.empty()) {
            std::string path = std::move(window_user_data.pending_drop_path);
            window_user_data.pending_drop_path.clear();
            if (graph.load(path.c_str())) {
                registry.load_for_graph(graph);
                auto result = runtime_api.reload(has_gpu_ops, has_audio);
                if (result.ok) graph_loaded = true;
                std::fprintf(stderr, "[vivid] Drop: %s\n", result.message.c_str());
            } else {
                std::fprintf(stderr, "[vivid] Drop: failed to load %s\n", path.c_str());
            }
        }

        // Reconfigure GPU surface if framebuffer size changed.
        if (fb_w != fb_width || fb_h != fb_height) {
            fb_width = fb_w;
            fb_height = fb_h;
            gpu.resize(static_cast<uint32_t>(fb_width), static_cast<uint32_t>(fb_height));
        }

        // Drain control server requests (may set pending topology changes)
        control_server.process_requests(runtime_api, graph, scheduler, registry,
                                        has_gpu_ops, has_audio);

        if (runtime_api.has_pending()) {
            runtime_api.apply_pending(has_gpu_ops, has_audio);
            // Re-allocate per-node GPU textures after topology change
            if (has_gpu_ops) {
                scheduler.allocate_gpu_textures(gpu.device(), kDefaultTexW, kDefaultTexH, kOffscreenFormat);
            }
            video_out_idx = has_gpu_ops ? scheduler.find_gpu_sink() : -1;
            capture_coordinator.set_audio_engine(has_audio ? &audio_engine : nullptr);
            // Evict thumbnail cache entries for removed nodes
            {
                std::unordered_set<std::string> active_ids;
                for (const auto& ns : scheduler.nodes())
                    active_ids.insert(ns.node_id);
                thumb_cache.retain_only(active_ids);
            }
        }
        // Handle GPU realloc after reload command or operator-requested resize
        if (runtime_api.needs_gpu_realloc() || scheduler.needs_gpu_realloc()) {
            runtime_api.clear_gpu_realloc();
            scheduler.clear_gpu_realloc();
            scheduler.allocate_gpu_textures(gpu.device(), kDefaultTexW, kDefaultTexH, kOffscreenFormat);
            video_out_idx = has_gpu_ops ? scheduler.find_gpu_sink() : -1;
        }
        if (!graph_loaded && !scheduler.nodes().empty()) {
            graph_loaded = true;
        }

        // --- Compute dt unconditionally (before GPU work) ---
        double now = glfwGetTime();
        double dt = now - prev_time;
        prev_time = now;
        graph_ui.set_dt(static_cast<float>(dt));

        // --- Apply MIDI mappings (before tick so wire wins on conflict) ---
        runtime_api.apply_midi_mappings();

        // --- Tick quantized variation switching ---
        runtime_api.tick_quantized_switch();

        // --- Try to acquire surface texture for presentation ---
        vivid::FrameState frame;
        bool have_surface = gpu.begin_frame(frame);

        // If no surface (e.g. during resize), create a standalone encoder
        // so offscreen GPU work (scheduler tick, thumbnails) still runs.
        WGPUCommandEncoder tick_encoder = nullptr;
        if (have_surface) {
            tick_encoder = frame.encoder;
        } else {
            WGPUCommandEncoderDescriptor enc_desc{};
            enc_desc.label = to_sv("Offscreen Tick Encoder");
            tick_encoder = wgpuDeviceCreateCommandEncoder(gpu.device(), &enc_desc);
        }

        // --- Tick graph (always runs, even without a surface) ---
        if (graph_loaded) {

            // Base GPU state (per-node textures are set by scheduler)
            VividGpuState gpu_state{};
            gpu_state.device              = gpu.device();
            gpu_state.queue               = gpu.queue();
            gpu_state.command_encoder     = tick_encoder;
            gpu_state.output_texture_view = nullptr;  // per-node
            gpu_state.output_width        = 0;
            gpu_state.output_height       = 0;
            gpu_state.output_format       = kOffscreenFormat;
            gpu_state.input_texture_views = nullptr;
            gpu_state.input_texture_count = 0;

            // --- Hot-reload polling ---
            if (hot_reload_enabled) {
                poll_hot_reload(file_watcher, hot_reloader, scheduler, registry,
                                audio_engine, has_audio, &op_info_cache,
                                scheduler.operators_src_dir());
            }

            if (has_audio) {
                audio_engine.inject_analysis(scheduler);
                audio_engine.update_sources(now, scheduler);
            }

            // --- Build input state for operators (when UI hidden) ---
            const VividInputState* input_ptr = nullptr;
            VividInputState input_state{};
            if (!window_user_data.pending_events.empty() ||
                (window_user_data.buttons_held && !(graph_ui.visible()))) {
                // Compute inverse blit_fit transform: window coords → [0,1] texture UV
                float scale_x = 1.0f, scale_y = 1.0f;
                float offset_x = 0.0f, offset_y = 0.0f;
                if (has_gpu_ops && video_out_idx >= 0 && fb_width > 0 && fb_height > 0) {
                    uint32_t src_w = 0, src_h = 0;
                    scheduler.gpu_sink_source_size(video_out_idx, src_w, src_h);
                    if (src_w > 0 && src_h > 0) {
                        const auto& vo_ns = scheduler.nodes()[video_out_idx];
                        auto fit_mode = vivid::FitMode::Fit;
                        auto fm_it = vo_ns.param_indices.find("fit_mode");
                        if (fm_it != vo_ns.param_indices.end() && fm_it->second < vo_ns.param_values.size())
                            fit_mode = static_cast<vivid::FitMode>(
                                static_cast<int>(vo_ns.param_values[fm_it->second]));

                        float src_aspect = static_cast<float>(src_w) / static_cast<float>(src_h);
                        float dst_aspect = static_cast<float>(fb_width) / static_cast<float>(fb_height);

                        if (fit_mode == vivid::FitMode::Stretch) {
                            scale_x = 1.0f; scale_y = 1.0f;
                        } else if (fit_mode == vivid::FitMode::Fit) {
                            if (src_aspect > dst_aspect) {
                                scale_x = 1.0f; scale_y = dst_aspect / src_aspect;
                            } else {
                                scale_x = src_aspect / dst_aspect; scale_y = 1.0f;
                            }
                        } else { // Fill
                            if (src_aspect > dst_aspect) {
                                scale_x = src_aspect / dst_aspect; scale_y = 1.0f;
                            } else {
                                scale_x = 1.0f; scale_y = dst_aspect / src_aspect;
                            }
                        }
                        offset_x = (1.0f - scale_x) * 0.5f;
                        offset_y = (1.0f - scale_y) * 0.5f;
                    }
                }

                // Normalize mouse coords in all pending events: window px → [0,1] texture UV
                // ndc = cursor_pos / win_size;  tex_uv = (ndc - offset) / scale
                float inv_w = (win_w > 0) ? 1.0f / static_cast<float>(win_w) : 0.0f;
                float inv_h = (win_h > 0) ? 1.0f / static_cast<float>(win_h) : 0.0f;
                float inv_sx = (scale_x > 0.0f) ? 1.0f / scale_x : 0.0f;
                float inv_sy = (scale_y > 0.0f) ? 1.0f / scale_y : 0.0f;

                for (auto& ev : window_user_data.pending_events) {
                    float ndc_x = ev.mouse_x * inv_w;
                    float ndc_y = ev.mouse_y * inv_h;
                    ev.mouse_x = (ndc_x - offset_x) * inv_sx;
                    ev.mouse_y = (ndc_y - offset_y) * inv_sy;
                }

                float cur_ndc_x = static_cast<float>(window_user_data.raw_mouse_x) * inv_w;
                float cur_ndc_y = static_cast<float>(window_user_data.raw_mouse_y) * inv_h;

                input_state.events = window_user_data.pending_events.data();
                input_state.event_count = static_cast<uint32_t>(window_user_data.pending_events.size());
                input_state.mouse_x = (cur_ndc_x - offset_x) * inv_sx;
                input_state.mouse_y = (cur_ndc_y - offset_y) * inv_sy;
                input_state.buttons_held = window_user_data.buttons_held;
                input_state.modifiers = window_user_data.current_mods;
                input_ptr = &input_state;
            }

            // Tick with thumbnail capture callback for GPU nodes
            scheduler.tick(now, dt, frame_count, &gpu_state,
                [&](uint32_t, const std::string& node_id, WGPUTextureView node_tex_view) {
                    // Blit per-node texture → thumbnail (uses RGBA16Float pipeline)
                    if (!node_tex_view) return;
                    auto* thumb_view = thumb_cache.get_or_create(node_id);
                    if (thumb_view) {
                        thumb_blit.blit(tick_encoder, node_tex_view, thumb_view);
                    }
                },
                input_ptr);

            // Clear consumed input events
            window_user_data.pending_events.clear();

            draw_custom_thumbnails(scheduler, thumb_cache, graph_ui, now, kThumbW, kThumbH);

            // --- Tick state-preset mappings (after scheduler tick, state outputs are fresh) ---
            runtime_api.tick_state_presets();

            if (has_audio) {
                audio_engine.push_params(scheduler);
            }

            // Process capture/recording requests (after tick, textures are fresh)
            if (capture_coordinator.has_pending() || capture_coordinator.is_recording()) {
                WGPUTexture cap_tex = nullptr;
                uint32_t cap_w = 0, cap_h = 0;
                if (has_gpu_ops && video_out_idx >= 0) {
                    // Find the source node's texture (upstream of video_out)
                    cap_tex = scheduler.gpu_sink_source_texture(video_out_idx);
                    scheduler.gpu_sink_source_size(video_out_idx, cap_w, cap_h);
                }
                if (capture_coordinator.has_pending())
                    capture_coordinator.process_pending(
                        gpu.device(), gpu.queue(), cap_tex, cap_w, cap_h);
                if (capture_coordinator.is_recording())
                    capture_coordinator.tick_recording(
                        gpu.device(), gpu.queue(), cap_tex, cap_w, cap_h);
            }

            if (frame_count % 60 == 0) {
                std::fprintf(stderr, "[vivid] frame=%llu",
                    static_cast<unsigned long long>(frame_count));
                for (const auto& ns : scheduler.nodes()) {
                    for (const auto& [port_name, port_idx] : ns.output_port_indices) {
                        std::fprintf(stderr, " | %s/%s=%.4f",
                            ns.node_id.c_str(), port_name.c_str(),
                            ns.output_values[port_idx]);
                    }
                }
                std::fprintf(stderr, "\n");
            }
            frame_count++;
        }

        if (have_surface) {
            // --- Surface presentation path ---
            if (has_gpu_ops && video_out_idx >= 0) {
                // Find video_out's input texture from its resolved_tex_inputs
                const auto& vo_ns = scheduler.nodes()[video_out_idx];
                WGPUTextureView display_tex = nullptr;
                uint32_t src_w = 0, src_h = 0;
                if (!vo_ns.resolved_tex_inputs.empty()) {
                    display_tex = vo_ns.resolved_tex_inputs[0];
                    scheduler.gpu_sink_source_size(video_out_idx, src_w, src_h);
                }

                if (display_tex && src_w > 0 && src_h > 0) {
                    auto fit_mode = vivid::FitMode::Fit;
                    auto fm_it = vo_ns.param_indices.find("fit_mode");
                    if (fm_it != vo_ns.param_indices.end() && fm_it->second < vo_ns.param_values.size())
                        fit_mode = static_cast<vivid::FitMode>(static_cast<int>(vo_ns.param_values[fm_it->second]));
                    bool ui_vis = graph_ui.visible();
                    blit.blit_fit(frame.encoder, display_tex, frame.view,
                                  src_w, src_h,
                                  static_cast<uint32_t>(fb_width),
                                  static_cast<uint32_t>(fb_height),
                                  fit_mode, ui_vis);
                } else {
                    emit_clear_pass(frame.encoder, frame.view, clear);
                }
            } else {
                emit_clear_pass(frame.encoder, frame.view, clear);
            }

            // --- Node graph UI overlay (2-pass rendering) ---
            if (text_renderer_ok && graph_ui.visible()) {
                auto snapshot = build_graph_snapshot(
                    graph, scheduler, has_audio ? &audio_engine : nullptr,
                    registry, op_info_cache, &system_midi, &runtime_api,
                    &capture_coordinator);
                graph_ui.update(snapshot);
                graph_ui.draw(text_renderer, static_cast<uint32_t>(win_w), static_cast<uint32_t>(win_h));
                // Pass 1: text/rects
                text_renderer.flush(frame.encoder, frame.view, static_cast<uint32_t>(win_w), static_cast<uint32_t>(win_h));
                // Pass 2: thumbnails (GPU auto-captured + CPU custom, composited over text)
                if (thumb_renderer_ok) {
                    graph_ui.draw_thumbnails(thumb_renderer, thumb_cache,
                                             frame.encoder, frame.view,
                                             static_cast<uint32_t>(fb_width),
                                             static_cast<uint32_t>(fb_height));
                }
                // Pass 3: overlays (context menu, dropdown) on top of thumbnails
                graph_ui.draw_overlays(text_renderer);
                text_renderer.flush(frame.encoder, frame.view, static_cast<uint32_t>(win_w), static_cast<uint32_t>(win_h));
            }

            // --- Screenshot capture ---
            if (try_capture_screenshot(screenshot_path, gpu, frame, fb_width, fb_height,
                                       frame_count, screenshot_delay, window)) {
                return true; // frame already submitted inside try_capture_screenshot
            }

            gpu.end_frame(frame);
        } else {
            // No surface — submit offscreen GPU work (scheduler tick, thumbnails)
            // and poll the device so audio/compute operators still advance.
            WGPUCommandBufferDescriptor cmd_desc{};
            cmd_desc.label = to_sv("Offscreen Commands");
            WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(tick_encoder, &cmd_desc);
            wgpuQueueSubmit(gpu.queue(), 1, &cmd);
            wgpuCommandBufferRelease(cmd);
            wgpuCommandEncoderRelease(tick_encoder);
        }

        // wgpu-native: poll the device to process async operations
        wgpuDevicePoll(gpu.device(), false, nullptr);
        return true;
    };

#ifdef __APPLE__
    auto poll_events = [&]() -> bool {
        glfwPollEvents();
        return !glfwWindowShouldClose(window);
    };
    vivid::macos_run_frame_loop(poll_events, tick_frame);
#else
    while (true) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window)) break;
        if (!tick_frame()) break;
    }
#endif

    // --- Shutdown ---
    system_midi.close();
    control_server.stop();
    if (hot_reload_enabled) {
        file_watcher.stop();
        hot_reloader.stop();
    }
    if (has_audio) {
        audio_engine.shutdown();
    }
    if (graph_loaded) {
        scheduler.shutdown();
    }

    if (text_renderer_ok) {
        text_renderer.shutdown();
    }
    thumb_renderer.shutdown();
    thumb_cache.shutdown();
    thumb_blit.shutdown();
    blit.shutdown();
    gpu.shutdown();

    // Save window geometry for next launch
    {
        vivid::Settings s = settings;  // preserve editor/style prefs
        glfwGetWindowPos(window, &s.window_x, &s.window_y);
        glfwGetWindowSize(window, &s.window_width, &s.window_height);
        s.bezier_wires = graph_ui.bezier_wires();
        vivid::save_settings(s);
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    std::fprintf(stderr, "[vivid] Clean shutdown\n");
    return 0;
}
