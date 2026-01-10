#pragma once

// DLL export/import macros for vivid-opencv
// On Windows, shared libraries require explicit symbol export declarations

#ifdef _WIN32
    #ifdef vivid_opencv_EXPORTS
        // Building the DLL
        #define VIVID_OPENCV_API __declspec(dllexport)
    #else
        // Using the DLL
        #define VIVID_OPENCV_API __declspec(dllimport)
    #endif
#else
    // Non-Windows platforms don't need special export declarations
    #define VIVID_OPENCV_API
#endif
