#pragma once

/**
 * @file imgui_integration.h
 * @brief Dear ImGui integration for Vivid operators
 *
 * This addon provides Dear ImGui rendering within Vivid's WebGPU context,
 * enabling immediate-mode GUI for parameter tweaking, debugging, and
 * custom control panels.
 *
 * Usage:
 * @code
 *   #include <vivid/imgui/imgui_integration.h>
 *   #include <imgui.h>
 *
 *   Texture guiOutput;
 *   float myValue = 0.5f;
 *   glm::vec3 color = {1.0f, 0.5f, 0.0f};
 *
 *   void setup(Chain& chain) {
 *       chain.setOutput("out");
 *   }
 *
 *   void update(Chain& chain, Context& ctx) {
 *       // Initialize on first frame
 *       if (!guiOutput.valid()) {
 *           guiOutput = ctx.createTexture();
 *           vivid::imgui::init(ctx);
 *       }
 *
 *       // Begin ImGui frame
 *       vivid::imgui::beginFrame(ctx);
 *
 *       // Draw ImGui widgets
 *       ImGui::Begin("Controls");
 *       ImGui::SliderFloat("Value", &myValue, 0.0f, 1.0f);
 *       ImGui::ColorEdit3("Color", &color.x);
 *       if (ImGui::Button("Reset")) {
 *           myValue = 0.5f;
 *           color = {1.0f, 0.5f, 0.0f};
 *       }
 *       ImGui::End();
 *
 *       // Render ImGui to texture (with transparent background)
 *       vivid::imgui::render(ctx, guiOutput, {0, 0, 0, 0});
 *
 *       // Composite GUI over your visuals
 *       chain.get<Composite>("out")
 *           .background("effects")
 *           .foreground(guiOutput)
 *           .mode(BlendMode::Over);
 *   }
 * @endcode
 *
 * Input Handling:
 * - Mouse position and clicks are automatically forwarded to ImGui
 * - Use wantsMouse() / wantsKeyboard() to check if ImGui wants input
 * - When ImGui wants input, you may want to skip your own input handling
 *
 * @code
 *   void update(Chain& chain, Context& ctx) {
 *       vivid::imgui::beginFrame(ctx);
 *       // ... draw ImGui ...
 *
 *       // Only handle custom input if ImGui doesn't want it
 *       if (!vivid::imgui::wantsMouse()) {
 *           // Handle mouse input for your visuals
 *       }
 *   }
 * @endcode
 */

#include <vivid/vivid.h>
#include <glm/glm.hpp>

namespace vivid::imgui {

/**
 * Initialize ImGui with the Vivid WebGPU context.
 * Call once before using any other imgui functions.
 *
 * @param ctx The Vivid context (provides WebGPU device/queue)
 */
void init(Context& ctx);

/**
 * Shutdown ImGui and release resources.
 * Call when done using ImGui (e.g., in cleanup).
 */
void shutdown();

/**
 * Begin a new ImGui frame.
 * Call at the start of update() before any ImGui:: calls.
 *
 * Updates display size and input state from the context.
 *
 * @param ctx The Vivid context
 */
void beginFrame(Context& ctx);

/**
 * Render ImGui to a texture.
 * Call after all ImGui:: widget calls for the frame.
 *
 * @param ctx The Vivid context
 * @param output Target texture to render into
 * @param clearColor Background color (use alpha=0 for transparent overlay)
 */
void render(Context& ctx, Texture& output, glm::vec4 clearColor = {0, 0, 0, 0});

/**
 * Check if ImGui wants to capture mouse input.
 * When true, you should skip your own mouse handling.
 *
 * @return true if mouse is over an ImGui window or widget
 */
bool wantsMouse();

/**
 * Check if ImGui wants to capture keyboard input.
 * When true, you should skip your own keyboard handling.
 *
 * @return true if an ImGui text input or similar is active
 */
bool wantsKeyboard();

} // namespace vivid::imgui
