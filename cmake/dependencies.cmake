
# --- libcurl (system, for HTTP fetches) ---
find_package(CURL REQUIRED)

# --- wgpu-native via eliemichel's WebGPU-distribution (precompiled release) ---
# Metal interop symbols (wgpuDeviceGetNativeMetalDevice etc.) are shipped in
# upstream releases since v27.0.4.1, so no from-source build is needed.
include(FetchContent)
set(WGPU_VERSION "v29.0.0.0" CACHE STRING "wgpu-native release" FORCE)
# WebGPU-distribution picks the prebuilt binary using CMAKE_SYSTEM_PROCESSOR,
# which reflects the host cmake's arch — not the target's. On Apple Silicon
# runners where cmake runs under Rosetta that resolves to x86_64 and a
# mismatched dylib gets baked into the arm64 bundle. Honor
# CMAKE_OSX_ARCHITECTURES instead.
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    if("${CMAKE_OSX_ARCHITECTURES}" STREQUAL "arm64")
        set(ARCH "aarch64")
    elseif("${CMAKE_OSX_ARCHITECTURES}" STREQUAL "x86_64")
        set(ARCH "x86_64")
    else()
        message(FATAL_ERROR
            "CMAKE_OSX_ARCHITECTURES='${CMAKE_OSX_ARCHITECTURES}' is not a single-arch value "
            "recognized by the wgpu-native precompiled release selector (arm64 or x86_64).")
    endif()
endif()
FetchContent_Declare(
    webgpu
    GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution
    GIT_TAG        17dcd42a7683355e7a40ac4e97e77f36dff5b5ab
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(webgpu)

FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket
    GIT_TAG        v11.4.5
    GIT_SHALLOW    TRUE
)
set(USE_TLS OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ixwebsocket)

# efsw — cross-platform file system watcher (macOS/Windows/Linux)
FetchContent_Declare(
    efsw
    GIT_REPOSITORY https://github.com/SpartanJ/efsw.git
    GIT_TAG        1.5.1
    GIT_SHALLOW    TRUE
)
set(EFSW_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS_BAK ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(efsw)
set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_BAK})

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

# --- TinyXML-2 (lightweight XML parser, used by appcast) ---
FetchContent_Declare(
    tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    GIT_TAG        10.0.0
    GIT_SHALLOW    TRUE
)
set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tinyxml2)

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

# --- Google Highway (portable SIMD) ---
option(VIVID_ENABLE_HIGHWAY "Build with Google Highway SIMD acceleration" ON)
if(VIVID_ENABLE_HIGHWAY)
    FetchContent_Declare(
        highway
        GIT_REPOSITORY https://github.com/google/highway.git
        GIT_TAG        1.2.0
        GIT_SHALLOW    TRUE
    )
    set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(HWY_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(HWY_ENABLE_CONTRIB OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(highway)
endif()

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
# Tree is a snapshot of rossbencina/oscpack v1.1.0 (release 1.1, the last
# upstream release, from 2013) plus a local arm64 int32-typedef patch on
# osc/OscTypes.h, osc/OscOutboundPacketStream.h, osc/OscReceivedElements.h.
# Upstream has been dormant since 2013; rather than carry a submodule
# pointing at a local fork (unfetchable by CI) or mirror+rewrite headers
# at configure time, we vendor the tree the same way deps/rtmidi,
# deps/nanosvg, and deps/stb are vendored — as regular tracked files.
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

    # Syphon's upstream code loads Metal shaders via newDefaultLibraryWithBundle:,
    # which looks for default.metallib in the main app bundle's Resources/.
    # Try to compile from source; fall back to a pre-built copy if the Metal
    # toolchain isn't installed (newer Xcode may require:
    #   xcodebuild -downloadComponent MetalToolchain).
    set(SYPHON_METAL_SRC "${CMAKE_SOURCE_DIR}/deps/syphon/SyphonMetalShaders.metal")
    set(SYPHON_METALLIB "${CMAKE_BINARY_DIR}/syphon_default.metallib")
    set(SYPHON_METALLIB_FALLBACK "${CMAKE_SOURCE_DIR}/deps/syphon_default.metallib")

    execute_process(
        COMMAND xcrun metal -v
        RESULT_VARIABLE _metal_result
        OUTPUT_QUIET ERROR_QUIET
    )
    if(_metal_result EQUAL 0)
        set(SYPHON_AIR "${CMAKE_BINARY_DIR}/SyphonMetalShaders.air")
        add_custom_command(
            OUTPUT ${SYPHON_AIR}
            COMMAND xcrun metal -c ${SYPHON_METAL_SRC} -o ${SYPHON_AIR}
                -I ${CMAKE_SOURCE_DIR}/deps/syphon
            DEPENDS ${SYPHON_METAL_SRC}
            COMMENT "Compiling Syphon Metal shaders"
        )
        add_custom_command(
            OUTPUT ${SYPHON_METALLIB}
            COMMAND xcrun metallib ${SYPHON_AIR} -o ${SYPHON_METALLIB}
            DEPENDS ${SYPHON_AIR}
            COMMENT "Linking Syphon metallib"
        )
        add_custom_target(syphon_metallib DEPENDS ${SYPHON_METALLIB})
    else()
        message(STATUS "Metal toolchain not available — using pre-built syphon_default.metallib")
        add_custom_command(
            OUTPUT ${SYPHON_METALLIB}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${SYPHON_METALLIB_FALLBACK} ${SYPHON_METALLIB}
            DEPENDS ${SYPHON_METALLIB_FALLBACK}
        )
        add_custom_target(syphon_metallib DEPENDS ${SYPHON_METALLIB})
    endif()
    add_dependencies(syphon_runtime syphon_metallib)
endif()

# --- midifile (Standard MIDI File parser) ---
FetchContent_Declare(
    midifile
    GIT_REPOSITORY https://github.com/craigsapp/midifile.git
    GIT_TAG        98917df5b1bf0d6e8d4c0e5fff86d6b05343e793
)
FetchContent_GetProperties(midifile)
if(NOT midifile_POPULATED)
    FetchContent_Populate(midifile)
endif()

add_library(midifile STATIC
    ${midifile_SOURCE_DIR}/src/MidiFile.cpp
    ${midifile_SOURCE_DIR}/src/MidiEvent.cpp
    ${midifile_SOURCE_DIR}/src/MidiEventList.cpp
    ${midifile_SOURCE_DIR}/src/MidiMessage.cpp
    ${midifile_SOURCE_DIR}/src/Binasc.cpp
)
target_include_directories(midifile PUBLIC ${midifile_SOURCE_DIR}/include)

# --- tree-sitter C runtime (C++ parsing foundation for SourceSyntaxParser) ---
# Minimal C API only — no parser generator, no CLI, no Node/npm.
FetchContent_Declare(
    tree_sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    GIT_TAG        v0.23.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(tree_sitter)
add_library(tree_sitter_runtime STATIC
    ${tree_sitter_SOURCE_DIR}/lib/src/lib.c
)
target_include_directories(tree_sitter_runtime PUBLIC
    ${tree_sitter_SOURCE_DIR}/lib/include
)

# --- CLAP (CLever Audio Plugin) headers ---
# Pure C header-only spec for the CLAP plugin standard.
FetchContent_Declare(
    clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG        1.2.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(clap)
add_library(clap_headers INTERFACE)
target_include_directories(clap_headers INTERFACE ${clap_SOURCE_DIR}/include)

# --- VST3 SDK (pluginterfaces + base — header-only + IID sources) ---
# MIT license as of v3.8.0. Using FetchContent_Populate to skip the SDK's
# own CMakeLists.txt, which would add hundreds of unwanted build targets.
FetchContent_Declare(
    vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG        v3.8.0_build_66
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES "base pluginterfaces"
)
FetchContent_GetProperties(vst3sdk)
if(NOT vst3sdk_POPULATED)
    FetchContent_Populate(vst3sdk)
endif()

add_library(vst3_headers INTERFACE)
target_include_directories(vst3_headers INTERFACE ${vst3sdk_SOURCE_DIR})

# --- tree-sitter-cpp grammar (C/C++/ObjC/ObjC++ parser) ---
# Provides a pre-generated parser.c for C-family languages.
FetchContent_Declare(
    tree_sitter_cpp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
    GIT_TAG        v0.23.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(tree_sitter_cpp)
add_library(tree_sitter_cpp_lib STATIC
    ${tree_sitter_cpp_SOURCE_DIR}/src/parser.c
    ${tree_sitter_cpp_SOURCE_DIR}/src/scanner.c
)
target_include_directories(tree_sitter_cpp_lib PUBLIC
    ${tree_sitter_cpp_SOURCE_DIR}/src
)
target_link_libraries(tree_sitter_cpp_lib PUBLIC tree_sitter_runtime)

# --- Signalsmith Stretch (pitch-preserving time stretch, MIT) ---
# Header-only; DSP helpers are in the dsp/ subdirectory (no submodules needed).
FetchContent_Declare(signalsmith_stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        1.1.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(signalsmith_stretch)
