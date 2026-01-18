# Vivid CLI - Main executable
# Contains: entry point, argument parsing, runtime API (WebSocket server for MCP)

set(VIVID_CLI_SOURCES
    main.cpp
    app.cpp
    cli.cpp
    runtime_api.cpp
    mcp_server.cpp
    module_loader.cpp
)

set(VIVID_CLI_HEADERS
    include/vivid/cli.h
    include/vivid/runtime_api.h
    include/vivid/mcp_server.h
    include/vivid/module_loader.h
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
)

# Link against core library
# Note: glfw3webgpu is explicitly linked here because app.cpp uses glfwCreateWindowWGPUSurface
# Modules (audio, render3d, etc.) are NOT linked - chains link them when needed
target_link_libraries(vivid
    PRIVATE
        vivid-core
        glfw3webgpu
        CLI11::CLI11
        ixwebsocket
)


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
