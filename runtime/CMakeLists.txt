# Runtime CMakeLists.txt - Phase 2: Window and WebGPU
# Additional source files will be added in later phases

set(RUNTIME_SOURCES
    src/main.cpp
    src/window.cpp
    src/renderer.cpp
)

# Platform-specific source files
if(APPLE)
    list(APPEND RUNTIME_SOURCES src/platform_surface_macos.mm)
    # Enable Objective-C++ for the .mm file
    set_source_files_properties(src/platform_surface_macos.mm PROPERTIES
        COMPILE_FLAGS "-x objective-c++"
    )
endif()

add_executable(vivid-runtime ${RUNTIME_SOURCES})

target_include_directories(vivid-runtime
    PUBLIC include
    PRIVATE src
    PRIVATE ${stb_SOURCE_DIR}
)

target_link_libraries(vivid-runtime
    PRIVATE
    glfw
    glm::glm
    wgpu
    ixwebsocket
    nlohmann_json::nlohmann_json
    efsw
    ${CMAKE_DL_LIBS}
)

# Platform-specific libraries
if(APPLE)
    target_link_libraries(vivid-runtime PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework Metal"
        "-framework QuartzCore"
    )
elseif(UNIX)
    target_link_libraries(vivid-runtime PRIVATE pthread dl)
endif()

# Copy include headers to build directory for operator compilation
add_custom_command(TARGET vivid-runtime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)

# Copy shaders to build directory
add_custom_command(TARGET vivid-runtime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/shaders
    ${CMAKE_BINARY_DIR}/shaders
)
