# Vivid Visualizer Module
# Chain visualizer (node graph) for development and debugging
#
# This module is optional - bundled apps can exclude it for production.
# Provides the chain visualizer UI that appears when pressing Tab.
#
# Architecture:
# - Uses OverlayCanvas and GUI widgets from vivid-core (general-purpose primitives)
# - Contains ChainVisualizer and NodeGraph (dev tools)
# - Loaded dynamically by app.cpp via dlsym/GetProcAddress
# - Production bundles can simply not include this library

set(VISUALIZER_SOURCES
    src/chain_visualizer.cpp
    src/gui/node_graph.cpp
    src/visualizer_exports.cpp
)

add_library(vivid-visualizer SHARED
    ${VISUALIZER_SOURCES}
)

target_include_directories(vivid-visualizer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        # Access vivid-core public headers for Context, Operator, OverlayCanvas, etc.
        ${CMAKE_SOURCE_DIR}/modules/vivid-core/include
        # Access vivid-core private headers for FontAtlas, etc. (internal implementation)
        ${CMAKE_SOURCE_DIR}/modules/vivid-core/src
)

# Link against vivid-core for Context, Operator, OverlayCanvas, VideoExporter, etc.
target_link_libraries(vivid-visualizer PUBLIC
    vivid-core
)

# Platform-specific configuration
if(APPLE)
    set_target_properties(vivid-visualizer PROPERTIES
        INSTALL_RPATH "@executable_path/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(WIN32)
    # Export all symbols to generate import library
    set_target_properties(vivid-visualizer PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/Debug
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/Release
    )
    # Windows: DLLs must be in same directory as exe (no RPATH on Windows)
    add_custom_command(TARGET vivid-visualizer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:vivid-visualizer>
            ${CMAKE_BINARY_DIR}/bin/
        COMMENT "Copying vivid-visualizer DLL to bin"
    )
elseif(UNIX)
    set_target_properties(vivid-visualizer PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

message(STATUS "[vivid-visualizer] Configured chain visualizer module")
