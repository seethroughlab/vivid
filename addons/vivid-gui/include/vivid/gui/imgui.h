#pragma once

/**
 * @file imgui.h
 * @brief Dear ImGui integration for Vivid
 *
 * This header provides access to Dear ImGui for use in Vivid chains.
 * Include this header in your chain.cpp to use ImGui widgets.
 *
 * Example usage:
 * @code
 * #include <vivid/gui/imgui.h>
 *
 * void update(Context& ctx) {
 *     // ImGui is available after vivid-gui addon is loaded
 *     ImGui::Begin("My Window");
 *     ImGui::Text("Hello from ImGui!");
 *     ImGui::End();
 *
 *     ctx.chain().process();
 * }
 * @endcode
 *
 * Note: The vivid-gui addon must be loaded for ImGui to work.
 * The core chain visualizer works without this addon.
 */

// Include Dear ImGui
#include <imgui.h>

// Include FrameInput from core
#include <vivid/frame_input.h>

namespace vivid::gui {

/**
 * @brief Check if ImGui is available
 * @return true if the vivid-gui addon is loaded and ImGui is initialized
 */
bool isAvailable();

/**
 * @brief Get the current ImGui context
 * @return ImGuiContext pointer, or nullptr if not available
 */
ImGuiContext* getContext();

} // namespace vivid::gui

// Forward declaration for Context-based init
namespace vivid { class Context; }

// Provide the vivid::imgui namespace for backward compatibility with CLI
namespace vivid::imgui {

// Re-export FrameInput and Key for backward compatibility
using FrameInput = vivid::FrameInput;
using Key = vivid::Key;

// Initialize ImGui with WebGPU context (low-level)
void init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format);

// Initialize ImGui from Context (recommended for user chains)
void init(Context& ctx);

// Set the directory for imgui.ini
void setIniDirectory(const char* path);

// Shutdown ImGui
void shutdown();

// Begin a new ImGui frame
void beginFrame(const FrameInput& input);

// Render ImGui draw data to the current render pass
void render(WGPURenderPassEncoder pass);

// Check if ImGui wants mouse/keyboard input
bool wantsMouse();
bool wantsKeyboard();

// Visibility toggle
void setVisible(bool visible);
bool isVisible();
void toggleVisible();

} // namespace vivid::imgui
