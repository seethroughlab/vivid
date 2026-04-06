
# --- Dawn/WebGPU via eliemichel's distribution (pre-built binaries) ---
include(FetchContent)
FetchContent_Declare(
    webgpu
    GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution
    GIT_TAG        17dcd42a7683355e7a40ac4e97e77f36dff5b5ab
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(webgpu)

option(VIVID_USE_WGPU_NATIVE_UPSTREAM "Build upstream wgpu-native from source for Metal interop symbols" ON)
if(APPLE AND VIVID_USE_WGPU_NATIVE_UPSTREAM)
    FetchContent_Declare(
        wgpu_native_upstream
        GIT_REPOSITORY https://github.com/gfx-rs/wgpu-native
        GIT_TAG        1487faea32e3b7fc891fd671323582b487038aea
        GIT_SUBMODULES_RECURSE TRUE
    )
    FetchContent_GetProperties(wgpu_native_upstream)
    if(NOT wgpu_native_upstream_POPULATED)
        FetchContent_Populate(wgpu_native_upstream)
    endif()

    find_program(VIVID_CARGO_EXECUTABLE cargo REQUIRED)
    set(VIVID_WGPU_NATIVE_UPSTREAM_LIB
        "${wgpu_native_upstream_SOURCE_DIR}/target/release/libwgpu_native.dylib")

    add_custom_target(wgpu_native_upstream_build ALL
        COMMAND ${VIVID_CARGO_EXECUTABLE} build --release
        WORKING_DIRECTORY ${wgpu_native_upstream_SOURCE_DIR}
        COMMENT "Building upstream wgpu-native from source (Metal interop symbols)"
        VERBATIM
    )

    if(TARGET webgpu)
        add_dependencies(webgpu wgpu_native_upstream_build)
        set_target_properties(webgpu PROPERTIES IMPORTED_LOCATION "${VIVID_WGPU_NATIVE_UPSTREAM_LIB}")
    endif()
    set(WEBGPU_RUNTIME_LIB "${VIVID_WGPU_NATIVE_UPSTREAM_LIB}" CACHE INTERNAL "Path to the WebGPU library binary" FORCE)
endif()

FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket
    GIT_TAG        v11.4.5
    GIT_SHALLOW    TRUE
)
set(USE_TLS OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ixwebsocket)

# CLI11 - Command-line argument parser (header-only)
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.6.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(cli11)

# snappy (used by HAP frame decoder)
FetchContent_Declare(
    snappy
    GIT_REPOSITORY https://github.com/google/snappy.git
    GIT_TAG        1.2.1
    GIT_SHALLOW    TRUE
)
set(SNAPPY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SNAPPY_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SNAPPY_FUZZING_BUILD OFF CACHE BOOL "" FORCE)
set(SNAPPY_REQUIRE_AVX OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(snappy)

# --- nlohmann/json (header-only JSON library) ---
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# --- Dragonbox (shortest float-to-decimal conversion) ---
FetchContent_Declare(
    dragonbox
    GIT_REPOSITORY https://github.com/jk-jeon/dragonbox.git
    GIT_TAG        1.1.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(dragonbox)

# --- GLFW (submodule) ---
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/glfw)

# --- glfw3webgpu bridge (submodule) ---
add_subdirectory(deps/glfw3webgpu)

# --- miniaudio (vendored, header-only) ---
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE deps/miniaudio)

# --- stb_truetype (vendored, header-only) ---
add_library(stb_truetype INTERFACE)
target_include_directories(stb_truetype INTERFACE deps/stb)

# --- NanoSVG (header-only SVG parser + rasterizer) ---
add_library(nanosvg INTERFACE)
target_include_directories(nanosvg INTERFACE deps/nanosvg)

# --- RtMidi (vendored, CoreMIDI backend on macOS) ---
add_library(rtmidi STATIC deps/rtmidi/RtMidi.cpp)
target_include_directories(rtmidi PUBLIC deps/rtmidi)
target_compile_definitions(rtmidi PUBLIC __MACOSX_CORE__)
target_link_libraries(rtmidi PUBLIC
    "-framework CoreMIDI"
    "-framework CoreAudio"
    "-framework CoreFoundation"
)

# --- oscpack (vendored OSC over UDP) ---
if(WIN32)
    set(VIVID_OSCPACK_IP_PATH deps/oscpack/ip/win32)
else()
    set(VIVID_OSCPACK_IP_PATH deps/oscpack/ip/posix)
endif()

add_library(oscpack STATIC
    deps/oscpack/ip/IpEndpointName.cpp
    ${VIVID_OSCPACK_IP_PATH}/NetworkingUtils.cpp
    ${VIVID_OSCPACK_IP_PATH}/UdpSocket.cpp
    deps/oscpack/osc/OscTypes.cpp
    deps/oscpack/osc/OscReceivedElements.cpp
    deps/oscpack/osc/OscPrintReceivedElements.cpp
    deps/oscpack/osc/OscOutboundPacketStream.cpp
)
target_include_directories(oscpack PUBLIC deps/oscpack)
if(WIN32)
    target_link_libraries(oscpack PUBLIC ws2_32 winmm)
endif()

# --- Syphon runtime (macOS) ---
if(APPLE)
    add_library(syphon_runtime SHARED
        deps/syphon/SyphonCFMessageReceiver.m
        deps/syphon/SyphonCFMessageSender.m
        deps/syphon/SyphonClientBase.m
        deps/syphon/SyphonClientConnectionManager.m
        deps/syphon/SyphonDispatch.c
        deps/syphon/SyphonMessageQueue.m
        deps/syphon/SyphonMessageReceiver.m
        deps/syphon/SyphonMessageSender.m
        deps/syphon/SyphonMessaging.m
        deps/syphon/SyphonMetalClient.m
        deps/syphon/SyphonMetalServer.m
        deps/syphon/SyphonPrivate.m
        deps/syphon/SyphonServerBase.m
        deps/syphon/SyphonServerConnectionManager.m
        deps/syphon/SyphonServerDirectory.m
        deps/syphon/SyphonServerRendererMetal.m
    )
    target_include_directories(syphon_runtime PUBLIC deps/syphon deps)
    target_link_libraries(syphon_runtime PUBLIC
        "-framework Foundation"
        "-framework AppKit"
        "-framework Metal"
        "-framework IOSurface"
        "-framework CoreVideo"
        "-framework QuartzCore"
        "-framework CoreFoundation"
    )
    target_compile_options(syphon_runtime PRIVATE "-include${CMAKE_SOURCE_DIR}/deps/syphon/Syphon_Prefix.pch")
    set_source_files_properties(
        deps/syphon/SyphonCFMessageReceiver.m
        deps/syphon/SyphonCFMessageSender.m
        deps/syphon/SyphonClientBase.m
        deps/syphon/SyphonClientConnectionManager.m
        deps/syphon/SyphonMessageQueue.m
        deps/syphon/SyphonMessageReceiver.m
        deps/syphon/SyphonMessageSender.m
        deps/syphon/SyphonMessaging.m
        deps/syphon/SyphonMetalClient.m
        deps/syphon/SyphonMetalServer.m
        deps/syphon/SyphonPrivate.m
        deps/syphon/SyphonServerBase.m
        deps/syphon/SyphonServerConnectionManager.m
        deps/syphon/SyphonServerDirectory.m
        deps/syphon/SyphonServerRendererMetal.m
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
endif()

