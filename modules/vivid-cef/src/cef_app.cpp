#include "cef_app.h"
#include <vivid/cef/browser.h>

#include <include/cef_app.h>
#include <include/cef_browser.h>
#include <include/wrapper/cef_helpers.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <libgen.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <linux/limits.h>
#endif

#include <atomic>
#include <cstring>

namespace vivid::cef {

// Global state
static std::atomic<bool> g_cefInitialized{false};
static CefRefPtr<VividCefApp> g_cefApp;

// -----------------------------------------------------------------------------
// VividCefApp
// -----------------------------------------------------------------------------

VividCefApp::VividCefApp() = default;

void VividCefApp::OnBeforeCommandLineProcessing(const CefString& process_type,
                                                CefRefPtr<CefCommandLine> command_line) {
    // Enable features for offscreen rendering
    command_line->AppendSwitch("disable-gpu-shader-disk-cache");

    // Enable hardware acceleration
    command_line->AppendSwitch("enable-gpu");
    command_line->AppendSwitch("enable-webgl");
    command_line->AppendSwitch("enable-webgl2-compute-context");
    command_line->AppendSwitch("ignore-gpu-blocklist");

    // For shared texture support on macOS
    command_line->AppendSwitch("enable-begin-frame-scheduling");
    command_line->AppendSwitch("use-angle");  // Use ANGLE for OpenGL ES emulation

    // Ensure GPU compositing is enabled
    command_line->AppendSwitch("enable-gpu-compositing");
    command_line->AppendSwitch("enable-accelerated-2d-canvas");

    // Reduce log spam
    command_line->AppendSwitchWithValue("log-severity", "warning");

    // Avoid "Chromium Safe Storage" keychain prompt on macOS
    command_line->AppendSwitch("use-mock-keychain");

    // Disable features we don't need
    command_line->AppendSwitch("disable-extensions");
    command_line->AppendSwitch("disable-spell-checking");
    command_line->AppendSwitch("disable-pdf-extension");
}

void VividCefApp::OnContextInitialized() {
    CEF_REQUIRE_UI_THREAD();
    // Browser context is ready
}

// -----------------------------------------------------------------------------
// Helper path detection
// -----------------------------------------------------------------------------

std::string getSubprocessPath() {
    std::string basePath;

#if defined(__APPLE__)
    // Get executable path
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        char* dir = dirname(path);
        basePath = std::string(dir) + "/vivid-cef-helper";
    }
#elif defined(_WIN32)
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string fullPath(path);
    size_t pos = fullPath.find_last_of("\\/");
    if (pos != std::string::npos) {
        basePath = fullPath.substr(0, pos) + "\\vivid-cef-helper.exe";
    }
#else
    char path[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
    if (count != -1) {
        path[count] = '\0';
        std::string fullPath(path);
        size_t pos = fullPath.find_last_of('/');
        if (pos != std::string::npos) {
            basePath = fullPath.substr(0, pos) + "/vivid-cef-helper";
        }
    }
#endif

    return basePath;
}

// -----------------------------------------------------------------------------
// Global CEF Lifecycle
// -----------------------------------------------------------------------------

bool initializeCef(int argc, char* argv[]) {
    if (g_cefInitialized) {
        return true;
    }

    // Create app handler
    g_cefApp = new VividCefApp();

    // CEF settings
    CefSettings settings;
    settings.no_sandbox = true;
    settings.windowless_rendering_enabled = true;
    settings.external_message_pump = true;

    // Disable persistent session cookies to avoid "Chromium Safe Storage" keychain prompt
    settings.persist_session_cookies = false;

    // Set cache path to avoid keychain access for encrypted storage
    // Using an empty string disables disk caching entirely (in-memory only)
    std::string cachePath;
#if defined(__APPLE__)
    // Use temp directory for cache on macOS to avoid keychain prompts
    char* tmpDir = getenv("TMPDIR");
    if (tmpDir) {
        cachePath = std::string(tmpDir) + "vivid-cef-cache";
    } else {
        cachePath = "/tmp/vivid-cef-cache";
    }
#endif
    if (!cachePath.empty()) {
        CefString(&settings.root_cache_path).FromString(cachePath);
    }

    // Subprocess path
    std::string subprocessPath = getSubprocessPath();
    CefString(&settings.browser_subprocess_path).FromString(subprocessPath);

    // Log settings
    settings.log_severity = LOGSEVERITY_WARNING;

#if defined(__APPLE__)
    // On macOS, set framework directory path
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        char* dir = dirname(path);
        std::string frameworkPath = std::string(dir) + "/Chromium Embedded Framework.framework";
        CefString(&settings.framework_dir_path).FromString(frameworkPath);
    }
#endif

    // Main args
    CefMainArgs mainArgs(argc, argv);

    // Check if this is a subprocess
    int exitCode = CefExecuteProcess(mainArgs, g_cefApp, nullptr);
    if (exitCode >= 0) {
        // This was a subprocess, exit with the provided code
        exit(exitCode);
    }

    // Initialize CEF for the browser process
    if (!CefInitialize(mainArgs, settings, g_cefApp, nullptr)) {
        fprintf(stderr, "[CEF] Failed to initialize CEF\n");
        g_cefApp = nullptr;
        return false;
    }

    g_cefInitialized = true;
    printf("[CEF] Initialized successfully\n");
    return true;
}

void shutdownCef() {
    if (!g_cefInitialized) {
        return;
    }

    CefShutdown();
    g_cefApp = nullptr;
    g_cefInitialized = false;
    printf("[CEF] Shutdown complete\n");
}

void pumpCefMessageLoop() {
    if (g_cefInitialized) {
        CefDoMessageLoopWork();
    }
}

bool isCefInitialized() {
    return g_cefInitialized;
}

} // namespace vivid::cef
