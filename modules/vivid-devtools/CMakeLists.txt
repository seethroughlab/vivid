# Vivid DevTools Module
# Live performance and debugging tools: node graph, inspector, performance
#
# This module is optional - production bundles can exclude it.
#
# Architecture:
# - Uses GUI primitives from vivid-core (OverlayCanvas, Panel system)
# - Contains performer/debugger panels (NodeGraph, Inspector, Performance, etc.)
# - Contains ChainVisualizer for operator chain visualization
# - Loaded dynamically by app.cpp via dlsym/GetProcAddress
# - Production bundles simply don't include this library
#
# Dependencies:
# - vivid-core: Core library including GUI primitives (OverlayCanvas, NodeGraph, Panel system)

# -----------------------------------------------------------------------------
# vivid-devtools module
# -----------------------------------------------------------------------------
set(DEVTOOLS_SOURCES
    # DevTools core
    src/shortcut_manager.cpp
    src/preferences.cpp
    src/devtools.cpp
    src/devtools_exports.cpp

    # Modal dialogs
    src/modal_dialog.cpp
    src/preferences_panel.cpp

    # Panels
    src/panels/node_graph_panel.cpp
    src/panels/inspector_panel.cpp
    src/panels/status_bar_panel.cpp
    src/panels/performance_panel.cpp
    src/panels/console_panel.cpp

    # ChainVisualizer (moved from vivid-core)
    src/chain_visualizer.cpp

    # Note: Panel system (panel.cpp, panel_manager.cpp, etc.)
    # is in vivid-core module
)

add_library(vivid-devtools SHARED ${DEVTOOLS_SOURCES})

target_include_directories(vivid-devtools
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/modules/vivid-core/src  # For font_atlas.h (private header)
)

# Link against vivid-core for GUI primitives (OverlayCanvas, Panel system, etc.)
target_link_libraries(vivid-devtools
    PUBLIC vivid-core
)

# Platform-specific configuration
if(APPLE)
    set_target_properties(vivid-devtools PROPERTIES
        INSTALL_RPATH "@executable_path/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(WIN32)
    set_target_properties(vivid-devtools PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/Debug
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/Release
    )
    # Windows: DLLs must be in same directory as exe
    add_custom_command(TARGET vivid-devtools POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:vivid-devtools>
            ${CMAKE_BINARY_DIR}/bin/
        COMMENT "Copying vivid-devtools DLL to bin"
    )
elseif(UNIX)
    set_target_properties(vivid-devtools PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Copy devtools assets (fonts) to build directory
add_custom_command(TARGET vivid-devtools POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/assets/devtools/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts
        ${CMAKE_BINARY_DIR}/assets/devtools/fonts
    COMMENT "Copying devtools assets..."
)

message(STATUS "[vivid-devtools] Configured unified devtools module")
