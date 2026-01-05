# Vivid ImGui Library
# Provides Dear ImGui integration for user chains
#
# This addon is optional - the core chain visualizer works without it.
# Load this addon if you want to use ImGui in your chains.

include(FetchContent)

# Fetch Dear ImGui
FetchContent_Declare(
    imgui_gui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR nonexistent  # We manually specify source files
)
FetchContent_MakeAvailable(imgui_gui)

message(STATUS "[vivid-imgui] ImGui source: ${imgui_gui_SOURCE_DIR}")

# ImGui sources
set(IMGUI_SOURCES
    ${imgui_gui_SOURCE_DIR}/imgui.cpp
    ${imgui_gui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_gui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_gui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_gui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_gui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
    ${imgui_gui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
)

# Addon sources
set(GUI_SOURCES
    src/imgui_integration.cpp
)

add_library(vivid-imgui SHARED
    ${GUI_SOURCES}
    ${IMGUI_SOURCES}
)

target_include_directories(vivid-imgui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${imgui_gui_SOURCE_DIR}
        ${imgui_gui_SOURCE_DIR}/backends
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/vivid-core/include
)

# ImGui backend configuration
target_compile_definitions(vivid-imgui PUBLIC
    IMGUI_IMPL_WEBGPU_BACKEND_WGPU
)

# Note: vivid-core already links webgpu, glfw, glm - we just need vivid-core
target_link_libraries(vivid-imgui PUBLIC
    vivid-core
)

# Suppress warnings in ImGui code
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        ${IMGUI_SOURCES}
        PROPERTIES COMPILE_FLAGS "-Wno-deprecated-declarations -Wno-unused-parameter"
    )
endif()

# Platform-specific configuration
if(APPLE)
    set_target_properties(vivid-imgui PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
    # imgui_impl_wgpu.cpp needs Objective-C++ on macOS for Cocoa/CAMetalLayer
    set_source_files_properties(
        ${imgui_gui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
        PROPERTIES
            COMPILE_OPTIONS "-x;objective-c++"
            LANGUAGE CXX
    )
elseif(WIN32)
    # Export all symbols to generate import library for hot-reload chains
    set_target_properties(vivid-imgui PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/Debug
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/Release
    )
    # Copy DLL to bin directory
    add_custom_command(TARGET vivid-imgui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:vivid-imgui>
            ${CMAKE_BINARY_DIR}/bin/
        COMMENT "Copying vivid-imgui DLL"
    )
endif()

# Copy to lib directory for consistency
add_custom_command(TARGET vivid-imgui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:vivid-imgui>
        ${CMAKE_BINARY_DIR}/lib/
    COMMENT "Copying vivid-imgui library"
)

# Copy ImGui headers to build directory so user chains can include them
# Hot-reload needs these when compiling user chains that use <vivid/gui/imgui.h>
add_custom_command(TARGET vivid-imgui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/include/imgui
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${imgui_gui_SOURCE_DIR}/imgui.h
        ${imgui_gui_SOURCE_DIR}/imconfig.h
        ${imgui_gui_SOURCE_DIR}/imgui_internal.h
        ${imgui_gui_SOURCE_DIR}/imstb_rectpack.h
        ${imgui_gui_SOURCE_DIR}/imstb_textedit.h
        ${imgui_gui_SOURCE_DIR}/imstb_truetype.h
        ${CMAKE_BINARY_DIR}/include/imgui/
    COMMENT "Copying ImGui headers for user chains"
)

message(STATUS "[vivid-imgui] Configured with Dear ImGui")
