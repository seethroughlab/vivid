// Vivid IDE Module - C-linkage Exports
// Provides dynamic loading interface for the IDE panel
//
// app.cpp uses dlsym/GetProcAddress to look up these functions.
// If the module is not loaded, the IDE simply doesn't exist.

#include <vivid/ide/ide_panel.h>
#include <vivid/frame_input.h>
#include <vivid/context.h>
#include <memory>

namespace {
    // Global IDE panel instance
    std::unique_ptr<vivid::IdePanel> g_idePanel;
    bool g_initialized = false;
}

extern "C" {

// Initialize the IDE panel
void vivid_ide_init(vivid::Context* ctx, WGPUTextureFormat surfaceFormat) {
    if (g_initialized || !ctx) return;

    g_idePanel = std::make_unique<vivid::IdePanel>();
    if (g_idePanel->init(*ctx, surfaceFormat)) {
        g_initialized = true;
    } else {
        g_idePanel.reset();
    }
}

// Shutdown and release resources
void vivid_ide_shutdown() {
    if (!g_initialized) return;

    if (g_idePanel) {
        g_idePanel->shutdown();
        g_idePanel.reset();
    }
    g_initialized = false;
}

// Update IDE state (process PTY, etc.)
void vivid_ide_update() {
    if (!g_initialized || !g_idePanel) return;
    g_idePanel->update();
}

// Render the IDE panel
void vivid_ide_render(WGPURenderPassEncoder pass, const vivid::FrameInput* input,
                      float screenWidth, float screenHeight) {
    if (!g_initialized || !g_idePanel) return;
    g_idePanel->render(pass, *input, screenWidth, screenHeight);
}

// Check if IDE consumed input this frame
bool vivid_ide_consumed_input() {
    if (!g_initialized || !g_idePanel) return false;
    return g_idePanel->consumedInput();
}

// Check if IDE panel is available
bool vivid_ide_is_available() {
    return g_initialized && g_idePanel != nullptr;
}

// Check if IDE panel is visible
bool vivid_ide_is_visible() {
    if (!g_initialized || !g_idePanel) return false;
    return g_idePanel->isVisible();
}

// Set IDE panel visibility
void vivid_ide_set_visible(bool visible) {
    if (!g_initialized || !g_idePanel) return;
    g_idePanel->setVisible(visible);
}

// Toggle IDE panel visibility
void vivid_ide_toggle_visible() {
    if (!g_initialized || !g_idePanel) return;
    g_idePanel->toggleVisible();
}

// Set working directory (spawns terminal)
void vivid_ide_set_working_dir(const char* path) {
    if (!g_initialized || !g_idePanel) return;
    g_idePanel->setWorkingDirectory(path ? path : "");
}

// Open a file in the editor
bool vivid_ide_open_file(const char* path) {
    if (!g_initialized || !g_idePanel) return false;
    return g_idePanel->openFile(path ? path : "");
}

// Set compile status (from hot-reload)
void vivid_ide_set_compile_status(bool success, const char* message) {
    if (!g_initialized || !g_idePanel) return;
    g_idePanel->setCompileStatus(success, message ? message : "");
}

// Forward character input to IDE
void vivid_ide_on_char(uint32_t codepoint) {
    if (!g_initialized || !g_idePanel) return;

    // Forward to active panel based on current tab
    if (g_idePanel->activeTab() == vivid::IdeTab::Terminal) {
        g_idePanel->terminal().onChar(codepoint);
    } else {
        g_idePanel->editor().onChar(codepoint);
    }
}

// Forward key input to IDE
void vivid_ide_on_key(int key, int mods) {
    if (!g_initialized || !g_idePanel) return;

    // Handle IDE-level shortcuts first
    bool ctrl = (mods & 0x2) != 0;
    bool super = (mods & 0x8) != 0;
    bool cmdOrCtrl = ctrl || super;

    // Cmd+1 = Terminal, Cmd+2 = Editor
    if (cmdOrCtrl && key == 49) {  // 1
        g_idePanel->setActiveTab(vivid::IdeTab::Terminal);
        return;
    }
    if (cmdOrCtrl && key == 50) {  // 2
        g_idePanel->setActiveTab(vivid::IdeTab::Editor);
        return;
    }

    // Forward to active panel
    if (g_idePanel->activeTab() == vivid::IdeTab::Terminal) {
        g_idePanel->terminal().onKeyDown(key, mods);
    } else {
        g_idePanel->editor().onKeyDown(key, mods);
    }
}

// Get panel bounds (for input blocking)
void vivid_ide_get_bounds(float* x, float* y, float* w, float* h) {
    if (!g_initialized || !g_idePanel) {
        *x = *y = *w = *h = 0;
        return;
    }
    glm::vec4 bounds = g_idePanel->bounds();
    *x = bounds.x;
    *y = bounds.y;
    *w = bounds.z;
    *h = bounds.w;
}

// Set panel bounds
void vivid_ide_set_bounds(float x, float y, float w, float h) {
    if (!g_initialized || !g_idePanel) return;
    g_idePanel->setBounds(glm::vec4(x, y, w, h));
}

// Check if IDE is being interacted with (dragging/resizing)
bool vivid_ide_is_interacting() {
    if (!g_initialized || !g_idePanel) return false;
    return g_idePanel->isDragging() || g_idePanel->isResizing();
}

// Check if mouse is hovering over IDE panel
bool vivid_ide_is_hovered() {
    if (!g_initialized || !g_idePanel) return false;
    return g_idePanel->isHovered();
}

} // extern "C"
