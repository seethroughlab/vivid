#pragma once

/**
 * @file imgui_integration.h
 * @brief ImGUI integration for Vivid operators.
 *
 * This addon provides Dear ImGui integration for creating debug UIs,
 * control panels, and development tools in Vivid operators.
 *
 * NOTE: This addon requires WebGPU backend integration.
 * Implementation pending - placeholder header.
 *
 * Planned usage:
 *   #include <vivid/imgui/imgui_integration.h>
 *   #include <imgui.h>
 *
 *   void update(Chain& chain, Context& ctx) {
 *       vivid::imgui::beginFrame(ctx);
 *
 *       ImGui::Begin("Controls");
 *       ImGui::SliderFloat("Speed", &speed, 0.0f, 10.0f);
 *       ImGui::ColorEdit3("Color", &color.x);
 *       ImGui::End();
 *
 *       vivid::imgui::endFrame(ctx, output);
 *   }
 */

namespace vivid::imgui {

// Placeholder - implementation pending
// ImGUI requires direct WebGPU context access which needs additional runtime support

} // namespace vivid::imgui
