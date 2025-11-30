#include <vivid/imgui/imgui_integration.h>
#include <imgui.h>
#include <imgui_impl_wgpu.h>
#include <webgpu/webgpu.h>
#include <iostream>

namespace vivid::imgui {

static bool g_initialized = false;
static WGPUDevice g_device = nullptr;
static WGPUQueue g_queue = nullptr;
static WGPUTextureFormat g_format = WGPUTextureFormat_RGBA8Unorm;
static int g_width = 0;
static int g_height = 0;

void init(Context& ctx) {
    if (g_initialized) return;

    // Get WebGPU handles from context
    g_device = static_cast<WGPUDevice>(ctx.webgpuDevice());
    g_queue = static_cast<WGPUQueue>(ctx.webgpuQueue());
    g_format = static_cast<WGPUTextureFormat>(ctx.webgpuTextureFormat());
    g_width = ctx.width();
    g_height = ctx.height();

    if (!g_device || !g_queue) {
        std::cerr << "[vivid-imgui] Failed to get WebGPU handles\n";
        return;
    }

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize = ImVec2(static_cast<float>(g_width), static_cast<float>(g_height));

    // Setup style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 3.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.9f;  // Slightly transparent windows

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
    std::cout << "[vivid-imgui] Initialized successfully\n";
}

void shutdown() {
    if (!g_initialized) return;

    ImGui_ImplWGPU_Shutdown();
    ImGui::DestroyContext();

    g_initialized = false;
    g_device = nullptr;
    g_queue = nullptr;
}

void beginFrame(Context& ctx) {
    if (!g_initialized) return;

    // Update display size if changed
    if (ctx.width() != g_width || ctx.height() != g_height) {
        g_width = ctx.width();
        g_height = ctx.height();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(g_width), static_cast<float>(g_height));
    }

    // Update input state
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = ctx.dt() > 0 ? ctx.dt() : 1.0f / 60.0f;

    // Mouse position (convert from normalized to screen space)
    io.MousePos = ImVec2(ctx.mouseNormX() * g_width, (1.0f - ctx.mouseNormY()) * g_height);

    // Mouse buttons
    io.MouseDown[0] = ctx.isMouseDown(0);  // Left
    io.MouseDown[1] = ctx.isMouseDown(1);  // Right
    io.MouseDown[2] = ctx.isMouseDown(2);  // Middle

    // Start new frame
    ImGui_ImplWGPU_NewFrame();
    ImGui::NewFrame();
}

void render(Context& ctx, Texture& output, glm::vec4 clearColor) {
    if (!g_initialized) return;

    // End ImGui frame
    ImGui::Render();

    // Get the texture view from the output texture
    WGPUTextureView textureView = static_cast<WGPUTextureView>(ctx.webgpuTextureView(output));
    if (!textureView) {
        std::cerr << "[vivid-imgui] Invalid output texture\n";
        return;
    }

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(g_device, &encoderDesc);

    // Create render pass
    // If clearColor has alpha=0, load existing content instead of clearing
    bool shouldClear = clearColor.a > 0.0f;

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = textureView;
    colorAttachment.loadOp = shouldClear ? WGPULoadOp_Clear : WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {clearColor.r, clearColor.g, clearColor.b, clearColor.a};
#if !defined(__EMSCRIPTEN__)
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = nullptr;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    // Render ImGui
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    // Submit commands
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(g_queue, 1, &cmdBuffer);

    // Cleanup
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
}

bool wantsMouse() {
    if (!g_initialized) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

bool wantsKeyboard() {
    if (!g_initialized) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace vivid::imgui
