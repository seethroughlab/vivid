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
add_vivid_operator(vivid_video packages/core-visuals/video.cpp)
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
