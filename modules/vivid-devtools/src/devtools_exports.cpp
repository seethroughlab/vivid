// DevTools C-linkage exports for dynamic loading

#include <vivid/devtools/devtools.h>
#include <vivid/context.h>
#include <vivid/operator.h>
#include <GLFW/glfw3.h>

extern "C" {

// =============================================================================
// Lifecycle
// =============================================================================

void vivid_devtools_init(vivid::Context* ctx, WGPUTextureFormat surfaceFormat) {
    if (!ctx) return;
    vivid::DevTools::instance().init(*ctx, surfaceFormat);
}

void vivid_devtools_shutdown() {
    vivid::DevTools::instance().shutdown();
}

void vivid_devtools_update() {
    vivid::DevTools::instance().update();
}

void vivid_devtools_render(WGPURenderPassEncoder pass, const vivid::FrameInput* input,
                            vivid::Context* ctx) {
    if (!input || !ctx) return;
    vivid::DevTools::instance().render(pass, *input, *ctx);
}

bool vivid_devtools_is_available() {
    return vivid::DevTools::instance().isAvailable();
}

// =============================================================================
// Input
// =============================================================================

bool vivid_devtools_consumed_input() {
    return vivid::DevTools::instance().consumedInput();
}

bool vivid_devtools_is_interacting() {
    return vivid::DevTools::instance().isInteracting();
}

void vivid_devtools_on_char(uint32_t codepoint) {
    vivid::DevTools::instance().onChar(codepoint);
}

bool vivid_devtools_on_key(int key, int mods) {
    return vivid::DevTools::instance().onKeyDown(key, mods);
}

// =============================================================================
// Shortcuts
// =============================================================================

void vivid_devtools_set_fullscreen_callback(void (*callback)()) {
    if (!callback) {
        vivid::DevTools::instance().onFullscreenToggle(nullptr);
        return;
    }
    vivid::DevTools::instance().onFullscreenToggle([callback]() { callback(); });
}

void vivid_devtools_set_help_callback(void (*callback)()) {
    if (!callback) {
        vivid::DevTools::instance().onHelpToggle(nullptr);
        return;
    }
    vivid::DevTools::instance().onHelpToggle([callback]() { callback(); });
}

// =============================================================================
// Preferences
// =============================================================================

void vivid_devtools_show_preferences() {
    vivid::DevTools::instance().showPreferences();
}

void vivid_devtools_hide_preferences() {
    vivid::DevTools::instance().hidePreferences();
}

bool vivid_devtools_is_preferences_visible() {
    return vivid::DevTools::instance().isPreferencesVisible();
}

// =============================================================================
// Panel Control
// =============================================================================

void vivid_devtools_show_panel(const char* panelId) {
    if (!panelId) return;
    vivid::DevTools::instance().showPanel(panelId);
}

void vivid_devtools_hide_panel(const char* panelId) {
    if (!panelId) return;
    vivid::DevTools::instance().hidePanel(panelId);
}

void vivid_devtools_toggle_panel(const char* panelId) {
    if (!panelId) return;
    vivid::DevTools::instance().togglePanel(panelId);
}

bool vivid_devtools_is_panel_visible(const char* panelId) {
    if (!panelId) return false;
    return vivid::DevTools::instance().isPanelVisible(panelId);
}

// =============================================================================
// IDE Features (Terminal + Editor)
// =============================================================================

void vivid_devtools_toggle_ide() {
    vivid::DevTools::instance().toggleIde();
}

bool vivid_devtools_is_ide_visible() {
    return vivid::DevTools::instance().isIdeVisible();
}

void vivid_devtools_set_ide_visible(bool visible) {
    vivid::DevTools::instance().setIdeVisible(visible);
}

void vivid_devtools_set_working_dir(const char* path) {
    if (!path) return;
    vivid::DevTools::instance().setWorkingDirectory(path);
}

bool vivid_devtools_open_file(const char* path) {
    if (!path) return false;
    return vivid::DevTools::instance().openFile(path);
}

void vivid_devtools_set_compile_status(bool success, const char* message) {
    vivid::DevTools::instance().setCompileStatus(success, message ? message : "");
}

void vivid_devtools_set_window(GLFWwindow* window) {
    vivid::DevTools::instance().setWindow(window);
}

void vivid_devtools_get_ide_bounds(float* x, float* y, float* w, float* h) {
    glm::vec4 bounds = vivid::DevTools::instance().getIdeBounds();
    if (x) *x = bounds.x;
    if (y) *y = bounds.y;
    if (w) *w = bounds.z;
    if (h) *h = bounds.w;
}

void vivid_devtools_set_ide_bounds(float x, float y, float w, float h) {
    vivid::DevTools::instance().setIdeBounds({x, y, w, h});
}

// =============================================================================
// Visualizer Features (NodeGraph + Inspector)
// =============================================================================

void vivid_devtools_toggle_visualizer() {
    vivid::DevTools::instance().toggleVisualizer();
}

bool vivid_devtools_is_visualizer_visible() {
    return vivid::DevTools::instance().isVisualizerVisible();
}

void vivid_devtools_enter_solo(vivid::Operator* op, const char* name) {
    if (!op) return;
    vivid::DevTools::instance().enterSoloMode(op, name ? name : "");
}

void vivid_devtools_exit_solo() {
    vivid::DevTools::instance().exitSoloMode();
}

bool vivid_devtools_in_solo_mode() {
    return vivid::DevTools::instance().inSoloMode();
}

const char* vivid_devtools_solo_name() {
    return vivid::DevTools::instance().soloOperatorName().c_str();
}

void vivid_devtools_update_solo(vivid::Context* ctx) {
    if (!ctx) return;
    vivid::DevTools::instance().updateSoloOutput(*ctx);
}

void vivid_devtools_select_node(const char* name) {
    if (!name) return;
    vivid::DevTools::instance().selectNode(name);
}

void vivid_devtools_set_focused_node(const char* name) {
    if (!name) return;
    vivid::DevTools::instance().setFocusedNode(name);
}

void vivid_devtools_clear_focused_node() {
    vivid::DevTools::instance().clearFocusedNode();
}

// =============================================================================
// Status & Callbacks
// =============================================================================

void vivid_devtools_set_pending_count(size_t count) {
    vivid::DevTools::instance().setPendingChangeCount(count);
}

void vivid_devtools_set_mcp_warning(const char* warning) {
    vivid::DevTools::instance().setMcpWarning(warning ? warning : "");
}

void vivid_devtools_set_param_callback(void (*callback)(const char* opName, const char* paramName,
                                                         const float* oldVal, const float* newVal, int line)) {
    if (!callback) {
        vivid::DevTools::instance().onParamChange(nullptr);
        return;
    }

    vivid::DevTools::instance().onParamChange(
        [callback](const std::string& opName, const std::string& paramName,
                   const float oldVal[4], const float newVal[4], int line) {
            callback(opName.c_str(), paramName.c_str(), oldVal, newVal, line);
        }
    );
}

// =============================================================================
// Console
// =============================================================================

void vivid_devtools_set_compile_errors(const vivid::CompileError* errors, size_t count) {
    if (!errors && count > 0) return;
    std::vector<vivid::CompileError> errVec(errors, errors + count);
    vivid::DevTools::instance().setCompileErrors(errVec);
}

void vivid_devtools_clear_compile_errors() {
    vivid::DevTools::instance().clearCompileErrors();
}

void vivid_devtools_add_console_message(int type, const char* message) {
    if (!message) return;
    vivid::DevTools::instance().addConsoleMessage(type, message);
}

void vivid_devtools_clear_console() {
    vivid::DevTools::instance().clearConsole();
}

bool vivid_devtools_console_has_errors() {
    return vivid::DevTools::instance().consoleHasErrors();
}

// =============================================================================
// Video/Snapshot Export
// =============================================================================

void vivid_devtools_save_snapshot(vivid::Context* ctx) {
    if (!ctx) return;
    vivid::DevTools::instance().saveSnapshot(*ctx);
}

bool vivid_devtools_snapshot_requested() {
    return vivid::DevTools::instance().snapshotRequested();
}

vivid::VideoExporter* vivid_devtools_get_exporter() {
    return vivid::DevTools::instance().getExporter();
}

} // extern "C"
