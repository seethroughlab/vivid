# Vivid OpenCV Addon
# Computer vision operators using OpenCV
# - Contours: Edge detection and contour drawing
# - OpticalFlow: Motion vector calculation (planned)
# - ColorTrack: Color-based object tracking (planned)
# - BlobTrack: Blob detection (planned)
# - FaceDetect: Face detection (planned)

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
        # ARM64 Linux (Raspberry Pi, etc.)
        set(OPENCV_MOBILE_PLATFORM "armlinux")
        set(OPENCV_MOBILE_URL "https://github.com/nihui/opencv-mobile/releases/download/${OPENCV_MOBILE_TAG}/opencv-mobile-${OPENCV_MOBILE_VERSION}-armlinux.zip")
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

# Verify the headers exist based on platform
set(OPENCV_INCLUDE_FOUND FALSE)

if(APPLE)
    # macOS uses Headers/ directory (framework style)
    if(EXISTS "${opencv_mobile_SOURCE_DIR}/Headers")
        set(OPENCV_INCLUDE_FOUND TRUE)
        message(STATUS "vivid-opencv: Found opencv2 headers at ${opencv_mobile_SOURCE_DIR}/Headers (macOS framework)")
    endif()
else()
    # Windows/Linux use include/opencv2/ structure
    if(EXISTS "${opencv_mobile_SOURCE_DIR}/include/opencv2")
        set(OPENCV_INCLUDE_FOUND TRUE)
        message(STATUS "vivid-opencv: Found opencv2 headers at ${opencv_mobile_SOURCE_DIR}/include/opencv2")
    endif()

    # Some archives extract with a subdirectory
    if(NOT OPENCV_INCLUDE_FOUND)
        file(GLOB OPENCV_SUBDIRS "${opencv_mobile_SOURCE_DIR}/opencv-mobile-*")
        if(OPENCV_SUBDIRS)
            list(GET OPENCV_SUBDIRS 0 OPENCV_SUBDIR)
            if(EXISTS "${OPENCV_SUBDIR}/include/opencv2")
                set(opencv_mobile_SOURCE_DIR "${OPENCV_SUBDIR}")
                set(OPENCV_INCLUDE_FOUND TRUE)
                message(STATUS "vivid-opencv: Found opencv-mobile in subdirectory: ${opencv_mobile_SOURCE_DIR}")
            endif()
        endif()
    endif()
endif()

# List contents for debugging if not found
if(NOT OPENCV_INCLUDE_FOUND)
    message(STATUS "vivid-opencv: Listing contents of ${opencv_mobile_SOURCE_DIR}:")
    file(GLOB OPENCV_CONTENTS "${opencv_mobile_SOURCE_DIR}/*")
    foreach(ITEM ${OPENCV_CONTENTS})
        message(STATUS "  - ${ITEM}")
    endforeach()
    message(WARNING "vivid-opencv: Could not find opencv2 headers - build may fail")
endif()

set(OPENCV_SOURCES
    src/opencv.cpp
    src/texture_converter.cpp
    src/contours.cpp
    src/optical_flow.cpp
    # TODO: Add these when ready
    # src/blob_track.cpp
    # src/face_detect.cpp
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
    # Windows: traditional lib/include layout
    set(OPENCV_MOBILE_DIR "${opencv_mobile_SOURCE_DIR}")
    set(OPENCV_LIB_DIR "${OPENCV_MOBILE_DIR}/x64/vc17/staticlib")

    target_include_directories(vivid-opencv PRIVATE
        ${OPENCV_MOBILE_DIR}/include
    )

    target_link_libraries(vivid-opencv PRIVATE
        ${OPENCV_LIB_DIR}/opencv_imgproc4130.lib
        ${OPENCV_LIB_DIR}/opencv_core4130.lib
        ${OPENCV_LIB_DIR}/zlib.lib
    )

    target_compile_definitions(vivid-opencv PRIVATE vivid_opencv_EXPORTS)

    set_target_properties(vivid-opencv PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/Debug
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/Release
    )

else()
    # Linux: traditional lib/include layout
    set(OPENCV_MOBILE_DIR "${opencv_mobile_SOURCE_DIR}")

    target_include_directories(vivid-opencv PRIVATE
        ${OPENCV_MOBILE_DIR}/include
    )

    target_link_libraries(vivid-opencv PRIVATE
        ${OPENCV_MOBILE_DIR}/lib/libopencv_imgproc.a
        ${OPENCV_MOBILE_DIR}/lib/libopencv_core.a
        ${OPENCV_MOBILE_DIR}/lib/libz.a
        pthread
        dl
    )

    set_target_properties(vivid-opencv PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

message(STATUS "vivid-opencv: Configured with opencv-mobile ${OPENCV_MOBILE_VERSION}")
