/**
 * @file main.cpp
 * @brief CEF subprocess helper executable
 *
 * CEF uses a multi-process architecture similar to Chrome. When the main
 * application creates a browser, CEF spawns separate processes for:
 * - Renderer process (runs JavaScript, renders web content)
 * - GPU process (handles hardware acceleration)
 * - Utility processes (network, etc.)
 *
 * This helper executable is used for all CEF subprocess types. It simply
 * initializes CEF and enters its message loop.
 *
 * The subprocess path is configured via CefSettings::browser_subprocess_path.
 */

#include <include/cef_app.h>
#include <include/cef_command_line.h>
#include <include/cef_v8.h>

#if defined(_WIN32)
#include <windows.h>
#endif

/**
 * @brief V8 handler for window.vivid callbacks
 *
 * This handler receives JavaScript calls to window.vivid.* functions
 * and forwards them to the browser process via IPC.
 */
class VividV8Handler : public CefV8Handler {
public:
    explicit VividV8Handler(CefRefPtr<CefBrowser> browser) : m_browser(browser) {}

    bool Execute(const CefString& name,
                 CefRefPtr<CefV8Value> object,
                 const CefV8ValueList& arguments,
                 CefRefPtr<CefV8Value>& retval,
                 CefString& exception) override {
        // Send IPC message to browser process
        CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("vivid_callback");
        CefRefPtr<CefListValue> args = msg->GetArgumentList();

        // First argument is the callback name
        args->SetString(0, name);

        // Second argument is the data (convert first JS argument to string)
        if (!arguments.empty()) {
            if (arguments[0]->IsString()) {
                args->SetString(1, arguments[0]->GetStringValue());
            } else if (arguments[0]->IsBool()) {
                args->SetString(1, arguments[0]->GetBoolValue() ? "true" : "false");
            } else if (arguments[0]->IsInt()) {
                args->SetString(1, std::to_string(arguments[0]->GetIntValue()));
            } else if (arguments[0]->IsObject()) {
                // For objects (like {cols, rows}), serialize to JSON-like string
                // For PTY resize, we expect {cols: N, rows: N}
                CefRefPtr<CefV8Value> obj = arguments[0];
                std::string json = "{";
                std::vector<CefString> keys;
                if (obj->GetKeys(keys)) {
                    for (size_t i = 0; i < keys.size(); i++) {
                        if (i > 0) json += ",";
                        json += "\"" + keys[i].ToString() + "\":";
                        CefRefPtr<CefV8Value> val = obj->GetValue(keys[i]);
                        if (val->IsInt()) {
                            json += std::to_string(val->GetIntValue());
                        } else if (val->IsDouble()) {
                            json += std::to_string(val->GetDoubleValue());
                        } else if (val->IsString()) {
                            json += "\"" + val->GetStringValue().ToString() + "\"";
                        } else if (val->IsBool()) {
                            json += val->GetBoolValue() ? "true" : "false";
                        }
                    }
                }
                json += "}";
                args->SetString(1, json);
            } else {
                args->SetString(1, "");
            }
        } else {
            args->SetString(1, "");
        }

        // Send to browser process
        m_browser->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
        return true;
    }

private:
    CefRefPtr<CefBrowser> m_browser;
    IMPLEMENT_REFCOUNTING(VividV8Handler);
};

/**
 * @brief CEF app for renderer subprocess
 *
 * Handles renderer-process specific initialization. This is where
 * the window.vivid JavaScript bridge is created.
 */
class VividRendererApp : public CefApp, public CefRenderProcessHandler {
public:
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
        return this;
    }

    void OnContextCreated(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) override {
        // Create the window.vivid object with callback functions
        CefRefPtr<CefV8Value> global = context->GetGlobal();

        // Create vivid object
        CefRefPtr<CefV8Value> vividObj = CefV8Value::CreateObject(nullptr, nullptr);

        // Create handler for all vivid functions
        CefRefPtr<VividV8Handler> handler = new VividV8Handler(browser);

        // Add ptyInput function: window.vivid.ptyInput(data)
        CefRefPtr<CefV8Value> ptyInputFunc = CefV8Value::CreateFunction("ptyInput", handler);
        vividObj->SetValue("ptyInput", ptyInputFunc, V8_PROPERTY_ATTRIBUTE_NONE);

        // Add ptyResize function: window.vivid.ptyResize({cols, rows})
        CefRefPtr<CefV8Value> ptyResizeFunc = CefV8Value::CreateFunction("ptyResize", handler);
        vividObj->SetValue("ptyResize", ptyResizeFunc, V8_PROPERTY_ATTRIBUTE_NONE);

        // Add setTerminalMode function: window.vivid.setTerminalMode(true/false)
        CefRefPtr<CefV8Value> setTerminalModeFunc = CefV8Value::CreateFunction("setTerminalMode", handler);
        vividObj->SetValue("setTerminalMode", setTerminalModeFunc, V8_PROPERTY_ATTRIBUTE_NONE);

        // Set window.vivid
        global->SetValue("vivid", vividObj, V8_PROPERTY_ATTRIBUTE_NONE);
    }

private:
    IMPLEMENT_REFCOUNTING(VividRendererApp);
};

/**
 * @brief Determine which CefApp to use based on process type
 */
CefRefPtr<CefApp> createAppForProcessType(const std::string& processType) {
    if (processType == "renderer") {
        return new VividRendererApp();
    }

    // Browser, GPU, and utility processes use the base CefApp
    return nullptr;
}

#if defined(_WIN32)
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    PWSTR pCmdLine, int nCmdShow) {
    CefMainArgs mainArgs(hInstance);
#else
int main(int argc, char* argv[]) {
    CefMainArgs mainArgs(argc, argv);
#endif

    // Check process type
    CefRefPtr<CefCommandLine> commandLine = CefCommandLine::CreateCommandLine();
#if defined(_WIN32)
    commandLine->InitFromString(GetCommandLineW());
#else
    commandLine->InitFromArgv(argc, argv);
#endif

    std::string processType = commandLine->GetSwitchValue("type").ToString();

    // Create appropriate app
    CefRefPtr<CefApp> app = createAppForProcessType(processType);

    // Execute the subprocess
    int exitCode = CefExecuteProcess(mainArgs, app, nullptr);

    // Exit with the provided code
    return exitCode;
}
