#pragma once

// DLL export/import macros for vivid-webview
// On Windows, shared libraries require explicit symbol export declarations

#ifdef _WIN32
    #ifdef vivid_webview_EXPORTS
        // Building the DLL
        #define VIVID_WEBVIEW_API __declspec(dllexport)
    #else
        // Using the DLL
        #define VIVID_WEBVIEW_API __declspec(dllimport)
    #endif
#else
    // Non-Windows platforms don't need special export declarations
    #define VIVID_WEBVIEW_API
#endif
