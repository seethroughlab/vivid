// Vivid ImGui Integration Implementation

#include <vivid/imgui/imgui_integration.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <ImGuiImplDiligent.hpp>

// Diligent includes for complete types
#include <SwapChain.h>
#include <DeviceContext.h>
#include <RenderDevice.h>

#include <GLFW/glfw3.h>
#include <iostream>
#include <memory>

namespace vivid::imgui {

namespace {

// Global state
std::unique_ptr<Diligent::ImGuiImplDiligent> g_imguiRenderer;
bool g_initialized = false;

} // anonymous namespace

void init(Context& ctx) {
    if (g_initialized) {
        std::cerr << "[vivid::imgui] Already initialized" << std::endl;
        return;
    }

    // Initialize ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup style
    ImGui::StyleColorsDark();

    // Initialize GLFW backend for input
    GLFWwindow* window = ctx.window();
    if (!window) {
        std::cerr << "[vivid::imgui] No GLFW window available" << std::endl;
        return;
    }
    ImGui_ImplGlfw_InitForOther(window, true);

    // Initialize Diligent renderer
    Diligent::ImGuiDiligentCreateInfo ci;
    ci.pDevice = ctx.device();

    auto* swapChain = ctx.swapChain();
    if (swapChain) {
        const auto& scDesc = swapChain->GetDesc();
        ci.BackBufferFmt = scDesc.ColorBufferFormat;
        ci.DepthBufferFmt = scDesc.DepthBufferFormat;
    }

    g_imguiRenderer = std::make_unique<Diligent::ImGuiImplDiligent>(ci);

    g_initialized = true;
    std::cout << "[vivid::imgui] Initialized" << std::endl;
}

void shutdown() {
    if (!g_initialized) {
        return;
    }

    g_imguiRenderer.reset();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    g_initialized = false;
    std::cout << "[vivid::imgui] Shutdown" << std::endl;
}

void beginFrame(Context& ctx) {
    if (!g_initialized) {
        return;
    }

    // Update GLFW input
    ImGui_ImplGlfw_NewFrame();

    // Begin new Diligent frame
    auto* swapChain = ctx.swapChain();
    if (swapChain) {
        const auto& scDesc = swapChain->GetDesc();
        g_imguiRenderer->NewFrame(
            scDesc.Width,
            scDesc.Height,
            scDesc.PreTransform
        );
    }
}

void render(Context& ctx) {
    if (!g_initialized) {
        return;
    }

    // End frame and render
    g_imguiRenderer->EndFrame();
    g_imguiRenderer->Render(ctx.immediateContext());
}

bool wantsMouse() {
    if (!g_initialized) {
        return false;
    }
    return ImGui::GetIO().WantCaptureMouse;
}

bool wantsKeyboard() {
    if (!g_initialized) {
        return false;
    }
    return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace vivid::imgui
