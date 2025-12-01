# vivid-models addon
# Provides 3D model loading via Assimp
#
# Usage in project CMakeLists.txt:
#   include(path/to/vivid-models/addon.cmake)
#   vivid_use_models(your_target)

cmake_minimum_required(VERSION 3.20)

set(VIVID_MODELS_DIR ${CMAKE_CURRENT_LIST_DIR})

include(FetchContent)

# Assimp - 3D model loading
FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v5.4.3
)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(assimp)

# Function to add vivid-models to a target
function(vivid_use_models TARGET)
    target_sources(${TARGET} PRIVATE
        ${VIVID_MODELS_DIR}/src/model_loader.cpp
    )
    target_include_directories(${TARGET} PRIVATE
        ${VIVID_MODELS_DIR}/include
    )
    target_link_libraries(${TARGET} PRIVATE
        assimp
    )
endfunction()
