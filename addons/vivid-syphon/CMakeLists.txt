# vivid-syphon addon
# Builds Syphon texture sharing as a static library (macOS only)
#
# This addon is built once during the main Vivid build.
# User projects automatically link against the pre-built library
# when they #include <vivid/syphon/syphon.h>

cmake_minimum_required(VERSION 3.20)

# Temporarily disabled - requires OBJCXX language support
# TODO: Fix OBJCXX compatibility with GLFW
message(STATUS "[vivid-syphon] Skipping (OBJCXX disabled)")
return()

# Only build on macOS
if(NOT APPLE)
    message(STATUS "[vivid-syphon] Skipping (macOS only)")
    return()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# === Fetch and Build Syphon Framework ===

set(SYPHON_FRAMEWORK_DIR "${CMAKE_BINARY_DIR}/_deps/syphon")
set(SYPHON_FRAMEWORK "${SYPHON_FRAMEWORK_DIR}/Syphon.framework")
set(SYPHON_SRC_DIR "${CMAKE_BINARY_DIR}/_deps/syphon-src")
set(SYPHON_BUILD_DIR "${CMAKE_BINARY_DIR}/_deps/syphon-build")

# Clone Syphon if needed
if(NOT EXISTS "${SYPHON_SRC_DIR}/Syphon.xcodeproj")
    message(STATUS "[vivid-syphon] Cloning Syphon-Framework...")
    execute_process(
        COMMAND git clone --depth 1 https://github.com/Syphon/Syphon-Framework.git "${SYPHON_SRC_DIR}"
        RESULT_VARIABLE GIT_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(NOT GIT_RESULT EQUAL 0)
        message(FATAL_ERROR "[vivid-syphon] Failed to clone Syphon-Framework")
    endif()
endif()

# Build Syphon framework if needed
if(NOT EXISTS "${SYPHON_FRAMEWORK}")
    message(STATUS "[vivid-syphon] Building Syphon framework...")

    file(MAKE_DIRECTORY "${SYPHON_FRAMEWORK_DIR}")

    # Build using xcodebuild
    execute_process(
        COMMAND xcodebuild -project "${SYPHON_SRC_DIR}/Syphon.xcodeproj"
                          -scheme "Syphon"
                          -configuration Release
                          -arch ${CMAKE_SYSTEM_PROCESSOR}
                          ONLY_ACTIVE_ARCH=YES
                          CONFIGURATION_BUILD_DIR=${SYPHON_BUILD_DIR}
        RESULT_VARIABLE BUILD_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )

    if(NOT BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "[vivid-syphon] Failed to build Syphon framework")
    endif()

    # Copy the built framework
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${SYPHON_BUILD_DIR}/Syphon.framework"
                "${SYPHON_FRAMEWORK}"
    )

    # Fix the framework's install name so it can be loaded from the rpath
    execute_process(
        COMMAND install_name_tool -id "@rpath/Syphon.framework/Versions/A/Syphon"
                "${SYPHON_FRAMEWORK}/Versions/A/Syphon"
        RESULT_VARIABLE FIX_RESULT
    )

    if(EXISTS "${SYPHON_FRAMEWORK}")
        message(STATUS "[vivid-syphon] Syphon framework built successfully")
    else()
        message(FATAL_ERROR "[vivid-syphon] Syphon.framework not found after build")
    endif()
endif()

# === Build vivid-syphon static library ===

add_library(vivid-syphon STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/syphon_server.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/src/syphon_client.mm
)

# Get vivid runtime include directory
set(VIVID_RUNTIME_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/runtime/include")

target_include_directories(vivid-syphon
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${SYPHON_FRAMEWORK_DIR}
        ${VIVID_RUNTIME_INCLUDE_DIR}
)

# Link against frameworks
target_link_libraries(vivid-syphon
    PUBLIC
        "-framework Cocoa"
        "-framework OpenGL"
        "-framework IOSurface"
        "-F${SYPHON_FRAMEWORK_DIR}"
        "-framework Syphon"
        glm::glm
)

# Compile Objective-C++ sources correctly
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/syphon_server.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/src/syphon_client.mm
    PROPERTIES COMPILE_FLAGS "-x objective-c++"
)

# Set output directory to addons/lib
set_target_properties(vivid-syphon PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${VIVID_ADDONS_LIB_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${VIVID_ADDONS_LIB_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${VIVID_ADDONS_LIB_DIR}"
)

# === Install addon files ===

# Copy headers to addons/include
add_custom_command(TARGET vivid-syphon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${VIVID_ADDONS_INCLUDE_DIR}"
    COMMENT "[vivid-syphon] Installing headers"
)

# Copy addon.json to addons/meta
add_custom_command(TARGET vivid-syphon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/addon.json"
        "${VIVID_ADDONS_META_DIR}/syphon.json"
    COMMENT "[vivid-syphon] Installing addon metadata"
)

# Copy Syphon.framework to addons/lib
add_custom_command(TARGET vivid-syphon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${SYPHON_FRAMEWORK}"
        "${VIVID_ADDONS_LIB_DIR}/Syphon.framework"
    COMMENT "[vivid-syphon] Installing Syphon.framework"
)

message(STATUS "[vivid-syphon] Configured successfully")
