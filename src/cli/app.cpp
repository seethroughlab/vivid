// Vivid Application Implementation
// Contains WebGPU initialization, main loop, and cleanup

// Prevent Windows.h from defining min/max macros
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <vivid/app.h>

#include <vivid/vivid.h>
#include <vivid/effects/texture_operator.h>
#include <vivid/context.h>
#include <vivid/display.h>
#include <vivid/hot_reload.h>
#include <vivid/runtime_api.h>
#include <vivid/audio_buffer.h>
#include <vivid/video_exporter.h>
#include <vivid/cli.h>
#include <vivid/module_manager.h>
#include <vivid/window_manager.h>
#include <vivid/asset_loader.h>
#include <vivid/frame_input.h>
// Note: DevTools (terminal, editor, node graph, inspector) is loaded dynamically
// from vivid-devtools module. See devtools_dynamic namespace below.
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>  // wgpu-native extensions (wgpuDevicePoll)
#include <glfw3webgpu.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <nlohmann/json.hpp>
#include <iostream>
#include <iomanip>
#include <string>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <functional>
#include <mutex>
#include <thread>
#include <chrono>

// Memory debugging and path lookup (macOS)
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach-o/dyld.h>  // For _NSGetExecutablePath
#include <dlfcn.h>  // For dlsym
#endif

#ifdef __linux__
#include <dlfcn.h>
#include <unistd.h>  // For readlink
#include <linux/limits.h>  // For PATH_MAX
#endif

#ifdef _WIN32
#include <windows.h>
#endif

// Platform-specific helpers (autoreleasepool for macOS)
#include <vivid/platform_macos.h>

// CEF Browser support for IDE panel (optional)
#ifdef VIVID_HAS_CEF
#include <vivid/cef/browser.h>
#include <vivid/cef/terminal_utils.h>
#include <vivid/pty.h>
#endif

// -----------------------------------------------------------------------------
// Dynamic ImGui Support (vivid-gui module)
// -----------------------------------------------------------------------------
// Function pointers for optional vivid-gui module
// These are looked up at runtime if the module is loaded

namespace vivid::imgui_dynamic {

using InitFn = void(*)(WGPUDevice, WGPUQueue, WGPUTextureFormat);
using BeginFrameFn = void(*)(const vivid::FrameInput*);
using RenderFn = void(*)(WGPURenderPassEncoder);
using ShutdownFn = void(*)();
using IsAvailableFn = bool(*)();

static InitFn init = nullptr;
static BeginFrameFn beginFrame = nullptr;
static RenderFn render = nullptr;
static ShutdownFn shutdown = nullptr;
static IsAvailableFn isAvailable = nullptr;
static bool g_lookedUp = false;
static bool g_initialized = false;

// Try to find vivid-gui functions via dlsym/GetProcAddress (C-linkage names)
static void lookupFunctions() {
    if (g_lookedUp) return;
    g_lookedUp = true;

#if defined(__APPLE__) || defined(__linux__)
    // RTLD_DEFAULT searches all loaded libraries
    init = (InitFn)dlsym(RTLD_DEFAULT, "vivid_gui_init");
    beginFrame = (BeginFrameFn)dlsym(RTLD_DEFAULT, "vivid_gui_begin_frame");
    render = (RenderFn)dlsym(RTLD_DEFAULT, "vivid_gui_render");
    shutdown = (ShutdownFn)dlsym(RTLD_DEFAULT, "vivid_gui_shutdown");
    isAvailable = (IsAvailableFn)dlsym(RTLD_DEFAULT, "vivid_gui_is_available");
#elif defined(_WIN32)
    // On Windows, try to get the vivid-imgui.dll module handle
    HMODULE guiModule = GetModuleHandleA("vivid-imgui.dll");
    if (guiModule) {
        init = (InitFn)GetProcAddress(guiModule, "vivid_gui_init");
        beginFrame = (BeginFrameFn)GetProcAddress(guiModule, "vivid_gui_begin_frame");
        render = (RenderFn)GetProcAddress(guiModule, "vivid_gui_render");
        shutdown = (ShutdownFn)GetProcAddress(guiModule, "vivid_gui_shutdown");
        isAvailable = (IsAvailableFn)GetProcAddress(guiModule, "vivid_gui_is_available");
    }
#endif
}

static bool available() {
    lookupFunctions();
    return init != nullptr && beginFrame != nullptr && render != nullptr;
}

static void tryInit(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format) {
    if (!available() || g_initialized) return;
    init(device, queue, format);
    g_initialized = true;
}

static void tryBeginFrame(const vivid::FrameInput& input) {
    if (!available() || !g_initialized) return;
    beginFrame(&input);
}

static void tryRender(WGPURenderPassEncoder pass) {
    if (!available() || !g_initialized) return;
    render(pass);
}

static void tryShutdown() {
    if (!available() || !g_initialized) return;
    shutdown();
    g_initialized = false;
}

// Reset lookup state (called after chain reload since libs may have changed)
static void resetLookup() {
    g_lookedUp = false;
    init = nullptr;
    beginFrame = nullptr;
    render = nullptr;
    shutdown = nullptr;
    isAvailable = nullptr;
}

} // namespace vivid::imgui_dynamic

// -----------------------------------------------------------------------------
// Dynamic DevTools Support (vivid-devtools module)
// -----------------------------------------------------------------------------
// Function pointers for optional vivid-devtools module
// Provides unified IDE (terminal + editor) and visualizer (node graph + inspector)
// Production bundles can exclude this module for smaller size

namespace vivid::devtools_dynamic {

// Function pointer types matching devtools_exports.cpp
using InitFn = void(*)(void* ctx, WGPUTextureFormat surfaceFormat);
using ShutdownFn = void(*)();
using UpdateFn = void(*)();
using RenderFn = void(*)(WGPURenderPassEncoder pass, const void* input, void* ctx);
using IsAvailableFn = bool(*)();
using ConsumedInputFn = bool(*)();
using IsInteractingFn = bool(*)();
using OnCharFn = void(*)(uint32_t codepoint);
using OnKeyFn = bool(*)(int key, int mods);

// Panel control
using ShowPanelFn = void(*)(const char* panelId);
using HidePanelFn = void(*)(const char* panelId);
using TogglePanelFn = void(*)(const char* panelId);
using IsPanelVisibleFn = bool(*)(const char* panelId);
// Window
using SetWindowFn = void(*)(GLFWwindow* window);

// Visualizer features
using ToggleVisualizerFn = void(*)();
using IsVisualizerVisibleFn = bool(*)();
using EnterSoloFn = void(*)(void* op, const char* name);
using ExitSoloFn = void(*)();
using InSoloModeFn = bool(*)();
using SoloNameFn = const char*(*)();
using UpdateSoloFn = void(*)(void* ctx);
using SelectNodeFn = void(*)(const char* name);
using SetFocusedNodeFn = void(*)(const char* name);
using ClearFocusedNodeFn = void(*)();

// Status & callbacks
using SetPendingCountFn = void(*)(size_t count);
using SetMcpWarningFn = void(*)(const char* warning);
using SetParamCallbackFn = void(*)(void (*callback)(const char*, const char*, const float*, const float*, int));

// Video/snapshot
using SaveSnapshotFn = void(*)(void* ctx);
using SnapshotRequestedFn = bool(*)();
using GetExporterFn = void*(*)();

// Shortcuts
using SetFullscreenCallbackFn = void(*)(void (*)());
using SetHelpCallbackFn = void(*)(void (*)());

// Function pointers
static InitFn init = nullptr;
static ShutdownFn shutdown = nullptr;
static UpdateFn update = nullptr;
static RenderFn render = nullptr;
static IsAvailableFn isAvailable = nullptr;
static ConsumedInputFn consumedInput = nullptr;
static IsInteractingFn isInteracting = nullptr;
static OnCharFn onChar = nullptr;
static OnKeyFn onKey = nullptr;
static ShowPanelFn showPanel = nullptr;
static HidePanelFn hidePanel = nullptr;
static TogglePanelFn togglePanel = nullptr;
static IsPanelVisibleFn isPanelVisible = nullptr;
static SetWindowFn setWindow = nullptr;
static ToggleVisualizerFn toggleVisualizer = nullptr;
static IsVisualizerVisibleFn isVisualizerVisible = nullptr;
static EnterSoloFn enterSolo = nullptr;
static ExitSoloFn exitSolo = nullptr;
static InSoloModeFn inSoloMode = nullptr;
static SoloNameFn soloName = nullptr;
static UpdateSoloFn updateSolo = nullptr;
static SelectNodeFn selectNode = nullptr;
static SetFocusedNodeFn setFocusedNode = nullptr;
static ClearFocusedNodeFn clearFocusedNode = nullptr;
static SetPendingCountFn setPendingCount = nullptr;
static SetMcpWarningFn setMcpWarning = nullptr;
static SetParamCallbackFn setParamCallback = nullptr;
static SaveSnapshotFn saveSnapshot = nullptr;
static SnapshotRequestedFn snapshotRequested = nullptr;
static GetExporterFn getExporter = nullptr;
static SetFullscreenCallbackFn setFullscreenCallback = nullptr;
static SetHelpCallbackFn setHelpCallback = nullptr;

// Static context pointer for callbacks (set during init)
static vivid::Context* g_callbackContext = nullptr;

static bool g_lookedUp = false;
static bool g_initialized = false;

static void lookupFunctions() {
    if (g_lookedUp) return;
    g_lookedUp = true;

#if defined(__APPLE__) || defined(__linux__)
    // RTLD_DEFAULT searches all loaded libraries
    init = (InitFn)dlsym(RTLD_DEFAULT, "vivid_devtools_init");
    shutdown = (ShutdownFn)dlsym(RTLD_DEFAULT, "vivid_devtools_shutdown");
    update = (UpdateFn)dlsym(RTLD_DEFAULT, "vivid_devtools_update");
    render = (RenderFn)dlsym(RTLD_DEFAULT, "vivid_devtools_render");
    isAvailable = (IsAvailableFn)dlsym(RTLD_DEFAULT, "vivid_devtools_is_available");
    consumedInput = (ConsumedInputFn)dlsym(RTLD_DEFAULT, "vivid_devtools_consumed_input");
    isInteracting = (IsInteractingFn)dlsym(RTLD_DEFAULT, "vivid_devtools_is_interacting");
    onChar = (OnCharFn)dlsym(RTLD_DEFAULT, "vivid_devtools_on_char");
    onKey = (OnKeyFn)dlsym(RTLD_DEFAULT, "vivid_devtools_on_key");
    showPanel = (ShowPanelFn)dlsym(RTLD_DEFAULT, "vivid_devtools_show_panel");
    hidePanel = (HidePanelFn)dlsym(RTLD_DEFAULT, "vivid_devtools_hide_panel");
    togglePanel = (TogglePanelFn)dlsym(RTLD_DEFAULT, "vivid_devtools_toggle_panel");
    isPanelVisible = (IsPanelVisibleFn)dlsym(RTLD_DEFAULT, "vivid_devtools_is_panel_visible");
    setWindow = (SetWindowFn)dlsym(RTLD_DEFAULT, "vivid_devtools_set_window");
    toggleVisualizer = (ToggleVisualizerFn)dlsym(RTLD_DEFAULT, "vivid_devtools_toggle_visualizer");
    isVisualizerVisible = (IsVisualizerVisibleFn)dlsym(RTLD_DEFAULT, "vivid_devtools_is_visualizer_visible");
    enterSolo = (EnterSoloFn)dlsym(RTLD_DEFAULT, "vivid_devtools_enter_solo");
    exitSolo = (ExitSoloFn)dlsym(RTLD_DEFAULT, "vivid_devtools_exit_solo");
    inSoloMode = (InSoloModeFn)dlsym(RTLD_DEFAULT, "vivid_devtools_in_solo_mode");
    soloName = (SoloNameFn)dlsym(RTLD_DEFAULT, "vivid_devtools_solo_name");
    updateSolo = (UpdateSoloFn)dlsym(RTLD_DEFAULT, "vivid_devtools_update_solo");
    selectNode = (SelectNodeFn)dlsym(RTLD_DEFAULT, "vivid_devtools_select_node");
    setFocusedNode = (SetFocusedNodeFn)dlsym(RTLD_DEFAULT, "vivid_devtools_set_focused_node");
    clearFocusedNode = (ClearFocusedNodeFn)dlsym(RTLD_DEFAULT, "vivid_devtools_clear_focused_node");
    setPendingCount = (SetPendingCountFn)dlsym(RTLD_DEFAULT, "vivid_devtools_set_pending_count");
    setMcpWarning = (SetMcpWarningFn)dlsym(RTLD_DEFAULT, "vivid_devtools_set_mcp_warning");
    setParamCallback = (SetParamCallbackFn)dlsym(RTLD_DEFAULT, "vivid_devtools_set_param_callback");
    saveSnapshot = (SaveSnapshotFn)dlsym(RTLD_DEFAULT, "vivid_devtools_save_snapshot");
    snapshotRequested = (SnapshotRequestedFn)dlsym(RTLD_DEFAULT, "vivid_devtools_snapshot_requested");
    getExporter = (GetExporterFn)dlsym(RTLD_DEFAULT, "vivid_devtools_get_exporter");
    setFullscreenCallback = (SetFullscreenCallbackFn)dlsym(RTLD_DEFAULT, "vivid_devtools_set_fullscreen_callback");
    setHelpCallback = (SetHelpCallbackFn)dlsym(RTLD_DEFAULT, "vivid_devtools_set_help_callback");
#elif defined(_WIN32)
    // First try GetModuleHandle (already loaded), then LoadLibrary (load from disk)
    HMODULE devtoolsModule = GetModuleHandleA("vivid-devtools.dll");
    if (!devtoolsModule) {
        devtoolsModule = LoadLibraryA("vivid-devtools.dll");
    }
    if (devtoolsModule) {
        init = (InitFn)GetProcAddress(devtoolsModule, "vivid_devtools_init");
        shutdown = (ShutdownFn)GetProcAddress(devtoolsModule, "vivid_devtools_shutdown");
        update = (UpdateFn)GetProcAddress(devtoolsModule, "vivid_devtools_update");
        render = (RenderFn)GetProcAddress(devtoolsModule, "vivid_devtools_render");
        isAvailable = (IsAvailableFn)GetProcAddress(devtoolsModule, "vivid_devtools_is_available");
        consumedInput = (ConsumedInputFn)GetProcAddress(devtoolsModule, "vivid_devtools_consumed_input");
        isInteracting = (IsInteractingFn)GetProcAddress(devtoolsModule, "vivid_devtools_is_interacting");
        onChar = (OnCharFn)GetProcAddress(devtoolsModule, "vivid_devtools_on_char");
        onKey = (OnKeyFn)GetProcAddress(devtoolsModule, "vivid_devtools_on_key");
        showPanel = (ShowPanelFn)GetProcAddress(devtoolsModule, "vivid_devtools_show_panel");
        hidePanel = (HidePanelFn)GetProcAddress(devtoolsModule, "vivid_devtools_hide_panel");
        togglePanel = (TogglePanelFn)GetProcAddress(devtoolsModule, "vivid_devtools_toggle_panel");
        isPanelVisible = (IsPanelVisibleFn)GetProcAddress(devtoolsModule, "vivid_devtools_is_panel_visible");
        setWindow = (SetWindowFn)GetProcAddress(devtoolsModule, "vivid_devtools_set_window");
        toggleVisualizer = (ToggleVisualizerFn)GetProcAddress(devtoolsModule, "vivid_devtools_toggle_visualizer");
        isVisualizerVisible = (IsVisualizerVisibleFn)GetProcAddress(devtoolsModule, "vivid_devtools_is_visualizer_visible");
        enterSolo = (EnterSoloFn)GetProcAddress(devtoolsModule, "vivid_devtools_enter_solo");
        exitSolo = (ExitSoloFn)GetProcAddress(devtoolsModule, "vivid_devtools_exit_solo");
        inSoloMode = (InSoloModeFn)GetProcAddress(devtoolsModule, "vivid_devtools_in_solo_mode");
        soloName = (SoloNameFn)GetProcAddress(devtoolsModule, "vivid_devtools_solo_name");
        updateSolo = (UpdateSoloFn)GetProcAddress(devtoolsModule, "vivid_devtools_update_solo");
        selectNode = (SelectNodeFn)GetProcAddress(devtoolsModule, "vivid_devtools_select_node");
        setFocusedNode = (SetFocusedNodeFn)GetProcAddress(devtoolsModule, "vivid_devtools_set_focused_node");
        clearFocusedNode = (ClearFocusedNodeFn)GetProcAddress(devtoolsModule, "vivid_devtools_clear_focused_node");
        setPendingCount = (SetPendingCountFn)GetProcAddress(devtoolsModule, "vivid_devtools_set_pending_count");
        setMcpWarning = (SetMcpWarningFn)GetProcAddress(devtoolsModule, "vivid_devtools_set_mcp_warning");
        setParamCallback = (SetParamCallbackFn)GetProcAddress(devtoolsModule, "vivid_devtools_set_param_callback");
        saveSnapshot = (SaveSnapshotFn)GetProcAddress(devtoolsModule, "vivid_devtools_save_snapshot");
        snapshotRequested = (SnapshotRequestedFn)GetProcAddress(devtoolsModule, "vivid_devtools_snapshot_requested");
        getExporter = (GetExporterFn)GetProcAddress(devtoolsModule, "vivid_devtools_get_exporter");
        setFullscreenCallback = (SetFullscreenCallbackFn)GetProcAddress(devtoolsModule, "vivid_devtools_set_fullscreen_callback");
        setHelpCallback = (SetHelpCallbackFn)GetProcAddress(devtoolsModule, "vivid_devtools_set_help_callback");
    }
#endif
}

static bool available() {
    lookupFunctions();
    return init != nullptr && render != nullptr;
}

static void tryInit(void* ctx, WGPUTextureFormat format) {
    if (!available() || g_initialized) return;
    init(ctx, format);
    g_initialized = true;
}

static void tryShutdown() {
    if (!available() || !g_initialized) return;
    shutdown();
    g_initialized = false;
}

static void tryUpdate() {
    if (!available() || !g_initialized) return;
    update();
}

static void tryRender(WGPURenderPassEncoder pass, const void* input, void* ctx) {
    if (!available() || !g_initialized) return;
    render(pass, input, ctx);
}

static bool tryConsumedInput() {
    if (!available() || !g_initialized) return false;
    return consumedInput();
}

static bool tryIsInteracting() {
    if (!available() || !g_initialized) return false;
    return isInteracting();
}

static void tryOnChar(uint32_t codepoint) {
    if (!available() || !g_initialized) return;
    onChar(codepoint);
}

static bool tryOnKey(int key, int mods) {
    if (!available() || !g_initialized || !onKey) return false;
    return onKey(key, mods);
}

static void trySetFullscreenCallback(void (*callback)()) {
    if (!available() || !g_initialized || !setFullscreenCallback) return;
    setFullscreenCallback(callback);
}

static void trySetHelpCallback(void (*callback)()) {
    if (!available() || !g_initialized || !setHelpCallback) return;
    setHelpCallback(callback);
}

// Callback for fullscreen toggle shortcut
static void fullscreenToggleCallback() {
    if (g_callbackContext) {
        g_callbackContext->fullscreen(!g_callbackContext->fullscreen());
    }
}

// Set up all shortcut callbacks after init
static void setupShortcutCallbacks(vivid::Context* ctx) {
    g_callbackContext = ctx;
    trySetFullscreenCallback(fullscreenToggleCallback);
    // Help callback can be set up later when we have a help panel
}

static void trySetWindow(GLFWwindow* window) {
    if (!available() || !g_initialized || !setWindow) return;
    setWindow(window);
}

// Visualizer helpers
static void tryUpdateSolo(void* ctx) {
    if (!available() || !g_initialized || !updateSolo) return;
    updateSolo(ctx);
}

static void trySaveSnapshot(void* ctx) {
    if (!available() || !g_initialized || !saveSnapshot) return;
    saveSnapshot(ctx);
}

static bool trySnapshotRequested() {
    if (!available() || !g_initialized || !snapshotRequested) return false;
    return snapshotRequested();
}

static void* tryGetExporter() {
    if (!available() || !g_initialized || !getExporter) return nullptr;
    return getExporter();
}

static void tryEnterSolo(void* op, const char* name) {
    if (!available() || !g_initialized || !enterSolo) return;
    enterSolo(op, name);
}

static void tryExitSolo() {
    if (!available() || !g_initialized || !exitSolo) return;
    exitSolo();
}

static void trySelectNode(const char* name) {
    if (!available() || !g_initialized || !selectNode) return;
    selectNode(name);
}

static void trySetFocusedNode(const char* name) {
    if (!available() || !g_initialized || !setFocusedNode) return;
    setFocusedNode(name);
}

static void tryClearFocusedNode() {
    if (!available() || !g_initialized || !clearFocusedNode) return;
    clearFocusedNode();
}

static void trySetPendingCount(size_t count) {
    if (!available() || !g_initialized || !setPendingCount) return;
    setPendingCount(count);
}

static void trySetMcpWarning(const char* warning) {
    if (!available() || !g_initialized || !setMcpWarning) return;
    setMcpWarning(warning);
}

static void trySetParamCallback(void (*callback)(const char*, const char*, const float*, const float*, int)) {
    if (!available() || !g_initialized || !setParamCallback) return;
    setParamCallback(callback);
}

static bool tryIsPanelVisible(const char* panelId) {
    if (!available() || !g_initialized || !isPanelVisible) return false;
    return isPanelVisible(panelId);
}

} // namespace vivid::devtools_dynamic

namespace fs = std::filesystem;

namespace vivid {

// Get executable directory for checking bundle markers
static fs::path getExecutableDir() {
#ifdef __APPLE__
    char pathBuf[4096];
    uint32_t size = sizeof(pathBuf);
    if (_NSGetExecutablePath(pathBuf, &size) == 0) {
        return fs::canonical(pathBuf).parent_path();
    }
#elif defined(_WIN32)
    char pathBuf[MAX_PATH];
    GetModuleFileNameA(nullptr, pathBuf, MAX_PATH);
    return fs::path(pathBuf).parent_path();
#elif defined(__linux__)
    char pathBuf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", pathBuf, sizeof(pathBuf) - 1);
    if (len != -1) {
        pathBuf[len] = '\0';
        return fs::path(pathBuf).parent_path();
    }
#endif
    return fs::current_path();
}

// -----------------------------------------------------------------------------
// MCP Configuration Check
// -----------------------------------------------------------------------------
// Checks if Claude Code MCP is configured for Vivid integration

static std::string checkMcpConfiguration() {
    // Look for Claude Code MCP config
    const char* home = getenv("HOME");
    if (!home) return "";

    fs::path claudeConfig = fs::path(home) / ".claude.json";

    // Also check for Claude Code-specific config locations
    std::vector<fs::path> configPaths = {
        claudeConfig,
        fs::path(home) / ".config" / "claude" / "settings.json",
    };

    for (const auto& configPath : configPaths) {
        if (!fs::exists(configPath)) continue;

        try {
            std::ifstream file(configPath);
            if (!file) continue;

            nlohmann::json config = nlohmann::json::parse(file);

            // Check for mcpServers.vivid
            if (config.contains("mcpServers")) {
                auto& servers = config["mcpServers"];
                if (servers.contains("vivid")) {
                    // MCP is configured
                    return "";
                }
            }
        } catch (...) {
            // Ignore parse errors
        }
    }

    // MCP not configured - return warning
    return "MCP not configured";
}

// -----------------------------------------------------------------------------
// Memory Debugging
// -----------------------------------------------------------------------------

#ifdef __APPLE__
static size_t getMemoryUsageMB() {
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
        return info.resident_size / (1024 * 1024);  // Convert to MB
    }
    return 0;
}
#else
static size_t getMemoryUsageMB() {
    return 0;  // Not implemented for other platforms
}
#endif

static double g_lastMemoryLogTime = 0.0;
static size_t g_initialMemory = 0;
static size_t g_lastMemory = 0;


static void logMemoryUsage(double time) {
    size_t currentMB = getMemoryUsageMB();
    if (currentMB == 0) return;  // Not supported on this platform

    if (g_initialMemory == 0) {
        g_initialMemory = currentMB;
        g_lastMemory = currentMB;
        std::cout << "=== Memory Tracking Started ===" << std::endl;
    }

    int64_t deltaMB = static_cast<int64_t>(currentMB) - static_cast<int64_t>(g_initialMemory);
    int64_t deltaFromLast = static_cast<int64_t>(currentMB) - static_cast<int64_t>(g_lastMemory);

    std::cout << "[" << std::fixed << std::setprecision(1) << time << "s] "
              << "Memory: " << currentMB << " MB "
              << "(total: " << (deltaMB >= 0 ? "+" : "") << deltaMB << " MB, "
              << "last 10s: " << (deltaFromLast >= 0 ? "+" : "") << deltaFromLast << " MB)"
              << std::endl;

    g_lastMemory = currentMB;
}

// -----------------------------------------------------------------------------
// WebGPU Initialization Helpers
// -----------------------------------------------------------------------------

// Helper to create WGPUStringView from C string
inline WGPUStringView toStringView(const char* str) {
    WGPUStringView sv;
    sv.data = str;
    sv.length = WGPU_STRLEN;  // Null-terminated
    return sv;
}

struct AdapterUserData {
    WGPUAdapter adapter = nullptr;
    bool done = false;
};

static void onAdapterRequestEnded(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                                  WGPUStringView message, void* userdata1, void* userdata2) {
    auto* data = static_cast<AdapterUserData*>(userdata1);
    if (status == WGPURequestAdapterStatus_Success) {
        data->adapter = adapter;
    } else {
        std::cerr << "Failed to request adapter: "
                  << (message.data ? std::string(message.data, message.length == WGPU_STRLEN ? strlen(message.data) : message.length) : "unknown error")
                  << std::endl;
    }
    data->done = true;
}

struct DeviceUserData {
    WGPUDevice device = nullptr;
    bool done = false;
};

static void onDeviceRequestEnded(WGPURequestDeviceStatus status, WGPUDevice device,
                                 WGPUStringView message, void* userdata1, void* userdata2) {
    auto* data = static_cast<DeviceUserData*>(userdata1);
    if (status == WGPURequestDeviceStatus_Success) {
        data->device = device;
    } else {
        std::cerr << "Failed to request device: "
                  << (message.data ? std::string(message.data, message.length == WGPU_STRLEN ? strlen(message.data) : message.length) : "unknown error")
                  << std::endl;
    }
    data->done = true;
}

static void onDeviceLost(WGPUDevice const* device, WGPUDeviceLostReason reason,
                         WGPUStringView message, void* userdata1, void* userdata2) {
    std::cerr << "WebGPU Device Lost: "
              << (message.data ? std::string(message.data, message.length == WGPU_STRLEN ? strlen(message.data) : message.length) : "unknown")
              << std::endl;
}

static void onDeviceError(WGPUDevice const* device, WGPUErrorType type,
                          WGPUStringView message, void* userdata1, void* userdata2) {
    std::cerr << "WebGPU Error: "
              << (message.data ? std::string(message.data, message.length == WGPU_STRLEN ? strlen(message.data) : message.length) : "unknown")
              << std::endl;
}

// -----------------------------------------------------------------------------
// Main Loop Context
// -----------------------------------------------------------------------------
// Encapsulates all state needed for the main loop iteration.

struct MainLoopContext {
    // WebGPU infrastructure
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUSurface surface = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_BGRA8Unorm;
    WGPUSurfaceConfigurationExtras configExtras = {};
    WGPUSurfaceConfiguration config = {};

    // Window state
    GLFWwindow* window = nullptr;
    int width = 0;
    int height = 0;
    bool isFullscreen = false;
    int windowedX = 0;
    int windowedY = 0;
    int windowedWidth = 1280;
    int windowedHeight = 720;
    WindowManager* windowManager = nullptr;

    // Timing & performance
    double lastFpsTime = 0.0;
    int frameCount = 0;
    double lastFrameTime = 0.0;
    RuntimePerformanceStats perfStats;
    static constexpr size_t kHistorySize = 60;

    // Loop control
    int snapshotFrameCounter = 0;
    std::set<int> snapshotFramesPending;  // Frames still to capture
    int snapshotFramesCaptured = 0;        // Count of frames captured
    double snapshotStartTime = 0.0;        // When snapshot mode started (for timeout)
    static constexpr double kSnapshotTimeout = 30.0;  // Exit after 30s if no snapshot
    VideoExporter cliRecorder;
    bool cliRecordingStarted = false;
    bool chainNeedsSetup = true;
    bool chainAlreadyLoaded = false;  // True if chain was loaded during early config extraction
    bool visualizerVisible = false;  // Chain visualizer visibility (Cmd/Ctrl+4 to toggle)
    bool initialStatusShown = false;  // Have we shown the startup status banner?

    // MCP live capture request (set by WebSocket command, cleared after capture)
    std::string mcpCaptureRequestPath;  // Non-empty = capture requested
    std::mutex mcpCaptureMutex;

    // Core runtime objects (non-owning pointers)
    Context* ctx = nullptr;
    Display* display = nullptr;
    HotReload* hotReload = nullptr;
    RuntimeAPI* editorBridge = nullptr;

    // Visualizer state (dynamically loaded module)
    bool visualizerAvailable = false;

    // IDE CEF Browser panel (part of the UI, enabled with --show-ui)
#ifdef VIVID_HAS_CEF
    std::unique_ptr<vivid::cef::Browser> ideBrowser;
    bool idePanelVisible = false;
    bool idePanelInitialized = false;
    glm::vec4 idePanelBounds = {0, 0, 400, 600};  // x, y, w, h
    bool idePanelHovered = false;
    // Dragging state
    bool idePanelDragging = false;
    glm::vec2 ideDragOffset = {0, 0};  // Offset from panel corner to mouse position
    // Resize state (bitmask: 1=left, 2=right, 4=top, 8=bottom)
    int idePanelResizing = 0;
    glm::vec2 ideResizeStart = {0, 0};      // Mouse position when resize started
    glm::vec4 ideResizeStartBounds = {0, 0, 0, 0};  // Bounds when resize started
    static constexpr float ideResizeHandleSize = 8.0f;
    static constexpr float ideMinPanelWidth = 300.0f;
    static constexpr float ideMinPanelHeight = 200.0f;
    // PTY for terminal
    std::unique_ptr<vivid::PTY> idePty;
    bool idePtyStarted = false;
#endif

    // CLI args needed in loop
    std::string snapshotPath;
    std::set<int> snapshotFrames;  // All frames to capture
    bool headless = false;
    int renderWidth = 0;
    int renderHeight = 0;
    std::string recordPath;
    float recordFps = 60.0f;
    float recordDuration = 0.0f;
    bool recordAudio = false;
    ExportCodec recordCodec = ExportCodec::H264;
    int maxFrames = 0;
    int windowWidth = 1280;
    int windowHeight = 720;
    bool showUI = false;

    // Project info
    std::string projectName;

    // Composite texture for UI-inclusive screenshots
    // Renders chain + ImGui + DevTools to this texture before blitting to swapchain
    WGPUTexture compositeTexture = nullptr;
    WGPUTextureView compositeTextureView = nullptr;
    uint32_t compositeWidth = 0;
    uint32_t compositeHeight = 0;

    // Callbacks (lambdas converted to std::function)
    std::function<void(const std::string&)> updateSourceLines;
    std::function<std::vector<RuntimeOperatorInfo>()> gatherOperatorInfo;
    std::function<std::vector<RuntimeParamInfo>()> gatherParamValues;
    std::function<RuntimeWindowState()> gatherWindowState;
};

// -----------------------------------------------------------------------------
// Main Loop Iteration
// -----------------------------------------------------------------------------
// Returns true to continue running, false to exit.

// Saved scroll value from before blockMouseInput() is called
// Used to pass scroll to NodeGraph even when blocking input to user code
static glm::vec2 g_savedScrollForVisualizer;

// Key press tracking for one-shot key events
static bool g_keyPressed[512] = {};      // Keys pressed this frame
static bool g_keyPrevDown[512] = {};     // Keys down previous frame

// Update key states (call once per frame before using keyPressed)
static void updateKeyStates(GLFWwindow* window) {
    // Check common keys and detect rising edge (was up, now down)
    const int keysToCheck[] = {
        GLFW_KEY_ESCAPE, GLFW_KEY_ENTER, GLFW_KEY_GRAVE_ACCENT, GLFW_KEY_SPACE,
        GLFW_KEY_RIGHT, GLFW_KEY_LEFT, GLFW_KEY_DOWN, GLFW_KEY_UP,
        // Number keys 0-9 for Cmd+1..4 shortcuts
        GLFW_KEY_0, GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4,
        GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9,
        // Terminal special keys
        GLFW_KEY_BACKSPACE, GLFW_KEY_TAB, GLFW_KEY_DELETE, GLFW_KEY_INSERT,
        GLFW_KEY_HOME, GLFW_KEY_END, GLFW_KEY_PAGE_UP, GLFW_KEY_PAGE_DOWN,
        // Punctuation for shortcuts (Cmd+, for preferences)
        GLFW_KEY_COMMA,
        // Function keys
        GLFW_KEY_F1,
        // Letter keys A-Z for Ctrl+letter combinations
        GLFW_KEY_A, GLFW_KEY_B, GLFW_KEY_C, GLFW_KEY_D, GLFW_KEY_E, GLFW_KEY_F,
        GLFW_KEY_G, GLFW_KEY_H, GLFW_KEY_I, GLFW_KEY_J, GLFW_KEY_K, GLFW_KEY_L,
        GLFW_KEY_M, GLFW_KEY_N, GLFW_KEY_O, GLFW_KEY_P, GLFW_KEY_Q, GLFW_KEY_R,
        GLFW_KEY_S, GLFW_KEY_T, GLFW_KEY_U, GLFW_KEY_V, GLFW_KEY_W, GLFW_KEY_X,
        GLFW_KEY_Y, GLFW_KEY_Z
    };

    for (int key : keysToCheck) {
        bool isDown = glfwGetKey(window, key) == GLFW_PRESS;
        g_keyPressed[key] = isDown && !g_keyPrevDown[key];  // Rising edge
        g_keyPrevDown[key] = isDown;
    }
}

// Ensure composite texture exists and matches window size
// Returns false if texture creation failed
static bool ensureCompositeTexture(MainLoopContext& mlc, uint32_t width, uint32_t height) {
    if (mlc.compositeTexture && mlc.compositeWidth == width && mlc.compositeHeight == height) {
        return true;  // Already correct size
    }

    // Release old texture if any
    if (mlc.compositeTextureView) {
        wgpuTextureViewRelease(mlc.compositeTextureView);
        mlc.compositeTextureView = nullptr;
    }
    if (mlc.compositeTexture) {
        wgpuTextureRelease(mlc.compositeTexture);
        mlc.compositeTexture = nullptr;
    }

    if (width == 0 || height == 0) {
        return false;
    }

    // Create new composite texture
    WGPUTextureDescriptor textureDesc = {};
    textureDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding;
    textureDesc.dimension = WGPUTextureDimension_2D;
    textureDesc.size = {width, height, 1};
    textureDesc.format = mlc.surfaceFormat;
    textureDesc.mipLevelCount = 1;
    textureDesc.sampleCount = 1;

    mlc.compositeTexture = wgpuDeviceCreateTexture(mlc.device, &textureDesc);
    if (!mlc.compositeTexture) {
        std::cerr << "[vivid] Failed to create composite texture" << std::endl;
        return false;
    }

    // Create view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = mlc.surfaceFormat;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;

    mlc.compositeTextureView = wgpuTextureCreateView(mlc.compositeTexture, &viewDesc);
    if (!mlc.compositeTextureView) {
        std::cerr << "[vivid] Failed to create composite texture view" << std::endl;
        wgpuTextureRelease(mlc.compositeTexture);
        mlc.compositeTexture = nullptr;
        return false;
    }

    mlc.compositeWidth = width;
    mlc.compositeHeight = height;

    return true;
}

// Cleanup composite texture on shutdown
static void releaseCompositeTexture(MainLoopContext& mlc) {
    if (mlc.compositeTextureView) {
        wgpuTextureViewRelease(mlc.compositeTextureView);
        mlc.compositeTextureView = nullptr;
    }
    if (mlc.compositeTexture) {
        wgpuTextureRelease(mlc.compositeTexture);
        mlc.compositeTexture = nullptr;
    }
    mlc.compositeWidth = 0;
    mlc.compositeHeight = 0;
}

static bool mainLoopIteration(MainLoopContext& mlc) {
    glfwPollEvents();

    // Update key states for one-shot detection
    updateKeyStates(mlc.window);

    // Memory logging every 10 seconds
    {
        double now = glfwGetTime();
        if (now - g_lastMemoryLogTime >= 10.0) {
            logMemoryUsage(now);
            g_lastMemoryLogTime = now;
        }
    }

    // Begin frame (updates time, input, etc.)
    mlc.ctx->beginFrame();
    mlc.ctx->beginDebugFrame();

    // Handle window resize
    if (mlc.ctx->width() != mlc.width || mlc.ctx->height() != mlc.height) {
        mlc.width = mlc.ctx->width();
        mlc.height = mlc.ctx->height();
        if (mlc.width > 0 && mlc.height > 0) {
            mlc.config.width = static_cast<uint32_t>(mlc.width);
            mlc.config.height = static_cast<uint32_t>(mlc.height);
            wgpuSurfaceConfigure(mlc.surface, &mlc.config);
        }
    }

    // Handle vsync change
    if (mlc.ctx->consumeVsyncChange()) {
        mlc.config.presentMode = mlc.ctx->vsync() ? WGPUPresentMode_Fifo : WGPUPresentMode_Immediate;
        wgpuSurfaceConfigure(mlc.surface, &mlc.config);
    }

    // Handle fullscreen change (from ctx.fullscreen() API)
    if (mlc.ctx->consumeFullscreenChange()) {
        if (mlc.ctx->fullscreen() && !mlc.isFullscreen) {
            // Save windowed position and size
            glfwGetWindowPos(mlc.window, &mlc.windowedX, &mlc.windowedY);
            glfwGetWindowSize(mlc.window, &mlc.windowedWidth, &mlc.windowedHeight);

            // Get target monitor (use current or selected)
            int monitorCount = 0;
            GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
            int targetIdx = std::min(mlc.ctx->targetMonitor(), monitorCount - 1);
            targetIdx = std::max(0, targetIdx);
            GLFWmonitor* monitor = monitors[targetIdx];
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);

            // Enter fullscreen
            glfwSetWindowMonitor(mlc.window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
            mlc.isFullscreen = true;
        } else if (!mlc.ctx->fullscreen() && mlc.isFullscreen) {
            // Exit fullscreen - restore windowed mode
            glfwSetWindowMonitor(mlc.window, nullptr, mlc.windowedX, mlc.windowedY, mlc.windowedWidth, mlc.windowedHeight, 0);
            mlc.isFullscreen = false;
        }
    }

    // Handle borderless (decorated) window change
    if (mlc.ctx->consumeBorderlessChange()) {
        glfwSetWindowAttrib(mlc.window, GLFW_DECORATED, mlc.ctx->borderless() ? GLFW_FALSE : GLFW_TRUE);
    }

    // Handle always-on-top (floating) change
    if (mlc.ctx->consumeAlwaysOnTopChange()) {
        glfwSetWindowAttrib(mlc.window, GLFW_FLOATING, mlc.ctx->alwaysOnTop() ? GLFW_TRUE : GLFW_FALSE);
    }

    // Handle cursor visibility change
    if (mlc.ctx->consumeCursorVisibleChange()) {
        glfwSetInputMode(mlc.window, GLFW_CURSOR,
            mlc.ctx->cursorVisible() ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
    }

    // Handle display mode change
    if (mlc.ctx->consumeDisplayModeChange()) {
        mlc.display->setDisplayMode(mlc.ctx->displayMode());
    }

    // Handle monitor change (move window to different display)
    if (mlc.ctx->consumeMonitorChange()) {
        int monitorCount = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
        int targetIdx = std::min(mlc.ctx->targetMonitor(), monitorCount - 1);
        targetIdx = std::max(0, targetIdx);

        if (targetIdx < monitorCount) {
            GLFWmonitor* monitor = monitors[targetIdx];
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);

            if (mlc.isFullscreen) {
                // In fullscreen: switch to fullscreen on target monitor
                glfwSetWindowMonitor(mlc.window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
            } else {
                // In windowed: center window on target monitor
                int mx, my;
                glfwGetMonitorPos(monitor, &mx, &my);

                int ww, wh;
                glfwGetWindowSize(mlc.window, &ww, &wh);

                int newX = mx + (mode->width - ww) / 2;
                int newY = my + (mode->height - wh) / 2;
                glfwSetWindowPos(mlc.window, newX, newY);
            }
        }
    }

    // Handle window position change
    if (mlc.ctx->consumeWindowPosChange()) {
        glfwSetWindowPos(mlc.window, mlc.ctx->targetWindowX(), mlc.ctx->targetWindowY());
    }

    // Handle window size change
    if (mlc.ctx->consumeWindowSizeChange()) {
        glfwSetWindowSize(mlc.window, mlc.ctx->targetWindowWidth(), mlc.ctx->targetWindowHeight());
    }

    // Skip frame if minimized (width/height = 0)
    if (mlc.width == 0 || mlc.height == 0) {
        mlc.ctx->endFrame();
        return true;  // continue
    }

    // Get current texture
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(mlc.surface, &surfaceTexture);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        mlc.ctx->endFrame();
        return true;  // continue
    }

    // Create view with explicit format matching the surface texture
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = mlc.surfaceFormat;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;
    WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, &viewDesc);

    // Check for hot-reload using safe API
    bool justReloaded = false;
    if (mlc.hotReload->checkNeedsReload()) {
        // Try to compile first - this doesn't affect the old chain
        if (mlc.hotReload->tryCompile()) {
            // Compilation succeeded - now safe to destroy old chain and load new one
            // Save operator states before destroying chain
            if (mlc.ctx->hasChain()) {
                mlc.ctx->preserveStates(mlc.ctx->chain());
            }
            // Destroy operators BEFORE unloading the library
            mlc.ctx->clearRegisteredOperators();
            mlc.ctx->resetChain();

            // Load the newly compiled library
            mlc.hotReload->loadCompiled();
            mlc.chainNeedsSetup = true;
        }
        // If tryCompile() failed, the old chain is still running - just show the error
        justReloaded = true;
    }

    // Update error state from hot-reload
    if (mlc.hotReload->hasError()) {
        mlc.ctx->setError(mlc.hotReload->getError());

        // Show prominent status banner on initial load failure
        if (!mlc.initialStatusShown) {
            std::cerr << "\n══════════════════════════════════════\n"
                      << "  VIVID: COMPILE FAILED\n"
                      << "══════════════════════════════════════\n" << std::endl;
            mlc.initialStatusShown = true;
        }

        // In snapshot mode, exit immediately on compile error (don't wait for timeout)
        if (!mlc.snapshotPath.empty() && !mlc.snapshotFramesPending.empty()) {
            std::cerr << "Snapshot aborted: chain failed to compile\n"
                      << mlc.hotReload->getError() << std::endl;
            glfwSetWindowShouldClose(mlc.window, GLFW_TRUE);
        }
    } else if (mlc.hotReload->isLoaded()) {
        mlc.ctx->clearError();

        // Show prominent status banner on initial load success
        if (!mlc.initialStatusShown) {
            std::cout << "\n======================================\n"
                      << "  VIVID: READY\n"
                      << "======================================\n" << std::endl;
            mlc.initialStatusShown = true;
        }
    }

    // In snapshot mode, also exit immediately if there's a context-level error (e.g., file not found)
    if (!mlc.snapshotPath.empty() && !mlc.snapshotFramesPending.empty() && mlc.ctx->hasError()) {
        std::cerr << "Snapshot aborted: " << mlc.ctx->errorMessage() << std::endl;
        glfwSetWindowShouldClose(mlc.window, GLFW_TRUE);
    }

    // Notify connected editors of compile status
    if (justReloaded && mlc.editorBridge->clientCount() > 0) {
        if (mlc.hotReload->hasError()) {
            mlc.editorBridge->sendCompileStatus(false, mlc.hotReload->getError());
        } else {
            mlc.editorBridge->sendCompileStatus(true, "");
        }
    }

    // Call chain functions if loaded
    if (mlc.hotReload->isLoaded()) {
        // Call setup if needed (after reload)
        if (mlc.chainNeedsSetup) {
            mlc.hotReload->getSetupFn()(*mlc.ctx);

            // Auto-initialize the chain
            mlc.ctx->chain().init(*mlc.ctx);

            // Honor chain's window size request
            if (mlc.ctx->chain().hasWindowSize()) {
                int w = mlc.ctx->chain().windowWidth();
                int h = mlc.ctx->chain().windowHeight();
                if (w > 0 && h > 0 && !mlc.isFullscreen) {
                    glfwSetWindowSize(mlc.window, w, h);
                }
            }

            // Update render resolution from chain if set
            if (mlc.ctx->chain().hasResolution()) {
                mlc.ctx->setRenderResolution(mlc.ctx->chain().defaultWidth(), mlc.ctx->chain().defaultHeight());
            }

            // Restore preserved states across hot-reloads
            if (mlc.ctx->hasPreservedStates()) {
                mlc.ctx->restoreStates(mlc.ctx->chain());
            }

            mlc.chainNeedsSetup = false;

            // Check if vivid-gui module was loaded and initialize ImGui
            imgui_dynamic::resetLookup();  // Re-scan since new libs may be loaded
            imgui_dynamic::tryInit(mlc.device, mlc.queue, mlc.surfaceFormat);


            // Update operator source line numbers from chain.cpp
            if (mlc.updateSourceLines) {
                mlc.updateSourceLines(mlc.ctx->chainPath());
            }

            // Send operator list to connected editors
            if (mlc.editorBridge->clientCount() > 0 && mlc.gatherOperatorInfo) {
                mlc.editorBridge->sendOperatorList(mlc.gatherOperatorInfo());
            }

            // Start CLI recording (once, after first chain load)
            if (!mlc.recordPath.empty() && !mlc.cliRecordingStarted) {
                int recW = mlc.renderWidth > 0 ? mlc.renderWidth : mlc.windowWidth;
                int recH = mlc.renderHeight > 0 ? mlc.renderHeight : mlc.windowHeight;
                bool started = false;
                if (mlc.recordAudio) {
                    started = mlc.cliRecorder.startWithAudio(mlc.recordPath, recW, recH,
                                                         mlc.recordFps, mlc.recordCodec,
                                                         AUDIO_SAMPLE_RATE, AUDIO_CHANNELS);
                    if (started) {
                        // Start audio recording tap (captures audio during playback)
                        mlc.ctx->chain().startAudioRecordingTap();
                    }
                } else {
                    started = mlc.cliRecorder.start(mlc.recordPath, recW, recH, mlc.recordFps, mlc.recordCodec);
                }
                if (started) {
                    std::cout << "Recording to: " << mlc.recordPath
                              << " (" << recW << "x" << recH << " @ " << mlc.recordFps << "fps";
                    if (mlc.recordDuration > 0) {
                        std::cout << ", " << mlc.recordDuration << "s";
                    }
                    std::cout << ")" << std::endl;
                } else {
                    std::cerr << "Failed to start recording: " << mlc.cliRecorder.error() << std::endl;
                }
                mlc.cliRecordingStarted = true;
            }
        }

        // Begin ImGui frame if vivid-gui is loaded (before user update)
        {
            float xscale, yscale;
            glfwGetWindowContentScale(mlc.window, &xscale, &yscale);
            mlc.ctx->setContentScale(xscale);  // Store for Canvas/ChainVisualizer font scaling

            vivid::FrameInput frameInput;
            frameInput.width = mlc.ctx->width();
            frameInput.height = mlc.ctx->height();
            frameInput.contentScale = xscale;
            frameInput.dt = static_cast<float>(mlc.ctx->dt());
            frameInput.time = static_cast<float>(mlc.ctx->time());
            frameInput.mousePos = mlc.ctx->mouse();
            frameInput.mouseDown[0] = mlc.ctx->mouseButton(0).held;
            frameInput.mouseDown[1] = mlc.ctx->mouseButton(1).held;
            frameInput.mouseDown[2] = mlc.ctx->mouseButton(2).held;
            frameInput.scroll = mlc.ctx->scroll();
            frameInput.keyCtrl = glfwGetKey(mlc.window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                                 glfwGetKey(mlc.window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
            frameInput.keyShift = glfwGetKey(mlc.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                                  glfwGetKey(mlc.window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
            frameInput.keyAlt = glfwGetKey(mlc.window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                                glfwGetKey(mlc.window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
            frameInput.keySuper = glfwGetKey(mlc.window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
                                  glfwGetKey(mlc.window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;

            imgui_dynamic::tryBeginFrame(frameInput);
        }

        // Block mouse input when visualizer is visible
        // The visualizer is a full-screen overlay, so all mouse input should go to it
        // Save scroll before blocking since NodeGraph needs it for zooming
        g_savedScrollForVisualizer = mlc.ctx->scroll();
        if (mlc.visualizerVisible) {
            mlc.ctx->blockMouseInput();
        }

        // Call user's update function
        mlc.hotReload->getUpdateFn()(*mlc.ctx);

        // Auto-process the chain
        mlc.ctx->chain().process(*mlc.ctx);

        // Capture frame for video export if recording (visualizer UI initiated)
        auto* vizExporter = static_cast<VideoExporter*>(devtools_dynamic::tryGetExporter());
        if (vizExporter && vizExporter->isRecording() && mlc.ctx->outputTexture()) {
            WGPUTexture outputTex = mlc.ctx->chain().outputTexture();
            if (outputTex) {
                vizExporter->captureFrame(mlc.device, mlc.queue, outputTex);

                // Capture audio if recording with audio (using non-blocking tap)
                if (vizExporter->hasAudio()) {
                    // Pop available audio from the recording tap
                    // The tap buffer holds audio generated by the audio thread
                    static std::vector<float> audioBuffer;
                    constexpr uint32_t MAX_FRAMES_PER_CALL = 4096;
                    if (audioBuffer.size() < MAX_FRAMES_PER_CALL * AUDIO_CHANNELS) {
                        audioBuffer.resize(MAX_FRAMES_PER_CALL * AUDIO_CHANNELS);
                    }

                    // Pop whatever audio is available (non-blocking)
                    uint32_t framesRead = mlc.ctx->chain().popAudioRecordedSamples(
                        audioBuffer.data(), MAX_FRAMES_PER_CALL);
                    if (framesRead > 0) {
                        vizExporter->pushAudioSamples(
                            audioBuffer.data(), framesRead);
                    }
                }
            }
        }

        // CLI video recording capture
        if (mlc.cliRecorder.isRecording() && mlc.ctx->outputTexture()) {
            WGPUTexture outputTex = mlc.ctx->chain().outputTexture();
            if (outputTex) {
                mlc.cliRecorder.captureFrame(mlc.device, mlc.queue, outputTex);

                // Capture audio if enabled (using non-blocking tap)
                if (mlc.cliRecorder.hasAudio()) {
                    static std::vector<float> cliAudioBuffer;
                    constexpr uint32_t MAX_FRAMES_PER_CALL = 4096;
                    if (cliAudioBuffer.size() < MAX_FRAMES_PER_CALL * AUDIO_CHANNELS) {
                        cliAudioBuffer.resize(MAX_FRAMES_PER_CALL * AUDIO_CHANNELS);
                    }

                    // Pop whatever audio is available (non-blocking)
                    uint32_t framesRead = mlc.ctx->chain().popAudioRecordedSamples(
                        cliAudioBuffer.data(), MAX_FRAMES_PER_CALL);
                    if (framesRead > 0) {
                        mlc.cliRecorder.pushAudioSamples(cliAudioBuffer.data(), framesRead);
                    }
                }

                // Check duration limit
                if (mlc.recordDuration > 0 && mlc.cliRecorder.duration() >= mlc.recordDuration) {
                    std::cout << "Recording complete: " << mlc.cliRecorder.frameCount() << " frames, "
                              << mlc.cliRecorder.duration() << "s" << std::endl;
                    mlc.ctx->chain().stopAudioRecordingTap();  // Stop tap before stopping recorder
                    mlc.cliRecorder.stop();
                    glfwSetWindowShouldClose(mlc.window, GLFW_TRUE);
                }
            }
        }

        // Save snapshot if requested (interactive UI via visualizer)
        if (devtools_dynamic::trySnapshotRequested()) {
            devtools_dynamic::trySaveSnapshot(mlc.ctx);
        }

        // MCP live capture request - defer to after render so we capture with UI
        // (actual capture happens after render pass ends, using composite texture)

        // Track total frames for --snapshot and --frames options
        int currentFrame = mlc.snapshotFrameCounter;
        mlc.snapshotFrameCounter++;

        // Automated snapshot mode (CLI --snapshot flag)
        // Captures multiple frames if specified
        if (!mlc.snapshotPath.empty() && !mlc.snapshotFramesPending.empty()) {
            if (mlc.snapshotFramesPending.count(currentFrame) > 0) {
                // Generate output path with frame number if multiple frames
                std::string outputPath = mlc.snapshotPath;
                if (mlc.snapshotFrames.size() > 1) {
                    // Insert frame number before extension: output.png -> output_0005.png
                    fs::path p(mlc.snapshotPath);
                    std::string stem = p.stem().string();
                    std::string ext = p.extension().string();
                    fs::path dir = p.parent_path();
                    char buf[32];
                    snprintf(buf, sizeof(buf), "_%04d", currentFrame);
                    outputPath = (dir / (stem + buf + ext)).string();
                }

                std::string savedPath = mlc.ctx->snapshot(outputPath);
                if (!savedPath.empty()) {
                    mlc.snapshotFramesCaptured++;
                    mlc.snapshotFramesPending.erase(currentFrame);
                    std::cout << "Snapshot saved (" << mlc.snapshotFramesCaptured
                              << "/" << mlc.snapshotFrames.size() << ")" << std::endl;

                    // Exit after saving all frames (unless --frames is also set)
                    if (mlc.snapshotFramesPending.empty() && mlc.maxFrames == 0) {
                        glfwSetWindowShouldClose(mlc.window, GLFW_TRUE);
                    }
                } else {
                    std::cerr << "Failed to save snapshot for frame " << currentFrame << std::endl;
                    mlc.snapshotFramesPending.erase(currentFrame);  // Don't retry
                }
            }

            // Timeout check: if no snapshots captured after kSnapshotTimeout seconds, exit with error
            if (mlc.snapshotFramesCaptured == 0 &&
                glfwGetTime() - mlc.snapshotStartTime > mlc.kSnapshotTimeout) {
                std::cerr << "Snapshot timeout: no frames captured after "
                          << mlc.kSnapshotTimeout << " seconds (chain may have failed to compile)" << std::endl;
                glfwSetWindowShouldClose(mlc.window, GLFW_TRUE);
            }
        }

        // Frame limit mode (CLI --frames flag)
        if (mlc.maxFrames > 0 && mlc.snapshotFrameCounter >= mlc.maxFrames) {
            std::cout << "Rendered " << mlc.maxFrames << " frames, exiting." << std::endl;
            glfwSetWindowShouldClose(mlc.window, GLFW_TRUE);
        }
    }

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(mlc.device, &encoderDesc);

    // Decide whether to use composite texture (for UI-inclusive captures)
    // Use composite when devtools is available (so MCP captures include UI)
    bool useComposite = devtools_dynamic::available() &&
                        ensureCompositeTexture(mlc, static_cast<uint32_t>(mlc.width),
                                               static_cast<uint32_t>(mlc.height));

    // Render pass - clear to black
    // Target composite texture if available, otherwise swapchain
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = useComposite ? mlc.compositeTextureView : view;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};

    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    // Update display with current screen size (if display is valid)
    if (mlc.display->isValid()) {
        mlc.display->setScreenSize(mlc.width, mlc.height);
    }

    // Build frame input for NodeGraph/ImGui
    // IMPORTANT: Use raw GLFW state for mouse buttons, NOT Context's state
    // because blockMouseInput() may have zeroed Context's mouse state to prevent
    // user code from receiving input while the visualizer is being interacted with.
    float xscale, yscale;
    glfwGetWindowContentScale(mlc.window, &xscale, &yscale);
    mlc.ctx->setContentScale(xscale);  // Store for Canvas font scaling

    double mx, my;
    glfwGetCursorPos(mlc.window, &mx, &my);

    // Static variables for computing mouse delta and click states
    static glm::vec2 lastMousePos = {0, 0};
    static bool lastMouseDown[3] = {false, false, false};

    glm::vec2 currentMousePos = glm::vec2(static_cast<float>(mx), static_cast<float>(my));
    bool currentMouseDown[3] = {
        glfwGetMouseButton(mlc.window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS,
        glfwGetMouseButton(mlc.window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS,
        glfwGetMouseButton(mlc.window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS
    };

    vivid::FrameInput frameInput;
    frameInput.width = mlc.ctx->width();
    frameInput.height = mlc.ctx->height();
    frameInput.contentScale = xscale;
    frameInput.dt = static_cast<float>(mlc.ctx->dt());
    frameInput.time = static_cast<float>(mlc.ctx->time());
    frameInput.mousePos = currentMousePos;
    frameInput.mouseDelta = currentMousePos - lastMousePos;
    frameInput.mouseDown[0] = currentMouseDown[0];
    frameInput.mouseDown[1] = currentMouseDown[1];
    frameInput.mouseDown[2] = currentMouseDown[2];
    // Compute one-shot click/release states
    for (int i = 0; i < 3; i++) {
        frameInput.mouseClicked[i] = currentMouseDown[i] && !lastMouseDown[i];
        frameInput.mouseReleased[i] = !currentMouseDown[i] && lastMouseDown[i];
    }
    // Use saved scroll from before blockMouseInput() was called
    // This ensures NodeGraph can still zoom even when we're blocking input to user code
    frameInput.scroll = g_savedScrollForVisualizer;
    frameInput.keyCtrl = glfwGetKey(mlc.window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                         glfwGetKey(mlc.window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    frameInput.keyShift = glfwGetKey(mlc.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                          glfwGetKey(mlc.window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    frameInput.keyAlt = glfwGetKey(mlc.window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                        glfwGetKey(mlc.window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
    frameInput.keySuper = glfwGetKey(mlc.window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
                          glfwGetKey(mlc.window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
    // Copy one-shot key states
    std::memcpy(frameInput.keyPressed, g_keyPressed, sizeof(g_keyPressed));
    frameInput.surfaceFormat = mlc.surfaceFormat;

    // Update previous mouse state for next frame
    lastMousePos = currentMousePos;
    for (int i = 0; i < 3; i++) {
        lastMouseDown[i] = currentMouseDown[i];
    }

    // Update solo mode output texture before blit
    if (mlc.visualizerVisible && mlc.visualizerAvailable) {
        devtools_dynamic::tryUpdateSolo(mlc.ctx);
    }

    // Blit output texture (may have been modified by solo mode)
    if (mlc.ctx->outputTexture() && mlc.display->isValid()) {
        mlc.display->setTextureSize(mlc.ctx->renderWidth(), mlc.ctx->renderHeight());
        mlc.display->blit(pass, mlc.ctx->outputTexture());
    }

    // Render ImGui if vivid-gui is loaded (user chain UI)
    imgui_dynamic::tryRender(pass);

    // Check IDE panel hover state BEFORE visualizer render (to block input if needed)
#ifdef VIVID_HAS_CEF
    bool idePanelConsumedInput = false;
    if (mlc.idePanelInitialized && mlc.idePanelVisible && mlc.ideBrowser) {
        glm::vec2 mousePos = frameInput.mousePos;

        // Check if mouse is over the whole panel (including resize handles)
        const float hs = mlc.ideResizeHandleSize;
        mlc.idePanelHovered = mousePos.x >= mlc.idePanelBounds.x - hs &&
                             mousePos.x <= mlc.idePanelBounds.x + mlc.idePanelBounds.z + hs &&
                             mousePos.y >= mlc.idePanelBounds.y - hs &&
                             mousePos.y <= mlc.idePanelBounds.y + mlc.idePanelBounds.w + hs;

        // Block input to visualizer if IDE panel is hovered, being dragged, or being resized
        if (mlc.idePanelHovered || mlc.idePanelDragging || mlc.idePanelResizing != 0) {
            idePanelConsumedInput = true;
        }
    }
#endif

    // Render NodeGraph overlay (uses OverlayCanvas, no ImGui dependency)
    if (mlc.visualizerVisible && mlc.visualizerAvailable) {
        // Update pending change count for status bar display
        if (mlc.editorBridge) {
            devtools_dynamic::trySetPendingCount(mlc.editorBridge->pendingChangeCount());
        }

        // Render devtools (handles input routing internally)
        devtools_dynamic::tryRender(pass, &frameInput, mlc.ctx);
    }

#ifdef VIVID_HAS_CEF
    // Update IDE CEF Browser panel (if initialized and visible)
    if (mlc.idePanelInitialized && mlc.idePanelVisible && mlc.ideBrowser) {
        // Pump CEF message loop
        vivid::cef::pumpCefMessageLoop();

        // Handle dragging
        const float titleBarHeight = 37.0f;  // Height of the tab bar
        const float dragHandleWidth = 80.0f; // Width of drag handle at right side of title bar
        glm::vec2 mousePos = frameInput.mousePos;
        bool leftMouseDown = frameInput.mouseDown[0];

        // Check if mouse is over the drag handle (right side of title bar only)
        // This allows clicking on tabs on the left side while still allowing drag from right
        float dragHandleX = mlc.idePanelBounds.x + mlc.idePanelBounds.z - dragHandleWidth;
        bool overDragHandle = mousePos.x >= dragHandleX &&
                             mousePos.x <= mlc.idePanelBounds.x + mlc.idePanelBounds.z &&
                             mousePos.y >= mlc.idePanelBounds.y &&
                             mousePos.y <= mlc.idePanelBounds.y + titleBarHeight;

        // Start dragging if clicking on drag handle area (right side of title bar)
        if (overDragHandle && leftMouseDown && !mlc.idePanelDragging) {
            mlc.idePanelDragging = true;
            mlc.ideDragOffset = mousePos - glm::vec2(mlc.idePanelBounds.x, mlc.idePanelBounds.y);
        }

        // Update position while dragging
        if (mlc.idePanelDragging) {
            if (leftMouseDown) {
                mlc.idePanelBounds.x = mousePos.x - mlc.ideDragOffset.x;
                mlc.idePanelBounds.y = mousePos.y - mlc.ideDragOffset.y;

                // Clamp to logical window bounds
                float logicalWidth = mlc.windowWidth / frameInput.contentScale;
                float logicalHeight = mlc.windowHeight / frameInput.contentScale;
                mlc.idePanelBounds.x = std::max(0.0f, std::min(mlc.idePanelBounds.x,
                    logicalWidth - mlc.idePanelBounds.z));
                mlc.idePanelBounds.y = std::max(0.0f, std::min(mlc.idePanelBounds.y,
                    logicalHeight - mlc.idePanelBounds.w));
            } else {
                mlc.idePanelDragging = false;
            }
        }

        // Handle resize edges/corners (only if not dragging title bar)
        if (!mlc.idePanelDragging) {
            const float hs = mlc.ideResizeHandleSize;
            float px = mlc.idePanelBounds.x;
            float py = mlc.idePanelBounds.y;
            float pw = mlc.idePanelBounds.z;
            float ph = mlc.idePanelBounds.w;

            int hoveredEdge = 0;
            if (mousePos.x >= px - hs && mousePos.x <= px + hs &&
                mousePos.y >= py && mousePos.y <= py + ph) hoveredEdge |= 1;
            if (mousePos.x >= px + pw - hs && mousePos.x <= px + pw + hs &&
                mousePos.y >= py && mousePos.y <= py + ph) hoveredEdge |= 2;
            if (mousePos.y >= py - hs && mousePos.y <= py + hs &&
                mousePos.x >= px && mousePos.x <= px + pw) hoveredEdge |= 4;
            if (mousePos.y >= py + ph - hs && mousePos.y <= py + ph + hs &&
                mousePos.x >= px && mousePos.x <= px + pw) hoveredEdge |= 8;

            if (hoveredEdge != 0 && leftMouseDown && mlc.idePanelResizing == 0) {
                mlc.idePanelResizing = hoveredEdge;
                mlc.ideResizeStart = mousePos;
                mlc.ideResizeStartBounds = mlc.idePanelBounds;
            }

            if (mlc.idePanelResizing != 0) {
                if (leftMouseDown) {
                    glm::vec2 delta = mousePos - mlc.ideResizeStart;
                    float newX = mlc.ideResizeStartBounds.x;
                    float newY = mlc.ideResizeStartBounds.y;
                    float newW = mlc.ideResizeStartBounds.z;
                    float newH = mlc.ideResizeStartBounds.w;

                    if (mlc.idePanelResizing & 1) { newX += delta.x; newW -= delta.x; }
                    if (mlc.idePanelResizing & 2) { newW += delta.x; }
                    if (mlc.idePanelResizing & 4) { newY += delta.y; newH -= delta.y; }
                    if (mlc.idePanelResizing & 8) { newH += delta.y; }

                    // Enforce minimum size
                    if (newW < mlc.ideMinPanelWidth) {
                        if (mlc.idePanelResizing & 1)
                            newX = mlc.ideResizeStartBounds.x + mlc.ideResizeStartBounds.z - mlc.ideMinPanelWidth;
                        newW = mlc.ideMinPanelWidth;
                    }
                    if (newH < mlc.ideMinPanelHeight) {
                        if (mlc.idePanelResizing & 4)
                            newY = mlc.ideResizeStartBounds.y + mlc.ideResizeStartBounds.w - mlc.ideMinPanelHeight;
                        newH = mlc.ideMinPanelHeight;
                    }

                    // Clamp to window bounds
                    float logicalWidth = mlc.windowWidth / frameInput.contentScale;
                    float logicalHeight = mlc.windowHeight / frameInput.contentScale;
                    if (newX < 0) { newW += newX; newX = 0; }
                    if (newY < 0) { newH += newY; newY = 0; }
                    if (newX + newW > logicalWidth) newW = logicalWidth - newX;
                    if (newY + newH > logicalHeight) newH = logicalHeight - newY;
                    newW = std::max(newW, mlc.ideMinPanelWidth);
                    newH = std::max(newH, mlc.ideMinPanelHeight);

                    mlc.idePanelBounds = glm::vec4(newX, newY, newW, newH);
                } else {
                    mlc.idePanelResizing = 0;
                    mlc.ideBrowser->setSize((int)mlc.idePanelBounds.z, (int)mlc.idePanelBounds.w);
                }
            }
        }

        // Set input offset so mouse coordinates are properly translated
        mlc.ideBrowser->setInputOffset((int)mlc.idePanelBounds.x, (int)mlc.idePanelBounds.y);

        // Handle focus: request when clicking inside panel, release when clicking outside
        static bool wasMouseDown = false;
        bool mouseJustPressed = frameInput.mouseDown[0] && !wasMouseDown;
        wasMouseDown = frameInput.mouseDown[0];

        if (mouseJustPressed) {
            if (mlc.idePanelHovered && !overDragHandle) {
                mlc.ideBrowser->requestFocus();
            } else if (!mlc.idePanelHovered) {
                mlc.ideBrowser->releaseFocus();
            }
        }

        // Process browser input using raw GLFW state (Context's state may be blocked)
        vivid::cef::RawInputState rawInput;
        rawInput.mouseX = frameInput.mousePos.x;
        rawInput.mouseY = frameInput.mousePos.y;
        rawInput.mouseButtons[0] = frameInput.mouseDown[0];
        rawInput.mouseButtons[1] = frameInput.mouseDown[1];
        rawInput.mouseButtons[2] = frameInput.mouseDown[2];
        rawInput.scrollX = frameInput.scroll.x;
        rawInput.scrollY = frameInput.scroll.y;
        rawInput.shiftHeld = frameInput.keyShift;
        rawInput.ctrlHeld = frameInput.keyCtrl;
        rawInput.altHeld = glfwGetKey(mlc.window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                          glfwGetKey(mlc.window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
        rawInput.superHeld = glfwGetKey(mlc.window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
                            glfwGetKey(mlc.window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;

        // Populate keyboard state from GLFW
        for (int key = 0; key < 512 && key <= GLFW_KEY_LAST; ++key) {
            rawInput.keyDown[key] = glfwGetKey(mlc.window, key) == GLFW_PRESS;
        }

        // Pass character input from Context (collected by glfwSetCharCallback)
        rawInput.characterInput = mlc.ctx->characterInput();

        mlc.ideBrowser->processRawInput(rawInput);

        // Clamp panel position to current window bounds
        float logicalWidth = mlc.windowWidth / frameInput.contentScale;
        float logicalHeight = mlc.windowHeight / frameInput.contentScale;
        mlc.idePanelBounds.x = std::max(0.0f, std::min(mlc.idePanelBounds.x,
            logicalWidth - mlc.idePanelBounds.z));
        mlc.idePanelBounds.y = std::max(0.0f, std::min(mlc.idePanelBounds.y,
            logicalHeight - mlc.idePanelBounds.w));

        // Render browser texture as an overlay
        WGPUTextureView browserTexture = mlc.ideBrowser->outputView();
        if (browserTexture && mlc.display) {
            float scale = frameInput.contentScale;
            mlc.display->blitAtPosition(pass, browserTexture,
                (int)(mlc.idePanelBounds.z * scale), (int)(mlc.idePanelBounds.w * scale),
                (int)(mlc.idePanelBounds.x * scale), (int)(mlc.idePanelBounds.y * scale));
        }

        // Read PTY output and send to terminal
        if (mlc.idePty && mlc.idePtyStarted && mlc.ideBrowser && mlc.ideBrowser->isReady()) {
            std::string ptyOutput = mlc.idePty->read();
            if (!ptyOutput.empty()) {
                // Escape for JavaScript string literal
                std::string escaped;
                escaped.reserve(ptyOutput.size() * 2);
                for (char c : ptyOutput) {
                    switch (c) {
                        case '\\': escaped += "\\\\"; break;
                        case '\'': escaped += "\\'"; break;
                        case '\n': escaped += "\\n"; break;
                        case '\r': escaped += "\\r"; break;
                        case '\t': escaped += "\\t"; break;
                        default:
                            if (static_cast<unsigned char>(c) < 32) {
                                char buf[8];
                                snprintf(buf, sizeof(buf), "\\x%02x", static_cast<unsigned char>(c));
                                escaped += buf;
                            } else {
                                escaped += c;
                            }
                            break;
                    }
                }
                mlc.ideBrowser->executeJS("window.vividIDE && window.vividIDE.writePtyOutput('" + escaped + "')");
            }
        }
    }
#endif

    // Handle devtools updates (if module is loaded)
    if (devtools_dynamic::available()) {
        devtools_dynamic::tryUpdate();

        // Tilde/Backtick: Toggle devtools visibility (always active)
#ifdef __APPLE__
        bool hasCmdOrCtrl = frameInput.keySuper;
#else
        bool hasCmdOrCtrl = frameInput.keyCtrl;
#endif
        if (frameInput.keyPressed[GLFW_KEY_GRAVE_ACCENT] && !hasCmdOrCtrl) {
            mlc.visualizerVisible = !mlc.visualizerVisible;
        }

        // Built-in shortcuts (always available, even without devtools visible)
        if (hasCmdOrCtrl && frameInput.keyPressed[GLFW_KEY_F]) {
            // Cmd/Ctrl+F: Toggle fullscreen
            mlc.ctx->fullscreen(!mlc.ctx->fullscreen());
        }

        // Forward input to devtools only when visible
        if (mlc.visualizerVisible) {
            // Forward character input
            for (uint32_t codepoint : mlc.ctx->characterInput()) {
                devtools_dynamic::tryOnChar(codepoint);
            }

            // Forward key press events (for special keys: Enter, Backspace, arrows, etc.)
            int mods = 0;
            if (frameInput.keyCtrl) mods |= 0x2;
            if (frameInput.keyShift) mods |= 0x1;
            if (frameInput.keyAlt) mods |= 0x4;
            if (frameInput.keySuper) mods |= 0x8;

            // Forward special keys that were pressed this frame
            static const int specialKeys[] = {
                257,  // Enter
                259,  // Backspace
                258,  // Tab
                256,  // Escape
                265, 264, 263, 262,  // Arrow keys
                268, 269,  // Home, End
                266, 267,  // Page Up, Page Down
                261,  // Delete
                260,  // Insert
                290,  // F1 (help shortcut)
            };
            for (int key : specialKeys) {
                if (frameInput.keyPressed[key]) {
                    devtools_dynamic::tryOnKey(key, mods);
                }
            }

            // Forward Cmd/Ctrl+letter combinations
            if (hasCmdOrCtrl) {
                // Forward A-Z keys to devtools (skip F since it's handled above)
                for (int key = 65; key <= 90; key++) {  // A-Z (GLFW key codes)
                    if (key == GLFW_KEY_F) continue;  // Handled as built-in shortcut
                    if (frameInput.keyPressed[key]) {
                        devtools_dynamic::tryOnKey(key, mods);
                    }
                }
                // Forward 0-9 keys (for Cmd+1..4 panel shortcuts)
                for (int key = 48; key <= 57; key++) {  // 0-9 (GLFW key codes)
                    if (frameInput.keyPressed[key]) {
                        devtools_dynamic::tryOnKey(key, mods);
                    }
                }
                // Forward comma key (for Cmd+, preferences shortcut)
                if (frameInput.keyPressed[GLFW_KEY_COMMA]) {
                    devtools_dynamic::tryOnKey(GLFW_KEY_COMMA, mods);
                }
            }

            // Also forward plain Ctrl+letter for terminal (Ctrl+C, Ctrl+D, etc.)
            if (frameInput.keyCtrl && !frameInput.keySuper) {
                for (int key = 65; key <= 90; key++) {  // A-Z
                    if (frameInput.keyPressed[key]) {
                        devtools_dynamic::tryOnKey(key, mods);
                    }
                }
            }
        }
    }

    // Render error message if present (bitmap font fallback)
    if (mlc.ctx->hasError() && mlc.display->isValid()) {
        mlc.display->renderText(pass, mlc.ctx->errorMessage(), 20.0f, 20.0f, 2.0f);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    // If we rendered to composite texture, blit it to swapchain
    if (useComposite && mlc.display->isValid()) {
        // Create a new render pass targeting swapchain
        WGPURenderPassColorAttachment swapchainAttachment = {};
        swapchainAttachment.view = view;
        swapchainAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        swapchainAttachment.loadOp = WGPULoadOp_Clear;
        swapchainAttachment.storeOp = WGPUStoreOp_Store;
        swapchainAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};

        WGPURenderPassDescriptor swapchainPassDesc = {};
        swapchainPassDesc.colorAttachmentCount = 1;
        swapchainPassDesc.colorAttachments = &swapchainAttachment;

        WGPURenderPassEncoder swapchainPass = wgpuCommandEncoderBeginRenderPass(encoder, &swapchainPassDesc);
        mlc.display->blit(swapchainPass, mlc.compositeTextureView);
        wgpuRenderPassEncoderEnd(swapchainPass);
        wgpuRenderPassEncoderRelease(swapchainPass);
    }

    // MCP live capture request (captures from composite texture if available - includes UI)
    {
        std::lock_guard<std::mutex> lock(mlc.mcpCaptureMutex);
        if (!mlc.mcpCaptureRequestPath.empty()) {
            std::string requestedPath = mlc.mcpCaptureRequestPath;
            mlc.mcpCaptureRequestPath.clear();  // Clear the request

            // Use composite texture if available (includes UI), otherwise chain output
            WGPUTexture captureSource = useComposite ? mlc.compositeTexture :
                (mlc.ctx->hasChain() ? mlc.ctx->chain().outputTexture() : nullptr);
            std::string savedPath;
            if (captureSource) {
                if (VideoExporter::saveSnapshot(mlc.device, mlc.queue, captureSource, requestedPath)) {
                    savedPath = requestedPath;
                    printf("[MCP Capture] Saved %s snapshot: %s\n",
                           useComposite ? "UI-inclusive" : "chain-only", savedPath.c_str());
                }
            }

            if (!savedPath.empty()) {
                if (mlc.editorBridge) {
                    mlc.editorBridge->sendCaptureResult(true, savedPath);
                }
            } else {
                if (mlc.editorBridge) {
                    mlc.editorBridge->sendCaptureResult(false, requestedPath, "Failed to save snapshot");
                }
            }
        }
    }

    // Submit
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(mlc.queue, 1, &cmdBuffer);

    // Release command resources
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    // Present BEFORE releasing the texture view
    wgpuSurfacePresent(mlc.surface);

    // Present to secondary windows (span/multi-output)
    if (mlc.windowManager->windowCount() > 1) {
        mlc.windowManager->presentAll(&mlc.ctx->chain(), mlc.ctx->outputTexture());
    }

    // Poll device to process pending GPU work
    wgpuDevicePoll(mlc.device, false, nullptr);

    // Release texture view after present
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(surfaceTexture.texture);

    // End frame
    mlc.ctx->endFrame();

    // Process frame advance requests (MCP debugging tool)
    // This advances simulation by N frames without rendering to display
    if (mlc.editorBridge && mlc.hotReload->isLoaded()) {
        if (int advanceCount = mlc.editorBridge->consumePendingFrameAdvance()) {
            for (int i = 0; i < advanceCount; i++) {
                mlc.ctx->beginFrame();
                mlc.ctx->chain().process(*mlc.ctx);
                mlc.ctx->endFrame();
                mlc.snapshotFrameCounter++;
            }
            mlc.editorBridge->sendFrameAdvanceComplete(mlc.snapshotFrameCounter);
        }
    }

    // FPS counter and title update
    mlc.frameCount++;
    double currentTime = glfwGetTime();

    // Track frame time for performance stats
    double frameTimeMs = (currentTime - mlc.lastFrameTime) * 1000.0;
    mlc.lastFrameTime = currentTime;

    // Update frame time history
    mlc.perfStats.frameTimeHistory.push_back(static_cast<float>(frameTimeMs));
    if (mlc.perfStats.frameTimeHistory.size() > mlc.kHistorySize) {
        mlc.perfStats.frameTimeHistory.pop_front();
    }
    mlc.perfStats.frameTimeMs = static_cast<float>(frameTimeMs);

    // Update FPS every second
    if (currentTime - mlc.lastFpsTime >= 1.0) {
        float fps = mlc.frameCount / static_cast<float>(currentTime - mlc.lastFpsTime);
        mlc.perfStats.fps = fps;

        if (!mlc.headless) {
            // Update window title with FPS
            std::string title = mlc.projectName.empty() ? "Vivid" : mlc.projectName;
            title += " - " + std::to_string(static_cast<int>(fps)) + " fps";
            glfwSetWindowTitle(mlc.window, title.c_str());
        }

        mlc.lastFpsTime = currentTime;
        mlc.frameCount = 0;

        // Send performance stats to connected editors
        if (mlc.editorBridge->clientCount() > 0 && mlc.gatherParamValues) {
            // Estimate GPU memory (rough: count texture operators)
            size_t textureOpCount = 0;
            if (mlc.ctx->hasChain()) {
                for (const auto& name : mlc.ctx->chain().operatorNames()) {
                    Operator* op = mlc.ctx->chain().getByName(name);
                    if (op && op->outputKind() == OutputKind::Texture) {
                        textureOpCount++;
                    }
                }
            }
            mlc.perfStats.textureMemoryBytes = textureOpCount * mlc.ctx->width() * mlc.ctx->height() * 4;

            mlc.editorBridge->sendPerformanceStats(mlc.perfStats);
        }
    }

    return true;  // continue running
}

// -----------------------------------------------------------------------------
// Application Implementation
// -----------------------------------------------------------------------------

struct Application::Impl {
    // Owned objects
    std::unique_ptr<WindowManager> windowManager;
    std::unique_ptr<Context> ctx;
    std::unique_ptr<Display> display;
    std::unique_ptr<HotReload> hotReload;
    // Note: ChainVisualizer is now loaded dynamically from vivid-devtools module
    std::unique_ptr<RuntimeAPI> editorBridge;

    // WebGPU objects
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUSurface surface = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_BGRA8Unorm;
    WGPUSurfaceConfigurationExtras configExtras = {};
    WGPUSurfaceConfiguration config = {};

    // Window
    GLFWwindow* window = nullptr;
    int width = 0;
    int height = 0;

    // Main loop context
    MainLoopContext mlc;

    // Config copy
    AppConfig appConfig;
};

Application::~Application() {
    shutdown();
}

int Application::init(const AppConfig& config) {
    if (m_initialized) {
        return 0;  // Already initialized
    }

    m_impl = new Impl();
    m_impl->appConfig = config;

    // Extract project name for window title
    std::string initialWindowTitle = "Vivid";
    if (!config.projectPath.empty()) {
        fs::path pp(config.projectPath);
        if (fs::is_directory(pp)) {
            initialWindowTitle = pp.filename().string();
        } else if (fs::is_regular_file(pp)) {
            initialWindowTitle = pp.parent_path().filename().string();
        }
    }

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return 1;
    }

    // No OpenGL context - we're using WebGPU
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // Headless mode: create invisible window
    if (config.headless) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    // Get window settings from chain config (if available)
    int windowWidth = config.windowWidth;
    int windowHeight = config.windowHeight;
    bool windowResizable = true;
    bool startFullscreen = config.startFullscreen;
    DisplayMode displayMode = DisplayMode::Fit;  // Default display mode

    // Create HotReload early so we can use it for config extraction
    // The same instance will be used for the main loop hot-reload
    m_impl->hotReload = std::make_unique<HotReload>();

    if (!config.projectPath.empty()) {
        // Compile chain early to get its config before window creation
        fs::path chainPath;

        if (fs::is_directory(config.projectPath)) {
            chainPath = config.projectPath / "chain.cpp";
        } else if (fs::is_regular_file(config.projectPath)) {
            chainPath = config.projectPath;
        }

        if (fs::exists(chainPath)) {
            // Compile and load chain to extract config
            // The library stays loaded - no unload/reload cycle
            m_impl->hotReload->setSourceFile(chainPath);
            if (m_impl->hotReload->tryCompile() && m_impl->hotReload->loadCompiled()) {
                m_impl->mlc.chainAlreadyLoaded = true;
                if (m_impl->hotReload->hasConfig()) {
                    ChainConfig chainConfig = m_impl->hotReload->getConfig();
                    windowWidth = chainConfig.windowWidth;
                    windowHeight = chainConfig.windowHeight;
                    windowResizable = chainConfig.resizable;
                    startFullscreen = chainConfig.fullscreen;
                    displayMode = chainConfig.displayMode;
                    std::cout << "Using chain config: " << windowWidth << "x" << windowHeight << std::endl;
                }
            }
        }
    }

    // Set resizable hint
    glfwWindowHint(GLFW_RESIZABLE, windowResizable ? GLFW_TRUE : GLFW_FALSE);

    // Create window
    m_impl->window = glfwCreateWindow(windowWidth, windowHeight,
                                       initialWindowTitle.c_str(), nullptr, nullptr);
    if (!m_impl->window) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return 1;
    }

    // Create WebGPU instance
    WGPUInstanceDescriptor instanceDesc = {};
    m_impl->instance = wgpuCreateInstance(&instanceDesc);
    if (!m_impl->instance) {
        std::cerr << "Failed to create WebGPU instance" << std::endl;
        glfwDestroyWindow(m_impl->window);
        glfwTerminate();
        return 1;
    }

    // Create surface from GLFW window
    m_impl->surface = glfwCreateWindowWGPUSurface(m_impl->instance, m_impl->window);
    if (!m_impl->surface) {
        std::cerr << "Failed to create surface" << std::endl;
        wgpuInstanceRelease(m_impl->instance);
        glfwDestroyWindow(m_impl->window);
        glfwTerminate();
        return 1;
    }

    // Request adapter
    std::cout << "Requesting adapter..." << std::endl;
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = m_impl->surface;
    adapterOpts.powerPreference = WGPUPowerPreference_HighPerformance;

    AdapterUserData adapterData;
    WGPURequestAdapterCallbackInfo adapterCallback = {};
    adapterCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    adapterCallback.callback = onAdapterRequestEnded;
    adapterCallback.userdata1 = &adapterData;

    wgpuInstanceRequestAdapter(m_impl->instance, &adapterOpts, adapterCallback);

    while (!adapterData.done) {
        // Spin
    }

    if (!adapterData.adapter) {
        std::cerr << "Failed to get adapter" << std::endl;
        wgpuSurfaceRelease(m_impl->surface);
        wgpuInstanceRelease(m_impl->instance);
        glfwDestroyWindow(m_impl->window);
        glfwTerminate();
        return 1;
    }

    m_impl->adapter = adapterData.adapter;

    // Print adapter info
    WGPUAdapterInfo info = {};
    wgpuAdapterGetInfo(m_impl->adapter, &info);
    std::cout << "Adapter: ";
    if (info.device.data && info.device.length > 0) {
        size_t len = info.device.length == WGPU_STRLEN ? strlen(info.device.data) : info.device.length;
        std::cout << std::string(info.device.data, len);
    } else {
        std::cout << "unknown";
    }
    std::cout << std::endl;

    std::cout << "Backend: ";
    switch (info.backendType) {
        case WGPUBackendType_Metal: std::cout << "Metal"; break;
        case WGPUBackendType_Vulkan: std::cout << "Vulkan"; break;
        case WGPUBackendType_D3D12: std::cout << "D3D12"; break;
        case WGPUBackendType_D3D11: std::cout << "D3D11"; break;
        default: std::cout << "Other"; break;
    }
    std::cout << std::endl;

    // Request device
    std::cout << "Requesting device..." << std::endl;
    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.label = toStringView("Vivid Device");

    // Request BC texture compression for HAP video codec support
    WGPUFeatureName requiredFeatures[] = {
        WGPUFeatureName_TextureCompressionBC
    };
    deviceDesc.requiredFeatures = requiredFeatures;
    deviceDesc.requiredFeatureCount = 1;

    // Set error callbacks
    deviceDesc.deviceLostCallbackInfo.callback = onDeviceLost;
    deviceDesc.uncapturedErrorCallbackInfo.callback = onDeviceError;

    DeviceUserData deviceData;
    WGPURequestDeviceCallbackInfo deviceCallback = {};
    deviceCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    deviceCallback.callback = onDeviceRequestEnded;
    deviceCallback.userdata1 = &deviceData;

    wgpuAdapterRequestDevice(m_impl->adapter, &deviceDesc, deviceCallback);

    while (!deviceData.done) {
        // Spin
    }

    if (!deviceData.device) {
        std::cerr << "Failed to get device" << std::endl;
        wgpuAdapterRelease(m_impl->adapter);
        wgpuSurfaceRelease(m_impl->surface);
        wgpuInstanceRelease(m_impl->instance);
        glfwDestroyWindow(m_impl->window);
        glfwTerminate();
        return 1;
    }

    m_impl->device = deviceData.device;

    // Get queue
    m_impl->queue = wgpuDeviceGetQueue(m_impl->device);

    // Configure surface
    glfwGetFramebufferSize(m_impl->window, &m_impl->width, &m_impl->height);

    // Query surface capabilities to get preferred format
    WGPUSurfaceCapabilities capabilities = {};
    wgpuSurfaceGetCapabilities(m_impl->surface, m_impl->adapter, &capabilities);

    m_impl->surfaceFormat = WGPUTextureFormat_BGRA8Unorm;  // Default
    if (capabilities.formatCount > 0) {
        m_impl->surfaceFormat = capabilities.formats[0];
        std::cout << "Using surface format: " << m_impl->surfaceFormat << std::endl;
    }

    WGPUPresentMode presentMode = WGPUPresentMode_Fifo;  // Default (vsync)
    if (config.headless) {
        presentMode = WGPUPresentMode_Immediate;
    } else if (capabilities.presentModeCount > 0) {
        presentMode = capabilities.presentModes[0];
        std::cout << "Using present mode: " << presentMode << std::endl;
    }

    wgpuSurfaceCapabilitiesFreeMembers(capabilities);

    // Configure surface with frame latency limit
    m_impl->configExtras = {};
    m_impl->configExtras.chain.sType = static_cast<WGPUSType>(WGPUSType_SurfaceConfigurationExtras);
    m_impl->configExtras.desiredMaximumFrameLatency = 2;

    m_impl->config = {};
    m_impl->config.nextInChain = &m_impl->configExtras.chain;
    m_impl->config.device = m_impl->device;
    m_impl->config.format = m_impl->surfaceFormat;
    m_impl->config.width = static_cast<uint32_t>(m_impl->width);
    m_impl->config.height = static_cast<uint32_t>(m_impl->height);
    m_impl->config.presentMode = presentMode;
    m_impl->config.alphaMode = WGPUCompositeAlphaMode_Auto;
    m_impl->config.usage = WGPUTextureUsage_RenderAttachment;
    wgpuSurfaceConfigure(m_impl->surface, &m_impl->config);

    std::cout << "WebGPU initialized successfully!" << std::endl;
    std::cout << "Window size: " << m_impl->width << "x" << m_impl->height << std::endl;

    // Create WindowManager and adopt primary window
    m_impl->windowManager = std::make_unique<WindowManager>(
        m_impl->instance, m_impl->adapter, m_impl->device, m_impl->queue);
    m_impl->windowManager->adoptPrimaryWindow(m_impl->window, m_impl->surface,
                                               m_impl->width, m_impl->height);

    // Create context
    m_impl->ctx = std::make_unique<Context>(m_impl->window, m_impl->device, m_impl->queue);
    m_impl->ctx->setWindowManager(m_impl->windowManager.get());

    // Query GPU limits and pass to context
    WGPULimits limits = {};
    wgpuDeviceGetLimits(m_impl->device, &limits);
    m_impl->ctx->setMaxTextureDimension2D(limits.maxTextureDimension2D);
    std::cout << "Max texture size: " << limits.maxTextureDimension2D << "x" << limits.maxTextureDimension2D << std::endl;

    // Set render resolution from command-line (or default to window size)
    if (config.renderWidth > 0 && config.renderHeight > 0) {
        m_impl->ctx->setRenderResolution(config.renderWidth, config.renderHeight);
    } else {
        m_impl->ctx->setRenderResolution(windowWidth, windowHeight);
    }

    // Start in fullscreen if requested
    if (startFullscreen) {
        m_impl->ctx->fullscreen(true);
        // Consume the change flag since we're handling it here via glfwSetWindowMonitor
        m_impl->ctx->consumeFullscreenChange();
    }

    // Set initial display mode
    m_impl->ctx->displayMode(displayMode);

    // Set up scroll callback
    glfwSetWindowUserPointer(m_impl->window, m_impl->ctx.get());
    glfwSetScrollCallback(m_impl->window, [](GLFWwindow* w, double xoffset, double yoffset) {
        Context* c = static_cast<Context*>(glfwGetWindowUserPointer(w));
        if (c) c->addScroll(static_cast<float>(xoffset), static_cast<float>(yoffset));
    });

    // Set up character callback (for text input in editors/terminals)
    glfwSetCharCallback(m_impl->window, [](GLFWwindow* w, unsigned int codepoint) {
        Context* c = static_cast<Context*>(glfwGetWindowUserPointer(w));
        if (c) c->addCharacter(codepoint);
    });

    // Create display
    m_impl->display = std::make_unique<Display>(m_impl->device, m_impl->queue, m_impl->surfaceFormat);
    if (!m_impl->display->isValid()) {
        std::cerr << "Warning: Display initialization failed (shaders may be missing)" << std::endl;
    }
    m_impl->display->setDisplayMode(displayMode);

    // Initialize devtools (dynamically loaded from vivid-devtools module)
    // If the module is not present (e.g., production bundles), this is a no-op
    devtools_dynamic::tryInit(m_impl->ctx.get(), m_impl->surfaceFormat);
    devtools_dynamic::trySetWindow(m_impl->window);  // For clipboard support
    devtools_dynamic::setupShortcutCallbacks(m_impl->ctx.get());  // Set up Cmd/Ctrl+F fullscreen etc.

    // Check MCP configuration and set warning on devtools status bar
    if (devtools_dynamic::available()) {
        std::string warning = checkMcpConfiguration();
        if (!warning.empty()) {
            devtools_dynamic::trySetMcpWarning(warning.c_str());
        }
    }

    // HotReload was created earlier (before window creation) for config extraction
    // No need to create it again here

    // Create editor bridge
    m_impl->editorBridge = std::make_unique<RuntimeAPI>();
    m_impl->editorBridge->start();

    // Set asset directory for serving IDE panel files via HTTP
    auto exeDir = vivid::AssetLoader::instance().executableDir();
    std::filesystem::path idePanelDir = exeDir / ".." / "assets" / "ide-panel";
    idePanelDir = std::filesystem::weakly_canonical(idePanelDir);
    m_impl->editorBridge->setAssetDirectory(idePanelDir.string());

    m_impl->editorBridge->onReloadCommand([this](const std::string&) {
        std::cout << "[RuntimeAPI] Force reload triggered by editor\n";
        m_impl->hotReload->forceReload();
    });
    m_impl->editorBridge->onParamChange([this](const std::string& opName, const std::string& paramName, const float value[4]) {
        if (!m_impl->ctx->hasChain()) return;
        Operator* op = m_impl->ctx->chain().getByName(opName);
        if (op) {
            // Get current value before applying (for pending change tracking)
            float oldValue[4] = {0};
            op->getParam(paramName, oldValue);

            // Apply the change immediately (preview)
            op->setParam(paramName, value);

            // Store as pending change for Claude to review
            PendingChange change;
            change.operatorName = opName;
            change.paramName = paramName;
            for (int i = 0; i < 4; ++i) {
                change.oldValue[i] = oldValue[i];
                change.newValue[i] = value[i];
            }
            change.sourceLine = op->sourceLine;

            // Get param type from operator's param declarations
            for (const auto& decl : op->params()) {
                if (decl.name == paramName) {
                    switch (decl.type) {
                        case ParamType::Float:      change.paramType = "Float"; break;
                        case ParamType::Int:        change.paramType = "Int"; break;
                        case ParamType::Bool:       change.paramType = "Bool"; break;
                        case ParamType::Vec2:       change.paramType = "Vec2"; break;
                        case ParamType::Vec3:       change.paramType = "Vec3"; break;
                        case ParamType::Vec4:       change.paramType = "Vec4"; break;
                        case ParamType::Color:      change.paramType = "Color"; break;
                        case ParamType::String:     change.paramType = "String"; break;
                        case ParamType::FilePath:   change.paramType = "FilePath"; break;
                        case ParamType::Enum:       change.paramType = "Enum"; break;
                        case ParamType::ADSR:       change.paramType = "ADSR"; break;
                        case ParamType::DeviceList: change.paramType = "DeviceList"; break;
                    }
                    break;
                }
            }

            m_impl->editorBridge->addPendingChange(change);
        }
    });
    m_impl->editorBridge->onDiscardChanges([this](const std::vector<PendingChange>& changes) {
        if (!m_impl->ctx->hasChain()) return;
        // Revert each change to its original value
        for (const auto& change : changes) {
            Operator* op = m_impl->ctx->chain().getByName(change.operatorName);
            if (op) {
                op->setParam(change.paramName, change.oldValue);
            }
        }
    });

    // Direct parameter control (MCP debugging tools) - apply immediately, no pending queue
    m_impl->editorBridge->onSetParamImmediate([this](const std::string& opName, const std::string& paramName, const float value[4]) -> bool {
        if (!m_impl->ctx->hasChain()) return false;
        Operator* op = m_impl->ctx->chain().getByName(opName);
        if (op) {
            bool success = op->setParam(paramName, value);
            if (success) {
                op->markDirty();
            }
            return success;
        }
        return false;
    });

    // MCP get_chain_structure tool: return chain operators and connections
    m_impl->editorBridge->onRequestChainStructure([this]() -> std::vector<RuntimeAPI::ChainOperatorInfo> {
        std::vector<RuntimeAPI::ChainOperatorInfo> result;
        if (!m_impl->ctx->hasChain()) return result;

        for (const auto& opInfo : m_impl->ctx->registeredOperators()) {
            RuntimeAPI::ChainOperatorInfo info;
            info.name = opInfo.name;
            if (opInfo.op) {
                info.displayName = opInfo.op->name();
                info.outputType = outputKindName(opInfo.op->outputKind());
                // Build inputs list from operator
                for (size_t i = 0; i < opInfo.op->inputNameCount(); ++i) {
                    std::string inputName = opInfo.op->getInputName(static_cast<int>(i));
                    if (!inputName.empty()) {
                        info.inputs.push_back(inputName);
                    }
                }
            }
            result.push_back(info);
        }
        return result;
    });

    // MCP inspect_chain tool: return full introspection data from all operators
    m_impl->editorBridge->onInspectChain([this]() -> std::string {
        if (!m_impl->ctx || !m_impl->ctx->hasChain()) return "{}";
        auto inspection = m_impl->ctx->chain().inspectAll(*m_impl->ctx);
        return inspection.toJSON();
    });

    // MCP get_frame_info tool: return current frame/time/fps
    m_impl->editorBridge->onRequestFrameInfo([this]() -> RuntimeAPI::FrameInfo {
        RuntimeAPI::FrameInfo info;
        if (m_impl->ctx) {
            info.frame = m_impl->ctx->frame();
            info.time = m_impl->ctx->time();
            info.fps = m_impl->mlc.perfStats.fps;
        }
        return info;
    });

    // MCP reset_time tool: reset animation to frame 0
    m_impl->editorBridge->onResetTime([this]() {
        if (m_impl->ctx) {
            m_impl->ctx->resetTime();
        }
    });

    // IDE panel file operations
    m_impl->editorBridge->onFileRead([this](const std::string& path) -> std::string {
        if (!m_impl->ctx || path.empty()) return "";

        // Get the project directory from chainPath
        fs::path chainPath = m_impl->ctx->chainPath();
        if (chainPath.empty()) return "";

        fs::path projectDir = chainPath.parent_path();
        fs::path filePath;

        // Allow reading chain.cpp specifically
        if (path == "chain.cpp") {
            filePath = chainPath;
        } else {
            // Restrict to project directory for security
            filePath = projectDir / path;
            // Ensure the resolved path is still within project directory
            filePath = fs::weakly_canonical(filePath);
            if (filePath.string().find(projectDir.string()) != 0) {
                std::cerr << "[RuntimeAPI] File read denied (outside project): " << path << "\n";
                return "";
            }
        }

        if (!fs::exists(filePath)) {
            std::cerr << "[RuntimeAPI] File not found: " << filePath << "\n";
            return "";
        }

        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "[RuntimeAPI] Failed to open: " << filePath << "\n";
            return "";
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    });

    m_impl->editorBridge->onFileWrite([this](const std::string& path, const std::string& content) -> bool {
        if (!m_impl->ctx || path.empty()) return false;

        // Get the project directory from chainPath
        fs::path chainPath = m_impl->ctx->chainPath();
        if (chainPath.empty()) return false;

        fs::path projectDir = chainPath.parent_path();
        fs::path filePath;

        // Allow writing chain.cpp specifically
        if (path == "chain.cpp") {
            filePath = chainPath;
        } else {
            // Restrict to project directory for security
            filePath = projectDir / path;
            // Ensure the resolved path is still within project directory
            filePath = fs::weakly_canonical(filePath);
            if (filePath.string().find(projectDir.string()) != 0) {
                std::cerr << "[RuntimeAPI] File write denied (outside project): " << path << "\n";
                return false;
            }
        }

        std::ofstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "[RuntimeAPI] Failed to open for writing: " << filePath << "\n";
            return false;
        }

        file << content;
        file.close();
        std::cout << "[RuntimeAPI] File written: " << filePath << "\n";
        return true;
    });

    // PTY callbacks are now handled via WKScriptMessageHandler in the WebView
    // (registered in initChainVisualizer before setUrl)

    // Connect chain visualizer inspector panel to pending changes system (if available)
    // The callback needs access to m_impl, so we store a static pointer (single instance)
    static Application::Impl* s_paramCallbackImpl = nullptr;
    s_paramCallbackImpl = m_impl;

    if (devtools_dynamic::available()) {
        devtools_dynamic::trySetParamCallback([](const char* opName, const char* paramName,
                                                    const float* oldValue, const float* newValue, int sourceLine) {
            if (!s_paramCallbackImpl || !s_paramCallbackImpl->editorBridge) return;
            auto* impl = s_paramCallbackImpl;

            // Queue as pending change for Claude to review
            PendingChange change;
            change.operatorName = opName;
            change.paramName = paramName;
            change.sourceLine = sourceLine;
            for (int i = 0; i < 4; ++i) {
                change.oldValue[i] = oldValue[i];
                change.newValue[i] = newValue[i];
            }

            // Get param type from operator
            if (impl->ctx && impl->ctx->hasChain()) {
                Operator* op = impl->ctx->chain().getByName(opName);
                if (op) {
                    for (const auto& decl : op->params()) {
                        if (decl.name == paramName) {
                            switch (decl.type) {
                                case ParamType::Float:      change.paramType = "Float"; break;
                                case ParamType::Int:        change.paramType = "Int"; break;
                                case ParamType::Bool:       change.paramType = "Bool"; break;
                                case ParamType::Vec2:       change.paramType = "Vec2"; break;
                                case ParamType::Vec3:       change.paramType = "Vec3"; break;
                                case ParamType::Vec4:       change.paramType = "Vec4"; break;
                                case ParamType::Color:      change.paramType = "Color"; break;
                                case ParamType::String:     change.paramType = "String"; break;
                                case ParamType::FilePath:   change.paramType = "FilePath"; break;
                                case ParamType::Enum:       change.paramType = "Enum"; break;
                                case ParamType::ADSR:       change.paramType = "ADSR"; break;
                                case ParamType::DeviceList: change.paramType = "DeviceList"; break;
                            }
                            break;
                        }
                    }
                }
            }

            impl->editorBridge->addPendingChange(change);
        });
    }

    m_impl->editorBridge->onSoloNode([this](const std::string& opName) {
        if (!m_impl->ctx->hasChain()) return;
        Operator* op = m_impl->ctx->chain().getByName(opName);
        if (op) {
            devtools_dynamic::tryEnterSolo(op, opName.c_str());
            m_impl->editorBridge->sendSoloState(true, opName);
        }
    });
    m_impl->editorBridge->onSelectNode([this](const std::string& opName) {
        devtools_dynamic::trySelectNode(opName.c_str());
    });
    m_impl->editorBridge->onSoloExit([this]() {
        devtools_dynamic::tryExitSolo();
        m_impl->editorBridge->sendSoloState(false, "");
    });
    m_impl->editorBridge->onFocusedNode([this](const std::string& opName) {
        if (opName.empty()) {
            devtools_dynamic::tryClearFocusedNode();
        } else {
            devtools_dynamic::trySetFocusedNode(opName.c_str());
        }
    });
    m_impl->editorBridge->onWindowControl([this](const std::string& setting, int value) {
        if (setting == "fullscreen") {
            m_impl->ctx->fullscreen(value != 0);
        } else if (setting == "borderless") {
            m_impl->ctx->borderless(value != 0);
        } else if (setting == "alwaysOnTop") {
            m_impl->ctx->alwaysOnTop(value != 0);
        } else if (setting == "cursorVisible") {
            m_impl->ctx->cursorVisible(value != 0);
        } else if (setting == "monitor") {
            m_impl->ctx->moveToMonitor(value);
        }
    });

    // Helper lambdas for editor bridge callbacks
    auto updateSourceLines = [this](const std::string& chainFilePath) {
        if (!m_impl->ctx->hasChain() || chainFilePath.empty()) return;

        fs::path chainFile(chainFilePath);
        if (!fs::exists(chainFile)) return;

        std::ifstream file(chainFile);
        if (!file) return;

        std::regex addPattern("chain\\.add<\\w+>\\s*\\(\\s*\"(\\w+)\"");
        std::string lineStr;
        int lineNum = 0;
        Chain& chain = m_impl->ctx->chain();

        while (std::getline(file, lineStr)) {
            lineNum++;
            std::smatch match;
            if (std::regex_search(lineStr, match, addPattern)) {
                std::string opName = match[1].str();
                Operator* op = chain.getByName(opName);
                if (op) {
                    op->sourceLine = lineNum;
                }
            }
        }
    };

    auto gatherOperatorInfo = [this]() -> std::vector<RuntimeOperatorInfo> {
        std::vector<RuntimeOperatorInfo> result;
        if (!m_impl->ctx->hasChain()) return result;

        Chain& chain = m_impl->ctx->chain();
        for (const auto& name : chain.operatorNames()) {
            Operator* op = chain.getByName(name);
            if (!op) continue;

            RuntimeOperatorInfo info;
            info.chainName = name;
            info.displayName = op->name();
            info.outputType = outputKindName(op->outputKind());
            info.sourceLine = op->sourceLine;

            // Check for texture operator errors (e.g., texture size exceeded)
            if (auto* texOp = dynamic_cast<vivid::effects::TextureOperator*>(op)) {
                if (texOp->hasError()) {
                    info.error = texOp->errorMessage();
                }
            }

            for (size_t i = 0; i < op->inputCount(); ++i) {
                Operator* input = op->getInput(static_cast<int>(i));
                if (input) {
                    info.inputNames.push_back(chain.getName(input));
                }
            }
            result.push_back(info);
        }
        return result;
    };

    auto gatherParamValues = [this]() -> std::vector<RuntimeParamInfo> {
        std::vector<RuntimeParamInfo> result;
        if (!m_impl->ctx->hasChain()) return result;

        Chain& chain = m_impl->ctx->chain();
        for (const auto& name : chain.operatorNames()) {
            Operator* op = chain.getByName(name);
            if (!op) continue;

            for (const auto& decl : op->params()) {
                RuntimeParamInfo info;
                info.operatorName = name;
                info.paramName = decl.name;
                info.minVal = decl.minVal;
                info.maxVal = decl.maxVal;

                switch (decl.type) {
                    case ParamType::Float:  info.paramType = "Float"; break;
                    case ParamType::Int:    info.paramType = "Int"; break;
                    case ParamType::Bool:   info.paramType = "Bool"; break;
                    case ParamType::Vec2:   info.paramType = "Vec2"; break;
                    case ParamType::Vec3:   info.paramType = "Vec3"; break;
                    case ParamType::Vec4:   info.paramType = "Vec4"; break;
                    case ParamType::Color:  info.paramType = "Color"; break;
                    case ParamType::String: info.paramType = "String"; break;
                    default:                info.paramType = "Unknown"; break;
                }

                op->getParam(decl.name, info.value);
                result.push_back(info);
            }
        }
        return result;
    };

    auto gatherWindowState = [this]() -> RuntimeWindowState {
        RuntimeWindowState state;
        state.fullscreen = m_impl->ctx->fullscreen();
        state.borderless = m_impl->ctx->borderless();
        state.alwaysOnTop = m_impl->ctx->alwaysOnTop();
        state.cursorVisible = m_impl->ctx->cursorVisible();
        state.currentMonitor = m_impl->ctx->currentMonitor();

        int monitorCount = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
        for (int i = 0; i < monitorCount; ++i) {
            RuntimeMonitorInfo mInfo;
            mInfo.index = i;
            const char* name = glfwGetMonitorName(monitors[i]);
            mInfo.name = name ? name : ("Monitor " + std::to_string(i + 1));
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            if (mode) {
                mInfo.width = mode->width;
                mInfo.height = mode->height;
            }
            state.monitors.push_back(mInfo);
        }
        return state;
    };

    m_impl->editorBridge->onRequestOperators([this, gatherOperatorInfo, gatherParamValues, gatherWindowState]() {
        m_impl->editorBridge->sendOperatorList(gatherOperatorInfo());
        m_impl->editorBridge->sendParamValues(gatherParamValues());
        m_impl->editorBridge->sendWindowState(gatherWindowState());
    });

    // MCP compile status callback - sends current compile status
    // Check cached status first (for errors like "chain file not found"), then hot_reload
    m_impl->editorBridge->onRequestCompileStatus([this]() {
        bool cachedSuccess;
        std::string cachedMessage;
        m_impl->editorBridge->getCachedCompileStatus(cachedSuccess, cachedMessage);

        // If cached status has an error, send that
        if (!cachedSuccess) {
            m_impl->editorBridge->sendCompileStatus(false, cachedMessage);
        }
        // Otherwise check hot reload for compile errors
        else if (m_impl->hotReload->hasError()) {
            m_impl->editorBridge->sendCompileStatus(false, m_impl->hotReload->getError());
        } else {
            m_impl->editorBridge->sendCompileStatus(true, "");
        }
    });

    // MCP capture frame callback - sets the request path, main loop handles actual capture
    m_impl->editorBridge->onCaptureFrame([this](const std::string& outputPath) {
        std::lock_guard<std::mutex> lock(m_impl->mlc.mcpCaptureMutex);
        m_impl->mlc.mcpCaptureRequestPath = outputPath;
    });

    // Extract project name and set up chain path
    std::string projectName;
    fs::path projectDir;
    if (!config.projectPath.empty()) {
        fs::path chainPath;
        if (fs::is_directory(config.projectPath)) {
            chainPath = config.projectPath / "chain.cpp";
            projectName = config.projectPath.filename().string();
            projectDir = config.projectPath;
        } else if (fs::is_regular_file(config.projectPath)) {
            chainPath = config.projectPath;
            projectName = config.projectPath.parent_path().filename().string();
            projectDir = config.projectPath.parent_path();
        }

        if (!projectDir.empty()) {
            AssetLoader::instance().setProjectDir(projectDir);
        }

        if (fs::exists(chainPath)) {
            m_impl->ctx->setChainPath(chainPath.string());
            if (m_impl->mlc.chainAlreadyLoaded) {
                // Chain was already loaded during config extraction
                // Just set path for watching, don't trigger reload
                m_impl->hotReload->setSourcePath(chainPath);
            } else {
                // Chain not yet loaded, trigger compile and load
                std::cout << "Loading chain: " << chainPath << std::endl;
                m_impl->hotReload->setSourceFile(chainPath);
            }
        } else {
            std::string errorMsg = "Chain file not found: " + chainPath.string();
            std::cerr << "\n*** ERROR: " << errorMsg << " ***\n" << std::endl;
            m_impl->ctx->setError(errorMsg);
            // Notify MCP clients of the failure
            if (m_impl->editorBridge) {
                m_impl->editorBridge->sendCompileStatus(false, errorMsg);
            }
        }
    } else {
        std::string errorMsg = "No chain specified. Usage: vivid <path/to/chain.cpp>";
        std::cerr << "\n*** ERROR: " << errorMsg << " ***\n" << std::endl;
        m_impl->ctx->setError(errorMsg);
        // Notify MCP clients of the failure
        if (m_impl->editorBridge) {
            m_impl->editorBridge->sendCompileStatus(false, errorMsg);
        }
    }

    // Initialize MainLoopContext
    MainLoopContext& mlc = m_impl->mlc;

    // WebGPU infrastructure
    mlc.instance = m_impl->instance;
    mlc.adapter = m_impl->adapter;
    mlc.surface = m_impl->surface;
    mlc.device = m_impl->device;
    mlc.queue = m_impl->queue;
    mlc.surfaceFormat = m_impl->surfaceFormat;
    mlc.configExtras = m_impl->configExtras;
    mlc.config = m_impl->config;
    mlc.config.nextInChain = &mlc.configExtras.chain;

    // Window state
    mlc.window = m_impl->window;
    mlc.width = m_impl->width;
    mlc.height = m_impl->height;
    mlc.isFullscreen = false;
    glfwGetWindowPos(m_impl->window, &mlc.windowedX, &mlc.windowedY);
    glfwGetWindowSize(m_impl->window, &mlc.windowedWidth, &mlc.windowedHeight);
    mlc.windowManager = m_impl->windowManager.get();

    // Timing
    mlc.lastFpsTime = glfwGetTime();
    mlc.frameCount = 0;
    mlc.lastFrameTime = glfwGetTime();

    // Core runtime objects
    mlc.ctx = m_impl->ctx.get();
    mlc.display = m_impl->display.get();
    mlc.hotReload = m_impl->hotReload.get();
    mlc.editorBridge = m_impl->editorBridge.get();

    // Check if visualizer module is available (dynamically loaded)
    // In production bundles without vivid-devtools.dylib, this will be false
    mlc.visualizerAvailable = devtools_dynamic::available();

    // Show visualizer if requested via --show-ui flag (and module is available)
    mlc.visualizerVisible = mlc.visualizerAvailable && config.showUI;

    // CLI args
    mlc.snapshotPath = config.snapshotPath;
    // Set up snapshot frames (default to frame 5 for backwards compatibility)
    if (!config.snapshotPath.empty()) {
        if (config.snapshotFrames.empty()) {
            mlc.snapshotFrames.insert(5);  // Default: capture frame 5
        } else {
            mlc.snapshotFrames = config.snapshotFrames;
        }
        mlc.snapshotFramesPending = mlc.snapshotFrames;
        mlc.snapshotStartTime = glfwGetTime();  // Start timeout clock
    }
    mlc.headless = config.headless;
    mlc.renderWidth = config.renderWidth;
    mlc.renderHeight = config.renderHeight;
    mlc.recordPath = config.recordPath;
    mlc.recordFps = config.recordFps;
    mlc.recordDuration = config.recordDuration;
    mlc.recordAudio = config.recordAudio;
    mlc.recordCodec = config.recordCodec;
    mlc.maxFrames = config.maxFrames;
    mlc.windowWidth = m_impl->width;   // Use actual window size (may differ from CLI if chain config used)
    mlc.windowHeight = m_impl->height;
    mlc.showUI = config.showUI;
// IDE panel using CEF Browser
#ifdef VIVID_HAS_CEF
    // IDE panel visibility follows visualizer visibility
    mlc.idePanelVisible = mlc.visualizerVisible;

    // Initialize IDE Browser panel (always, so it's ready when Tab is pressed)
    if (mlc.visualizerAvailable) {
        // Initialize CEF if not already done
        if (!vivid::cef::isCefInitialized()) {
            static char* argv[] = { (char*)"vivid", nullptr };
            if (!vivid::cef::initializeCef(1, argv)) {
                std::cerr << "[IDE] Failed to initialize CEF\n";
            }
        }

        if (vivid::cef::isCefInitialized()) {
            mlc.ideBrowser = std::make_unique<vivid::cef::Browser>();
            int panelWidth = 500;
            int panelHeight = 400;
            mlc.ideBrowser->setSize(panelWidth, panelHeight);
            mlc.ideBrowser->setTransparent(false);
            mlc.ideBrowser->setInputEnabled(true);

            // Console callback for JS debugging
            mlc.ideBrowser->onConsole([](vivid::cef::ConsoleMessage::Level level,
                                         const std::string& message,
                                         const std::string& source,
                                         int line) {
                const char* levelStr = "INFO";
                if (level == vivid::cef::ConsoleMessage::Level::Warning) levelStr = "WARN";
                else if (level == vivid::cef::ConsoleMessage::Level::Error) levelStr = "ERROR";
                std::cerr << "[IDE JS:" << levelStr << "] " << message << " (" << source << ":" << line << ")\n";
            });

            // Initialize PTY for terminal BEFORE registering callbacks
            mlc.idePty = std::make_unique<PTY>();
            fs::path chainPath = m_impl->ctx->chainPath();
            std::string ptyWorkDir = chainPath.empty() ? "" : chainPath.parent_path().string();
            if (mlc.idePty->start("", ptyWorkDir)) {
                mlc.idePtyStarted = true;
                std::cout << "[IDE] PTY started in " << ptyWorkDir << "\n";
            } else {
                std::cerr << "[IDE] Failed to start PTY\n";
            }

            // Register PTY resize callback (JS calls window.vivid.ptyResize(data))
            mlc.ideBrowser->registerCallback("ptyResize", [&mlc](const std::string& json) {
                // Parse JSON: {"cols": 80, "rows": 24}
                if (mlc.idePty && mlc.idePtyStarted) {
                    int cols = 80, rows = 24;
                    size_t colsPos = json.find("\"cols\":");
                    size_t rowsPos = json.find("\"rows\":");
                    if (colsPos != std::string::npos) {
                        cols = std::atoi(json.c_str() + colsPos + 7);
                    }
                    if (rowsPos != std::string::npos) {
                        rows = std::atoi(json.c_str() + rowsPos + 7);
                    }
                    mlc.idePty->setSize(cols, rows);
                }
            });

            // Set up keyboard intercept for direct PTY input
            // This bypasses CEF/IPC for reliable terminal input
            // Only active when terminal mode is enabled (vs editor mode)
            mlc.ideBrowser->setKeyInterceptCallback([&mlc](int key, bool pressed, uint32_t mods) -> bool {
                // Only intercept in terminal mode
                if (!mlc.ideBrowser->isTerminalMode()) return false;

                // Only handle key press (not release)
                if (!pressed) return false;
                if (!mlc.idePty || !mlc.idePtyStarted) return false;

                // Let Cmd+C/V/A pass through to CEF for copy/paste/select-all
                if ((mods & vivid::cef::ModSuper) && (key == 67 || key == 86 || key == 65)) {  // C, V, A
                    return false;
                }

                // Convert to terminal sequence
                std::string seq = vivid::cef::keyToTerminalSequence(key, mods);
                if (!seq.empty()) {
                    mlc.idePty->write(seq);
                    return true;  // Consumed
                }

                return false;  // Not a special key, let char callback handle
            });

            mlc.ideBrowser->setCharInterceptCallback([&mlc](uint32_t codepoint) -> bool {
                // Only intercept in terminal mode
                if (!mlc.ideBrowser->isTerminalMode()) return false;

                if (!mlc.idePty || !mlc.idePtyStarted) return false;

                // Send character directly to PTY as UTF-8
                mlc.idePty->write(vivid::cef::codepointToUtf8(codepoint));
                return true;  // Consumed
            });

            // Register callback for JS to switch terminal/editor mode
            mlc.ideBrowser->registerCallback("setTerminalMode", [&mlc](const std::string& data) {
                bool enabled = (data == "true" || data == "1");
                mlc.ideBrowser->setTerminalMode(enabled);
            });

            // Get logical window size
            int logicalWidth, logicalHeight;
            glfwGetWindowSize(m_impl->window, &logicalWidth, &logicalHeight);

            // Position in bottom-left corner with padding
            mlc.idePanelBounds = glm::vec4(20, logicalHeight - panelHeight - 60, panelWidth, panelHeight);

            // Load IDE panel from HTTP server
            if (m_impl->editorBridge && m_impl->editorBridge->isRunning()) {
                mlc.ideBrowser->setUrl("http://localhost:9876/");
            } else {
                mlc.ideBrowser->loadHtml(
                    "<html><body style='background:#1e1e1e;color:#f14c4c;font-family:system-ui;padding:20px;'>"
                    "<h2>IDE Panel Failed to Load</h2>"
                    "<p>The RuntimeAPI server could not start (port 9876 may be in use).</p>"
                    "</body></html>", "");
                std::cerr << "[IDE] RuntimeAPI not running - showing error page\n";
            }

            // Initialize the browser (creates CEF browser instance asynchronously)
            mlc.ideBrowser->init(*m_impl->ctx);
            mlc.idePanelInitialized = true;

            // Pump the CEF message loop to allow browser creation to complete
            // Browser creation is async, so we need to pump until OnAfterCreated fires
            for (int i = 0; i < 50; i++) {
                vivid::cef::pumpCefMessageLoop();
                glfwPollEvents();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if (mlc.ideBrowser->isReady()) break;
            }

            std::cout << "[IDE] CEF Browser panel initialized (" << panelWidth << "x" << panelHeight << ")" << std::endl;
        }
    }
#endif

    // Project info
    mlc.projectName = projectName;

    // Assign helper lambdas
    mlc.updateSourceLines = updateSourceLines;
    mlc.gatherOperatorInfo = gatherOperatorInfo;
    mlc.gatherParamValues = gatherParamValues;
    mlc.gatherWindowState = gatherWindowState;

    m_initialized = true;
    return 0;
}

int Application::run() {
    if (!m_initialized || !m_impl) {
        return 1;
    }

    MainLoopContext& mlc = m_impl->mlc;

    // Main loop
    while (!glfwWindowShouldClose(mlc.window)) {
        bool shouldContinue = true;
        platform::withAutoreleasePool([&]() {
            if (!mainLoopIteration(mlc)) {
                shouldContinue = false;
            }
        });
        if (!shouldContinue) break;
    }

    return 0;
}

void Application::shutdown() {
    if (!m_impl) {
        return;
    }

    std::cout << "Shutting down..." << std::endl;

    MainLoopContext& mlc = m_impl->mlc;

    // Stop CLI recording if active
    if (mlc.cliRecorder.isRecording()) {
        std::cout << "Stopping recording: " << mlc.cliRecorder.frameCount() << " frames, "
                  << mlc.cliRecorder.duration() << "s" << std::endl;
        mlc.cliRecorder.stop();
    }

    // Stop editor bridge
    if (m_impl->editorBridge) {
        m_impl->editorBridge->stop();
    }

    // Release chain operators before WebGPU cleanup
    if (m_impl->ctx) {
        m_impl->ctx->resetChain();
    }

    // Shutdown ImGui if vivid-gui was loaded
    imgui_dynamic::tryShutdown();

    // Shutdown devtools (if dynamically loaded)
    devtools_dynamic::tryShutdown();

#ifdef VIVID_HAS_CEF
    // Shutdown PTY
    if (mlc.idePty) {
        mlc.idePty->stop();
        mlc.idePty.reset();
        mlc.idePtyStarted = false;
    }

    // Cleanup CEF Browser
    if (mlc.ideBrowser) {
        mlc.ideBrowser->cleanup();

        // Pump CEF message loop to process any pending callbacks
        for (int i = 0; i < 10; i++) {
            vivid::cef::pumpCefMessageLoop();
            glfwPollEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
#endif

    // Release display resources
    if (m_impl->display) {
        m_impl->display->shutdown();
    }

    // Release composite texture
    releaseCompositeTexture(mlc);

    // WebGPU cleanup
    if (m_impl->surface) {
        wgpuSurfaceUnconfigure(m_impl->surface);
    }
    if (m_impl->queue) {
        wgpuQueueRelease(m_impl->queue);
    }
    if (m_impl->device) {
        wgpuDeviceRelease(m_impl->device);
    }
    if (m_impl->adapter) {
        wgpuAdapterRelease(m_impl->adapter);
    }
    if (m_impl->surface) {
        wgpuSurfaceRelease(m_impl->surface);
    }
    if (m_impl->instance) {
        wgpuInstanceRelease(m_impl->instance);
    }
    if (m_impl->window) {
        glfwDestroyWindow(m_impl->window);
    }
    glfwTerminate();

#ifdef VIVID_HAS_CEF
    // Now safe to destroy CEF Browser - message loop is done pumping
    if (mlc.ideBrowser) {
        mlc.ideBrowser.reset();
    }

    // Shutdown CEF
    vivid::cef::shutdownCef();
#endif

    delete m_impl;
    m_impl = nullptr;
    m_initialized = false;
}

} // namespace vivid
