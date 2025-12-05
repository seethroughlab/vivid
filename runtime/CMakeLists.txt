cmake_minimum_required(VERSION 3.16)

project(vivid-runtime CXX)

set(VIVID_HEADERS
    include/vivid/vivid.h
    include/vivid/context.h
    include/vivid/chain.h
    include/vivid/operator.h
    include/vivid/texture_utils.h
    include/vivid/shader_utils.h
    include/vivid/operators.h
    # Phase 4: 3D support
    include/vivid/mesh.h
    include/vivid/camera.h
    include/vivid/pbr_material.h
    include/vivid/ibl.h
    include/vivid/gltf_model.h
    # Phase 5: Hot Reload
    include/vivid/hot_reload.h
    # Phase 7: VS Code Extension
    include/vivid/preview_server.h
    # Phase 2: Core operators
    include/vivid/operators/solid_color.h
    include/vivid/operators/noise.h
    include/vivid/operators/composite.h
    include/vivid/operators/blur.h
    include/vivid/operators/output.h
    # Phase 3: Additional 2D operators
    include/vivid/operators/passthrough.h
    include/vivid/operators/gradient.h
    include/vivid/operators/brightness_contrast.h
    include/vivid/operators/hsv.h
    include/vivid/operators/transform.h
    include/vivid/operators/feedback.h
    include/vivid/operators/edge_detect.h
    include/vivid/operators/displacement.h
    include/vivid/operators/chromatic_aberration.h
    include/vivid/operators/pixelate.h
    include/vivid/operators/mirror.h
    # Phase 4: 3D operators
    include/vivid/operators/render3d.h
    include/vivid/operators/gltf_viewer.h
)

set(VIVID_SOURCES
    src/main.cpp
    src/context.cpp
    src/chain.cpp
    src/texture_utils.cpp
    src/shader_utils.cpp
    src/operator.cpp
    # Phase 4: 3D support
    src/mesh.cpp
    src/camera.cpp
    src/pbr_material.cpp
    src/ibl.cpp
    src/gltf_model.cpp
    # Phase 5: Hot Reload
    src/hot_reload.cpp
    # Phase 6: Addon System
    src/addon_registry.cpp
    # Phase 7: VS Code Extension
    src/preview_server.cpp
    # Phase 2: Core operators
    src/operators/solid_color.cpp
    src/operators/noise.cpp
    src/operators/composite.cpp
    src/operators/blur.cpp
    src/operators/output.cpp
    # Phase 3: Additional 2D operators
    src/operators/passthrough.cpp
    src/operators/gradient.cpp
    src/operators/brightness_contrast.cpp
    src/operators/hsv.cpp
    src/operators/transform.cpp
    src/operators/feedback.cpp
    src/operators/edge_detect.cpp
    src/operators/displacement.cpp
    src/operators/chromatic_aberration.cpp
    src/operators/pixelate.cpp
    src/operators/mirror.cpp
    src/operators/shape.cpp
    # Phase 4: 3D operators
    src/operators/render3d.cpp
    src/operators/gltf_viewer.cpp
)

# Add macOS-specific sources
if(PLATFORM_MACOS)
    list(APPEND VIVID_SOURCES src/macos_helpers.mm)
endif()

# Create executable
if(PLATFORM_MACOS)
    add_executable(vivid MACOSX_BUNDLE ${VIVID_HEADERS} ${VIVID_SOURCES})
else()
    add_executable(vivid ${VIVID_HEADERS} ${VIVID_SOURCES})
endif()

# Include directories
target_include_directories(vivid
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(vivid
    PRIVATE
        Diligent-Common
        Diligent-GraphicsTools
        Diligent-TextureLoader
        Diligent-AssetLoader
        glfw
        glm::glm
        # Phase 7: VS Code Extension
        ixwebsocket
        nlohmann_json::nlohmann_json
)

# Link Vulkan backend
if(VULKAN_SUPPORTED)
    target_link_libraries(vivid PRIVATE Diligent-GraphicsEngineVk-shared)
    target_compile_definitions(vivid PRIVATE VULKAN_SUPPORTED=1)
endif()

# Link OpenGL backend as fallback
if(GL_SUPPORTED)
    target_link_libraries(vivid PRIVATE Diligent-GraphicsEngineOpenGL-shared)
    target_compile_definitions(vivid PRIVATE GL_SUPPORTED=1)
endif()

# Link DiligentFX for PBR support
target_link_libraries(vivid PRIVATE DiligentFX)

# Platform-specific settings
if(PLATFORM_MACOS)
    target_compile_definitions(vivid PRIVATE PLATFORM_MACOS=1)

    # Find and link Cocoa and QuartzCore frameworks
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore)
    target_link_libraries(vivid PRIVATE ${COCOA_FRAMEWORK} ${QUARTZCORE_FRAMEWORK})

    # Configure rpath for Vulkan library
    if(VULKAN_LIB_PATH)
        set_target_properties(vivid PROPERTIES
            BUILD_RPATH "${VULKAN_LIB_PATH}"
        )
    endif()
elseif(PLATFORM_WIN32)
    target_compile_definitions(vivid PRIVATE PLATFORM_WIN32=1 UNICODE)
    copy_required_dlls(vivid)
elseif(PLATFORM_LINUX)
    target_compile_definitions(vivid PRIVATE PLATFORM_LINUX=1)
endif()

# Set C++ standard
target_compile_features(vivid PUBLIC cxx_std_17)

# Set folder for IDE
set_target_properties(vivid PROPERTIES
    FOLDER Vivid
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
)

# Copy assets to build directory (shaders are now in assets/shaders)
add_custom_command(TARGET vivid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:vivid>/assets"
)
