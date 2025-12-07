// Vivid ImGui Integration Implementation

#include <vivid/imgui/imgui_integration.h>
#include <imgui.h>
#include <imgui_impl_wgpu.h>
#include <iostream>

namespace vivid::imgui {

static bool g_initialized = false;
static bool g_visible = false;
static WGPUDevice g_device = nullptr;
static WGPUQueue g_queue = nullptr;
static WGPUTextureFormat g_format = WGPUTextureFormat_RGBA8Unorm;
static std::string g_iniFilePath;  // Persisted path for imgui.ini

void init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format) {
    if (g_initialized) return;

    g_device = device;
    g_queue = queue;
    g_format = format;

    if (!g_device || !g_queue) {
        std::cerr << "[vivid-imgui] Invalid WebGPU device or queue\n";
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
        std::cerr << "[vivid-imgui] Failed to initialize WebGPU backend\n";
        ImGui::DestroyContext();
        return;
    }

    g_initialized = true;
    g_visible = false;  // Start hidden
    std::cout << "[vivid-imgui] Initialized successfully\n";
}

void setIniDirectory(const char* path) {
    if (!g_initialized) return;

    // Build full path: directory + "/imgui.ini"
    g_iniFilePath = std::string(path) + "/imgui.ini";

    // Set ImGui to use this path (must persist for lifetime of ImGui)
    ImGui::GetIO().IniFilename = g_iniFilePath.c_str();
}

void shutdown() {
    if (!g_initialized) return;

    ImGui_ImplWGPU_Shutdown();
    ImGui::DestroyContext();

    g_initialized = false;
    g_device = nullptr;
    g_queue = nullptr;
}

void beginFrame(const FrameInput& input) {
    if (!g_initialized) return;

    ImGuiIO& io = ImGui::GetIO();

    // Get content scale (DPI scaling for Retina displays)
    float scale = input.contentScale > 0.0f ? input.contentScale : 1.0f;

    // DisplaySize is in screen coordinates (window size, not framebuffer)
    // DisplayFramebufferScale tells ImGui how to convert to framebuffer pixels
    float windowWidth = static_cast<float>(input.width) / scale;
    float windowHeight = static_cast<float>(input.height) / scale;
    io.DisplaySize = ImVec2(windowWidth, windowHeight);
    io.DisplayFramebufferScale = ImVec2(scale, scale);

    // Update delta time
    io.DeltaTime = input.dt > 0 ? input.dt : 1.0f / 60.0f;

    // Update mouse position (GLFW gives us window coordinates, which is what ImGui expects)
    io.MousePos = ImVec2(input.mousePos.x, input.mousePos.y);

    // Update mouse buttons
    io.AddMouseButtonEvent(0, input.mouseDown[0]);  // Left
    io.AddMouseButtonEvent(1, input.mouseDown[1]);  // Right
    io.AddMouseButtonEvent(2, input.mouseDown[2]);  // Middle

    // Update scroll
    io.AddMouseWheelEvent(input.scroll.x, input.scroll.y);

    // Start new frame
    ImGui_ImplWGPU_NewFrame();
    ImGui::NewFrame();
}

void render(WGPURenderPassEncoder pass) {
    if (!g_initialized) return;

    ImGui::Render();
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);
}

bool wantsMouse() {
    if (!g_initialized || !g_visible) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

bool wantsKeyboard() {
    if (!g_initialized || !g_visible) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

void setVisible(bool visible) {
    g_visible = visible;
}

bool isVisible() {
    return g_visible && g_initialized;
}

void toggleVisible() {
    g_visible = !g_visible;
}

} // namespace vivid::imgui
