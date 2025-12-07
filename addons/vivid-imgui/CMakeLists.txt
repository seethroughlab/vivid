# Vivid ImGui Addon
# Provides Dear ImGui + ImNodes for visual chain editing

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

message(STATUS "[vivid-imgui] ImGui source: ${imgui_SOURCE_DIR}")
message(STATUS "[vivid-imgui] ImNodes source: ${imnodes_SOURCE_DIR}")

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

add_library(vivid-imgui SHARED
    src/imgui_integration.cpp
    src/chain_visualizer.cpp
    ${IMGUI_SOURCES}
    ${IMNODES_SOURCES}
)

target_include_directories(vivid-imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core/include
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imnodes_SOURCE_DIR}
)

# Use wgpu-native backend (not Dawn)
target_compile_definitions(vivid-imgui PRIVATE
    IMGUI_IMPL_WEBGPU_BACKEND_WGPU
)

target_link_libraries(vivid-imgui PUBLIC
    webgpu
    glfw
    glm
)

# Platform-specific settings
if(APPLE)
    # imgui_impl_wgpu.cpp needs Objective-C++ on macOS for Cocoa/CAMetalLayer
    set_source_files_properties(
        ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )

    # Link QuartzCore for CAMetalLayer
    target_link_libraries(vivid-imgui PRIVATE
        "-framework QuartzCore"
    )

    # Set install name for macOS
    set_target_properties(vivid-imgui PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Suppress warnings in third-party code
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(vivid-imgui PRIVATE
        -Wno-deprecated-declarations
        -Wno-unused-parameter
    )
endif()

# Copy library next to vivid executable
add_custom_command(TARGET vivid-imgui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:vivid-imgui>
        $<TARGET_FILE_DIR:vivid>/
    COMMENT "Copying vivid-imgui library"
)

message(STATUS "[vivid-imgui] Configured successfully")
