cmake_minimum_required(VERSION 3.20)
project(vivid VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Single version source of truth (P4.1): project(VERSION) -> generated version.h.
# The bundle version strings + the get_version endpoint + the version-guard CI check
# all read from here, so there's exactly one number to bump.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
endif()

# ---- build options ----
# The GUI app pulls heavy, partly macOS-only deps (wgpu/glfw/VST3/AVFoundation);
# the headless test suite needs none of them. Keeping them separable lets CI
# build+run the tests fast and cross-platform (VIVID_BUILD_APP=OFF).
option(VIVID_BUILD_APP   "Build the GUI/GPU/audio app (heavy deps; macOS)" ON)
option(VIVID_BUILD_TESTS "Build the headless test suite (cheap, portable)"  ON)
option(VIVID_SANITIZE        "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(VIVID_SANITIZE_THREAD "Enable ThreadSanitizer (mutually exclusive with VIVID_SANITIZE)" OFF)
# ADR-0017: the undo completeness guard — asserts (each frame) that no document edit bypassed the
# EditGateway. On for dev/CI audit builds; off in release (it re-serializes the session per frame).
option(VIVID_UNDO_AUDIT      "Assert every document mutation routes through the edit gateway" OFF)

if(VIVID_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-optimize-sibling-calls)
    add_link_options(-fsanitize=address,undefined)
endif()
if(VIVID_SANITIZE_THREAD)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()
if(VIVID_UNDO_AUDIT)
    add_compile_definitions(VIVID_UNDO_AUDIT)
endif()

# nlohmann/json is header-only and shared by the app AND the headless tests
# (control-server JSON shape, persistence), so fetch it before the app guard.
include(FetchContent)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# --- signalsmith-stretch (vendored, header-only, MIT) — pitch-preserving time-stretch
# for the audio-clip warp engine (A2). Bundles its signalsmith-linear dependency.
# Defined BEFORE the tests subdir + the tests-only `return()` below: it's header-only and
# portable, and `test_clip_dsp` links it, so it must exist even when VIVID_BUILD_APP=OFF. ---
add_library(signalsmith_stretch INTERFACE)
target_include_directories(signalsmith_stretch INTERFACE deps/signalsmith-stretch)

if(VIVID_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    # Production gate (P4.2): `cmake --build … --target production_gate_core` runs the
    # HEADLESS_SMOKE suite through the budget-checked report tool. Self-contained (just
    # ctest + uv), so it works in the tests-only (VIVID_BUILD_APP=OFF) CI configure too.
    add_custom_target(production_gate_core
        COMMAND ${CMAKE_COMMAND} -E env VIVID_BUILD_DIR=${CMAKE_BINARY_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/run_production_gate.sh core
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
        USES_TERMINAL
        COMMENT "Production gate (core profile)")
endif()

if(NOT VIVID_BUILD_APP)
    return()  # tests-only configure: skip all the heavy app deps + target below
endif()

# --- wgpu-native via eliemichel's WebGPU-distribution (precompiled) ---
set(WGPU_VERSION "v29.0.0.0" CACHE STRING "wgpu-native release" FORCE)
# The distribution auto-detects arch from CMAKE_SYSTEM_PROCESSOR (the HOST). That breaks when the
# GitHub Actions self-hosted runner runs under Rosetta (host reports x86_64) while we target arm64
# (forced above): it fetches the x86_64 wgpu and fails to link against the arm64 build. Pin the
# distribution's ARCH to our TARGET arch (CMAKE_OSX_ARCHITECTURES) — detect_system_architecture
# honors a pre-set ARCH (it early-outs on `if(NOT ARCH)`).
if(APPLE AND NOT ARCH)
    if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
        set(ARCH "aarch64")
    elseif(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
        set(ARCH "x86_64")
    endif()
endif()
FetchContent_Declare(webgpu
    GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution
    GIT_TAG        17dcd42a7683355e7a40ac4e97e77f36dff5b5ab
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(webgpu)

# --- GLFW (window + input) ---
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(glfw)

# --- glfw3webgpu (GLFW <-> WebGPU surface bridge) ---
# Pinned to a specific commit (was tracking `main`) for reproducible builds. This
# is the exact commit the app has been building against; the project's latest tag
# (v1.2.0) is newer and untested here, so bump deliberately, not implicitly.
# GIT_SHALLOW is FALSE here: a shallow clone can't reliably fetch a commit that
# isn't a branch/tag tip, and this is a mid-history SHA. The repo is tiny.
FetchContent_Declare(glfw3webgpu
    GIT_REPOSITORY https://github.com/eliemichel/glfw3webgpu
    GIT_TAG        8f14534e79c590e8a7f7795ea42e01bedea9e137
    GIT_SHALLOW    FALSE)
FetchContent_MakeAvailable(glfw3webgpu)

# --- miniaudio (vendored, header-only) ---
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE deps/miniaudio)

# --- nlohmann/json: fetched above the app guard (shared with headless tests) ---

# --- cpp-httplib (header-only; loopback control server for MCP) ---
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE    OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE  OFF CACHE BOOL "" FORCE)
FetchContent_Declare(cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib
    GIT_TAG        v0.18.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(cpp_httplib)

# --- VST3 SDK (header-only pluginterfaces + IID sources) ---
# SOURCE_SUBDIR points at a nonexistent dir so MakeAvailable populates the
# sources without running the SDK's own CMakeLists (hundreds of unwanted targets).
FetchContent_Declare(vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG        v3.8.0_build_66
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES "base pluginterfaces"
    SOURCE_SUBDIR  _skip_sdk_cmake)
FetchContent_MakeAvailable(vst3sdk)

add_library(vst3_headers INTERFACE)
target_include_directories(vst3_headers INTERFACE ${vst3sdk_SOURCE_DIR})

add_library(vst3_iids STATIC
    ${vst3sdk_SOURCE_DIR}/pluginterfaces/base/coreiids.cpp
    ${vst3sdk_SOURCE_DIR}/base/source/baseiids.cpp
    src/audio/vst3_vstiids.cpp)
target_link_libraries(vst3_iids PUBLIC vst3_headers)

# --- CLAP SDK (header-only C API; hosts CLAP plugins e.g. Surge XT / Surge XT Effects) ---
# SOURCE_SUBDIR points at a nonexistent dir so MakeAvailable populates the headers
# without running CLAP's own CMakeLists.
FetchContent_Declare(clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG        1.2.8
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _skip_clap_cmake)
FetchContent_MakeAvailable(clap)
add_library(clap_headers INTERFACE)
target_include_directories(clap_headers INTERFACE ${clap_SOURCE_DIR}/include)

# --- FreeType (real font rendering — hinted glyph atlas + glyph outlines for text geometry).
# Vendored via FetchContent as vivid-4 did; minimal build (TrueType only, no optional codecs). ---
set(FT_DISABLE_ZLIB     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   ON CACHE BOOL "" FORCE)
FetchContent_Declare(freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG        VER-2-13-3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(freetype)

# snappy — used by the vendored HAP frame decoder (deps/hap/hap.c). Minimal build:
# no tests / benchmarks / fuzzing / forced-AVX (matches how classic vendored it).
set(SNAPPY_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(SNAPPY_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SNAPPY_FUZZING_BUILD    OFF CACHE BOOL "" FORCE)
set(SNAPPY_REQUIRE_AVX      OFF CACHE BOOL "" FORCE)
set(SNAPPY_INSTALL          OFF CACHE BOOL "" FORCE)
# snappy's CMakeLists declares cmake_minimum_required < 3.5, which CMake 4.x refuses; this floor
# lets the old-policy subproject configure. Scoped to the deps here (our own targets are modern).
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "policy floor for old vendored deps (snappy)" FORCE)
FetchContent_Declare(snappy
    GIT_REPOSITORY https://github.com/google/snappy.git
    GIT_TAG        1.2.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(snappy)

# MACOSX_BUNDLE is required so hosted VST3 plugin GUI windows receive mouse
# events: a non-bundled CLI process can't become a proper foreground app, so its
# secondary NSWindows render but stay non-interactive.
add_executable(vivid MACOSX_BUNDLE
    src/main.cpp
    src/app/app.cpp
    src/app/undo_manager.cpp
    src/app/edit_gateway.cpp
    src/app/autosave.cpp
    src/app/crash_recovery.cpp
    src/app/crash_recovery_snapshot.cpp
    src/app/quarantine.cpp
    src/app/log.cpp
    src/app/input.cpp
    src/app/input_typing.cpp
    src/app/input_transport.cpp
    src/app/input_editor.cpp
    src/app/input_graph.cpp
    src/app/input_dock.cpp
    src/app/input_clipgrid.cpp
    src/app/frame.cpp
    src/app/editor_window.cpp
    src/app/project_io.cpp
    src/app/file_actions.cpp
    src/app/examples.cpp
    src/app/node_presets.cpp
    src/app/node_presets_graph.cpp
    src/app/window_prefs.cpp
    src/app/video_recorder.cpp
    src/app/operator_clone.cpp
    src/app/runtime_health.cpp
    src/app/runtime_health_collect.cpp
    src/miniaudio_impl.c
    src/gpu/gpu_context.cpp
    src/gpu/splash.cpp
    src/gpu/visual_graph.cpp
    src/gpu/shader_op.cpp
    src/gpu/shader_file_op.cpp
    src/gpu/shader_library.cpp
    src/gpu/file_drop_registry.cpp
    src/gpu/effect_op.cpp
    src/audio/vst3_host.cpp
    src/audio/vst3_host_render.cpp
    src/audio/vst3_host_presets.cpp
    src/audio/vst3_host_params.cpp
    src/audio/vst3_host_analysis.cpp
    src/audio/vst3_host_clap_loader.cpp
    src/audio/plugin_catalog.cpp
    src/audio/plugin_cache.cpp
    src/audio/plugin_probe.cpp
    src/audio/plugin_scan.cpp
    src/audio/audio_callback.cpp
    src/audio/movie_audio_bus.cpp
    src/audio/note_bus.cpp
    src/audio/spectrum_bus.cpp
    src/audio/note_event_bus.cpp
    src/audio/audio_op_runtime.cpp
    src/audio/audio_graph.cpp
    src/audio/builtin_audio_ops.cpp
    src/audio/glitch/glitch_ops.cpp
    src/audio/audio_clip.cpp
    src/audio/vst3_plugin_window_stub.cpp
    src/audio/clap_plugin_window_stub.cpp
    src/platform/platform.cpp
    src/platform/frame_loop_generic.cpp
    src/platform/app_nap_stub.cpp
    src/platform/sparkle_bridge_stub.cpp
    src/platform/file_dialog_stub.cpp
    src/platform/menu_bar_stub.cpp
    src/platform/av_exporter_stub.cpp
    src/platform/midi_input_stub.cpp
    src/platform/gemini_client_stub.cpp
    src/platform/secret_store_stub.cpp
    src/audio/music_eval.cpp
    src/persist.cpp
    src/persist_undo.cpp
    src/operator_api/operator_descriptor_validation.cpp
    src/operator_api/shader_meta.cpp
    src/gpu/op_runtime.cpp
    src/gpu/operator_loader.cpp
    src/gpu/loaded_operator.cpp
    src/gpu/operator_scan.cpp
    src/packages/package_manifest.cpp
    src/packages/package_compiler.cpp
    src/packages/package_manager.cpp
    src/packages/file_watcher.cpp
    src/packages/hot_reload.cpp
    src/packages/hot_reload_manager.cpp
    src/cli/control_server.cpp
    src/cli/audio_analysis_tools.cpp
    src/cli/image_analysis_tools.cpp
    src/cli/control_handlers_audio_analysis.cpp
    src/cli/control_handlers_visual_analysis.cpp
    src/cli/control_handlers_audio_clip_pool.cpp
    src/cli/control_handlers_audio_devices.cpp
    src/cli/control_handlers_audio_graph.cpp
    src/cli/control_handlers_audio_catalog.cpp
    src/cli/control_handlers_introspection.cpp
    src/cli/control_handlers_mappings.cpp
    src/cli/control_handlers_visuals.cpp
    src/cli/control_handlers_audio.cpp
    src/cli/control_handlers_project.cpp
    src/cli/project_recovery.cpp
    src/cli/control_handlers_packages.cpp
    src/cli/control_handlers_edit.cpp
    src/cli/control_handlers_music_eval.cpp
    src/cli/control_handlers_video_export.cpp
    src/cli/edit_methods.cpp
    src/ui/renderer_2d.cpp
    src/ui/mapping_overview.cpp
    src/ui/shader_library_view.cpp
    src/ui/diagnostics_panel.cpp
    src/ui/toasts.cpp
    src/ui/preset_popover.cpp
    src/ui/session_view.cpp
    src/ui/chooser.cpp
    src/ui/node_graph.cpp
    src/ui/audio_node_graph.cpp
    src/ui/clip_editor.cpp)
# macOS-only backends: AVFoundation video, Cocoa VST3-GUI, the CFRunLoop frame loop,
# and App-Nap. Their portable counterparts (the *_stub.cpp / frame_loop_generic.cpp /
# app_nap_stub.cpp above) are compiled on every OS and select via #ifdef, so exactly
# one backend of each is linked per platform.
if(APPLE)
    target_sources(vivid PRIVATE
        src/audio/vst3_plugin_window.mm
        src/audio/clap_plugin_window.mm
        src/platform/macos_frame_timer.cpp
        src/platform/macos_app_nap.mm
        src/platform/file_dialog.mm
        src/platform/menu_bar.mm
        src/platform/av_exporter.mm
        src/platform/midi_input.mm
        src/platform/gemini_client_macos.mm
        src/platform/secret_store_macos.mm
        platform/macos/Vivid.icns)   # app icon (the node-graph "V"), copied into Resources below
    set_source_files_properties(platform/macos/Vivid.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(vivid PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Vivid"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.vivid.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE Vivid.icns
        # Custom Info.plist template adds NSCameraUsageDescription (Webcam node needs the
        # camera-usage key or macOS TCC kills the app on camera access) + retina.
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/Info.plist.in")
endif()

# ADR-0016: the SHIPPED shader library. Every .wgsl/.glsl in app/shaders/ is an operator
# (its JSON header declares its params); the startup scan registers each as a type. They go
# beside the operator dylibs' PlugIns/ copy, in Resources/shaders — a non-bundle build reads
# <exe_dir>/shaders instead, and $VIVID_SHADERS_DIR overrides the user tier for development.
if(APPLE)
    set(_shader_dest "$<TARGET_BUNDLE_CONTENT_DIR:vivid>/Resources/shaders")
else()
    set(_shader_dest "$<TARGET_FILE_DIR:vivid>/shaders")
endif()
# Its OWN target, not a POST_BUILD on `vivid`: a POST_BUILD step only runs when the app target
# actually relinks, so editing a .wgsl and rebuilding would leave the STALE shader in the bundle
# and you would be debugging the last version of your file. (That is exactly what happened once.)
file(GLOB VIVID_SHADER_FILES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.wgsl" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.glsl")
add_custom_target(vivid_shader_library ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_shader_dest}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VIVID_SHADER_FILES} "${_shader_dest}"
    DEPENDS ${VIVID_SHADER_FILES}
    COMMENT "Copying the bundled shader library into the app"
    VERBATIM)
add_dependencies(vivid_shader_library vivid)   # the bundle dir has to exist first

# ADR-0021/P2: the bundled EXAMPLE projects. Each examples/demos/projects/<name>/ is a folder
# project (project.json + its .glsl); copy the tree into Resources/examples so File > Open Example
# can list them. Same bundle/non-bundle destination convention as the shader library above.
if(APPLE)
    set(_examples_dest "$<TARGET_BUNDLE_CONTENT_DIR:vivid>/Resources/examples")
else()
    set(_examples_dest "$<TARGET_FILE_DIR:vivid>/examples")
endif()
set(_examples_src "${CMAKE_CURRENT_SOURCE_DIR}/../examples/demos/projects")
if(EXISTS "${_examples_src}")
    add_custom_target(vivid_examples ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_examples_dest}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_examples_src}" "${_examples_dest}"
        COMMENT "Copying the bundled example projects into the app"
        VERBATIM)
else()
    add_custom_target(vivid_examples ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_examples_dest}"
        COMMAND ${CMAKE_COMMAND} -E echo "No bundled example projects found at ${_examples_src}; creating an empty examples dir"
        COMMENT "Preparing empty bundled examples directory"
        VERBATIM)
endif()
add_dependencies(vivid_examples vivid)   # the bundle dir has to exist first
target_include_directories(vivid PRIVATE src src/gpu src/audio src/ui deps/stb
    ${CMAKE_CURRENT_BINARY_DIR}/generated)   # generated version.h (P4.1)
# Package compiler (P2.3) compiles operator sources at runtime via clang++; pass it
# the toolchain + the in-tree operator_api headers + the wgpu include/lib dirs.
file(GLOB _wgpu_dist "${CMAKE_BINARY_DIR}/_deps/wgpu-*-src")
set(_wgpu_root "")
if(_wgpu_dist)
    list(GET _wgpu_dist 0 _wgpu_root)
endif()
# Fail loudly rather than baking in a "/include" that only shows up as a runtime clang error
# inside the package compiler (that silent failure is exactly what broke test_operator_clone
# on a fresh configure).
if(NOT _wgpu_root)
    message(FATAL_ERROR "wgpu dist not found under ${CMAKE_BINARY_DIR}/_deps — the package compiler "
                        "needs its include/lib dirs. Is FetchContent_MakeAvailable(webgpu) above this?")
endif()

# ADR-0020: ship the operator COMPILE toolchain so an operator can be built on an end-user's
# machine, not only a dev checkout. PackageCompiler resolves these bundle-relative at runtime
# (Resources/operator_api and Resources/webgpu), with the baked VIVID_PKG_* dev paths as fallback.
# libwgpu_native.dylib is already in Contents/MacOS (target_copy_webgpu_binaries), so only the
# HEADERS need shipping. Own target (not POST_BUILD on vivid) for the same reason as the shaders:
# a POST_BUILD only fires when the app relinks, leaving stale headers after a header-only edit.
if(APPLE)
    set(_opapi_dest "$<TARGET_BUNDLE_CONTENT_DIR:vivid>/Resources/operator_api")
    set(_wgpuhdr_dest "$<TARGET_BUNDLE_CONTENT_DIR:vivid>/Resources/webgpu/webgpu")
else()
    set(_opapi_dest "$<TARGET_FILE_DIR:vivid>/operator_api")
    set(_wgpuhdr_dest "$<TARGET_FILE_DIR:vivid>/webgpu/webgpu")
endif()
add_custom_target(vivid_operator_headers ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/src/operator_api" "${_opapi_dest}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${_wgpu_root}/include/webgpu" "${_wgpuhdr_dest}"
    COMMENT "Copying the operator_api + webgpu headers into the app (ADR-0020 shippable toolchain)"
    VERBATIM)
add_dependencies(vivid_operator_headers vivid)   # the bundle dir has to exist first
# test_operator_clone drives the same package compiler, so it needs the same wgpu dirs. They are
# set HERE (not in tests/CMakeLists) because tests/ is added before webgpu is fetched.
if(VIVID_BUILD_TESTS AND TARGET test_operator_clone)
    target_compile_definitions(test_operator_clone PRIVATE
        VIVID_PKG_WEBGPU_INCLUDE_DIR="${_wgpu_root}/include"
        VIVID_PKG_WEBGPU_LIB_DIR="${_wgpu_root}/lib")
endif()
# Same reason (tests/ is added before webgpu is fetched): test_shader_library compiles
# ShaderFileOp, which holds WGPU handles, so it needs the webgpu target's includes + lib.
if(VIVID_BUILD_TESTS AND TARGET test_shader_library)
    target_link_libraries(test_shader_library PRIVATE webgpu)
endif()
target_compile_definitions(vivid PRIVATE
    VIVID_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/JetBrainsMono-Regular.ttf"
    VIVID_PKG_CXX="${CMAKE_CXX_COMPILER}"
    VIVID_PKG_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src"
    VIVID_PKG_WEBGPU_INCLUDE_DIR="${_wgpu_root}/include"
    VIVID_PKG_WEBGPU_LIB_DIR="${_wgpu_root}/lib"
    VIVID_PKG_ARCH="${CMAKE_OSX_ARCHITECTURES}")
target_link_libraries(vivid PRIVATE webgpu glfw glfw3webgpu miniaudio signalsmith_stretch vst3_iids clap_headers freetype z nlohmann_json::nlohmann_json httplib::httplib)

if(APPLE)
    target_link_libraries(vivid PRIVATE
        "-framework Cocoa" "-framework Metal" "-framework QuartzCore"
        "-framework IOKit" "-framework CoreAudio" "-framework AudioToolbox"
        "-framework CoreMIDI" "-framework CoreFoundation" "-framework Security"
        "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo")
    # The bundled libwgpu_native.dylib sits next to the executable in Contents/MacOS
    # (target_copy_webgpu_binaries), and its install name is @rpath/libwgpu_native.dylib. Add an
    # @executable_path rpath so a signed/distributed .app resolves it OFF the build machine — the wgpu
    # import target also injects an absolute build-tree rpath, which dyld harmlessly skips once the app
    # is moved. Mirrors the operator plugins' @loader_path/../MacOS rpath (operators/CMakeLists.txt).
    target_link_options(vivid PRIVATE "-Wl,-rpath,@executable_path")
endif()

# Copy the wgpu-native dylib next to the executable at build time.
target_copy_webgpu_binaries(vivid)

# Loadable operator dylibs (P2). Declared after vivid so the operator targets
# can reference the bundle (for the PlugIns/ copy) + force them to build with the app.
add_subdirectory(operators)
add_dependencies(vivid vivid_spike_solid)

# ADR-0025: headless SESSION-EXECUTOR test. Links the audio engine (session_process + native ops +
# graph) WITHOUT the GPU/window stack, so the executor can be driven directly and its rendered output
# asserted. The engine reaches CoreFoundation (VST3 bundle loading), so this is macOS-only and lives in
# the full-app configure (VIVID_BUILD_APP=ON) — not the portable Linux tests tier. Runs via ctest.
if(VIVID_BUILD_TESTS AND APPLE)
    find_package(Threads REQUIRED)
    # The audio-engine sibling TUs both these tests link (no GPU/window). Shared so the concurrency
    # harness stays in lockstep with the executor test.
    set(_audio_engine_srcs
        src/audio/vst3_host.cpp
        src/audio/vst3_host_render.cpp
        src/audio/vst3_host_presets.cpp
        src/audio/vst3_host_params.cpp
        src/audio/vst3_host_analysis.cpp
        src/audio/vst3_host_clap_loader.cpp
        src/audio/audio_op_runtime.cpp
        src/audio/builtin_audio_ops.cpp
        src/audio/movie_audio_bus.cpp
        src/audio/note_bus.cpp
        src/audio/spectrum_bus.cpp
        src/audio/glitch/glitch_ops.cpp
        src/audio/audio_clip.cpp
        src/audio/audio_graph.cpp
        src/audio/plugin_catalog.cpp
        src/gpu/op_runtime.cpp
        src/operator_api/operator_descriptor_validation.cpp
        src/audio/vst3_plugin_window_stub.cpp
        src/audio/clap_plugin_window_stub.cpp
        src/miniaudio_impl.c)
    set(_audio_engine_libs
        signalsmith_stretch vst3_iids clap_headers miniaudio nlohmann_json::nlohmann_json Threads::Threads z
        "-framework CoreFoundation")

    # ADR-0030/0034: a minimal CLAP plugin built from source, as a .clap bundle, so tests can load a
    # REAL plugin without anything installed on CI. CLAP's ABI is MIT + header-only (clap_headers,
    # already vendored), so the whole plugin is one file. The bundle layout (Contents/MacOS/<exe> +
    # Info.plist) is what clap_host.h's CFBundle loader expects. Defined before the test targets so any
    # of them can depend on it. Consumers add: add_dependencies(<t> vivid_test_clap) +
    # target_compile_definitions(<t> PRIVATE VIVID_TEST_CLAP_PATH="$<TARGET_BUNDLE_DIR:vivid_test_clap>").
    add_library(vivid_test_clap MODULE tests/fixtures/vivid_test_clap.cpp)
    target_link_libraries(vivid_test_clap PRIVATE clap_headers)
    set_target_properties(vivid_test_clap PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "clap"
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/clap_bundle.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME "vivid_test_clap"
        MACOSX_BUNDLE_EXECUTABLE_NAME "vivid_test_clap")

    add_executable(test_session_executor tests/test_session_executor.cpp ${_audio_engine_srcs})
    target_include_directories(test_session_executor PRIVATE src src/audio src/gpu tests)
    target_link_libraries(test_session_executor PRIVATE ${_audio_engine_libs})
    add_test(NAME test_session_executor COMMAND test_session_executor)
    set_tests_properties(test_session_executor PROPERTIES LABELS "AUDIO_ENGINE")

    # ADR-0029 phase 2: the concurrent audio↔UI harness — a render thread racing a UI/mutator thread over
    # the same engine. The AUDIO_THREAD label is what the macOS ThreadSanitizer CI gate runs (-L AUDIO_THREAD).
    add_executable(test_session_concurrency tests/test_session_concurrency.cpp ${_audio_engine_srcs})
    target_include_directories(test_session_concurrency PRIVATE src src/audio src/gpu tests)
    target_link_libraries(test_session_concurrency PRIVATE ${_audio_engine_libs})
    # ADR-0034: the harness loads the in-tree CLAP fixture to race the plugin base mirror under TSan.
    add_dependencies(test_session_concurrency vivid_test_clap)
    target_compile_definitions(test_session_concurrency PRIVATE
        VIVID_TEST_CLAP_PATH="$<TARGET_BUNDLE_DIR:vivid_test_clap>")
    add_test(NAME test_session_concurrency COMMAND test_session_concurrency)
    set_tests_properties(test_session_concurrency PROPERTIES LABELS "AUDIO_ENGINE;AUDIO_THREAD")

    # ADR-0030 Phase 1: host-owned authored base cache on the plugin handles. Header-only usage of
    # Vst3Handle/ClapHandle (no plugin loaded), so it needs the SDK libs but not the engine TUs.
    add_executable(test_plugin_param_base tests/test_plugin_param_base.cpp)
    target_include_directories(test_plugin_param_base PRIVATE src src/audio src/gpu tests)
    target_link_libraries(test_plugin_param_base PRIVATE ${_audio_engine_libs})
    add_test(NAME test_plugin_param_base COMMAND test_plugin_param_base)
    set_tests_properties(test_plugin_param_base PROPERTIES LABELS "AUDIO_ENGINE")

    # ADR-0030 Phase 3: save/undo stay stable under a moving frame-bridge override (session-level,
    # native ops — the getter persist serializes keeps returning the authored base under automation).
    add_executable(test_bridge_undo_stability tests/test_bridge_undo_stability.cpp ${_audio_engine_srcs})
    target_include_directories(test_bridge_undo_stability PRIVATE src src/audio src/gpu tests)
    target_link_libraries(test_bridge_undo_stability PRIVATE ${_audio_engine_libs})
    add_test(NAME test_bridge_undo_stability COMMAND test_bridge_undo_stability)
    set_tests_properties(test_bridge_undo_stability PROPERTIES LABELS "AUDIO_ENGINE")

    add_executable(test_clap_plugin_roundtrip tests/test_clap_plugin_roundtrip.cpp ${_audio_engine_srcs})
    target_include_directories(test_clap_plugin_roundtrip PRIVATE src src/audio src/gpu tests)
    target_link_libraries(test_clap_plugin_roundtrip PRIVATE ${_audio_engine_libs})
    add_dependencies(test_clap_plugin_roundtrip vivid_test_clap)    # the .clap must exist before the test runs
    target_compile_definitions(test_clap_plugin_roundtrip PRIVATE
        VIVID_TEST_CLAP_PATH="$<TARGET_BUNDLE_DIR:vivid_test_clap>")
    add_test(NAME test_clap_plugin_roundtrip COMMAND test_clap_plugin_roundtrip)
    set_tests_properties(test_clap_plugin_roundtrip PROPERTIES LABELS "AUDIO_ENGINE")

    # ADR-0034 Phase 1: control-edge modulation reaches a CLAP plugin param (uses the same fixture).
    add_executable(test_clap_modulation tests/test_clap_modulation.cpp ${_audio_engine_srcs})
    target_include_directories(test_clap_modulation PRIVATE src src/audio src/gpu tests)
    target_link_libraries(test_clap_modulation PRIVATE ${_audio_engine_libs})
    add_dependencies(test_clap_modulation vivid_test_clap)
    target_compile_definitions(test_clap_modulation PRIVATE
        VIVID_TEST_CLAP_PATH="$<TARGET_BUNDLE_DIR:vivid_test_clap>")
    add_test(NAME test_clap_modulation COMMAND test_clap_modulation)
    set_tests_properties(test_clap_modulation PROPERTIES LABELS "AUDIO_ENGINE")
endif()
