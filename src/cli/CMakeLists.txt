# Vivid CLI - Main executable (development build with all features)
# Contains: entry point, argument parsing, runtime API (WebSocket server for MCP)
#
# Production bundles use vivid-production executable (main_production.cpp + Runtime class)
# which does NOT include MCP or RuntimeAPI.

set(VIVID_CLI_SOURCES
    main.cpp
    app.cpp
    cli.cpp
    module_loader.cpp
    runtime_api.cpp
    mcp_server.cpp
    pty.cpp
)

set(VIVID_CLI_HEADERS
    include/vivid/cli.h
    include/vivid/runtime_api.h
    include/vivid/mcp_server.h
    include/vivid/module_loader.h
    include/vivid/pty.h
)

add_executable(vivid
    ${VIVID_CLI_SOURCES}
    ${VIVID_CLI_HEADERS}
)

target_include_directories(vivid
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/modules/vivid-core
        ${CMAKE_SOURCE_DIR}/modules/vivid-core/include
)

# Pass version from CMake (or CI override)
target_compile_definitions(vivid PRIVATE
    VIVID_VERSION="${VIVID_VERSION}"
    VIVID_ENABLE_MCP  # Always enabled in dev build
)

# Static chain linking for production builds
# When VIVID_CHAIN_SOURCE is set, compile chain.cpp directly into the executable
# VIVID_CHAIN_MODULES specifies which modules the chain needs (detected by analyzing includes)
if(VIVID_CHAIN_SOURCE)
    if(EXISTS "${VIVID_CHAIN_SOURCE}")
        target_sources(vivid PRIVATE "${VIVID_CHAIN_SOURCE}")
        target_compile_definitions(vivid PRIVATE VIVID_STATIC_CHAIN)

        # Link only the modules this chain actually uses
        if(VIVID_CHAIN_MODULES)
            foreach(MODULE_NAME ${VIVID_CHAIN_MODULES})
                # Skip vivid-core (already linked above)
                if(NOT MODULE_NAME STREQUAL "vivid-core")
                    if(TARGET ${MODULE_NAME})
                        target_link_libraries(vivid PRIVATE ${MODULE_NAME})
                        message(STATUS "[vivid-cli] Linking module: ${MODULE_NAME}")
                    else()
                        message(WARNING "[vivid-cli] Module target not found: ${MODULE_NAME}")
                    endif()
                endif()
            endforeach()
        endif()

        message(STATUS "[vivid-cli] Static chain: ${VIVID_CHAIN_SOURCE}")
    else()
        message(WARNING "[vivid-cli] VIVID_CHAIN_SOURCE specified but file not found: ${VIVID_CHAIN_SOURCE}")
    endif()
endif()

# Link against core library
# Note: glfw3webgpu is explicitly linked here because app.cpp uses glfwCreateWindowWGPUSurface
# Modules (audio, render3d, etc.) are NOT linked - chains link them when needed
target_link_libraries(vivid
    PRIVATE
        vivid-core
        glfw3webgpu
        CLI11::CLI11
        ixwebsocket  # For MCP server (always enabled in dev build)
)

# Link vivid-visualizer for dev builds (Tab to show visualizer)
# The visualizer is loaded dynamically via dlsym, but the library must be loaded first
if(TARGET vivid-visualizer)
    target_link_libraries(vivid PRIVATE vivid-visualizer)
    message(STATUS "[vivid-cli] Linked with vivid-visualizer for Tab visualizer")
endif()

# Link vivid-webview for IDE panel (optional)
if(TARGET vivid-webview)
    target_link_libraries(vivid PRIVATE vivid-webview)
    target_compile_definitions(vivid PRIVATE VIVID_HAS_WEBVIEW)
    message(STATUS "[vivid-cli] Linked with vivid-webview for IDE panel")
endif()


# Platform-specific settings
if(APPLE)
    # macOS frameworks are linked via vivid-core
    # Set RPATH to find libraries in ../lib relative to executable
    set_target_properties(vivid PROPERTIES
        INSTALL_RPATH "@executable_path/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(WIN32)
    # Export symbols for dynamically loaded chain DLLs
    # ENABLE_EXPORTS generates an import library (vivid.lib) for the executable
    # WINDOWS_EXPORT_ALL_SYMBOLS exports all symbols automatically
    set_target_properties(vivid PROPERTIES
        ENABLE_EXPORTS ON
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
elseif(UNIX)
    # Linux: Link against X11 if needed
    find_package(X11)
    if(X11_FOUND)
        target_link_libraries(vivid PRIVATE ${X11_LIBRARIES})
    endif()
    # Set RPATH to find libraries in ../lib relative to executable
    set_target_properties(vivid PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Copy shaders to build root (matches release structure)
add_custom_command(TARGET vivid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/modules/vivid-core/shaders
        ${CMAKE_BINARY_DIR}/shaders
    COMMENT "Copying shaders..."
)

# Copy templates to build root (matches release structure)
add_custom_command(TARGET vivid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/modules/vivid-core/templates
        ${CMAKE_BINARY_DIR}/templates
    COMMENT "Copying templates..."
)

# Copy assets to build root (matches release structure)
add_custom_command(TARGET vivid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/modules/vivid-core/assets
        ${CMAKE_BINARY_DIR}/assets
    COMMENT "Copying assets..."
)

# Copy presets to build root (matches release structure)
add_custom_command(TARGET vivid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/modules/vivid-audio/presets
        ${CMAKE_BINARY_DIR}/presets
    COMMENT "Copying presets..."
)

# Copy projects to build root (matches release structure)
add_custom_command(TARGET vivid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/projects
        ${CMAKE_BINARY_DIR}/projects
    COMMENT "Copying projects..."
)

# Copy vivid-core examples (source is in modules/vivid-core/examples/, output to modules/vivid-core/examples/)
add_custom_command(TARGET vivid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/modules/vivid-core/examples
        ${CMAKE_BINARY_DIR}/modules/vivid-core/examples
    COMMENT "Copying vivid-core examples..."
)

# Copy module examples to build root (for .examples() paths in operator metadata)
# These paths are like "modules/vivid-audio/examples/..." so we preserve that structure
file(GLOB MODULE_EXAMPLE_DIRS ${CMAKE_SOURCE_DIR}/modules/*/examples)
foreach(EXAMPLE_DIR ${MODULE_EXAMPLE_DIRS})
    # Get the module name from the path (e.g., vivid-audio from modules/vivid-audio/examples)
    get_filename_component(MODULE_PATH ${EXAMPLE_DIR} DIRECTORY)
    get_filename_component(MODULE_NAME ${MODULE_PATH} NAME)
    add_custom_command(TARGET vivid POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${EXAMPLE_DIR}
            ${CMAKE_BINARY_DIR}/modules/${MODULE_NAME}/examples
        COMMENT "Copying ${MODULE_NAME} examples..."
    )
endforeach()

# Generate version.json for extension compatibility (at configure time)
file(WRITE ${CMAKE_BINARY_DIR}/version.json
    "{\"version\": \"${VIVID_VERSION}\", \"channel\": \"dev\"}\n"
)

# Install target
install(TARGETS vivid RUNTIME DESTINATION bin)

message(STATUS "[vivid-cli] Configured")

# =============================================================================
# Production executable (for bundling)
# =============================================================================
# This is a minimal executable for production bundles:
# - NO HotReload, NO MCP, NO Visualizer
# - chain.cpp is compiled directly into the executable
# - Uses the Runtime class instead of Application
#
# Usage: The bundle command generates a CMakeLists.txt that builds this with
# the user's chain.cpp compiled in.
#
# Note: This target is EXCLUDE_FROM_ALL - it's not built during normal builds.
# The bundle command builds it with the specific chain.cpp passed in.

if(VIVID_CHAIN_SOURCE)
    # When bundling, build the production executable with chain.cpp compiled in
    add_executable(vivid-production
        main_production.cpp
        "${VIVID_CHAIN_SOURCE}"
    )

    target_include_directories(vivid-production
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/modules/vivid-core
            ${CMAKE_SOURCE_DIR}/modules/vivid-core/include
    )

    target_compile_definitions(vivid-production PRIVATE
        VIVID_VERSION="${VIVID_VERSION}"
    )

    # Link against core library (which includes Runtime)
    target_link_libraries(vivid-production
        PRIVATE
            vivid-core
            glfw3webgpu
    )

    # Link required modules (detected by bundle command)
    if(VIVID_CHAIN_MODULES)
        foreach(MODULE_NAME ${VIVID_CHAIN_MODULES})
            if(NOT MODULE_NAME STREQUAL "vivid-core")
                if(TARGET ${MODULE_NAME})
                    target_link_libraries(vivid-production PRIVATE ${MODULE_NAME})
                    message(STATUS "[vivid-production] Linking module: ${MODULE_NAME}")
                else()
                    message(WARNING "[vivid-production] Module target not found: ${MODULE_NAME}")
                endif()
            endif()
        endforeach()
    endif()

    # Platform-specific settings
    if(APPLE)
        set_target_properties(vivid-production PROPERTIES
            INSTALL_RPATH "@executable_path/../lib"
            BUILD_WITH_INSTALL_RPATH TRUE
        )
    elseif(WIN32)
        # No need to export symbols - this is a standalone executable
    elseif(UNIX)
        find_package(X11)
        if(X11_FOUND)
            target_link_libraries(vivid-production PRIVATE ${X11_LIBRARIES})
        endif()
        set_target_properties(vivid-production PROPERTIES
            INSTALL_RPATH "$ORIGIN/../lib"
            BUILD_WITH_INSTALL_RPATH TRUE
        )
    endif()

    message(STATUS "[vivid-production] Production executable configured with chain: ${VIVID_CHAIN_SOURCE}")
endif()
