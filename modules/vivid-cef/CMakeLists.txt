# Vivid CEF Module
# CEF (Chromium Embedded Framework) browser operator for web content rendering
# - macOS: IOSurface GPU texture sharing
# - Windows: D3D11 shared textures
# - Linux: DMABuf (when available)
#
# CEF is downloaded automatically from Spotify's official CDN.

# CEF version - using LTS 120 series for stability
# Check https://cef-builds.spotifycdn.com/index.html for versions
set(CEF_VERSION "120.2.7+g4bc6a59+chromium-120.0.6099.234")

# -----------------------------------------------------------------------------
# Platform detection and CEF binary download
# -----------------------------------------------------------------------------

if(WIN32)
    set(CEF_PLATFORM "windows64")
    set(CEF_EXT "tar.bz2")
elseif(APPLE)
    # Detect macOS architecture
    execute_process(
        COMMAND uname -m
        OUTPUT_VARIABLE HOST_ARCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(HOST_ARCH STREQUAL "arm64")
        set(CEF_PLATFORM "macosarm64")
    else()
        set(CEF_PLATFORM "macosx64")
    endif()
    set(CEF_EXT "tar.bz2")
else()
    # Linux
    set(CEF_PLATFORM "linux64")
    set(CEF_EXT "tar.bz2")
endif()

# CEF download URL (Spotify CDN)
# Using minimal distribution (no sample apps, debug symbols)
string(REPLACE "+" "%2B" CEF_VERSION_ENCODED "${CEF_VERSION}")
set(CEF_URL "https://cef-builds.spotifycdn.com/cef_binary_${CEF_VERSION_ENCODED}_${CEF_PLATFORM}_minimal.${CEF_EXT}")
set(CEF_DIR "${CMAKE_BINARY_DIR}/_deps/cef")
set(CEF_ARCHIVE "${CEF_DIR}/cef.${CEF_EXT}")

# Create directory
file(MAKE_DIRECTORY "${CEF_DIR}")

# Check if already downloaded
set(CEF_SKIP_DOWNLOAD FALSE)
if(EXISTS "${CEF_ARCHIVE}")
    file(SIZE "${CEF_ARCHIVE}" CEF_FILE_SIZE)
    # Minimal CEF should be at least 50MB
    if(CEF_FILE_SIZE GREATER 50000000)
        message(STATUS "[vivid-cef] Using pre-downloaded CEF (${CEF_FILE_SIZE} bytes)")
        set(CEF_SKIP_DOWNLOAD TRUE)
    endif()
endif()

if(NOT CEF_SKIP_DOWNLOAD)
    message(STATUS "[vivid-cef] Downloading CEF from: ${CEF_URL}")

    # Try CMake download first
    file(DOWNLOAD ${CEF_URL} "${CEF_ARCHIVE}"
        STATUS CEF_DOWNLOAD_STATUS
        TIMEOUT 600
        INACTIVITY_TIMEOUT 120
        SHOW_PROGRESS
    )
    list(GET CEF_DOWNLOAD_STATUS 0 CEF_DOWNLOAD_OK)
    list(GET CEF_DOWNLOAD_STATUS 1 CEF_DOWNLOAD_MSG)

    if(NOT CEF_DOWNLOAD_OK EQUAL 0)
        # Try curl as fallback
        message(STATUS "[vivid-cef] CMake download failed (${CEF_DOWNLOAD_MSG}), trying curl...")
        find_program(CURL_EXECUTABLE curl)
        if(CURL_EXECUTABLE)
            execute_process(
                COMMAND ${CURL_EXECUTABLE} -fL --retry 3 --retry-delay 5 -o "${CEF_ARCHIVE}" "${CEF_URL}"
                RESULT_VARIABLE CURL_RESULT
            )
            if(NOT CURL_RESULT EQUAL 0)
                message(FATAL_ERROR "[vivid-cef] Failed to download CEF via curl (exit code ${CURL_RESULT})\nURL: ${CEF_URL}")
            endif()
        else()
            message(FATAL_ERROR "[vivid-cef] Failed to download CEF: ${CEF_DOWNLOAD_MSG}\nURL: ${CEF_URL}")
        endif()
    endif()

    # Verify download
    file(SIZE "${CEF_ARCHIVE}" CEF_FILE_SIZE)
    if(CEF_FILE_SIZE LESS 50000000)
        message(FATAL_ERROR "[vivid-cef] Downloaded CEF file is too small (${CEF_FILE_SIZE} bytes) - download may have failed")
    endif()
    message(STATUS "[vivid-cef] Downloaded CEF (${CEF_FILE_SIZE} bytes)")
endif()

# Extract CEF
# The archive contains a directory like cef_binary_120.2.7+g4bc6a59+chromium-120.0.6099.234_macosarm64_minimal
set(CEF_EXTRACT_DIR "${CEF_DIR}/extracted")
if(NOT EXISTS "${CEF_EXTRACT_DIR}")
    message(STATUS "[vivid-cef] Extracting CEF...")
    file(MAKE_DIRECTORY "${CEF_EXTRACT_DIR}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xjf "${CEF_ARCHIVE}"
        WORKING_DIRECTORY "${CEF_EXTRACT_DIR}"
        RESULT_VARIABLE EXTRACT_RESULT
    )
    if(NOT EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "[vivid-cef] Failed to extract CEF archive")
    endif()
endif()

# Find the extracted CEF directory (name varies by version)
file(GLOB CEF_ROOT_DIRS "${CEF_EXTRACT_DIR}/cef_binary_*")
list(GET CEF_ROOT_DIRS 0 CEF_ROOT)
if(NOT CEF_ROOT OR NOT EXISTS "${CEF_ROOT}")
    message(FATAL_ERROR "[vivid-cef] Could not find extracted CEF directory in ${CEF_EXTRACT_DIR}")
endif()
message(STATUS "[vivid-cef] CEF root: ${CEF_ROOT}")

# -----------------------------------------------------------------------------
# CEF wrapper library
# -----------------------------------------------------------------------------

# Build the CEF C++ wrapper (libcef_dll_wrapper)
# This provides the C++ API on top of CEF's C API
set(CEF_WRAPPER_SOURCES
    ${CEF_ROOT}/libcef_dll/wrapper/cef_byte_read_handler.cc
    ${CEF_ROOT}/libcef_dll/wrapper/cef_closure_task.cc
    ${CEF_ROOT}/libcef_dll/wrapper/cef_message_router.cc
    ${CEF_ROOT}/libcef_dll/wrapper/cef_message_router_utils.cc
    ${CEF_ROOT}/libcef_dll/wrapper/cef_resource_manager.cc
    ${CEF_ROOT}/libcef_dll/wrapper/cef_scoped_temp_dir.cc
    ${CEF_ROOT}/libcef_dll/wrapper/cef_stream_resource_handler.cc
    ${CEF_ROOT}/libcef_dll/wrapper/cef_xml_object.cc
    ${CEF_ROOT}/libcef_dll/wrapper/cef_zip_archive.cc
    ${CEF_ROOT}/libcef_dll/wrapper/libcef_dll_wrapper.cc
    ${CEF_ROOT}/libcef_dll/wrapper/libcef_dll_wrapper2.cc
)

# Platform-specific wrapper sources
if(APPLE)
    list(APPEND CEF_WRAPPER_SOURCES
        ${CEF_ROOT}/libcef_dll/wrapper/cef_library_loader_mac.mm
    )
endif()

# CEF base library sources
file(GLOB CEF_BASE_SOURCES "${CEF_ROOT}/libcef_dll/base/*.cc")

# CEF C++ to C translation layer
file(GLOB CEF_CPPTOC_SOURCES "${CEF_ROOT}/libcef_dll/cpptoc/*.cc")
file(GLOB CEF_CTOCPP_SOURCES "${CEF_ROOT}/libcef_dll/ctocpp/*.cc")
file(GLOB CEF_TRANSFER_SOURCES "${CEF_ROOT}/libcef_dll/transfer_util.cc" "${CEF_ROOT}/libcef_dll/shutdown_checker.cc")

add_library(cef_dll_wrapper STATIC
    ${CEF_WRAPPER_SOURCES}
    ${CEF_BASE_SOURCES}
    ${CEF_CPPTOC_SOURCES}
    ${CEF_CTOCPP_SOURCES}
    ${CEF_TRANSFER_SOURCES}
)

target_include_directories(cef_dll_wrapper PUBLIC
    ${CEF_ROOT}
    ${CEF_ROOT}/include
)

# CEF requires specific compiler settings
target_compile_definitions(cef_dll_wrapper PRIVATE
    WRAPPING_CEF_SHARED
)

if(WIN32)
    target_compile_definitions(cef_dll_wrapper PRIVATE
        WIN32
        _WIN32
        _WINDOWS
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
elseif(APPLE)
    target_compile_options(cef_dll_wrapper PRIVATE
        -Wno-undefined-var-template
    )
endif()

# -----------------------------------------------------------------------------
# CEF binary framework/library
# -----------------------------------------------------------------------------

if(APPLE)
    # On macOS, CEF is distributed as a framework
    set(CEF_FRAMEWORK_DIR "${CEF_ROOT}/Release/Chromium Embedded Framework.framework")

    # Create imported target for CEF framework
    add_library(cef SHARED IMPORTED GLOBAL)
    set_target_properties(cef PROPERTIES
        IMPORTED_LOCATION "${CEF_FRAMEWORK_DIR}/Chromium Embedded Framework"
        INTERFACE_INCLUDE_DIRECTORIES "${CEF_ROOT};${CEF_ROOT}/include"
    )

elseif(WIN32)
    # On Windows, CEF is a DLL
    set(CEF_LIB_DIR "${CEF_ROOT}/Release")

    add_library(cef SHARED IMPORTED GLOBAL)
    set_target_properties(cef PROPERTIES
        IMPORTED_LOCATION "${CEF_LIB_DIR}/libcef.dll"
        IMPORTED_IMPLIB "${CEF_LIB_DIR}/libcef.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${CEF_ROOT};${CEF_ROOT}/include"
    )

else()
    # On Linux, CEF is a shared library
    set(CEF_LIB_DIR "${CEF_ROOT}/Release")

    add_library(cef SHARED IMPORTED GLOBAL)
    set_target_properties(cef PROPERTIES
        IMPORTED_LOCATION "${CEF_LIB_DIR}/libcef.so"
        INTERFACE_INCLUDE_DIRECTORIES "${CEF_ROOT};${CEF_ROOT}/include"
    )
endif()

# -----------------------------------------------------------------------------
# vivid-cef library
# -----------------------------------------------------------------------------

set(VIVID_CEF_SOURCES
    src/browser.cpp
    src/browser_impl.cpp
    src/cef_app.cpp
    src/render_handler.cpp
    src/texture_provider.cpp
)

# Platform-specific texture provider
if(APPLE)
    list(APPEND VIVID_CEF_SOURCES
        src/texture_provider_macos.mm
    )
elseif(WIN32)
    list(APPEND VIVID_CEF_SOURCES
        src/texture_provider_windows.cpp
    )
else()
    list(APPEND VIVID_CEF_SOURCES
        src/texture_provider_linux.cpp
    )
endif()

add_library(vivid-cef SHARED ${VIVID_CEF_SOURCES})

target_include_directories(vivid-cef PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/modules/vivid-core/include
)

target_link_libraries(vivid-cef PUBLIC
    webgpu
    glm
    glfw
    vivid-core
    cef_dll_wrapper
    cef
)

# Platform-specific configuration
if(APPLE)
    target_link_libraries(vivid-cef PRIVATE
        "-framework Metal"
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework QuartzCore"
        "-framework AppKit"
        "-framework IOSurface"
    )

    set_source_files_properties(
        src/texture_provider_macos.mm
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )

    set_target_properties(vivid-cef PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )

elseif(WIN32)
    target_compile_definitions(vivid-cef PRIVATE vivid_cef_EXPORTS)

    target_link_libraries(vivid-cef PRIVATE
        d3d11
        dxgi
    )

    set_target_properties(vivid-cef PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/Debug
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/Release
    )
endif()

# -----------------------------------------------------------------------------
# CEF subprocess helper executable
# -----------------------------------------------------------------------------

add_executable(vivid-cef-helper
    subprocess/main.cpp
)

target_link_libraries(vivid-cef-helper
    cef_dll_wrapper
    cef
)

target_include_directories(vivid-cef-helper PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(APPLE)
    # macOS: Helper needs to be in a specific location relative to the framework
    set_target_properties(vivid-cef-helper PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # Copy CEF framework to Frameworks directory (CEF's install_name expects this location)
    add_custom_command(TARGET vivid-cef POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_FRAMEWORK_DIR}"
            "${CMAKE_BINARY_DIR}/Frameworks/Chromium Embedded Framework.framework"
        # Also copy Libraries to bin directory (CEF looks for libGLESv2.dylib at @executable_path)
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_FRAMEWORK_DIR}/Libraries"
            "${CMAKE_BINARY_DIR}/bin"
        COMMENT "Copying CEF framework to build directory"
    )

elseif(WIN32)
    # Windows: Copy CEF DLLs and resources
    set_target_properties(vivid-cef-helper PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # Copy CEF binaries
    add_custom_command(TARGET vivid-cef POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_ROOT}/Release"
            "${CMAKE_BINARY_DIR}/bin"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_ROOT}/Resources"
            "${CMAKE_BINARY_DIR}/bin"
        COMMENT "Copying CEF binaries to build directory"
    )

else()
    # Linux: Copy CEF library and resources
    set_target_properties(vivid-cef-helper PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    add_custom_command(TARGET vivid-cef POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_ROOT}/Release"
            "${CMAKE_BINARY_DIR}/bin"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_ROOT}/Resources"
            "${CMAKE_BINARY_DIR}/bin"
        COMMENT "Copying CEF binaries to build directory"
    )
endif()

message(STATUS "[vivid-cef] CEF ${CEF_VERSION} configured for ${CEF_PLATFORM}")
