// Vivid GUI Addon - ImGui Integration
//
// Provides Dear ImGui for use in Vivid chains.

#include <vivid/gui/imgui.h>
#include <imgui_impl_wgpu.h>
#include <vivid/frame_input.h>
#include <vivid/context.h>
#include <iostream>

namespace vivid::gui {

static bool g_initialized = false;
static bool g_visible = true;  // Visible by default for user chains
static WGPUDevice g_device = nullptr;
static WGPUQueue g_queue = nullptr;
static WGPUTextureFormat g_format = WGPUTextureFormat_RGBA8Unorm;
static std::string g_iniFilePath;

bool isAvailable() {
    return g_initialized;
}

ImGuiContext* getContext() {
    if (!g_initialized) return nullptr;
    return ImGui::GetCurrentContext();
}

} // namespace vivid::gui

// Also provide the vivid::imgui namespace for backward compatibility
namespace vivid::imgui {

void init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format) {
    using namespace vivid::gui;

    if (g_initialized) return;

    g_device = device;
    g_queue = queue;
    g_format = format;

    if (!g_device || !g_queue) {
        std::cerr << "[vivid-gui] Invalid WebGPU device or queue\n";
        return;
    }

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup dark style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 3.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.95f;

    // Initialize WebGPU backend
    ImGui_ImplWGPU_InitInfo init_info = {};
    init_info.Device = g_device;
    init_info.NumFramesInFlight = 1;
    init_info.RenderTargetFormat = g_format;
    init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;

    if (!ImGui_ImplWGPU_Init(&init_info)) {
        std::cerr << "[vivid-gui] Failed to initialize WebGPU backend\n";
        ImGui::DestroyContext();
        return;
    }

    g_initialized = true;
    g_visible = true;
    std::cout << "[vivid-gui] ImGui initialized successfully\n";
}

void init(Context& ctx) {
    // Get WebGPU device and queue from context
    WGPUDevice device = ctx.device();
    WGPUQueue queue = ctx.queue();

    // Use BGRA8Unorm as default format (matches most surfaces)
    WGPUTextureFormat format = WGPUTextureFormat_BGRA8Unorm;

    init(device, queue, format);
}

void setIniDirectory(const char* path) {
    using namespace vivid::gui;

    if (!g_initialized) return;

    g_iniFilePath = std::string(path) + "/imgui.ini";
    ImGui::GetIO().IniFilename = g_iniFilePath.c_str();
}

void shutdown() {
    using namespace vivid::gui;

    if (!g_initialized) return;

    ImGui_ImplWGPU_Shutdown();
    ImGui::DestroyContext();

    g_initialized = false;
    g_device = nullptr;
    g_queue = nullptr;
}

void beginFrame(const vivid::FrameInput& input) {
    using namespace vivid::gui;

    if (!g_initialized) return;

    ImGuiIO& io = ImGui::GetIO();

    float scale = input.contentScale > 0.0f ? input.contentScale : 1.0f;
    float windowWidth = static_cast<float>(input.width) / scale;
    float windowHeight = static_cast<float>(input.height) / scale;
    io.DisplaySize = ImVec2(windowWidth, windowHeight);
    io.DisplayFramebufferScale = ImVec2(scale, scale);

    io.DeltaTime = input.dt > 0 ? input.dt : 1.0f / 60.0f;
    io.MousePos = ImVec2(input.mousePos.x, input.mousePos.y);

    io.AddMouseButtonEvent(0, input.mouseDown[0]);
    io.AddMouseButtonEvent(1, input.mouseDown[1]);
    io.AddMouseButtonEvent(2, input.mouseDown[2]);
    io.AddMouseWheelEvent(input.scroll.x, input.scroll.y);

    ImGui_ImplWGPU_NewFrame();
    ImGui::NewFrame();

    io.AddKeyEvent(ImGuiMod_Ctrl, input.keyCtrl);
    io.AddKeyEvent(ImGuiMod_Shift, input.keyShift);
    io.AddKeyEvent(ImGuiMod_Alt, input.keyAlt);
    io.AddKeyEvent(ImGuiMod_Super, input.keySuper);
    io.KeyCtrl = input.keyCtrl;
    io.KeyShift = input.keyShift;
    io.KeyAlt = input.keyAlt;
    io.KeySuper = input.keySuper;
}

void render(WGPURenderPassEncoder pass) {
    using namespace vivid::gui;

    if (!g_initialized) return;

    ImGui::Render();
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);
}

bool wantsMouse() {
    using namespace vivid::gui;
    if (!g_initialized || !g_visible) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

bool wantsKeyboard() {
    using namespace vivid::gui;
    if (!g_initialized || !g_visible) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

void setVisible(bool visible) {
    vivid::gui::g_visible = visible;
}

bool isVisible() {
    using namespace vivid::gui;
    return g_visible && g_initialized;
}

void toggleVisible() {
    vivid::gui::g_visible = !vivid::gui::g_visible;
}

} // namespace vivid::imgui

// C-linkage exports for dynamic loading from CLI
extern "C" {

void vivid_gui_init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format) {
    vivid::imgui::init(device, queue, format);
}

void vivid_gui_begin_frame(const vivid::FrameInput* input) {
    if (input) {
        vivid::imgui::beginFrame(*input);
    }
}

void vivid_gui_render(WGPURenderPassEncoder pass) {
    vivid::imgui::render(pass);
}

void vivid_gui_shutdown() {
    vivid::imgui::shutdown();
}

bool vivid_gui_is_available() {
    return vivid::gui::isAvailable();
}

} // extern "C"
