# Vivid WebView Addon
# Cross-platform web content rendering to texture
# - macOS: WKWebView with IOSurface GPU-direct texture sharing
# - Windows: WebView2 (stub - requires Microsoft.WebView2 SDK)
# - Linux: CEF (stub - requires Chromium Embedded Framework)

# Common sources (all platforms)
set(WEBVIEW_SOURCES
    src/webview.cpp
)

# Platform-specific sources
if(APPLE)
    list(APPEND WEBVIEW_SOURCES
        src/webview_macos.mm
    )
elseif(WIN32)
    list(APPEND WEBVIEW_SOURCES
        src/webview_windows.cpp
    )
else()
    # Linux
    list(APPEND WEBVIEW_SOURCES
        src/webview_linux.cpp
    )
endif()

add_library(vivid-webview SHARED ${WEBVIEW_SOURCES})

target_include_directories(vivid-webview PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/modules/vivid-core/include
)

target_link_libraries(vivid-webview PUBLIC
    webgpu
    glm
    glfw
    vivid-core
)

# Platform-specific configuration
if(APPLE)
    # macOS frameworks for WKWebView and GPU texture sharing
    target_link_libraries(vivid-webview PRIVATE
        "-framework WebKit"
        "-framework Metal"
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework QuartzCore"
        "-framework AppKit"
        "-framework IOSurface"
    )

    # Enable Objective-C++
    set_source_files_properties(
        src/webview_macos.mm
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )

    # Set install name for macOS so chains can find the library
    set_target_properties(vivid-webview PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )

elseif(WIN32)
    # Windows WebView2 - requires Microsoft.WebView2 SDK
    # Users need to install WebView2 runtime or use fixed version distribution
    # TODO: Add FetchContent for WebView2 SDK when implementing
    message(STATUS "vivid-webview: Windows WebView2 support is a stub - not yet implemented")

    # Define export macro when building the DLL
    target_compile_definitions(vivid-webview PRIVATE vivid_webview_EXPORTS)

    # Set output directories for import library
    set_target_properties(vivid-webview PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/Debug
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/Release
    )

else()
    # Linux - CEF (Chromium Embedded Framework)
    # CEF is a large dependency (~200MB) that requires manual setup
    # TODO: Add CEF integration when implementing
    message(STATUS "vivid-webview: Linux CEF support is a stub - not yet implemented")
endif()

# Library is built to lib/ directly (CMAKE_LIBRARY_OUTPUT_DIRECTORY)
# Executable finds it via RPATH
