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
bool g_ownsContext = false;  // True if we created the ImGui context, false if reusing

} // anonymous namespace

void init(Context& ctx) {
    if (g_initialized) {
        std::cerr << "[vivid::imgui] Already initialized" << std::endl;
        return;
    }

    // Check if ImGui context already exists (e.g., created by ChainVisualizer)
    if (ImGui::GetCurrentContext() != nullptr) {
        // Reuse existing context - don't create our own renderer
        // beginFrame/render will be no-ops since ChainVisualizer handles them
        g_initialized = true;
        g_ownsContext = false;
        std::cout << "[vivid::imgui] Using existing ImGui context (frame managed externally)" << std::endl;
        return;
    }

    // Get swap chain info for renderer
    auto* swapChain = ctx.swapChain();
    if (!swapChain) {
        std::cerr << "[vivid::imgui] No swap chain available" << std::endl;
        return;
    }

    // Initialize Diligent renderer FIRST - it creates the ImGui context internally
    Diligent::ImGuiDiligentCreateInfo ci;
    ci.pDevice = ctx.device();
    const auto& scDesc = swapChain->GetDesc();
    ci.BackBufferFmt = scDesc.ColorBufferFormat;
    ci.DepthBufferFmt = scDesc.DepthBufferFormat;

    g_imguiRenderer = std::make_unique<Diligent::ImGuiImplDiligent>(ci);

    // Now configure the context that Diligent created
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup style
    ImGui::StyleColorsDark();

    // Initialize GLFW backend for input (uses the context Diligent created)
    GLFWwindow* window = ctx.window();
    if (!window) {
        std::cerr << "[vivid::imgui] No GLFW window available" << std::endl;
        g_imguiRenderer.reset();
        return;
    }
    ImGui_ImplGlfw_InitForOther(window, true);

    g_initialized = true;
    g_ownsContext = true;
    std::cout << "[vivid::imgui] Initialized" << std::endl;
}

void shutdown() {
    if (!g_initialized) {
        return;
    }

    // Only cleanup if we own the context
    if (g_ownsContext) {
        // Shutdown GLFW backend first
        ImGui_ImplGlfw_Shutdown();

        // Reset renderer - its destructor calls ImGui::DestroyContext()
        g_imguiRenderer.reset();
        std::cout << "[vivid::imgui] Shutdown" << std::endl;
    }

    g_initialized = false;
    g_ownsContext = false;
}

void beginFrame(Context& ctx) {
    if (!g_initialized) {
        return;
    }

    // If we don't own the context, frame is managed externally (by ChainVisualizer)
    if (!g_ownsContext) {
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
        std::cerr << "[vivid::imgui] render called but not initialized" << std::endl;
        return;
    }

    // If we don't own the context, rendering is managed externally (by ChainVisualizer)
    if (!g_ownsContext) {
        return;
    }

    // Ensure render target is set to swap chain
    auto* swapChain = ctx.swapChain();
    if (swapChain) {
        auto* rtv = swapChain->GetCurrentBackBufferRTV();
        auto* dsv = swapChain->GetDepthBufferDSV();
        ctx.immediateContext()->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    // Render ImGui - this internally calls ImGui::Render() which calls EndFrame()
    // Do NOT call EndFrame() separately!
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
