#pragma once

#include <webgpu/webgpu.h>
#include <GLFW/glfw3.h>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

class OperatorInfoCache;

namespace vivid {

class FileWatcher;
class HotReloader;
class RuntimeCore;
class OperatorRegistry;
class AudioEngine;
class BuildConsole;

} // namespace vivid

namespace vivid::ui {
class NodeGraphUI;
class ThumbnailCache;
class Renderer2D;
} // namespace vivid::ui

namespace vivid {

// --- String / format utilities ---
std::string url_encode(const std::string& text);
std::string platform_label();
std::string now_epoch_seconds_str();
std::vector<std::string> json_str_array(const nlohmann::json& arr);
std::string trim_copy(const std::string& s);
std::vector<std::string> split_csv(const std::string& csv);
std::string join_csv(const std::vector<std::string>& items);

bool is_srgb_format(WGPUTextureFormat fmt);

// --- Phase timer ---
struct PhaseTimer {
    const char* label;
    std::chrono::steady_clock::time_point start;
    PhaseTimer(const char* l) : label(l), start(std::chrono::steady_clock::now()) {}
    ~PhaseTimer() {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        std::fprintf(stderr, "[vivid] TIMING: %s took %lldms\n", label, ms);
    }
};

// --- GPU utilities ---
void emit_clear_pass(WGPUCommandEncoder encoder, WGPUTextureView view, const double clear[4]);

// --- Hot reload ---
void poll_hot_reload(FileWatcher& fw, HotReloader& hr,
                     RuntimeCore& runtime, OperatorRegistry& registry,
                     AudioEngine& audio_engine, bool has_audio,
                     OperatorInfoCache* op_cache = nullptr,
                     const std::string& operators_dir = {});

int add_watch_for_resolved_package(FileWatcher& fw, const struct PackageInfo& pkg);

// --- Custom thumbnails ---
void draw_custom_thumbnails(const RuntimeCore& runtime,
                            vivid::ui::ThumbnailCache& cache,
                            vivid::ui::NodeGraphUI& graph_ui,
                            vivid::ui::Renderer2D* thumb_draw_renderer,
                            WGPUDevice device, WGPUQueue queue,
                            WGPUCommandEncoder encoder,
                            double time, double delta_time, uint64_t frame,
                            uint32_t thumb_w, uint32_t thumb_h,
                            uint32_t thumb_logical_w, uint32_t thumb_logical_h,
                            WGPUTextureFormat thumb_format);

// --- Screenshot / capture ---
struct SurfaceCaptureResult {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> png_data;
};

struct GpuContext;
struct FrameState;

bool capture_surface_png(GpuContext& gpu,
                         FrameState& frame,
                         int fb_w, int fb_h,
                         SurfaceCaptureResult& out,
                         std::string& error);

bool try_capture_screenshot(const std::string& path, GpuContext& gpu,
                            FrameState& frame, int fb_w, int fb_h,
                            uint64_t frame_count, int delay, GLFWwindow* window);

} // namespace vivid
