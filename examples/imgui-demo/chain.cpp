// ImGui Demo Example
// Demonstrates using Dear ImGui with Vivid

#include <vivid/vivid.h>
#include <vivid/imgui/imgui_integration.h>
#include <imgui.h>

using namespace vivid;

// Demo state
static bool g_showDemoWindow = false;
static int g_counter = 0;
static float g_sliderValue = 0.5f;

void setup(Context& ctx) {
    // Initialize ImGui
    vivid::imgui::init(ctx);
}

void update(Context& ctx) {
    // Begin ImGui frame
    vivid::imgui::beginFrame(ctx);

    // Create a window with controls
    ImGui::Begin("Vivid ImGui Demo");

    ImGui::Text("Welcome to Vivid with Dear ImGui!");
    ImGui::Separator();

    ImGui::Text("Frame: %d", ctx.frame());
    ImGui::Text("Time: %.2f s", ctx.time());
    ImGui::Text("FPS: %.1f", 1.0f / ctx.dt());

    ImGui::Separator();

    ImGui::SliderFloat("Value", &g_sliderValue, 0.0f, 1.0f);

    if (ImGui::Button("Click Me!")) {
        g_counter++;
    }
    ImGui::SameLine();
    ImGui::Text("Clicked %d times", g_counter);

    ImGui::Checkbox("Show Demo Window", &g_showDemoWindow);

    ImGui::End();

    // Show ImGui demo window if enabled
    if (g_showDemoWindow) {
        ImGui::ShowDemoWindow(&g_showDemoWindow);
    }

    // Render ImGui
    vivid::imgui::render(ctx);
}

VIVID_CHAIN(setup, update)
