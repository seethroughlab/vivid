# Vivid ML Addon
# Machine learning inference for creative applications
# - ONNXModel: Generic ONNX model inference
# - PoseDetector: MoveNet body tracking
# - SegmentMask: Background segmentation
# - StyleTransfer: Neural style transfer

include(FetchContent)

# ONNX Runtime version (1.19+ supports IR version 10)
set(ONNXRUNTIME_VERSION "1.19.2")

# Platform-specific ONNX Runtime download
if(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-osx-arm64-${ONNXRUNTIME_VERSION}.tgz")
    else()
        set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-osx-x86_64-${ONNXRUNTIME_VERSION}.tgz")
    endif()
elseif(WIN32)
    set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-win-x64-${ONNXRUNTIME_VERSION}.zip")
elseif(UNIX)
    set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}.tgz")
endif()

message(STATUS "[vivid-ml] Downloading ONNX Runtime from: ${ONNXRUNTIME_URL}")

FetchContent_Declare(
    onnxruntime
    URL ${ONNXRUNTIME_URL}
)
FetchContent_MakeAvailable(onnxruntime)

# Find the ONNX Runtime headers and libraries
set(ONNXRUNTIME_INCLUDE_DIR "${onnxruntime_SOURCE_DIR}/include")
if(APPLE)
    set(ONNXRUNTIME_LIB "${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.dylib")
elseif(WIN32)
    set(ONNXRUNTIME_LIB "${onnxruntime_SOURCE_DIR}/lib/onnxruntime.lib")
    set(ONNXRUNTIME_DLL "${onnxruntime_SOURCE_DIR}/lib/onnxruntime.dll")
else()
    set(ONNXRUNTIME_LIB "${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so")
endif()

message(STATUS "[vivid-ml] ONNX Runtime include: ${ONNXRUNTIME_INCLUDE_DIR}")
message(STATUS "[vivid-ml] ONNX Runtime lib: ${ONNXRUNTIME_LIB}")

set(ML_SOURCES
    src/onnx_model.cpp
    src/pose_detector.cpp
)

add_library(vivid-ml SHARED ${ML_SOURCES})

target_include_directories(vivid-ml PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core/include
    ${ONNXRUNTIME_INCLUDE_DIR}
)

target_link_libraries(vivid-ml PUBLIC
    vivid-core
    ${ONNXRUNTIME_LIB}
)

# Platform-specific configuration
if(APPLE)
    set_target_properties(vivid-ml PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
    # Copy ONNX Runtime dylibs (both symlink and versioned file)
    add_custom_command(TARGET vivid-ml POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            ${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.${ONNXRUNTIME_VERSION}.dylib
            $<TARGET_FILE_DIR:vivid>/
        COMMAND ${CMAKE_COMMAND} -E copy
            ${ONNXRUNTIME_LIB}
            $<TARGET_FILE_DIR:vivid>/
        COMMENT "Copying ONNX Runtime library"
    )
elseif(WIN32)
    set_target_properties(vivid-ml PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
    # Copy ONNX Runtime DLL
    add_custom_command(TARGET vivid-ml POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            ${ONNXRUNTIME_DLL}
            $<TARGET_FILE_DIR:vivid>/
        COMMENT "Copying ONNX Runtime DLL"
    )
endif()

# Copy vivid-ml library next to vivid executable
add_custom_command(TARGET vivid-ml POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:vivid-ml>
        $<TARGET_FILE_DIR:vivid>/
    COMMENT "Copying vivid-ml library"
)
