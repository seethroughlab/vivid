#pragma once

/**
 * @file imgui_integration.h
 * @brief Dear ImGui integration for Vivid operators (PLACEHOLDER)
 *
 * STATUS: API scaffolded, implementation blocked on WebGPU API compatibility.
 * See JEFF_NOTES.txt for resolution options and technical details.
 *
 * When implemented, usage will be:
 *
 *   #include <vivid/imgui/imgui_integration.h>
 *   #include <imgui.h>
 *
 *   void update(Chain& chain, Context& ctx) {
 *       if (!output.valid()) {
 *           output = ctx.createTexture();
 *           vivid::imgui::init(ctx);
 *       }
 *
 *       vivid::imgui::beginFrame(ctx);
 *
 *       ImGui::Begin("Controls");
 *       ImGui::SliderFloat("Value", &myValue, 0.0f, 1.0f);
 *       ImGui::ColorEdit3("Color", &color.x);
 *       ImGui::End();
 *
 *       vivid::imgui::render(ctx, output);
 *   }
 *
 * The WebGPU access API has been added to Context:
 * - ctx.webgpuDevice()
 * - ctx.webgpuQueue()
 * - ctx.webgpuTextureFormat()
 * - ctx.webgpuTextureView(texture)
 */

// Placeholder - implementation pending
// See JEFF_NOTES.txt "ImGUI Integration (Blocked)" for details
