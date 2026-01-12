# Vivid OpenCV Addon
# Computer vision operators using opencv-mobile
# - Contours: Edge detection and contour drawing
# - OpticalFlow: Dense motion vector calculation
# - BlobTrack: Blob detection and tracking
#
# Note: opencv-mobile includes core, imgproc, video, features2d, photo
# Face detection and DNN features require vivid-onnx instead

include(FetchContent)

# opencv-mobile: Minimal pre-built OpenCV binaries
# https://github.com/nihui/opencv-mobile
set(OPENCV_MOBILE_VERSION "4.13.0")
set(OPENCV_MOBILE_TAG "v35")

if(APPLE)
    set(OPENCV_MOBILE_PLATFORM "macos")
    set(OPENCV_MOBILE_URL "https://github.com/nihui/opencv-mobile/releases/download/${OPENCV_MOBILE_TAG}/opencv-mobile-${OPENCV_MOBILE_VERSION}-macos.zip")
elseif(WIN32)
    set(OPENCV_MOBILE_PLATFORM "windows-vs2022")
    set(OPENCV_MOBILE_URL "https://github.com/nihui/opencv-mobile/releases/download/${OPENCV_MOBILE_TAG}/opencv-mobile-${OPENCV_MOBILE_VERSION}-windows-vs2022.zip")
else()
    # Linux - detect architecture
    execute_process(COMMAND uname -m OUTPUT_VARIABLE LINUX_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "vivid-opencv: Detected Linux architecture: ${LINUX_ARCH}")

    if(LINUX_ARCH STREQUAL "aarch64" OR LINUX_ARCH STREQUAL "arm64")
        # ARM64 Linux (Raspberry Pi, etc.) - use Debian Bookworm native aarch64 build
        set(OPENCV_MOBILE_PLATFORM "debian-bookworm-arm")
        set(OPENCV_MOBILE_URL "https://github.com/nihui/opencv-mobile/releases/download/${OPENCV_MOBILE_TAG}/opencv-mobile-${OPENCV_MOBILE_VERSION}-debian-bookworm-arm.zip")
    else()
        # x86_64 Linux - use Ubuntu 22.04 build
        set(OPENCV_MOBILE_PLATFORM "ubuntu-2204")
        set(OPENCV_MOBILE_URL "https://github.com/nihui/opencv-mobile/releases/download/${OPENCV_MOBILE_TAG}/opencv-mobile-${OPENCV_MOBILE_VERSION}-ubuntu-2204.zip")
    endif()
endif()

message(STATUS "vivid-opencv: Fetching opencv-mobile ${OPENCV_MOBILE_VERSION} for ${OPENCV_MOBILE_PLATFORM}")

FetchContent_Declare(opencv_mobile
    URL ${OPENCV_MOBILE_URL}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(opencv_mobile)

# Debug: Print the actual source directory
message(STATUS "vivid-opencv: opencv_mobile_SOURCE_DIR = ${opencv_mobile_SOURCE_DIR}")

# List all contents recursively for debugging
message(STATUS "vivid-opencv: Listing top-level contents:")
file(GLOB OPENCV_TOP_CONTENTS "${opencv_mobile_SOURCE_DIR}/*")
foreach(ITEM ${OPENCV_TOP_CONTENTS})
    message(STATUS "  - ${ITEM}")
endforeach()

# Verify the headers exist based on platform
set(OPENCV_INCLUDE_FOUND FALSE)
set(OPENCV_INCLUDE_DIR "")

if(APPLE)
    # macOS uses Headers/ directory (framework style)
    if(EXISTS "${opencv_mobile_SOURCE_DIR}/Headers")
        set(OPENCV_INCLUDE_FOUND TRUE)
        set(OPENCV_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/opencv_include")
        message(STATUS "vivid-opencv: Found opencv2 headers at ${opencv_mobile_SOURCE_DIR}/Headers (macOS framework)")
    endif()
else()
    # Windows/Linux: Try multiple possible locations

    # 1. Direct include/opencv2/ structure (older archives)
    if(EXISTS "${opencv_mobile_SOURCE_DIR}/include/opencv2")
        set(OPENCV_INCLUDE_FOUND TRUE)
        set(OPENCV_INCLUDE_DIR "${opencv_mobile_SOURCE_DIR}/include")
        message(STATUS "vivid-opencv: Found opencv2 headers at ${opencv_mobile_SOURCE_DIR}/include/opencv2")
    endif()

    # 2. Linux archives use include/opencv4/opencv2 layout
    if(NOT OPENCV_INCLUDE_FOUND)
        if(EXISTS "${opencv_mobile_SOURCE_DIR}/include/opencv4/opencv2")
            set(OPENCV_INCLUDE_FOUND TRUE)
            set(OPENCV_INCLUDE_DIR "${opencv_mobile_SOURCE_DIR}/include/opencv4")
            message(STATUS "vivid-opencv: Found opencv2 headers at ${opencv_mobile_SOURCE_DIR}/include/opencv4/opencv2")
        endif()
    endif()

    # 3. Some archives may have nested opencv-mobile-* subdirectory (shouldn't happen with FetchContent)
    if(NOT OPENCV_INCLUDE_FOUND)
        file(GLOB OPENCV_SUBDIRS "${opencv_mobile_SOURCE_DIR}/opencv-mobile-*")
        if(OPENCV_SUBDIRS)
            list(GET OPENCV_SUBDIRS 0 OPENCV_SUBDIR)
            if(EXISTS "${OPENCV_SUBDIR}/include/opencv4/opencv2")
                set(OPENCV_INCLUDE_FOUND TRUE)
                set(OPENCV_INCLUDE_DIR "${OPENCV_SUBDIR}/include/opencv4")
                set(OPENCV_LIB_BASE_DIR "${OPENCV_SUBDIR}")
                message(STATUS "vivid-opencv: Found opencv2 headers at ${OPENCV_SUBDIR}/include/opencv4/opencv2")
            elseif(EXISTS "${OPENCV_SUBDIR}/include/opencv2")
                set(OPENCV_INCLUDE_FOUND TRUE)
                set(OPENCV_INCLUDE_DIR "${OPENCV_SUBDIR}/include")
                set(OPENCV_LIB_BASE_DIR "${OPENCV_SUBDIR}")
                message(STATUS "vivid-opencv: Found opencv2 headers at ${OPENCV_SUBDIR}/include/opencv2")
            endif()
        endif()
    endif()

    # 4. Windows archive has platform subdirectories (x64, x86, arm64)
    if(NOT OPENCV_INCLUDE_FOUND AND WIN32)
        # Check common Windows paths in order of preference
        set(OPENCV_WIN_PATHS
            "${opencv_mobile_SOURCE_DIR}/x64/vc17/include"
            "${opencv_mobile_SOURCE_DIR}/x64/include"
            "${opencv_mobile_SOURCE_DIR}/x86/vc17/include"
            "${opencv_mobile_SOURCE_DIR}/x86/include"
        )
        foreach(WIN_PATH ${OPENCV_WIN_PATHS})
            if(EXISTS "${WIN_PATH}/opencv2/core.hpp")
                set(OPENCV_INCLUDE_FOUND TRUE)
                set(OPENCV_INCLUDE_DIR "${WIN_PATH}")
                message(STATUS "vivid-opencv: Found opencv2 headers at ${WIN_PATH}")
                break()
            endif()
        endforeach()
    endif()
endif()

# List contents for debugging if not found
if(NOT OPENCV_INCLUDE_FOUND)
    message(STATUS "vivid-opencv: Searching for opencv2 headers...")
    # List x64 contents on Windows
    if(WIN32 AND EXISTS "${opencv_mobile_SOURCE_DIR}/x64")
        message(STATUS "vivid-opencv: Contents of x64 directory:")
        file(GLOB X64_CONTENTS "${opencv_mobile_SOURCE_DIR}/x64/*")
        foreach(ITEM ${X64_CONTENTS})
            message(STATUS "  - ${ITEM}")
        endforeach()
        if(EXISTS "${opencv_mobile_SOURCE_DIR}/x64/vc17")
            message(STATUS "vivid-opencv: Contents of x64/vc17 directory:")
            file(GLOB X64_VC17_CONTENTS "${opencv_mobile_SOURCE_DIR}/x64/vc17/*")
            foreach(ITEM ${X64_VC17_CONTENTS})
                message(STATUS "  - ${ITEM}")
            endforeach()
        endif()
    endif()
    message(WARNING "vivid-opencv: Could not find opencv2 headers - build may fail")
endif()

set(OPENCV_SOURCES
    src/opencv.cpp
    src/texture_converter.cpp
    src/contours.cpp
    src/optical_flow.cpp
    src/blob_track.cpp
)

add_library(vivid-opencv SHARED ${OPENCV_SOURCES})

target_include_directories(vivid-opencv PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/modules/vivid-core/include
)

target_link_libraries(vivid-opencv PUBLIC
    webgpu
    glm
    vivid-core
)

# Platform-specific opencv-mobile linking
if(APPLE)
    # macOS: opencv-mobile is packaged as a framework structure
    # Headers in opencv_mobile-src/Headers/ (files directly, no opencv2/ subdir)
    # Library is opencv_mobile-src/opencv2 (universal static lib)

    # Create opencv2 include wrapper so #include <opencv2/core.hpp> works
    set(OPENCV_WRAPPER_DIR "${CMAKE_CURRENT_BINARY_DIR}/opencv_include")
    file(MAKE_DIRECTORY "${OPENCV_WRAPPER_DIR}")
    if(NOT EXISTS "${OPENCV_WRAPPER_DIR}/opencv2")
        file(CREATE_LINK "${opencv_mobile_SOURCE_DIR}/Headers" "${OPENCV_WRAPPER_DIR}/opencv2" SYMBOLIC)
    endif()

    target_include_directories(vivid-opencv PRIVATE
        ${OPENCV_WRAPPER_DIR}
    )

    target_link_libraries(vivid-opencv PRIVATE
        "${opencv_mobile_SOURCE_DIR}/opencv2"
        "-framework Accelerate"
        "-framework CoreVideo"
    )

    set_target_properties(vivid-opencv PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )

elseif(WIN32)
    # Windows: opencv-mobile has platform subdirectories (x64, x86, arm64)
    set(OPENCV_MOBILE_DIR "${opencv_mobile_SOURCE_DIR}")

    # Use the detected include directory, with fallback
    if(OPENCV_INCLUDE_DIR)
        target_include_directories(vivid-opencv PRIVATE ${OPENCV_INCLUDE_DIR})
        message(STATUS "vivid-opencv: Windows using include dir: ${OPENCV_INCLUDE_DIR}")
    else()
        target_include_directories(vivid-opencv PRIVATE ${OPENCV_MOBILE_DIR}/include)
        message(STATUS "vivid-opencv: Windows using fallback include dir: ${OPENCV_MOBILE_DIR}/include")
    endif()

    # Find the library directory - try multiple possible locations
    # Note: opencv-mobile Windows archive has nested x64/x64/vc17/staticlib structure
    set(OPENCV_WIN_LIB_PATHS
        "${OPENCV_MOBILE_DIR}/x64/x64/vc17/staticlib"
        "${OPENCV_MOBILE_DIR}/x64/vc17/staticlib"
        "${OPENCV_MOBILE_DIR}/x64/staticlib"
        "${OPENCV_MOBILE_DIR}/x64/lib"
    )
    set(OPENCV_LIB_DIR "")
    foreach(LIB_PATH ${OPENCV_WIN_LIB_PATHS})
        if(EXISTS "${LIB_PATH}/opencv_core4130.lib")
            set(OPENCV_LIB_DIR "${LIB_PATH}")
            message(STATUS "vivid-opencv: Windows using lib dir: ${OPENCV_LIB_DIR}")
            break()
        endif()
    endforeach()

    if(NOT OPENCV_LIB_DIR)
        # List x64 directory contents for debugging
        message(STATUS "vivid-opencv: Could not find Windows libs. Contents of x64:")
        file(GLOB X64_CONTENTS "${OPENCV_MOBILE_DIR}/x64/*")
        foreach(ITEM ${X64_CONTENTS})
            message(STATUS "  - ${ITEM}")
        endforeach()
        message(WARNING "vivid-opencv: Windows lib directory not found - link will fail")
        set(OPENCV_LIB_DIR "${OPENCV_MOBILE_DIR}/x64/lib")
    endif()

    # Note: opencv-mobile Windows static libs bundle zlib internally
    target_link_libraries(vivid-opencv PRIVATE
        ${OPENCV_LIB_DIR}/opencv_video4130.lib
        ${OPENCV_LIB_DIR}/opencv_features2d4130.lib
        ${OPENCV_LIB_DIR}/opencv_imgproc4130.lib
        ${OPENCV_LIB_DIR}/opencv_core4130.lib
    )

    target_compile_definitions(vivid-opencv PRIVATE vivid_opencv_EXPORTS)

    set_target_properties(vivid-opencv PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/Debug
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/Release
    )

else()
    # Linux: traditional lib/include layout
    # Use OPENCV_LIB_BASE_DIR if detected from nested archive, otherwise use source dir
    if(OPENCV_LIB_BASE_DIR)
        set(OPENCV_MOBILE_DIR "${OPENCV_LIB_BASE_DIR}")
        message(STATUS "vivid-opencv: Linux using lib base dir: ${OPENCV_MOBILE_DIR}")
    else()
        set(OPENCV_MOBILE_DIR "${opencv_mobile_SOURCE_DIR}")
    endif()

    # Use the detected include directory, with fallback
    if(OPENCV_INCLUDE_DIR)
        target_include_directories(vivid-opencv PRIVATE ${OPENCV_INCLUDE_DIR})
        message(STATUS "vivid-opencv: Linux using include dir: ${OPENCV_INCLUDE_DIR}")
    else()
        target_include_directories(vivid-opencv PRIVATE ${OPENCV_MOBILE_DIR}/include)
        message(STATUS "vivid-opencv: Linux using fallback include dir: ${OPENCV_MOBILE_DIR}/include")
    endif()

    target_link_libraries(vivid-opencv PRIVATE
        ${OPENCV_MOBILE_DIR}/lib/libopencv_video.a
        ${OPENCV_MOBILE_DIR}/lib/libopencv_features2d.a
        ${OPENCV_MOBILE_DIR}/lib/libopencv_imgproc.a
        ${OPENCV_MOBILE_DIR}/lib/libopencv_core.a
        z  # System zlib (required by opencv_core)
        pthread
        dl
    )

    set_target_properties(vivid-opencv PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

message(STATUS "vivid-opencv: Configured with opencv-mobile ${OPENCV_MOBILE_VERSION}")
