# vivid-syphon addon
# Provides Syphon texture sharing for macOS
#
# Usage in project CMakeLists.txt:
#   include(path/to/vivid-syphon/addon.cmake)
#   vivid_use_syphon(your_target)
#
# NOTE: macOS only. On other platforms, this addon does nothing.
#
# The Syphon framework is loaded at runtime (not linked at compile time).
# Users must have Syphon.framework installed in one of:
#   - /Library/Frameworks/Syphon.framework
#   - ~/Library/Frameworks/Syphon.framework
#
# Download from: https://github.com/Syphon/Syphon-Framework/releases

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

# Syphon is loaded at runtime, so we just need common framework paths
set(SYPHON_FRAMEWORK_SEARCH_PATHS
    "/Library/Frameworks/Syphon.framework"
    "$ENV{HOME}/Library/Frameworks/Syphon.framework"
)

# Check if Syphon is installed
set(SYPHON_FOUND FALSE)
foreach(path ${SYPHON_FRAMEWORK_SEARCH_PATHS})
    if(EXISTS "${path}/Versions/A/Syphon")
        message(STATUS "vivid-syphon: Found Syphon.framework at ${path}")
        set(SYPHON_FOUND TRUE)
        set(SYPHON_FRAMEWORK_PATH "${path}")
        break()
    endif()
endforeach()

if(NOT SYPHON_FOUND)
    message(STATUS "vivid-syphon: Syphon.framework not found. It will be loaded at runtime if available.")
    message(STATUS "  Install from: https://github.com/Syphon/Syphon-Framework/releases")
    set(SYPHON_FRAMEWORK_PATH "/Library/Frameworks/Syphon.framework")
endif()

# Function to add vivid-syphon to a target
function(vivid_use_syphon TARGET)
    target_sources(${TARGET} PRIVATE
        ${VIVID_SYPHON_DIR}/src/syphon_server.mm
        ${VIVID_SYPHON_DIR}/src/syphon_client.mm
    )

    target_include_directories(${TARGET} PRIVATE
        ${VIVID_SYPHON_DIR}/include
    )

    # Link required Apple frameworks (but NOT Syphon - it's loaded at runtime)
    target_link_libraries(${TARGET} PRIVATE
        "-framework Cocoa"
        "-framework OpenGL"
        "-framework IOSurface"
    )

    # Compile as Objective-C++
    set_source_files_properties(
        ${VIVID_SYPHON_DIR}/src/syphon_server.mm
        ${VIVID_SYPHON_DIR}/src/syphon_client.mm
        PROPERTIES COMPILE_FLAGS "-x objective-c++ -DGL_SILENCE_DEPRECATION"
    )

    # Define the framework path so runtime loading code can find it
    target_compile_definitions(${TARGET} PRIVATE
        VIVID_SYPHON_ENABLED=1
        SYPHON_FRAMEWORK_PATH="${SYPHON_FRAMEWORK_PATH}"
    )
endfunction()
