#pragma once

// Vivid ImGui Integration
// Provides Dear ImGui rendering in the WebGPU render pass

#include <webgpu/webgpu.h>
#include <glm/glm.hpp>

namespace vivid::imgui {

// Input state for ImGui frame
struct FrameInput {
    int width = 0;              // Framebuffer width (pixels)
    int height = 0;             // Framebuffer height (pixels)
    float contentScale = 1.0f;  // DPI scale (2.0 on Retina)
    float dt = 1.0f / 60.0f;
    glm::vec2 mousePos = {0, 0};
    bool mouseDown[3] = {false, false, false};
    glm::vec2 scroll = {0, 0};
};

// Initialize ImGui with WebGPU context
// Call once at startup
void init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format);

// Set the directory for imgui.ini (call after init, before first frame)
void setIniDirectory(const char* path);

// Shutdown ImGui
void shutdown();

// Begin a new ImGui frame
// Call before any ImGui:: widget calls
void beginFrame(const FrameInput& input);

// Render ImGui draw data to the current render pass
// Call after all ImGui:: widget calls
void render(WGPURenderPassEncoder pass);

// Check if ImGui wants mouse/keyboard input
bool wantsMouse();
bool wantsKeyboard();

// Visibility toggle
void setVisible(bool visible);
bool isVisible();

// Toggle visibility (call on keypress)
void toggleVisible();

} // namespace vivid::imgui
