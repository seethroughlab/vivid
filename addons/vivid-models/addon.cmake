# vivid-models addon
# Provides 3D model loading (Assimp) and skeletal animation (ozz-animation)
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

# ozz-animation - Skeletal animation runtime
FetchContent_Declare(
    ozz-animation
    GIT_REPOSITORY https://github.com/guillaumeblanc/ozz-animation.git
    GIT_TAG 0.15.0
)
set(ozz_build_samples OFF CACHE BOOL "" FORCE)
set(ozz_build_howtos OFF CACHE BOOL "" FORCE)
set(ozz_build_tests OFF CACHE BOOL "" FORCE)
set(ozz_build_tools OFF CACHE BOOL "" FORCE)
set(ozz_build_fbx OFF CACHE BOOL "" FORCE)
set(ozz_build_gltf OFF CACHE BOOL "" FORCE)
set(ozz_build_data OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(assimp ozz-animation)

# Function to add vivid-models to a target
function(vivid_use_models TARGET)
    target_sources(${TARGET} PRIVATE
        ${VIVID_MODELS_DIR}/src/model_loader.cpp
        ${VIVID_MODELS_DIR}/src/animation_system.cpp
    )
    target_include_directories(${TARGET} PRIVATE
        ${VIVID_MODELS_DIR}/include
    )
    target_link_libraries(${TARGET} PRIVATE
        assimp
        ozz_animation
        ozz_animation_offline
        ozz_base
    )
endfunction()
