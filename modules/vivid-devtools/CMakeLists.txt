# Vivid DevTools Module
# Unified development tools: terminal, editor, node graph, inspector, console
#
# This module is optional - production bundles can exclude it.
#
# Architecture:
# - Uses OverlayCanvas and GUI widgets from vivid-core
# - Contains Panel system with Terminal, Editor, NodeGraph, Inspector panels
# - Loaded dynamically by app.cpp via dlsym/GetProcAddress
# - Production bundles simply don't include this library
#
# Dependencies:
# - libvterm: VT220/xterm terminal emulator (from third_party/)

# -----------------------------------------------------------------------------
# libvterm (terminal emulation - full xterm support)
# -----------------------------------------------------------------------------
if(NOT TARGET vterm)
    add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/libvterm ${CMAKE_BINARY_DIR}/libvterm_devtools)
    set_target_properties(vterm PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# -----------------------------------------------------------------------------
# vivid-devtools module
# -----------------------------------------------------------------------------
set(DEVTOOLS_SOURCES
    # Core panel system
    src/panel.cpp
    src/panel_manager.cpp
    src/shortcut_manager.cpp
    src/preferences.cpp
    src/devtools.cpp
    src/devtools_exports.cpp

    # Layout system
    src/panel_group.cpp
    src/split_container.cpp
    src/panel_leaf.cpp

    # Modal dialogs
    src/modal_dialog.cpp
    src/preferences_panel.cpp

    # Panels
    src/panels/terminal_panel.cpp
    src/panels/editor_panel.cpp
    src/panels/node_graph_panel.cpp
    src/panels/inspector_panel.cpp
    src/panels/status_bar_panel.cpp
    src/panels/console_panel.cpp

    # ChainVisualizer and NodeGraph (used by vivid-c for embedding)
    ${CMAKE_SOURCE_DIR}/modules/vivid-core/src/chain_visualizer.cpp
    ${CMAKE_SOURCE_DIR}/modules/vivid-core/src/gui/node_graph.cpp

    # PTY is needed for terminal - include source directly
    ${CMAKE_SOURCE_DIR}/src/cli/pty.cpp
)

add_library(vivid-devtools SHARED ${DEVTOOLS_SOURCES})

target_include_directories(vivid-devtools
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/modules/vivid-core/include
        ${CMAKE_SOURCE_DIR}/modules/vivid-core/src
        ${CMAKE_SOURCE_DIR}/src/cli/include
        ${CMAKE_SOURCE_DIR}/third_party/libvterm/include
)

# Link against vivid-core for OverlayCanvas, Gui, Context, etc.
# Link against libvterm for terminal emulation
target_link_libraries(vivid-devtools
    PUBLIC vivid-core
    PRIVATE vterm
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
