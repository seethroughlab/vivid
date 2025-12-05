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
    include/vivid/chain_visualizer.h
    # ImGui integration
    include/vivid/imgui/imgui_integration.h
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
    include/vivid/operators/instanced_render3d.h
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
    src/chain_visualizer.cpp
    # ImGui integration
    src/imgui_integration.cpp
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
    src/operators/instanced_render3d.cpp
)

# Add macOS-specific sources
if(PLATFORM_MACOS)
    list(APPEND VIVID_SOURCES src/macos_helpers.mm)
endif()

# Add ImGui sources directly for symbol export to hot-reloaded chains
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/external/DiligentEngine/DiligentTools/ThirdParty/imgui")
set(IMGUI_BACKENDS_DIR "${IMGUI_DIR}/backends")
list(APPEND VIVID_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_BACKENDS_DIR}/imgui_impl_glfw.cpp
)

# Add imnodes for node graph visualization
set(IMNODES_DIR "${CMAKE_SOURCE_DIR}/external/imnodes")
list(APPEND VIVID_SOURCES
    ${IMNODES_DIR}/imnodes.cpp
)

# Create executable (regular executable on all platforms for simpler CLI usage)
add_executable(vivid ${VIVID_HEADERS} ${VIVID_SOURCES})

# DiligentEngine directory for ImGuiImplDiligent
set(DILIGENT_DIR "${CMAKE_SOURCE_DIR}/external/DiligentEngine")

# Include directories
target_include_directories(vivid
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        # ImGui includes (for chain visualizer)
        ${IMGUI_DIR}
        ${IMGUI_BACKENDS_DIR}
        ${DILIGENT_DIR}/DiligentTools/Imgui/interface
        # imnodes for node graph visualization
        ${IMNODES_DIR}
)

# Link libraries
target_link_libraries(vivid
    PRIVATE
        Diligent-Common
        Diligent-GraphicsTools
        Diligent-TextureLoader
        Diligent-AssetLoader
        Diligent-Imgui  # For chain visualization
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

    # Configure rpath for Vulkan/MoltenVK libraries
    # volk uses dlopen() to load libvulkan.dylib - these paths let it find the library
    set(VULKAN_RPATH_DIRS
        "/opt/homebrew/lib"     # Homebrew on Apple Silicon
        "/usr/local/lib"        # Homebrew on Intel
    )

    # Check for LunarG Vulkan SDK (VULKAN_SDK env var or CMake var)
    if(NOT VULKAN_SDK AND DEFINED ENV{VULKAN_SDK})
        set(VULKAN_SDK $ENV{VULKAN_SDK})
    endif()
    if(VULKAN_SDK)
        # LunarG SDK has lib in different locations depending on version
        if(EXISTS "${VULKAN_SDK}/lib")
            list(APPEND VULKAN_RPATH_DIRS "${VULKAN_SDK}/lib")
        endif()
        if(EXISTS "${VULKAN_SDK}/macOS/lib")
            list(APPEND VULKAN_RPATH_DIRS "${VULKAN_SDK}/macOS/lib")
        endif()
        message(STATUS "[Vivid] Using VULKAN_SDK: ${VULKAN_SDK}")
    endif()

    # Also allow explicit VULKAN_LIB_PATH override
    if(VULKAN_LIB_PATH)
        list(INSERT VULKAN_RPATH_DIRS 0 "${VULKAN_LIB_PATH}")
    endif()

    set_target_properties(vivid PROPERTIES
        BUILD_RPATH "${VULKAN_RPATH_DIRS}"
    )


    message(STATUS "[Vivid] Vulkan RPATH: ${VULKAN_RPATH_DIRS}")
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
