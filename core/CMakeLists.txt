# Vivid Core - Runtime executable
# Includes: window, timing, input, hot-reload, display, chain visualization (ImGui)

include(FetchContent)

# Fetch ImGui from master (required for wgpu-native v24.x compatibility)
# We manually specify source files, so use SOURCE_SUBDIR to skip its CMakeLists
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR nonexistent  # Prevents CMake from processing imgui's CMakeLists.txt
)
FetchContent_MakeAvailable(imgui)

# Fetch ImNodes for node graph editor
# We manually specify source files, so use SOURCE_SUBDIR to skip its CMakeLists
FetchContent_Declare(
    imnodes
    GIT_REPOSITORY https://github.com/Nelarius/imnodes.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR nonexistent  # Prevents CMake from processing imnodes's CMakeLists.txt
)
FetchContent_MakeAvailable(imnodes)

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

# Vivid core library sources (shared by executable and addons)
set(VIVID_CORE_LIB_SOURCES
    src/context.cpp
    src/chain.cpp
    src/operator.cpp
    src/audio_operator.cpp
    src/audio_output.cpp
    src/audio_graph.cpp
)

# Vivid runtime sources (executable only)
set(VIVID_CORE_SOURCES
    src/main.cpp
    src/display.cpp
    src/hot_reload.cpp
    src/addon_registry.cpp
    src/editor_bridge.cpp
    imgui/imgui_integration.cpp
    imgui/chain_visualizer.cpp
)

# Platform-specific sources
if(APPLE)
    list(APPEND VIVID_CORE_SOURCES src/video_exporter.mm)
elseif(WIN32)
    list(APPEND VIVID_CORE_SOURCES src/video_exporter_win.cpp)
elseif(UNIX)
    list(APPEND VIVID_CORE_SOURCES src/video_exporter_linux.cpp)
endif()

set(VIVID_CORE_HEADERS
    include/vivid/vivid.h
    include/vivid/context.h
    include/vivid/display.h
    include/vivid/hot_reload.h
    include/vivid/operator.h
    include/vivid/chain.h
    include/vivid/addon_registry.h
    include/vivid/video_exporter.h
    include/vivid/audio_buffer.h
    include/vivid/audio_operator.h
    include/vivid/audio_output.h
    include/vivid/editor_bridge.h
    imgui/imgui_integration.h
    imgui/chain_visualizer.h
)

# Core library (shared between executable and addons)
add_library(vivid-core SHARED
    ${VIVID_CORE_LIB_SOURCES}
)

target_include_directories(vivid-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(vivid-core PUBLIC
    webgpu
    glm::glm
    glfw
)

# Platform-specific settings for vivid-core
if(APPLE)
    set_target_properties(vivid-core PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(WIN32)
    # Export all symbols to generate import library (.lib)
    set_target_properties(vivid-core PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/Debug
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/Release
    )
endif()

# Copy vivid-core library next to vivid executable
add_custom_command(TARGET vivid-core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:vivid-core>
        ${CMAKE_BINARY_DIR}/bin/
    COMMENT "Copying vivid-core library"
)

# On Windows, copy GLFW DLL to bin directory (when built as shared library)
if(WIN32)
    add_custom_command(TARGET vivid-core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:glfw>
            ${CMAKE_BINARY_DIR}/bin/
        COMMENT "Copying GLFW DLL"
    )
endif()

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
        ${CMAKE_SOURCE_DIR}/addons/vivid-audio/include
        ${stb_SOURCE_DIR}
)

# Use wgpu-native backend for ImGui
target_compile_definitions(vivid PRIVATE
    IMGUI_IMPL_WEBGPU_BACKEND_WGPU
)

# Link against core library, GLFW-WebGPU integration, render3d for geometry previews, audio for analyzer type info, and ixwebsocket for EditorBridge
target_link_libraries(vivid
    PRIVATE
        vivid-core
        glfw3webgpu
        vivid-render3d
        vivid-audio
        ixwebsocket
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
        "-framework VideoToolbox"
        "-framework AVFoundation"
        "-framework CoreMedia"
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
