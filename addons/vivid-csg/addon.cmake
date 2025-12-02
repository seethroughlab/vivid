# vivid-csg addon
# Provides CSG (Constructive Solid Geometry) boolean operations via Manifold
#
# Usage in project CMakeLists.txt:
#   include(path/to/vivid-csg/addon.cmake)
#   vivid_use_csg(your_target)

cmake_minimum_required(VERSION 3.20)

set(VIVID_CSG_DIR ${CMAKE_CURRENT_LIST_DIR})

include(FetchContent)

# Manifold - Fast, robust CSG boolean operations
# https://github.com/elalish/manifold
FetchContent_Declare(
    manifold
    GIT_REPOSITORY https://github.com/elalish/manifold.git
    GIT_TAG v3.0.1
)

# Manifold build options
set(MANIFOLD_TEST OFF CACHE BOOL "" FORCE)
set(MANIFOLD_CROSS_SECTION OFF CACHE BOOL "" FORCE)
set(MANIFOLD_EXPORT OFF CACHE BOOL "" FORCE)
set(MANIFOLD_PAR OFF CACHE BOOL "" FORCE)  # Disable TBB for simpler builds

FetchContent_MakeAvailable(manifold)

# Function to add vivid-csg to a target
function(vivid_use_csg TARGET)
    target_sources(${TARGET} PRIVATE
        ${VIVID_CSG_DIR}/src/csg.cpp
    )
    target_include_directories(${TARGET} PRIVATE
        ${VIVID_CSG_DIR}/include
    )
    target_link_libraries(${TARGET} PRIVATE
        manifold
    )
endfunction()
