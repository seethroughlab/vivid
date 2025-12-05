# vivid-imgui addon CMakeLists.txt

cmake_minimum_required(VERSION 3.16)

project(vivid-imgui CXX)

# Sources
set(IMGUI_ADDON_SOURCES
    src/imgui_integration.cpp
)

set(IMGUI_ADDON_HEADERS
    include/vivid/imgui/imgui_integration.h
)

# ImGui sources from DiligentTools
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/external/DiligentEngine/DiligentTools/ThirdParty/imgui")
set(IMGUI_BACKENDS_DIR "${IMGUI_DIR}/backends")

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_BACKENDS_DIR}/imgui_impl_glfw.cpp
)

# Create static library
add_library(vivid-imgui STATIC
    ${IMGUI_ADDON_SOURCES}
    ${IMGUI_ADDON_HEADERS}
    ${IMGUI_SOURCES}
)

# Include directories
target_include_directories(vivid-imgui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/runtime/include
        ${IMGUI_DIR}
        ${IMGUI_BACKENDS_DIR}
)

# Link against DiligentTools ImGui implementation
target_link_libraries(vivid-imgui
    PRIVATE
        Diligent-Imgui
        Diligent-Common
        Diligent-GraphicsTools
        glfw
)

# Platform-specific settings
if(PLATFORM_MACOS)
    target_compile_definitions(vivid-imgui PRIVATE PLATFORM_MACOS=1)
elseif(PLATFORM_WIN32)
    target_compile_definitions(vivid-imgui PRIVATE PLATFORM_WIN32=1)
elseif(PLATFORM_LINUX)
    target_compile_definitions(vivid-imgui PRIVATE PLATFORM_LINUX=1)
endif()

# Set C++ standard
target_compile_features(vivid-imgui PUBLIC cxx_std_17)

# Set output directory
set_target_properties(vivid-imgui PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${VIVID_ADDONS_LIB_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${VIVID_ADDONS_LIB_DIR}"
    FOLDER "Vivid/Addons"
)

# Copy headers to output directory
add_custom_command(TARGET vivid-imgui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${VIVID_ADDONS_INCLUDE_DIR}"
)

# Also copy imgui.h so users can use ImGui directly
add_custom_command(TARGET vivid-imgui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${IMGUI_DIR}/imgui.h"
        "${VIVID_ADDONS_INCLUDE_DIR}/imgui.h"
)

# Copy addon.json to meta directory
add_custom_command(TARGET vivid-imgui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/addon.json"
        "${VIVID_ADDONS_META_DIR}/imgui.json"
)

message(STATUS "[vivid-imgui] Configured")
