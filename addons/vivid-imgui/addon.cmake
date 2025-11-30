# vivid-imgui addon
# Provides Dear ImGui integration for Vivid operators
#
# NOTE: This addon is a placeholder. Full implementation requires
# WebGPU backend integration work.
#
# Usage in project CMakeLists.txt:
#   include(path/to/vivid-imgui/addon.cmake)
#   vivid_use_imgui(your_target)

cmake_minimum_required(VERSION 3.20)

set(VIVID_IMGUI_DIR ${CMAKE_CURRENT_LIST_DIR})

include(FetchContent)

# Dear ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5
)
FetchContent_MakeAvailable(imgui)

# Function to add vivid-imgui to a target
function(vivid_use_imgui TARGET)
    # TODO: Add implementation files when WebGPU integration is complete
    # target_sources(${TARGET} PRIVATE
    #     ${VIVID_IMGUI_DIR}/src/imgui_integration.cpp
    # )
    target_include_directories(${TARGET} PRIVATE
        ${VIVID_IMGUI_DIR}/include
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    message(STATUS "vivid-imgui: Note - Full implementation pending WebGPU integration")
endfunction()
