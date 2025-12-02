# vivid-spout addon
# Provides Spout texture sharing for Windows
#
# Usage in project CMakeLists.txt:
#   include(path/to/vivid-spout/addon.cmake)
#   vivid_use_spout(your_target)
#
# NOTE: Windows only. On other platforms, this addon does nothing.

cmake_minimum_required(VERSION 3.20)

# Only enable on Windows
if(NOT WIN32)
    message(STATUS "vivid-spout: Skipping (Windows only)")
    function(vivid_use_spout TARGET)
        # No-op on non-Windows
    endfunction()
    return()
endif()

set(VIVID_SPOUT_DIR ${CMAKE_CURRENT_LIST_DIR})

# Download and build Spout2 SDK if not present
set(SPOUT_SDK_DIR "${CMAKE_BINARY_DIR}/_deps/spout2")
set(SPOUT_SRC_DIR "${CMAKE_BINARY_DIR}/_deps/spout2-src")
set(SPOUT_BUILD_DIR "${CMAKE_BINARY_DIR}/_deps/spout2-build")
set(SPOUT_INCLUDE_DIR "${SPOUT_SDK_DIR}/include")
set(SPOUT_LIB_DIR "${SPOUT_SDK_DIR}/lib")

# Clone Spout2 if needed
if(NOT EXISTS "${SPOUT_SRC_DIR}/SPOUTSDK")
    message(STATUS "Cloning Spout2 SDK...")
    execute_process(
        COMMAND git clone --depth 1 https://github.com/leadedge/Spout2.git "${SPOUT_SRC_DIR}"
        RESULT_VARIABLE GIT_RESULT
        OUTPUT_VARIABLE GIT_OUTPUT
        ERROR_VARIABLE GIT_ERROR
    )
    if(NOT GIT_RESULT EQUAL 0)
        message(STATUS "Git error: ${GIT_ERROR}")
        message(FATAL_ERROR "Failed to clone Spout2 SDK")
    endif()
endif()

# Check if we need to build SpoutLibrary
set(SPOUT_DLL "${SPOUT_LIB_DIR}/SpoutLibrary.dll")
set(SPOUT_LIB "${SPOUT_LIB_DIR}/SpoutLibrary.lib")

if(NOT EXISTS "${SPOUT_DLL}" OR NOT EXISTS "${SPOUT_LIB}")
    message(STATUS "Building SpoutLibrary from source...")

    file(MAKE_DIRECTORY "${SPOUT_BUILD_DIR}")
    file(MAKE_DIRECTORY "${SPOUT_SDK_DIR}")
    file(MAKE_DIRECTORY "${SPOUT_INCLUDE_DIR}")
    file(MAKE_DIRECTORY "${SPOUT_LIB_DIR}")

    # Configure from the top-level CMakeLists.txt (builds SpoutGL + SpoutLibrary)
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -S "${SPOUT_SRC_DIR}"
            -B "${SPOUT_BUILD_DIR}"
            -DCMAKE_BUILD_TYPE=Release
            -DSPOUT_BUILD_LIBRARY=ON
            -DSPOUT_BUILD_SPOUTDX=OFF
            -DSKIP_INSTALL_ALL=ON
        RESULT_VARIABLE CMAKE_CONFIG_RESULT
        OUTPUT_VARIABLE CMAKE_CONFIG_OUTPUT
        ERROR_VARIABLE CMAKE_CONFIG_ERROR
    )

    if(NOT CMAKE_CONFIG_RESULT EQUAL 0)
        message(STATUS "CMake config output: ${CMAKE_CONFIG_OUTPUT}")
        message(STATUS "CMake config error: ${CMAKE_CONFIG_ERROR}")
        message(FATAL_ERROR "Failed to configure SpoutLibrary")
    endif()

    # Build SpoutLibrary (will also build its dependency Spout_static)
    execute_process(
        COMMAND ${CMAKE_COMMAND} --build "${SPOUT_BUILD_DIR}" --config Release --target SpoutLibrary
        RESULT_VARIABLE CMAKE_BUILD_RESULT
        OUTPUT_VARIABLE CMAKE_BUILD_OUTPUT
        ERROR_VARIABLE CMAKE_BUILD_ERROR
    )

    if(NOT CMAKE_BUILD_RESULT EQUAL 0)
        message(STATUS "CMake build output: ${CMAKE_BUILD_OUTPUT}")
        message(STATUS "CMake build error: ${CMAKE_BUILD_ERROR}")
        message(FATAL_ERROR "Failed to build SpoutLibrary")
    endif()

    # Find and copy the built files
    # Try multiple possible output locations
    set(POSSIBLE_DLL_PATHS
        "${SPOUT_BUILD_DIR}/Release/SpoutLibrary.dll"
        "${SPOUT_BUILD_DIR}/SpoutLibrary.dll"
        "${SPOUT_BUILD_DIR}/bin/Release/SpoutLibrary.dll"
        "${SPOUT_BUILD_DIR}/bin/SpoutLibrary.dll"
        "${SPOUT_BUILD_DIR}/SPOUTSDK/SpoutLibrary/Release/SpoutLibrary.dll"
    )

    set(POSSIBLE_LIB_PATHS
        "${SPOUT_BUILD_DIR}/Release/SpoutLibrary.lib"
        "${SPOUT_BUILD_DIR}/SpoutLibrary.lib"
        "${SPOUT_BUILD_DIR}/lib/Release/SpoutLibrary.lib"
        "${SPOUT_BUILD_DIR}/lib/SpoutLibrary.lib"
        "${SPOUT_BUILD_DIR}/SPOUTSDK/SpoutLibrary/Release/SpoutLibrary.lib"
    )

    set(FOUND_DLL "")
    foreach(DLL_PATH ${POSSIBLE_DLL_PATHS})
        if(EXISTS "${DLL_PATH}")
            set(FOUND_DLL "${DLL_PATH}")
            break()
        endif()
    endforeach()

    set(FOUND_LIB "")
    foreach(LIB_PATH ${POSSIBLE_LIB_PATHS})
        if(EXISTS "${LIB_PATH}")
            set(FOUND_LIB "${LIB_PATH}")
            break()
        endif()
    endforeach()

    if(NOT FOUND_DLL)
        # List the build directory to help debug
        file(GLOB_RECURSE ALL_DLLS "${SPOUT_BUILD_DIR}/*.dll")
        message(STATUS "Found DLLs in build dir: ${ALL_DLLS}")
        message(FATAL_ERROR "Could not find SpoutLibrary.dll after build")
    endif()

    if(NOT FOUND_LIB)
        file(GLOB_RECURSE ALL_LIBS "${SPOUT_BUILD_DIR}/*.lib")
        message(STATUS "Found LIBs in build dir: ${ALL_LIBS}")
        message(FATAL_ERROR "Could not find SpoutLibrary.lib after build")
    endif()

    # Copy to our SDK directory
    file(COPY "${FOUND_DLL}" DESTINATION "${SPOUT_LIB_DIR}")
    file(COPY "${FOUND_LIB}" DESTINATION "${SPOUT_LIB_DIR}")

    # Copy headers
    file(COPY "${SPOUT_SRC_DIR}/SPOUTSDK/SpoutLibrary/SpoutLibrary.h"
         DESTINATION "${SPOUT_INCLUDE_DIR}")

    message(STATUS "SpoutLibrary built and installed to ${SPOUT_SDK_DIR}")
endif()

# Verify files exist
if(NOT EXISTS "${SPOUT_DLL}")
    message(FATAL_ERROR "SpoutLibrary.dll not found at ${SPOUT_DLL}")
endif()

if(NOT EXISTS "${SPOUT_LIB}")
    message(FATAL_ERROR "SpoutLibrary.lib not found at ${SPOUT_LIB}")
endif()

# Function to add vivid-spout to a target
function(vivid_use_spout TARGET)
    target_sources(${TARGET} PRIVATE
        ${VIVID_SPOUT_DIR}/src/spout_sender.cpp
        ${VIVID_SPOUT_DIR}/src/spout_receiver.cpp
    )

    target_include_directories(${TARGET} PRIVATE
        ${VIVID_SPOUT_DIR}/include
        ${SPOUT_INCLUDE_DIR}
    )

    # Link SpoutLibrary
    target_link_libraries(${TARGET} PRIVATE
        "${SPOUT_LIB}"
        opengl32
    )

    # Define that Spout is enabled
    target_compile_definitions(${TARGET} PRIVATE
        VIVID_SPOUT_ENABLED=1
    )

    # Copy SpoutLibrary.dll to output directory
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SPOUT_DLL}"
            "$<TARGET_FILE_DIR:${TARGET}>"
        COMMENT "Copying SpoutLibrary.dll"
    )
endfunction()
