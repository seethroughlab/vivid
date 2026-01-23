# Vivid IDE Module
# Native terminal and code editor for development
#
# This module is optional - production bundles can exclude it.
# Provides integrated terminal (libvterm) and editor (Zep) panels
# that render via OverlayCanvas, matching the visualizer's look.
#
# Architecture:
# - Uses OverlayCanvas and GUI widgets from vivid-core
# - Contains IdePanel (orchestrator), TerminalPanel, EditorPanel
# - Loaded dynamically by app.cpp via dlsym/GetProcAddress
# - Production bundles simply don't include this library
#
# Dependencies:
# - libvterm: VT220/xterm terminal emulator (from third_party/)
# - Zep: Embeddable code editor (vendored in deps/)

# -----------------------------------------------------------------------------
# libvterm (terminal emulation - full xterm support)
# -----------------------------------------------------------------------------
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/libvterm ${CMAKE_BINARY_DIR}/libvterm)
set_target_properties(vterm PROPERTIES POSITION_INDEPENDENT_CODE ON)

# -----------------------------------------------------------------------------
# vivid-ide module
# -----------------------------------------------------------------------------
set(IDE_SOURCES
    src/ide_panel.cpp
    src/terminal_panel.cpp
    src/editor_panel.cpp
    src/ide_exports.cpp
    # PTY is needed for terminal - include source directly
    ${CMAKE_SOURCE_DIR}/src/cli/pty.cpp
)

add_library(vivid-ide SHARED ${IDE_SOURCES})

target_include_directories(vivid-ide
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
target_link_libraries(vivid-ide
    PUBLIC vivid-core
    PRIVATE vterm
)

# Platform-specific configuration
if(APPLE)
    set_target_properties(vivid-ide PROPERTIES
        INSTALL_RPATH "@executable_path/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(WIN32)
    set_target_properties(vivid-ide PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/Debug
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/Release
    )
    # Windows: DLLs must be in same directory as exe
    add_custom_command(TARGET vivid-ide POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:vivid-ide>
            ${CMAKE_BINARY_DIR}/bin/
        COMMENT "Copying vivid-ide DLL to bin"
    )
elseif(UNIX)
    set_target_properties(vivid-ide PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

message(STATUS "[vivid-ide] Configured native IDE module")
