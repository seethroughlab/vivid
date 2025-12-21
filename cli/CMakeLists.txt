# Vivid CLI - Main executable
# Contains: entry point, argument parsing, editor bridge

set(VIVID_CLI_SOURCES
    main.cpp
    app.cpp
    cli.cpp
    editor_bridge.cpp
)

set(VIVID_CLI_HEADERS
    include/vivid/cli.h
    include/vivid/editor_bridge.h
)

add_executable(vivid
    ${VIVID_CLI_SOURCES}
    ${VIVID_CLI_HEADERS}
)

target_include_directories(vivid
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/core
        ${CMAKE_SOURCE_DIR}/core/include
)

# Pass version from CMake (or CI override)
target_compile_definitions(vivid PRIVATE
    VIVID_VERSION="${VIVID_VERSION}"
)

# Link against core library (includes ImGui)
# Note: glfw3webgpu is explicitly linked here because app.cpp uses glfwCreateWindowWGPUSurface
# Addons (audio, render3d, etc.) are NOT linked - chains link them when needed
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
endif()

# Copy shaders to build directory
add_custom_command(TARGET vivid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/core/shaders
        $<TARGET_FILE_DIR:vivid>/shaders
    COMMENT "Copying shaders..."
)

# Copy templates to build directory
add_custom_command(TARGET vivid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/core/templates
        $<TARGET_FILE_DIR:vivid>/templates
    COMMENT "Copying templates..."
)

# Install target
install(TARGETS vivid RUNTIME DESTINATION bin)

message(STATUS "[vivid-cli] Configured")
