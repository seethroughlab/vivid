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
#include "runtime/system_midi.h"
#include "runtime/settings.h"
#include "runtime/editor_detect.h"
#include "runtime/operator_info_cache.h"
#include "runtime/runtime_command_sink.h"
#include "ui/ui_style.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/data_driven_filter.h"
#include "operator_api/types.h"
#include "common/gpu_util.h"
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
        vivid::SystemMidiListener* system_midi = nullptr) {
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
        sn.output_values = ns.output_values;
        sn.output_spreads = ns.output_spreads;
        for (const auto& [name, idx] : ns.file_param_indices)
            sn.file_param_values[name] = ns.file_param_storage[idx];
        sn.gpu_tex_width = ns.gpu_tex_width;
        sn.gpu_tex_height = ns.gpu_tex_height;

        // Layout from graph
        const auto* ndef = graph.find_node(ns.node_id);
        if (ndef && ndef->has_layout()) {
            sn.layout_x = ndef->layout_x;
            sn.layout_y = ndef->layout_y;
            sn.has_layout = true;
        }

        // Operator info (cached)
        sn.op_info = op_cache.get(sn.type_name, registry);

        // Index
        snap.node_index[ns.node_id] = i;
    }

    // Connections
    snap.connections.resize(conns.size());
    for (size_t i = 0; i < conns.size(); ++i) {
        snap.connections[i] = {conns[i].from_node, conns[i].from_port,
                               conns[i].to_node, conns[i].to_port};
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
    }

    // Operator catalog
    snap.operator_types = registry.type_names();
    std::sort(snap.operator_types.begin(), snap.operator_types.end());
    for (const auto& tn : snap.operator_types) {
        auto info = op_cache.get(tn, registry);
        if (info) snap.operator_catalog[tn] = info;
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
                            OperatorInfoCache* op_cache = nullptr) {
    auto changes = fw.poll_changes();
    for (const auto& change : changes) {
        hr.queue_rebuild(change.target_name);
    }

    auto ready = hr.poll_ready();
    for (const auto& result : ready) {
        if (!result.success) continue;

        const std::string* type_name_ptr = registry.type_name_for_target(result.target_name);
        if (!type_name_ptr) {
            std::fprintf(stderr, "[vivid] Hot-reload: unknown target '%s'\n",
                result.target_name.c_str());
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
};

static void char_callback(GLFWwindow* w, unsigned int codepoint) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud) return;
    if (ud->graph_ui && ud->graph_ui->visible() && ud->graph_ui->wants_keyboard())
        ud->graph_ui->on_char(codepoint);
}

static void key_callback(GLFWwindow* w, int key, int /*scancode*/, int action, int mods) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud) return;

    // Tilde toggles graph UI visibility (intercept before any dispatch)
    if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS && mods == 0) {
        if (ud->graph_ui) ud->graph_ui->toggle_visible();
        return;
    }

    // Cmd+S / Ctrl+S saves the graph (intercept before any dispatch)
    if (key == GLFW_KEY_S && action == GLFW_PRESS &&
        (mods & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL))) {
        if (ud->runtime_api) {
            // Capture viewport before saving
            if (ud->graph && ud->graph_ui)
                ud->graph->set_viewport(ud->graph_ui->pan_x(), ud->graph_ui->pan_y(), ud->graph_ui->zoom());
            // Read back working filter shaders before saving
            if (ud->graph && !ud->working_filters_dir.empty()) {
                for (const auto& fd : ud->graph->filters()) {
                    std::string wpath = ud->working_filters_dir + "/" + fd.name + ".wgsl";
                    std::ifstream ifs(wpath);
                    if (ifs) {
                        std::ostringstream ss;
                        ss << ifs.rdbuf();
                        ud->graph->update_filter_shader(fd.name, ss.str());
                    }
                }
            }
            auto result = ud->runtime_api->save();
            std::fprintf(stderr, "[vivid] Save: %s\n", result.message.c_str());
        }
        return;
    }

    // Cmd+, / Ctrl+, opens preferences
    if (key == GLFW_KEY_COMMA && action == GLFW_PRESS &&
        (mods & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL))) {
        if (ud->graph_ui) ud->graph_ui->toggle_preferences();
        return;
    }

    if (ud->graph_ui && ud->graph_ui->visible())
        ud->graph_ui->on_key(key, action, mods);
}

static void cursor_pos_callback(GLFWwindow* w, double xpos, double ypos) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (ud && ud->graph_ui && ud->graph_ui->visible())
        ud->graph_ui->on_mouse_move(static_cast<float>(xpos), static_cast<float>(ypos));
}

static void mouse_button_callback(GLFWwindow* w, int button, int action, int /*mods*/) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (ud && ud->graph_ui && ud->graph_ui->visible())
        ud->graph_ui->on_mouse_button(button, action);
}

static void scroll_callback(GLFWwindow* w, double xoffset, double yoffset) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (ud && ud->graph_ui && ud->graph_ui->visible())
        ud->graph_ui->on_scroll(
            static_cast<float>(xoffset), static_cast<float>(yoffset));
}

int main(int argc, char* argv[]) {
    // Derive exe directory so resource lookup works from any CWD
    auto exe_path = std::filesystem::canonical(std::filesystem::path(argv[0]));
    auto exe_dir = exe_path.parent_path();

    const char* graph_path = (argc > 1) ? argv[1] : "graph.json";

    // --- Screenshot / headless flags ---
    std::string screenshot_path;
    int screenshot_delay = 5;
    bool headless = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot_path = argv[++i];
        } else if (std::strcmp(argv[i], "--screenshot-delay") == 0 && i + 1 < argc) {
            screenshot_delay = std::atoi(argv[++i]);
            if (screenshot_delay < 1) screenshot_delay = 1;
        } else if (std::strcmp(argv[i], "--headless") == 0) {
            headless = true;
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
    registry.scan_deferred(exe_dir.string().c_str());  // probe only — no full loads
    register_builtin_operators(registry);

    // --- Load graph ---
    vivid::Graph graph;
    vivid::Scheduler scheduler;
    bool graph_loaded = false;

    // Working directory for user filter shaders: {graph_dir}/{graph_stem}_filters/
    std::string working_filters_dir;

    if (graph.load(graph_path)) {
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
    vivid::ControlServer control_server;
    control_server.start(9876);

    vivid::ui::Renderer2D text_renderer;
    bool text_renderer_ok = false;
    {
        // Look for font next to executable, or in source tree
        std::string font_path = (exe_dir / "JetBrainsMono-Regular.ttf").string();
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
    vivid::ui::NodeGraphUI graph_ui(command_sink);
    graph_ui.set_dpi_scale(dpi_scale);
    graph_ui.set_bezier_wires(settings.bezier_wires);
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

        auto styles = vivid::ui::builtin_styles();
        int style_sel = 0;
        for (size_t i = 0; i < styles.size(); ++i) {
            if (styles[i].id == settings.style_id)
                style_sel = static_cast<int>(i);
        }
        graph_ui.set_style_options(std::move(styles), style_sel);
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

    // --- Hot-reload ---
    vivid::FileWatcher file_watcher;
    vivid::HotReloader hot_reloader;
    bool hot_reload_enabled = false;
    {
        std::string src_dir;
        for (int i = 1; i < argc - 1; ++i) {
            if (std::strcmp(argv[i], "--src-dir") == 0) {
                src_dir = argv[i + 1];
                break;
            }
        }
        if (src_dir.empty()) {
            auto parent = exe_dir.parent_path();
            if (std::filesystem::exists(parent / "operators")) {
                src_dir = parent.string();
            }
        }

        if (!src_dir.empty()) {
            std::string operators_dir = src_dir + "/operators";
            std::string build_dir = exe_dir.string();
            scheduler.set_operators_src_dir(operators_dir);
            command_sink.set_operators_dir(operators_dir);
            command_sink.set_build_dir(build_dir);
            op_info_cache.set_operators_dir(operators_dir);
            // Set working filters dir if not already determined from graph
            if (working_filters_dir.empty() && !graph.source_path().empty()) {
                auto gp = std::filesystem::path(graph.source_path());
                working_filters_dir = (gp.parent_path() / (gp.stem().string() + "_filters")).string();
                command_sink.set_working_filters_dir(working_filters_dir);
            }
            if (file_watcher.start(operators_dir)) {
                hot_reloader.start(build_dir);
                hot_reload_enabled = true;
                std::fprintf(stderr, "[vivid] Hot-reload enabled (watching %s)\n", operators_dir.c_str());
            }
        } else {
            std::fprintf(stderr, "[vivid] Hot-reload disabled (operators/ not found; use --src-dir)\n");
        }
    }

    double prev_time = glfwGetTime();
    uint64_t frame_count = 0;

    // --- Main loop ---
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        int win_w, win_h;
        glfwGetWindowSize(window, &win_w, &win_h);
        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);

        // Skip frame if minimized
        if (fb_w == 0 || fb_h == 0) continue;

        // Reconfigure GPU surface if framebuffer size changed
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

        vivid::FrameState frame;
        if (!gpu.begin_frame(frame))
            continue;

        // --- Compute dt unconditionally (for perf stats even with no graph) ---
        double now = glfwGetTime();
        double dt = now - prev_time;
        prev_time = now;
        graph_ui.set_dt(static_cast<float>(dt));

        // --- Apply MIDI mappings (before tick so wire wins on conflict) ---
        runtime_api.apply_midi_mappings();

        // --- Tick graph ---
        if (graph_loaded) {

            // Base GPU state (per-node textures are set by scheduler)
            VividGpuState gpu_state{};
            gpu_state.device              = gpu.device();
            gpu_state.queue               = gpu.queue();
            gpu_state.command_encoder     = frame.encoder;
            gpu_state.output_texture_view = nullptr;  // per-node
            gpu_state.output_width        = 0;
            gpu_state.output_height       = 0;
            gpu_state.output_format       = kOffscreenFormat;
            gpu_state.input_texture_views = nullptr;
            gpu_state.input_texture_count = 0;

            // --- Hot-reload polling ---
            if (hot_reload_enabled) {
                poll_hot_reload(file_watcher, hot_reloader, scheduler, registry,
                                audio_engine, has_audio, &op_info_cache);
            }

            if (has_audio) {
                audio_engine.inject_analysis(scheduler);
                audio_engine.update_sources(now, scheduler);
            }

            // Tick with thumbnail capture callback for GPU nodes
            scheduler.tick(now, dt, frame_count, &gpu_state,
                [&](uint32_t, const std::string& node_id, WGPUTextureView node_tex_view) {
                    // Blit per-node texture → thumbnail (uses RGBA16Float pipeline)
                    if (!node_tex_view) return;
                    auto* thumb_view = thumb_cache.get_or_create(node_id);
                    if (thumb_view) {
                        thumb_blit.blit(frame.encoder, node_tex_view, thumb_view);
                    }
                });

            draw_custom_thumbnails(scheduler, thumb_cache, graph_ui, now, kThumbW, kThumbH);

            if (has_audio) {
                audio_engine.push_params(scheduler);
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
                registry, op_info_cache, &system_midi);
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
            continue; // frame already submitted inside try_capture_screenshot
        }

        gpu.end_frame(frame);

        // wgpu-native: poll the device to process async operations
        wgpuDevicePoll(gpu.device(), false, nullptr);
    }

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
