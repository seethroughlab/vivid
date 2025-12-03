set(RUNTIME_SOURCES
    src/main.cpp
    src/diligent_renderer.cpp
    src/shader_utils.cpp
    src/texture_utils.cpp
    src/mesh.cpp
    src/camera.cpp
)

# macOS-specific Objective-C++ sources
if(APPLE)
    list(APPEND RUNTIME_SOURCES src/macos_helpers.mm)
endif()

set(RUNTIME_HEADERS
    include/vivid/diligent_renderer.h
)

add_executable(vivid-runtime
    ${RUNTIME_SOURCES}
    ${RUNTIME_HEADERS}
)

target_include_directories(vivid-runtime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link to Diligent targets - these should propagate include directories
target_link_libraries(vivid-runtime PRIVATE
    glfw
    glm::glm
    Diligent-GraphicsEngineVk-static
    Diligent-Common
    Diligent-GraphicsTools
    Diligent-TextureLoader
    DiligentFX
)

# Let Diligent targets provide include directories through target properties
target_link_libraries(vivid-runtime PRIVATE
    Diligent-PublicBuildSettings
)

# Additional include paths for internal headers not exposed by targets
target_include_directories(vivid-runtime PRIVATE
    ${diligentengine_SOURCE_DIR}/DiligentCore/Graphics/GraphicsEngine/include
    ${diligentengine_SOURCE_DIR}/DiligentFX/PBR/interface
    ${diligentengine_SOURCE_DIR}/DiligentFX
    ${diligentengine_SOURCE_DIR}/DiligentTools/ThirdParty/stb
)

# Copy shaders to build directory
add_custom_command(TARGET vivid-runtime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/shaders
    $<TARGET_FILE_DIR:vivid-runtime>/shaders
)

# macOS specific frameworks
if(APPLE)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    target_link_libraries(vivid-runtime PRIVATE
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${COCOA_FRAMEWORK}
    )
endif()

# macOS specific: Set environment variables for MoltenVK
if(APPLE)
    # Find MoltenVK paths
    find_path(MOLTENVK_ICD_PATH
        NAMES MoltenVK_icd.json
        PATHS
            /opt/homebrew/etc/vulkan/icd.d
            /usr/local/etc/vulkan/icd.d
            /opt/homebrew/share/vulkan/icd.d
            /usr/local/share/vulkan/icd.d
    )

    find_path(MOLTENVK_LIB_PATH
        NAMES libMoltenVK.dylib
        PATHS
            /opt/homebrew/lib
            /usr/local/lib
    )

    if(MOLTENVK_ICD_PATH AND MOLTENVK_LIB_PATH)
        message(STATUS "MoltenVK ICD found at: ${MOLTENVK_ICD_PATH}")
        message(STATUS "MoltenVK library found at: ${MOLTENVK_LIB_PATH}")

        # Create a run script with proper environment
        file(WRITE ${CMAKE_BINARY_DIR}/run-vivid.sh
"#!/bin/bash
export VK_ICD_FILENAMES=${MOLTENVK_ICD_PATH}/MoltenVK_icd.json
export DYLD_LIBRARY_PATH=${MOLTENVK_LIB_PATH}:\$DYLD_LIBRARY_PATH
cd \"$(dirname \"$0\")/runtime\"
./vivid-runtime \"$@\"
")
        execute_process(COMMAND chmod +x ${CMAKE_BINARY_DIR}/run-vivid.sh)
    else()
        message(WARNING "MoltenVK not found. Install via: brew install molten-vk")
    endif()
endif()
