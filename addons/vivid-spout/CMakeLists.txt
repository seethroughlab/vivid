# vivid-spout addon
# Builds Spout texture sharing as a static library (Windows only)
#
# This addon is built once during the main Vivid build.
# User projects automatically link against the pre-built library
# when they #include <vivid/spout/spout.h>

cmake_minimum_required(VERSION 3.20)

# Only build on Windows
if(NOT WIN32)
    message(STATUS "[vivid-spout] Skipping (Windows only)")
    return()
endif()

project(vivid-spout VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# === Fetch and Build Spout2 SDK ===

set(SPOUT_SDK_DIR "${CMAKE_BINARY_DIR}/_deps/spout2-sdk")
set(SPOUT_SRC_DIR "${CMAKE_BINARY_DIR}/_deps/spout2-src")
set(SPOUT_BUILD_DIR "${CMAKE_BINARY_DIR}/_deps/spout2-build")

# Clone Spout2 if needed
if(NOT EXISTS "${SPOUT_SRC_DIR}/SPOUTSDK")
    message(STATUS "[vivid-spout] Cloning Spout2 SDK...")
    execute_process(
        COMMAND git clone --depth 1 https://github.com/leadedge/Spout2.git "${SPOUT_SRC_DIR}"
        RESULT_VARIABLE GIT_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(NOT GIT_RESULT EQUAL 0)
        message(FATAL_ERROR "[vivid-spout] Failed to clone Spout2 SDK")
    endif()
endif()

# Build SpoutLibrary if needed
set(SPOUT_INCLUDE_DIR "${SPOUT_SDK_DIR}/include")
set(SPOUT_LIB_DIR "${SPOUT_SDK_DIR}/lib")
set(SPOUT_DLL "${SPOUT_LIB_DIR}/SpoutLibrary.dll")
set(SPOUT_LIB "${SPOUT_LIB_DIR}/SpoutLibrary.lib")

if(NOT EXISTS "${SPOUT_DLL}" OR NOT EXISTS "${SPOUT_LIB}")
    message(STATUS "[vivid-spout] Building SpoutLibrary...")

    file(MAKE_DIRECTORY "${SPOUT_BUILD_DIR}")
    file(MAKE_DIRECTORY "${SPOUT_SDK_DIR}")
    file(MAKE_DIRECTORY "${SPOUT_INCLUDE_DIR}")
    file(MAKE_DIRECTORY "${SPOUT_LIB_DIR}")

    # Configure Spout2
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -S "${SPOUT_SRC_DIR}"
            -B "${SPOUT_BUILD_DIR}"
            -DCMAKE_BUILD_TYPE=Release
            -DSPOUT_BUILD_LIBRARY=ON
            -DSPOUT_BUILD_SPOUTDX=OFF
            -DSKIP_INSTALL_ALL=ON
        RESULT_VARIABLE CMAKE_CONFIG_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )

    if(NOT CMAKE_CONFIG_RESULT EQUAL 0)
        message(FATAL_ERROR "[vivid-spout] Failed to configure SpoutLibrary")
    endif()

    # Build SpoutLibrary
    execute_process(
        COMMAND ${CMAKE_COMMAND} --build "${SPOUT_BUILD_DIR}" --config Release --target SpoutLibrary
        RESULT_VARIABLE CMAKE_BUILD_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )

    if(NOT CMAKE_BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "[vivid-spout] Failed to build SpoutLibrary")
    endif()

    # Find and copy built files
    set(POSSIBLE_DLL_PATHS
        "${SPOUT_BUILD_DIR}/Release/SpoutLibrary.dll"
        "${SPOUT_BUILD_DIR}/SpoutLibrary.dll"
        "${SPOUT_BUILD_DIR}/bin/Release/SpoutLibrary.dll"
        "${SPOUT_BUILD_DIR}/SPOUTSDK/SpoutLibrary/Release/SpoutLibrary.dll"
    )

    set(POSSIBLE_LIB_PATHS
        "${SPOUT_BUILD_DIR}/Release/SpoutLibrary.lib"
        "${SPOUT_BUILD_DIR}/SpoutLibrary.lib"
        "${SPOUT_BUILD_DIR}/lib/Release/SpoutLibrary.lib"
        "${SPOUT_BUILD_DIR}/SPOUTSDK/SpoutLibrary/Release/SpoutLibrary.lib"
    )

    foreach(DLL_PATH ${POSSIBLE_DLL_PATHS})
        if(EXISTS "${DLL_PATH}")
            file(COPY "${DLL_PATH}" DESTINATION "${SPOUT_LIB_DIR}")
            break()
        endif()
    endforeach()

    foreach(LIB_PATH ${POSSIBLE_LIB_PATHS})
        if(EXISTS "${LIB_PATH}")
            file(COPY "${LIB_PATH}" DESTINATION "${SPOUT_LIB_DIR}")
            break()
        endif()
    endforeach()

    # Copy header
    file(COPY "${SPOUT_SRC_DIR}/SPOUTSDK/SpoutLibrary/SpoutLibrary.h"
         DESTINATION "${SPOUT_INCLUDE_DIR}")

    message(STATUS "[vivid-spout] SpoutLibrary built successfully")
endif()

# Verify Spout files exist
if(NOT EXISTS "${SPOUT_LIB}")
    message(FATAL_ERROR "[vivid-spout] SpoutLibrary.lib not found")
endif()

# === Build vivid-spout static library ===

add_library(vivid-spout STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/spout_sender.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/spout_receiver.cpp
)

# Get vivid runtime include directory
set(VIVID_RUNTIME_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/runtime/include")

target_include_directories(vivid-spout
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${SPOUT_INCLUDE_DIR}
        ${VIVID_RUNTIME_INCLUDE_DIR}
)

# Link against SpoutLibrary and glm (consumers will need this too)
target_link_libraries(vivid-spout
    PUBLIC
        "${SPOUT_LIB}"
        opengl32
        glm::glm
)

# Set output directory to addons/lib
set_target_properties(vivid-spout PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${VIVID_ADDONS_LIB_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${VIVID_ADDONS_LIB_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${VIVID_ADDONS_LIB_DIR}"
)

# === Install addon files ===

# Copy headers to addons/include
add_custom_command(TARGET vivid-spout POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${VIVID_ADDONS_INCLUDE_DIR}"
    COMMENT "[vivid-spout] Installing headers"
)

# Copy addon.json to addons/meta
add_custom_command(TARGET vivid-spout POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/addon.json"
        "${VIVID_ADDONS_META_DIR}/spout.json"
    COMMENT "[vivid-spout] Installing addon metadata"
)

# Copy SpoutLibrary.dll and SpoutLibrary.lib to addons/lib (needed at link and runtime)
add_custom_command(TARGET vivid-spout POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SPOUT_DLL}"
        "${VIVID_ADDONS_LIB_DIR}/SpoutLibrary.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SPOUT_LIB}"
        "${VIVID_ADDONS_LIB_DIR}/SpoutLibrary.lib"
    COMMENT "[vivid-spout] Installing SpoutLibrary files"
)

message(STATUS "[vivid-spout] Configured successfully")
