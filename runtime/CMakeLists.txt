cmake_minimum_required(VERSION 3.16)

project(vivid-runtime CXX)

set(VIVID_HEADERS
    include/vivid/vivid.h
    include/vivid/context.h
    include/vivid/chain.h
    include/vivid/operator.h
    include/vivid/texture_utils.h
)

set(VIVID_SOURCES
    src/main.cpp
    src/context.cpp
    src/chain.cpp
    src/texture_utils.cpp
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
        glfw
        glm::glm
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

# Copy assets to build directory
add_custom_command(TARGET vivid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:vivid>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:vivid>/shaders"
)
