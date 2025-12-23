#pragma once

/**
 * @file frame_input.h
 * @brief Input state for frame rendering
 *
 * Contains the FrameInput struct used by chain visualizer and other
 * overlay rendering systems. This is independent of ImGui.
 */

#include <webgpu/webgpu.h>
#include <glm/glm.hpp>

namespace vivid {

// Key codes for common keys (matches GLFW values)
enum class Key : int {
    Escape = 256,
    Enter = 257,
    Tab = 258,
    Space = 32,
    // Number keys
    Num0 = 48,
    Num1 = 49,
    Num2 = 50,
    // Letter keys
    B = 66,
    F = 70,
    R = 82,
    S = 83,
    // Arrow keys
    Right = 262,
    Left = 263,
    Down = 264,
    Up = 265,
};

// Input state for frame rendering
struct FrameInput {
    int width = 0;              // Framebuffer width (pixels)
    int height = 0;             // Framebuffer height (pixels)
    float contentScale = 1.0f;  // DPI scale (2.0 on Retina)
    float dt = 1.0f / 60.0f;
    glm::vec2 mousePos = {0, 0};
    bool mouseDown[3] = {false, false, false};
    glm::vec2 scroll = {0, 0};
    // Modifier keys
    bool keyCtrl = false;
    bool keyShift = false;
    bool keyAlt = false;
    bool keySuper = false;      // Command on Mac
    // Key states (for shortcuts)
    bool keyPressed[512] = {};   // Keys pressed this frame (one-shot)
    bool keyDown[512] = {};      // Keys currently held down
    // Surface format for overlay rendering
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_BGRA8UnormSrgb;

    // Helper to check if a key was just pressed this frame
    bool isKeyPressed(Key key) const { return keyPressed[static_cast<int>(key)]; }
    bool isKeyDown(Key key) const { return keyDown[static_cast<int>(key)]; }
};

} // namespace vivid
