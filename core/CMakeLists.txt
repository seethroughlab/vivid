# Vivid Core - Runtime executable
# Includes: window, timing, input, hot-reload, display, chain visualization (ImGui)

include(FetchContent)

# Fetch ImGui from master (required for wgpu-native v24.x compatibility)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

# Fetch ImNodes for node graph editor (don't use its CMakeLists.txt)
FetchContent_Declare(
    imnodes
    GIT_REPOSITORY https://github.com/Nelarius/imnodes.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(imnodes)
if(NOT imnodes_POPULATED)
    FetchContent_Populate(imnodes)
endif()

message(STATUS "[vivid-core] ImGui source: ${imgui_SOURCE_DIR}")
message(STATUS "[vivid-core] ImNodes source: ${imnodes_SOURCE_DIR}")

# ImGui core sources
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
)

# ImNodes sources
set(IMNODES_SOURCES
    ${imnodes_SOURCE_DIR}/imnodes.cpp
)

# Vivid core sources
set(VIVID_CORE_SOURCES
    src/main.cpp
    src/context.cpp
    src/display.cpp
    src/hot_reload.cpp
    src/chain.cpp
    src/operator.cpp
    src/addon_registry.cpp
    imgui/imgui_integration.cpp
    imgui/chain_visualizer.cpp
)

set(VIVID_CORE_HEADERS
    include/vivid/vivid.h
    include/vivid/context.h
    include/vivid/display.h
    include/vivid/hot_reload.h
    include/vivid/operator.h
    include/vivid/chain.h
    include/vivid/addon_registry.h
    imgui/imgui_integration.h
    imgui/chain_visualizer.h
)

# Main runtime executable
add_executable(vivid
    ${VIVID_CORE_SOURCES}
    ${VIVID_CORE_HEADERS}
    ${IMGUI_SOURCES}
    ${IMNODES_SOURCES}
)

target_include_directories(vivid
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${imnodes_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/addons/vivid-render3d/include
        ${CMAKE_SOURCE_DIR}/addons/vivid-effects-2d/include
)

# Use wgpu-native backend (not Dawn)
target_compile_definitions(vivid PRIVATE
    IMGUI_IMPL_WEBGPU_BACKEND_WGPU
)

# Link against WebGPU, GLFW, GLM, GLFW-WebGPU integration, and render3d for geometry previews
target_link_libraries(vivid
    PRIVATE
        glfw
        glm::glm
        webgpu
        glfw3webgpu
        vivid-render3d
)

# Platform-specific settings
if(APPLE)
    # imgui_impl_wgpu.cpp needs Objective-C++ on macOS for Cocoa/CAMetalLayer
    # Use LANGUAGE property instead of compile flags for proper ObjC++ handling
    set_source_files_properties(
        ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
        PROPERTIES
            COMPILE_OPTIONS "-x;objective-c++"
            LANGUAGE CXX
    )

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

# Suppress warnings in third-party code
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        ${IMGUI_SOURCES}
        ${IMNODES_SOURCES}
        PROPERTIES COMPILE_FLAGS "-Wno-deprecated-declarations -Wno-unused-parameter"
    )
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

message(STATUS "[vivid-core] Configured with integrated ImGui/ImNodes")
