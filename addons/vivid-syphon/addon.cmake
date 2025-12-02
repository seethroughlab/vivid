# vivid-syphon addon
# Provides Syphon texture sharing for macOS
#
# Usage in project CMakeLists.txt:
#   include(path/to/vivid-syphon/addon.cmake)
#   vivid_use_syphon(your_target)
#
# NOTE: macOS only. On other platforms, this addon does nothing.

cmake_minimum_required(VERSION 3.20)

# Only enable on macOS
if(NOT APPLE)
    message(STATUS "vivid-syphon: Skipping (macOS only)")
    function(vivid_use_syphon TARGET)
        # No-op on non-macOS
    endfunction()
    return()
endif()

set(VIVID_SYPHON_DIR ${CMAKE_CURRENT_LIST_DIR})

# Download and build Syphon framework from source if not present
set(SYPHON_FRAMEWORK_DIR "${CMAKE_BINARY_DIR}/_deps/syphon")
set(SYPHON_FRAMEWORK "${SYPHON_FRAMEWORK_DIR}/Syphon.framework")

if(NOT EXISTS "${SYPHON_FRAMEWORK}")
    message(STATUS "Building Syphon framework from source...")

    # Clone Syphon
    set(SYPHON_SRC_DIR "${CMAKE_BINARY_DIR}/_deps/syphon-src")
    if(NOT EXISTS "${SYPHON_SRC_DIR}")
        execute_process(
            COMMAND git clone --depth 1 https://github.com/Syphon/Syphon-Framework.git "${SYPHON_SRC_DIR}"
            RESULT_VARIABLE GIT_RESULT
        )
        if(NOT GIT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to clone Syphon-Framework")
        endif()
    endif()

    # Build Syphon using xcodebuild
    set(SYPHON_BUILD_DIR "${CMAKE_BINARY_DIR}/_deps/syphon-build")
    execute_process(
        COMMAND xcodebuild -project "${SYPHON_SRC_DIR}/Syphon.xcodeproj"
                          -scheme "Syphon"
                          -configuration Release
                          -arch ${CMAKE_SYSTEM_PROCESSOR}
                          ONLY_ACTIVE_ARCH=YES
                          CONFIGURATION_BUILD_DIR=${SYPHON_BUILD_DIR}
        RESULT_VARIABLE BUILD_RESULT
        OUTPUT_VARIABLE BUILD_OUTPUT
        ERROR_VARIABLE BUILD_ERROR
    )

    if(NOT BUILD_RESULT EQUAL 0)
        message(STATUS "Build output: ${BUILD_OUTPUT}")
        message(STATUS "Build error: ${BUILD_ERROR}")
        message(FATAL_ERROR "Failed to build Syphon framework")
    endif()

    # Copy the built framework
    file(MAKE_DIRECTORY "${SYPHON_FRAMEWORK_DIR}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${SYPHON_BUILD_DIR}/Syphon.framework"
                "${SYPHON_FRAMEWORK}"
    )

    if(EXISTS "${SYPHON_FRAMEWORK}")
        message(STATUS "Syphon framework built and installed to ${SYPHON_FRAMEWORK}")

        # Fix the framework's install name so it can be loaded from the rpath
        execute_process(
            COMMAND install_name_tool -id "@rpath/Syphon.framework/Versions/A/Syphon"
                    "${SYPHON_FRAMEWORK}/Versions/A/Syphon"
            RESULT_VARIABLE FIX_RESULT
        )
        if(FIX_RESULT EQUAL 0)
            message(STATUS "Fixed Syphon framework install name for rpath loading")
        endif()
    else()
        message(FATAL_ERROR "Syphon.framework not found after build")
    endif()
endif()

# Function to add vivid-syphon to a target
function(vivid_use_syphon TARGET)
    target_sources(${TARGET} PRIVATE
        ${VIVID_SYPHON_DIR}/src/syphon_server.mm
        ${VIVID_SYPHON_DIR}/src/syphon_client.mm
    )

    target_include_directories(${TARGET} PRIVATE
        ${VIVID_SYPHON_DIR}/include
        ${SYPHON_FRAMEWORK_DIR}
    )

    # Link Syphon framework
    target_link_libraries(${TARGET} PRIVATE
        "-framework Cocoa"
        "-framework OpenGL"
        "-framework IOSurface"
        "-F${SYPHON_FRAMEWORK_DIR}"
        "-framework Syphon"
    )

    # Set rpath so the framework can be found at runtime
    set_target_properties(${TARGET} PROPERTIES
        BUILD_RPATH "${SYPHON_FRAMEWORK_DIR}"
        INSTALL_RPATH "${SYPHON_FRAMEWORK_DIR}"
    )

    # Compile as Objective-C++
    set_source_files_properties(
        ${VIVID_SYPHON_DIR}/src/syphon_server.mm
        ${VIVID_SYPHON_DIR}/src/syphon_client.mm
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )

    # Define the framework path so runtime loading code can find it
    target_compile_definitions(${TARGET} PRIVATE
        VIVID_SYPHON_ENABLED=1
        SYPHON_FRAMEWORK_PATH="${SYPHON_FRAMEWORK}"
    )
endfunction()
