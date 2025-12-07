# Vivid Core - Runtime executable
# ~600 lines: window, timing, input, hot-reload, display

set(VIVID_CORE_SOURCES
    src/main.cpp
    src/context.cpp
    src/display.cpp
    src/hot_reload.cpp
)

set(VIVID_CORE_HEADERS
    include/vivid/vivid.h
    include/vivid/context.h
    include/vivid/display.h
    include/vivid/hot_reload.h
)

# Main runtime executable
add_executable(vivid ${VIVID_CORE_SOURCES} ${VIVID_CORE_HEADERS})

target_include_directories(vivid
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link against WebGPU, GLFW, GLM, and the GLFW-WebGPU integration
target_link_libraries(vivid
    PRIVATE
        glfw
        glm::glm
        webgpu
        glfw3webgpu
)

# Platform-specific settings
if(APPLE)
    # macOS: Link against required frameworks
    target_link_libraries(vivid PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework QuartzCore"
        "-framework Metal"
    )
elseif(WIN32)
    # Windows settings if needed
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
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:vivid>/shaders
    COMMENT "Copying shaders..."
)

# Install target
install(TARGETS vivid RUNTIME DESTINATION bin)
install(DIRECTORY shaders/ DESTINATION share/vivid/shaders)
