# Vivid Render3D Addon
# 3D rendering with procedural geometry and CSG operations

include(FetchContent)

# Fetch Manifold for CSG boolean operations
FetchContent_Declare(
    manifold
    GIT_REPOSITORY https://github.com/elalish/manifold.git
    GIT_TAG v3.0.0
    GIT_SHALLOW TRUE
)

# Configure Manifold options before fetching
set(MANIFOLD_PAR OFF CACHE BOOL "Disable parallel execution (no TBB dependency)" FORCE)
set(MANIFOLD_CROSS_SECTION OFF CACHE BOOL "Disable 2D cross-section support" FORCE)
set(MANIFOLD_TEST OFF CACHE BOOL "Disable Manifold tests" FORCE)
set(MANIFOLD_EXPORT OFF CACHE BOOL "Disable mesh export" FORCE)
set(MANIFOLD_DOWNLOADS OFF CACHE BOOL "Don't download dependencies" FORCE)

FetchContent_MakeAvailable(manifold)

# Enable symbol export on Windows
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_library(vivid-render3d SHARED
    src/mesh.cpp
    src/mesh_builder.cpp
    src/camera.cpp
    src/scene.cpp
    src/renderer.cpp
    src/material.cpp
    src/textured_material.cpp
)

target_include_directories(vivid-render3d PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/addons/vivid-effects-2d/include
)

target_link_libraries(vivid-render3d PUBLIC
    webgpu
    glm
    glfw
    manifold
    vivid-effects-2d
)

# Platform-specific settings
if(APPLE)
    set_target_properties(vivid-render3d PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Copy library next to vivid executable
add_custom_command(TARGET vivid-render3d POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:vivid-render3d>
        $<TARGET_FILE_DIR:vivid>/
    COMMENT "Copying vivid-render3d library"
)

# Copy shaders to build directory
file(GLOB RENDER3D_SHADERS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.wgsl)
if(RENDER3D_SHADERS)
    add_custom_command(TARGET vivid-render3d POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:vivid>/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RENDER3D_SHADERS} $<TARGET_FILE_DIR:vivid>/shaders/
        COMMENT "Copying render3d shaders"
    )
endif()
