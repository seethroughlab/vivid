#pragma once

// DLL export/import macros for Windows
#if defined(_WIN32) || defined(_WIN64)
    #ifdef VIVID_BUILDING_RUNTIME
        #define VIVID_API __declspec(dllexport)
    #else
        #define VIVID_API __declspec(dllimport)
    #endif
#else
    #define VIVID_API
#endif
