#pragma once

/**
 * @file cef_app.h
 * @brief CEF application class and global lifecycle management
 *
 * Handles CEF initialization/shutdown and provides the CefApp implementation
 * for the browser process.
 */

#include <include/cef_app.h>
#include <include/cef_browser_process_handler.h>
#include <string>

namespace vivid::cef {

/**
 * @brief CEF application handler for browser process
 *
 * Implements CefApp for the browser process, handling command-line
 * configuration and browser process lifecycle events.
 */
class VividCefApp : public CefApp,
                    public CefBrowserProcessHandler {
public:
    VividCefApp();

    // CefApp methods
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        return this;
    }

    void OnBeforeCommandLineProcessing(const CefString& process_type,
                                       CefRefPtr<CefCommandLine> command_line) override;

    // CefBrowserProcessHandler methods
    void OnContextInitialized() override;

private:
    IMPLEMENT_REFCOUNTING(VividCefApp);
};

/**
 * @brief Get the subprocess helper path
 * @return Path to the vivid-cef-helper executable
 */
std::string getSubprocessPath();

} // namespace vivid::cef
