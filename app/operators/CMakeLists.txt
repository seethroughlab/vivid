# Loadable visual operators — each built as a standalone MODULE plugin that the app
# dlopen's at runtime (P2). The plugin links the SAME shared libwgpu_native as the host
# (target `webgpu`), so a host-created WGPUDevice handed in via VividGpuContext is valid
# inside the operator: one wgpu instance, two images.
#
# Self-contained against operator_api/ headers only. macOS is the verified platform;
# the non-Apple branches make the build cross-platform-ready (see app/src/platform/).

if(APPLE)
    set(VIVID_PLUGIN_SUFFIX ".dylib")
elseif(WIN32)
    set(VIVID_PLUGIN_SUFFIX ".dll")
else()
    set(VIVID_PLUGIN_SUFFIX ".so")
endif()

# add_vivid_operator(<name> <source>...) — a MODULE plugin copied into the host's
# PlugIns/ next to the executable + libwgpu_native.
function(add_vivid_operator name)
    add_library(${name} MODULE ${ARGN})
    set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR}/../deps/stb)
    target_link_libraries(${name} PRIVATE webgpu)
    if(APPLE)
        # wgpu symbols import from @rpath/libwgpu_native.dylib (already loaded by the
        # host); the rpath lets a fresh dlopen also resolve it from Contents/MacOS/.
        # -undefined dynamic_lookup future-proofs operator→host runtime callbacks.
        target_link_options(${name} PRIVATE
            -undefined dynamic_lookup
            "LINKER:-rpath,@loader_path/../MacOS")
        set(_plugin_dest "$<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns")
    else()
        # Non-Apple: the executable is not a bundle; plugins sit in a PlugIns/ dir next
        # to it and resolve libwgpu_native (copied beside the exe) via the loader's dir.
        if(UNIX)
            target_link_options(${name} PRIVATE
                "LINKER:--allow-shlib-undefined" "LINKER:-rpath,$ORIGIN")
        endif()
        set(_plugin_dest "$<TARGET_FILE_DIR:vivid>/PlugIns")
    endif()
    # Drop the plugin into the host's PlugIns/ so the startup scan finds it.
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_plugin_dest}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${name}>" "${_plugin_dest}/"
        VERBATIM)
endfunction()

add_vivid_operator(vivid_spike_solid spike/spike_solid.cpp)
add_vivid_operator(vivid_step_bars steps/step_bars.cpp)   # UI-4b: exports a custom editor

# core-visuals: the auto-discovered core visual nodes (migrating off builtin_ops.cpp).
add_vivid_operator(vivid_image packages/core-visuals/image.cpp)
add_vivid_operator(vivid_switch packages/core-visuals/switch.cpp)
# P8 migration: built-ins moved out of builtin_ops.cpp into packages (Bucket A).
add_vivid_operator(vivid_shape_grid packages/core-visuals/shape_grid.cpp)
add_vivid_operator(vivid_notes packages/core-visuals/notes_op.cpp)
add_vivid_operator(vivid_instancer packages/core-visuals/instancer.cpp)
add_vivid_operator(vivid_lines packages/core-visuals/lines.cpp)
add_vivid_operator(vivid_mesh packages/core-visuals/mesh.cpp)
# Model: a glTF/GLB loader — needs nlohmann/json (stb_image + webgpu come free with add_vivid_operator).
add_vivid_operator(vivid_model packages/core-visuals/model.cpp)
target_link_libraries(vivid_model PRIVATE nlohmann_json::nlohmann_json)
# Geometry pipeline (ADR-0025): mesh values flow between nodes. MeshLoad parses glTF (needs json);
# MeshRender + MeshDisplace are geometry-only (no parser). All three share operator_api/gltf_mesh.h
# + mesh_render.h with Model.
add_vivid_operator(vivid_mesh_load packages/core-visuals/mesh_load.cpp)
target_link_libraries(vivid_mesh_load PRIVATE nlohmann_json::nlohmann_json)
add_vivid_operator(vivid_mesh_render packages/core-visuals/mesh_render_op.cpp)
add_vivid_operator(vivid_mesh_displace packages/core-visuals/mesh_displace.cpp)
add_vivid_operator(vivid_feedback packages/core-visuals/feedback.cpp)
# Video: a SELF-DECODING movie source op. On macOS it owns the AVFoundation + HAP decoder layer
# (packages/core-visuals/movie/) and the vendored HAP lib (deps/hap/hap.c + snappy); the classic-
# ported .mm files use ARC, avf_decoder.mm uses manual retain/release. Off-macOS it degrades to a
# black source (decoder_factory returns unsupported).
if(APPLE)
    add_vivid_operator(vivid_video
        packages/core-visuals/video.cpp
        packages/core-visuals/movie/codec_probe.mm
        packages/core-visuals/movie/avf_decoder.mm
        packages/core-visuals/movie/hap_decoder.mm
        packages/core-visuals/movie/avf_audio_extractor.mm
        packages/core-visuals/movie/decoder_factory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../deps/hap/hap.c)
    target_include_directories(vivid_video PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../deps/hap
        ${CMAKE_CURRENT_SOURCE_DIR}/packages/core-visuals/movie)
    target_link_libraries(vivid_video PRIVATE snappy
        "-framework AVFoundation" "-framework AVFAudio" "-framework CoreMedia" "-framework CoreVideo"
        "-framework QuartzCore" "-framework Foundation" "-framework CoreGraphics")
    # the AVFoundation .mm files use ARC
    set_source_files_properties(
        packages/core-visuals/movie/codec_probe.mm
        packages/core-visuals/movie/hap_decoder.mm
        packages/core-visuals/movie/avf_decoder.mm
        packages/core-visuals/movie/avf_audio_extractor.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
    add_vivid_operator(vivid_video
        packages/core-visuals/video.cpp
        packages/core-visuals/movie/decoder_factory.cpp)
endif()
add_vivid_operator(vivid_output packages/core-visuals/output.cpp)
add_vivid_operator(vivid_customshader packages/core-visuals/customshader.cpp)
# Text/VectorText need FreeType (the app fetches it) + the bundled font path.
foreach(_txt text vectortext)
    add_vivid_operator(vivid_${_txt} packages/core-visuals/${_txt}.cpp)
    target_link_libraries(vivid_${_txt} PRIVATE freetype)
    target_compile_definitions(vivid_${_txt} PRIVATE
        VIVID_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../assets/fonts/JetBrainsMono-Regular.ttf")
endforeach()

# Webcam: the one native operator — bundles an AVFoundation capture .mm and links the
# camera frameworks (the install-time clang++ package route can't; this app-build CMake
# route can, and the dylib is still auto-discovered at launch). Off-macOS it builds from
# webcam.cpp alone and degrades to a black "No camera" source.
if(APPLE)
    add_vivid_operator(vivid_webcam packages/core-visuals/webcam.cpp packages/core-visuals/avf_capture.mm)
    target_link_libraries(vivid_webcam PRIVATE
        "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo" "-framework Foundation")
else()
    add_vivid_operator(vivid_webcam packages/core-visuals/webcam.cpp)
endif()

# vivid-3d scene-graph spine (ADR-0041, Phase 0 spike). Vendored from ../vivid-3d onto the trunk ABI.
# Each op needs the package-local operator_api/ (gpu_3d.h, port_type_registry.h shim) + linmath.h,
# so the package dir goes on the include path alongside the ../src default from add_vivid_operator.
foreach(_v3d render_3d scene_merge shape3d light3d)
    add_vivid_operator(vivid_${_v3d} packages/vivid-3d/${_v3d}.cpp)
    target_include_directories(vivid_${_v3d} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/packages/vivid-3d)
endforeach()
