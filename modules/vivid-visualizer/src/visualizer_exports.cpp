// Vivid Visualizer Module - C-linkage Exports
// Provides dynamic loading interface for the chain visualizer
//
// app.cpp uses dlsym/GetProcAddress to look up these functions.
// If the module is not loaded, the visualizer simply doesn't exist.

#include <vivid/chain_visualizer.h>
#include <vivid/frame_input.h>
#include <vivid/context.h>
#include <memory>

namespace {
    // Global visualizer instance (created on init, destroyed on shutdown)
    std::unique_ptr<vivid::ChainVisualizer> g_visualizer;
    bool g_initialized = false;
}

extern "C" {

// Initialize the visualizer with the given context and surface format
void vivid_visualizer_init(vivid::Context* ctx, WGPUTextureFormat surfaceFormat) {
    if (g_initialized) return;

    g_visualizer = std::make_unique<vivid::ChainVisualizer>();
    g_visualizer->init();
    g_visualizer->initNodeGraph(*ctx, surfaceFormat);
    g_initialized = true;
}

// Render the node graph overlay
void vivid_visualizer_render(WGPURenderPassEncoder pass, const vivid::FrameInput* input, vivid::Context* ctx) {
    if (!g_initialized || !g_visualizer) return;
    g_visualizer->renderNodeGraph(pass, *input, *ctx);
}

// Update solo mode output (call before blit)
void vivid_visualizer_update_solo(vivid::Context* ctx) {
    if (!g_initialized || !g_visualizer) return;
    g_visualizer->updateSoloOutput(*ctx);
}

// Shutdown and release resources
void vivid_visualizer_shutdown() {
    if (!g_initialized) return;

    if (g_visualizer) {
        g_visualizer->shutdown();
        g_visualizer.reset();
    }
    g_initialized = false;
}

// Check if the visualizer is available
bool vivid_visualizer_is_available() {
    return g_initialized && g_visualizer != nullptr;
}

// Save a snapshot (call from main loop after rendering)
void vivid_visualizer_save_snapshot(vivid::Context* ctx) {
    if (!g_initialized || !g_visualizer) return;
    g_visualizer->saveSnapshot(*ctx);
}

// Check if a snapshot was requested
bool vivid_visualizer_snapshot_requested() {
    if (!g_initialized || !g_visualizer) return false;
    return g_visualizer->snapshotRequested();
}

// Get the exporter for video recording (returns opaque pointer)
vivid::VideoExporter* vivid_visualizer_get_exporter() {
    if (!g_initialized || !g_visualizer) return nullptr;
    return &g_visualizer->exporter();
}

// Enter solo mode for an operator
void vivid_visualizer_enter_solo(vivid::Operator* op, const char* name) {
    if (!g_initialized || !g_visualizer) return;
    g_visualizer->enterSoloMode(op, name ? name : "");
}

// Exit solo mode
void vivid_visualizer_exit_solo() {
    if (!g_initialized || !g_visualizer) return;
    g_visualizer->exitSoloMode();
}

// Check if in solo mode
bool vivid_visualizer_in_solo_mode() {
    if (!g_initialized || !g_visualizer) return false;
    return g_visualizer->inSoloMode();
}

// Get solo operator name
const char* vivid_visualizer_solo_name() {
    if (!g_initialized || !g_visualizer) return "";
    return g_visualizer->soloOperatorName().c_str();
}

// Select a node from editor
void vivid_visualizer_select_node(const char* name) {
    if (!g_initialized || !g_visualizer) return;
    g_visualizer->selectNodeFromEditor(name ? name : "");
}

// Set focused node (cursor in operator code in editor)
void vivid_visualizer_set_focused_node(const char* name) {
    if (!g_initialized || !g_visualizer) return;
    g_visualizer->setFocusedNode(name ? name : "");
}

// Clear focused node
void vivid_visualizer_clear_focused_node() {
    if (!g_initialized || !g_visualizer) return;
    g_visualizer->clearFocusedNode();
}

// Set pending change count (for status bar indicator)
void vivid_visualizer_set_pending_count(size_t count) {
    if (!g_initialized || !g_visualizer) return;
    g_visualizer->setPendingChangeCount(count);
}

// Set MCP warning message
void vivid_visualizer_set_mcp_warning(const char* warning) {
    if (!g_initialized || !g_visualizer) return;
    g_visualizer->setMcpWarning(warning ? warning : "");
}

// Set parameter change callback
// Note: This uses a global function pointer since we can't pass std::function across C boundary
static void (*g_paramChangeCallback)(const char*, const char*, const float*, const float*, int) = nullptr;

void vivid_visualizer_set_param_callback(void (*callback)(const char*, const char*, const float*, const float*, int)) {
    g_paramChangeCallback = callback;

    if (g_initialized && g_visualizer) {
        if (callback) {
            g_visualizer->onParamChange([](const std::string& opName, const std::string& paramName,
                                            const float oldVal[4], const float newVal[4], int line) {
                if (g_paramChangeCallback) {
                    g_paramChangeCallback(opName.c_str(), paramName.c_str(), oldVal, newVal, line);
                }
            });
        } else {
            g_visualizer->onParamChange(nullptr);
        }
    }
}

// Check if visualizer consumed input (for blocking input to user code)
bool vivid_visualizer_consumed_input() {
    if (!g_initialized || !g_visualizer) return false;
    return g_visualizer->consumedInput();
}

// Check if visualizer is currently interacting (pan/drag in progress)
bool vivid_visualizer_is_interacting() {
    if (!g_initialized || !g_visualizer) return false;
    return g_visualizer->isInteracting();
}

} // extern "C"
