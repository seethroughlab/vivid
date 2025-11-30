# vivid-nuklear addon
# Provides Nuklear immediate-mode GUI integration
#
# Usage in project CMakeLists.txt:
#   include(path/to/vivid-nuklear/addon.cmake)
#   vivid_use_nuklear(your_target)
#
# Unlike ImGUI, Nuklear is a single-header ANSI C library with no WebGPU
# API compatibility issues. This addon uses software rendering to a
# framebuffer that gets uploaded to a GPU texture.

cmake_minimum_required(VERSION 3.20)

set(VIVID_NUKLEAR_DIR ${CMAKE_CURRENT_LIST_DIR})

include(FetchContent)

# Fetch Nuklear - single header library
# Nuklear includes its own embedded stb_truetype and stb_rect_pack for font baking
FetchContent_Declare(
    nuklear
    GIT_REPOSITORY https://github.com/Immediate-Mode-UI/Nuklear.git
    GIT_TAG master
)
FetchContent_MakeAvailable(nuklear)

# Function to add vivid-nuklear to a target
function(vivid_use_nuklear TARGET)
    target_sources(${TARGET} PRIVATE
        ${VIVID_NUKLEAR_DIR}/src/nuklear_integration.cpp
    )
    target_include_directories(${TARGET} PRIVATE
        ${VIVID_NUKLEAR_DIR}/include
        ${nuklear_SOURCE_DIR}
    )
endfunction()
