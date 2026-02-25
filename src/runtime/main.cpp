#include "runtime/gpu_context.h"
#include "runtime/fullscreen_blit.h"
#include "runtime/thumbnail_cache.h"
#include "runtime/thumbnail_renderer.h"
#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/audio_engine.h"
#include "runtime/file_watcher.h"
#include "runtime/hot_reload.h"
#include "runtime/runtime_api.h"
#include "runtime/text_renderer.h"
#include "runtime/repl.h"
#include "runtime/node_graph.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/types.h"
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unordered_set>

// #16191D in sRGB → linear: pow(x/255, 2.2)
static constexpr double kClearLinear[4]  = { 0.00699, 0.00821, 0.01041, 1.0 };
// #16191D as raw unorm (no gamma conversion)
static constexpr double kClearRaw[4]     = { 0.0863, 0.0980, 0.1137, 1.0 };

static constexpr uint32_t kWidth  = 1280;
static constexpr uint32_t kHeight = 800;

// Thumbnail size: node width × 16:10 aspect
static constexpr uint32_t kThumbW = 140;
static constexpr uint32_t kThumbH = 88;

static bool is_srgb_format(WGPUTextureFormat fmt) {
    switch (fmt) {
        case WGPUTextureFormat_RGBA8UnormSrgb:
        case WGPUTextureFormat_BGRA8UnormSrgb:
            return true;
        default:
            return false;
    }
}

static WGPUStringView to_sv(const char* s) {
    return { s, s ? std::strlen(s) : 0 };
}

// GLFW callback trampolines
struct WindowUserData {
    vivid::Repl* repl = nullptr;
    vivid::NodeGraphUI* graph_ui = nullptr;
};

static void char_callback(GLFWwindow* w, unsigned int codepoint) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (ud && ud->repl) ud->repl->on_char(codepoint);
}

static void key_callback(GLFWwindow* w, int key, int /*scancode*/, int action, int mods) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (ud && ud->repl) ud->repl->on_key(key, action, mods);
}

static void cursor_pos_callback(GLFWwindow* w, double xpos, double ypos) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (ud && ud->graph_ui) ud->graph_ui->on_mouse_move(static_cast<float>(xpos), static_cast<float>(ypos));
}

static void mouse_button_callback(GLFWwindow* w, int button, int action, int /*mods*/) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (ud && ud->graph_ui) ud->graph_ui->on_mouse_button(button, action);
}

int main(int argc, char* argv[]) {
    // Derive exe directory so resource lookup works from any CWD
    auto exe_path = std::filesystem::canonical(std::filesystem::path(argv[0]));
    auto exe_dir = exe_path.parent_path();

    const char* graph_path = (argc > 1) ? argv[1] : "graph.json";

    // --- GLFW ---
    if (!glfwInit()) {
        std::fprintf(stderr, "[vivid] Failed to init GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(kWidth, kHeight, "Vivid", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[vivid] Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    // --- GPU ---
    vivid::GpuContext gpu;
    if (!gpu.init(window, kWidth, kHeight)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    const double* clear = is_srgb_format(gpu.surface_format()) ? kClearLinear : kClearRaw;

    // --- Offscreen texture (RGBA16Float, for GPU operators to render into) ---
    static constexpr WGPUTextureFormat kOffscreenFormat = WGPUTextureFormat_RGBA16Float;

    WGPUTextureDescriptor offscreen_desc{};
    offscreen_desc.label = to_sv("Offscreen Texture");
    offscreen_desc.size = { kWidth, kHeight, 1 };
    offscreen_desc.mipLevelCount = 1;
    offscreen_desc.sampleCount = 1;
    offscreen_desc.dimension = WGPUTextureDimension_2D;
    offscreen_desc.format = kOffscreenFormat;
    offscreen_desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    WGPUTexture offscreen_tex = wgpuDeviceCreateTexture(gpu.device(), &offscreen_desc);

    WGPUTextureViewDescriptor offscreen_view_desc{};
    offscreen_view_desc.label = to_sv("Offscreen View");
    offscreen_view_desc.format = kOffscreenFormat;
    offscreen_view_desc.dimension = WGPUTextureViewDimension_2D;
    offscreen_view_desc.baseMipLevel = 0;
    offscreen_view_desc.mipLevelCount = 1;
    offscreen_view_desc.baseArrayLayer = 0;
    offscreen_view_desc.arrayLayerCount = 1;
    offscreen_view_desc.aspect = WGPUTextureAspect_All;
    WGPUTextureView offscreen_view = wgpuTextureCreateView(offscreen_tex, &offscreen_view_desc);

    // --- Fullscreen blit (offscreen → surface) ---
    vivid::FullscreenBlit blit;
    if (!blit.init(gpu.device(), gpu.surface_format())) {
        std::fprintf(stderr, "[vivid] Failed to init FullscreenBlit\n");
        wgpuTextureViewRelease(offscreen_view);
        wgpuTextureRelease(offscreen_tex);
        gpu.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // --- Thumbnail cache + renderer ---
    vivid::ThumbnailCache thumb_cache;
    thumb_cache.init(gpu.device(), gpu.queue(), kThumbW, kThumbH);

    // Separate blit pipeline for offscreen→thumbnail (targets RGBA16Float, not surface format)
    vivid::FullscreenBlit thumb_blit;
    if (!thumb_blit.init(gpu.device(), kOffscreenFormat)) {
        std::fprintf(stderr, "[vivid] Failed to init thumbnail blit\n");
    }

    vivid::ThumbnailRenderer thumb_renderer;
    bool thumb_renderer_ok = thumb_renderer.init(gpu.device(), gpu.surface_format());

    // --- Load operator plugins ---
    vivid::OperatorRegistry registry;
    registry.scan(exe_dir.string().c_str());

    // --- Load graph ---
    vivid::Graph graph;
    vivid::Scheduler scheduler;
    bool graph_loaded = false;

    if (graph.load(graph_path)) {
        if (scheduler.build(graph, registry)) {
            graph_loaded = true;
        } else {
            std::fprintf(stderr, "[vivid] Scheduler build failed (non-fatal, continuing)\n");
        }
    } else {
        std::fprintf(stderr, "[vivid] Graph load failed (non-fatal, continuing)\n");
    }

    bool has_gpu_ops = graph_loaded && scheduler.has_gpu_operators();

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

    // --- RuntimeAPI + REPL ---
    vivid::RuntimeAPI runtime_api(graph, scheduler, audio_engine, registry);

    vivid::TextRenderer text_renderer;
    bool repl_enabled = false;
    {
        // Look for font next to executable, or in source tree
        std::string font_path = (exe_dir / "JetBrainsMono-Regular.ttf").string();
        if (!std::filesystem::exists(font_path)) {
            auto alt = exe_dir.parent_path() / "fonts" / "JetBrainsMono-Regular.ttf";
            if (std::filesystem::exists(alt)) font_path = alt.string();
        }
        if (text_renderer.init(gpu.device(), gpu.surface_format(), font_path.c_str(), 16.0f)) {
            repl_enabled = true;
        } else {
            std::fprintf(stderr, "[vivid] REPL disabled (font not found)\n");
        }
    }

    vivid::Repl repl(runtime_api);
    vivid::NodeGraphUI graph_ui(runtime_api, graph, scheduler,
                                has_audio ? &audio_engine : nullptr);

    // Set up GLFW input callbacks
    WindowUserData window_user_data;
    window_user_data.repl = &repl;
    window_user_data.graph_ui = &graph_ui;
    glfwSetWindowUserPointer(window, &window_user_data);
    glfwSetCharCallback(window, char_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);

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

        // --- REPL update (process pending command before tick) ---
        repl.update(has_gpu_ops, has_audio);
        if (!graph_loaded && !scheduler.nodes().empty()) {
            graph_loaded = true;
        }

        vivid::FrameState frame;
        if (!gpu.begin_frame(frame))
            continue;

        // --- Tick graph ---
        if (graph_loaded) {
            double now = glfwGetTime();
            double dt = now - prev_time;
            prev_time = now;

            // Fill GPU state for GPU operators (render to offscreen texture)
            VividGpuState gpu_state{};
            gpu_state.device              = gpu.device();
            gpu_state.queue               = gpu.queue();
            gpu_state.command_encoder     = frame.encoder;
            gpu_state.output_texture_view = offscreen_view;
            gpu_state.output_width        = kWidth;
            gpu_state.output_height       = kHeight;
            gpu_state.output_format       = kOffscreenFormat;

            // --- Hot-reload polling ---
            if (hot_reload_enabled) {
                auto changes = file_watcher.poll_changes();
                for (const auto& change : changes) {
                    hot_reloader.queue_rebuild(change.target_name);
                }

                auto ready = hot_reloader.poll_ready();
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

                    bool is_audio_op = false;
                    for (const auto& ns : scheduler.nodes()) {
                        if (std::string(ns.loader->descriptor()->name) == tn &&
                            ns.loader->descriptor()->domain == VIVID_DOMAIN_AUDIO) {
                            is_audio_op = true;
                            break;
                        }
                    }

                    if (is_audio_op && has_audio) {
                        audio_engine.pause();
                    }

                    if (scheduler.reload_operator(tn, registry, result.staged_dylib_path)) {
                        if (is_audio_op && has_audio) {
                            audio_engine.reload_operator(tn, registry);
                        }
                        std::fprintf(stderr, "[vivid] Hot-reload: %s reloaded successfully\n", tn.c_str());
                    } else {
                        std::fprintf(stderr, "[vivid] Hot-reload: %s reload FAILED\n", tn.c_str());
                    }

                    if (is_audio_op && has_audio) {
                        audio_engine.resume();
                    }
                }
            }

            if (has_audio) {
                audio_engine.inject_analysis(scheduler);
            }

            // Tick with thumbnail capture callback for GPU nodes
            scheduler.tick(now, dt, frame_count, &gpu_state,
                [&](uint32_t, const std::string& node_id) {
                    // Blit offscreen → per-node thumbnail (uses RGBA16Float pipeline)
                    auto* thumb_view = thumb_cache.get_or_create(node_id);
                    if (thumb_view) {
                        thumb_blit.blit(frame.encoder, offscreen_view, thumb_view);
                    }
                });

            // Custom thumbnail pass: call draw_thumbnail for operators that implement it
            {
                std::vector<uint8_t> thumb_pixels(kThumbW * kThumbH * 4);
                std::unordered_set<std::string> custom_thumb_ids;
                for (const auto& ns : scheduler.nodes()) {
                    if (!ns.loader->has_draw_thumbnail()) continue;
                    VividThumbnailContext tctx{};
                    tctx.pixels = thumb_pixels.data();
                    tctx.width = kThumbW;
                    tctx.height = kThumbH;
                    tctx.stride = kThumbW * 4;
                    tctx.time = now;
                    tctx.output_values = const_cast<float*>(ns.output_values.data());
                    tctx.output_count = ns.output_port_count;
                    tctx.param_values = const_cast<float*>(ns.param_values.data());
                    tctx.param_count = static_cast<uint32_t>(ns.param_values.size());
                    std::memset(thumb_pixels.data(), 0, thumb_pixels.size());
                    ns.loader->draw_thumbnail(ns.instance, &tctx);
                    thumb_cache.upload_cpu(ns.node_id, thumb_pixels.data());
                    custom_thumb_ids.insert(ns.node_id);
                }
                graph_ui.set_custom_thumbnail_nodes(std::move(custom_thumb_ids));
            }

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

        if (has_gpu_ops) {
            // Blit offscreen texture → surface
            blit.blit(frame.encoder, offscreen_view, frame.view);
        } else {
            // Fallback: clear-only pass (#16191D)
            WGPURenderPassColorAttachment color_att{};
            color_att.nextInChain = nullptr;
            color_att.view = frame.view;
            color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            color_att.resolveTarget = nullptr;
            color_att.loadOp = WGPULoadOp_Clear;
            color_att.storeOp = WGPUStoreOp_Store;
            color_att.clearValue = { clear[0], clear[1], clear[2], clear[3] };

            WGPURenderPassDescriptor rp_desc{};
            rp_desc.nextInChain = nullptr;
            rp_desc.label = to_sv("Clear Pass");
            rp_desc.colorAttachmentCount = 1;
            rp_desc.colorAttachments = &color_att;
            rp_desc.depthStencilAttachment = nullptr;
            rp_desc.timestampWrites = nullptr;

            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(frame.encoder, &rp_desc);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }

        // --- Node graph UI + REPL overlay (3-pass rendering) ---
        if (repl_enabled) {
            graph_ui.update();
            graph_ui.draw(text_renderer, kWidth, kHeight);
            // Pass 1: text/rects
            text_renderer.flush(frame.encoder, frame.view, kWidth, kHeight);
            // Pass 2: thumbnails (GPU auto-captured + CPU custom, composited over text)
            if (thumb_renderer_ok) {
                graph_ui.draw_thumbnails(thumb_renderer, thumb_cache,
                                         frame.encoder, frame.view, kWidth, kHeight);
            }
            // Pass 3: REPL on top
            repl.draw(text_renderer, kWidth, kHeight);
            text_renderer.flush(frame.encoder, frame.view, kWidth, kHeight);
        }

        gpu.end_frame(frame);

        // wgpu-native: poll the device to process async operations
        wgpuDevicePoll(gpu.device(), false, nullptr);
    }

    // --- Shutdown ---
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

    if (repl_enabled) {
        text_renderer.shutdown();
    }
    thumb_renderer.shutdown();
    thumb_cache.shutdown();
    thumb_blit.shutdown();
    blit.shutdown();
    wgpuTextureViewRelease(offscreen_view);
    wgpuTextureRelease(offscreen_tex);
    gpu.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();

    std::fprintf(stderr, "[vivid] Clean shutdown\n");
    return 0;
}
