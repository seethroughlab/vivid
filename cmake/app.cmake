# --- UI library ---
add_library(vivid_ui STATIC
    src/ui/build_console_panel.cpp
    src/ui/rendering/renderer_2d.cpp
    src/ui/rendering/thumbnail_renderer.cpp
    src/ui/rendering/thumbnail_cache.cpp
    src/ui/style/i18n.cpp
    src/ui/graph/node_graph.cpp
    src/ui/graph/node_graph_update.cpp
    src/ui/graph/node_graph_update_drag.cpp
    src/ui/graph/node_graph_update_hover.cpp
    src/ui/graph/node_graph_draw.cpp
    src/ui/graph/node_graph_draw_inspector.cpp
    src/ui/graph/node_graph_draw_inspector_params.cpp
    src/ui/graph/node_graph_draw_inspector_sections.cpp
    src/ui/graph/node_graph_draw_connections.cpp
    src/ui/graph/node_graph_draw_overlays.cpp
    src/ui/graph/node_graph_draw_elements.cpp
    src/ui/graph/node_graph_input.cpp
    src/ui/graph/node_graph_input_click.cpp
    src/ui/graph/node_graph_input_click_context.cpp
    src/ui/graph/node_graph_input_click_widgets.cpp
    src/ui/inspector/inspector_controller.cpp
    src/ui/rendering/overlay_layouts.cpp
    src/ui/dialogs/dialog_manager.cpp
    src/ui/dialogs/dialog_manager_draw.cpp
    src/ui/dialogs/dialog_manager_input.cpp
    src/ui/style/ui_style.cpp
    src/ui/style/theme_loader.cpp
)
if(APPLE)
    target_sources(vivid_ui PRIVATE src/ui/dialogs/file_dialog.mm)
    set_source_files_properties(src/ui/dialogs/file_dialog.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()
target_include_directories(vivid_ui PRIVATE src)
target_link_libraries(vivid_ui PUBLIC vivid_operator_api webgpu)
target_link_libraries(vivid_ui PRIVATE stb_truetype glfw nlohmann_json::nlohmann_json)
target_compile_definitions(vivid_ui PRIVATE "VIVID_CORE_VERSION=\"${PROJECT_VERSION}\"")

# --- Vivid executable ---
add_executable(vivid
    src/runtime/core/main.cpp
    src/runtime/core/main_async_graph.cpp
    src/runtime/core/main_package_browser.cpp
    src/runtime/core/main_menu_actions.cpp
    src/runtime/control/graph_file_io.cpp
    src/runtime/core/workspace_manager.cpp
    src/runtime/core/window_manager.cpp
    src/runtime/graph/graph_snapshot_builder.cpp
    src/runtime/core/main_helpers.cpp
    src/runtime/control/runtime_command_sink.cpp
    src/runtime/debug/ui_test_runner.cpp
    src/runtime/gpu/gpu_context.cpp
    src/runtime/gpu/fullscreen_blit.cpp
    src/runtime/debug/output_window.cpp
    src/runtime/operators/operator_loader.cpp
    src/runtime/operators/operator_registry.cpp
    src/runtime/operators/operator_registry_scan.cpp
    src/runtime/operators/operator_registry_lookup.cpp
    src/runtime/operators/operator_registry_metadata.cpp
    src/runtime/operators/operator_registry_diagnostics.cpp
    src/runtime/operators/operator_descriptor_hash.cpp
    src/runtime/graph/port_type_registry.cpp
    src/runtime/gpu/wgsl_header_parser.cpp
    src/runtime/graph/graph.cpp
    src/runtime/graph/subgraph_module.cpp
    src/runtime/core/runtime_core.cpp
    src/runtime/graph/subgraph_module.cpp
    src/runtime/graph/graph_compiler.cpp
    src/runtime/graph/graph_compiler_init.cpp
    src/runtime/graph/graph_compiler_planning.cpp
    src/runtime/graph/graph_compiler_reload.cpp
    src/runtime/graph/lane_buffer_gpu.cpp
    src/runtime/audio/audio_frame_bridge.cpp
    src/runtime/graph/frame_executor.cpp
    src/runtime/audio/audio_engine.cpp
    src/runtime/audio/audio_frame_bridge.cpp
    src/runtime/graph/frame_executor.cpp
    src/runtime/graph/audio_executor.cpp
    src/runtime/core/file_watcher.cpp
    src/runtime/core/hot_reload.cpp
    src/runtime/core/source_index.cpp
    src/runtime/core/tool_discovery.cpp
    src/runtime/control/runtime_api.cpp
    src/runtime/control/runtime_api_live.cpp
    src/runtime/control/runtime_api_variations.cpp
    src/runtime/control/runtime_api_modulation.cpp
    src/runtime/control/runtime_api_persistence.cpp
    src/runtime/packages/package_compiler.cpp
    src/runtime/core/tool_discovery.cpp
    src/runtime/packages/package_manager.cpp
    src/runtime/packages/package_manager_discovery.cpp
    src/runtime/packages/package_manager_manifest.cpp
    src/runtime/packages/package_manager_install.cpp
    src/runtime/packages/package_manager_build.cpp
    src/runtime/packages/project_lockfile.cpp
    src/common/hash_util.cpp
    src/runtime/core/undo_manager.cpp
    src/runtime/gpu/screenshot.cpp
    src/runtime/operators/builtin_operators.cpp
    src/runtime/core/runtime_bootstrap.cpp
    src/runtime/assets/asset_library.cpp
    src/runtime/assets/asset_library_internal.cpp
    src/runtime/assets/asset_library_discovery.cpp
    src/runtime/assets/asset_library_metadata.cpp
    src/runtime/assets/asset_library_import.cpp
    src/runtime/control/control_server.cpp
    src/runtime/control/control_server_query.cpp
    src/runtime/control/control_server_dispatch.cpp
    src/runtime/control/control_server_checks.cpp
    src/runtime/control/control_server_assets.cpp
    src/runtime/operators/operator_source_docs.cpp
    src/runtime/debug/capture_coordinator.cpp
    src/runtime/debug/output_analyzer.cpp
    src/runtime/core/file_drop_registry.cpp
    src/runtime/platform/av_exporter.mm
    src/runtime/operators/operator_creator.cpp
    src/runtime/audio/system_midi.cpp
    src/runtime/platform/platform.cpp
    src/runtime/platform/process_runner.cpp
    src/runtime/core/settings.cpp
    src/runtime/core/crash_recovery.cpp
    src/runtime/core/editor_detect.cpp
    src/runtime/platform/macos_frame_timer.cpp
    src/export/export_pipeline.cpp
    src/runtime/packages/package_compiler.cpp
    src/runtime/core/tool_discovery.cpp
    src/runtime/packages/package_manager.cpp
    src/runtime/packages/package_scaffolder.cpp
    src/runtime/packages/package_test_runner.cpp
    src/runtime/packages/package_catalog.cpp
    src/runtime/platform/app_update_manager.cpp
    src/runtime/net/http_fetch.cpp
)

if(APPLE)
    set_target_properties(vivid PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Vivid"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.vivid.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/platform/macos/Info.plist.in"
    )
endif()

target_include_directories(vivid PRIVATE src)
target_link_libraries(vivid PRIVATE vivid_ui webgpu glfw glfw3webgpu vivid_operator_api nlohmann_json::nlohmann_json dragonbox::dragonbox_to_chars miniaudio stb_truetype ixwebsocket rtmidi CLI11::CLI11 efsw tinyxml2 CURL::libcurl)
if(VIVID_ENABLE_HIGHWAY)
    target_link_libraries(vivid PRIVATE hwy)
    target_compile_definitions(vivid PRIVATE
        VIVID_HAS_HIGHWAY=1
        "VIVID_DRAGONBOX_INCLUDE_DIR=\"${dragonbox_SOURCE_DIR}/include\""
        "VIVID_DRAGONBOX_LIBRARY_PATH=\"$<TARGET_FILE:dragonbox::dragonbox_to_chars>\""
        "VIVID_HIGHWAY_INCLUDE_DIR=\"${highway_SOURCE_DIR}\""
        "VIVID_HIGHWAY_LIBRARY_PATH=\"$<TARGET_FILE:hwy>\"")
else()
    target_compile_definitions(vivid PRIVATE
        "VIVID_DRAGONBOX_INCLUDE_DIR=\"${dragonbox_SOURCE_DIR}/include\""
        "VIVID_DRAGONBOX_LIBRARY_PATH=\"$<TARGET_FILE:dragonbox::dragonbox_to_chars>\"")
endif()
if(APPLE AND VIVID_ENABLE_ACCELERATE)
    target_compile_definitions(vivid PRIVATE VIVID_HAS_ACCELERATE=1)
    target_link_libraries(vivid PRIVATE "-framework Accelerate")
endif()
if(APPLE)
    target_link_libraries(vivid PRIVATE syphon_runtime)
    # Ensure bundle-launched app resolves syphon runtime from Contents/Frameworks.
    set_target_properties(vivid PROPERTIES
        BUILD_RPATH "@executable_path/../Frameworks"
        INSTALL_RPATH "@executable_path/../Frameworks")
endif()

if(APPLE)
    target_compile_definitions(vivid PRIVATE
        "VIVID_CORE_VERSION=\"${PROJECT_VERSION}\""
        "VIVID_BUILD_DIR=\"${CMAKE_BINARY_DIR}\""
        "VIVID_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")
else()
    target_compile_definitions(vivid PRIVATE
        "VIVID_CORE_VERSION=\"${PROJECT_VERSION}\"")
endif()

# macOS native menu bar
if(APPLE)
    target_sources(vivid PRIVATE src/runtime/platform/macos_menu.mm src/runtime/gpu/metal_interop.mm src/runtime/gpu/syphon_output.mm src/runtime/platform/sparkle_bridge.mm)
    set_source_files_properties(src/runtime/platform/macos_menu.mm src/runtime/gpu/metal_interop.mm src/runtime/gpu/syphon_output.mm src/runtime/platform/sparkle_bridge.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()

# ObjC ARC for AVExporter
set_source_files_properties(src/runtime/platform/av_exporter.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")

# Copy default startup graph
configure_file(graphs/intro/demo.json ${CMAKE_BINARY_DIR}/graph.json COPYONLY)

# Copy all graphs recursively into build/graphs
file(GLOB_RECURSE DEMO_GRAPHS CONFIGURE_DEPENDS RELATIVE ${CMAKE_SOURCE_DIR}/graphs graphs/*.json)
foreach(g ${DEMO_GRAPHS})
    configure_file(${CMAKE_SOURCE_DIR}/graphs/${g} ${CMAKE_BINARY_DIR}/graphs/${g} COPYONLY)
endforeach()

set(VIVID_DEMO_ASSETS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/assets")

# Copy all demo assets recursively into build/assets
file(GLOB_RECURSE DEMO_ASSETS RELATIVE ${VIVID_DEMO_ASSETS_SOURCE_DIR} ${VIVID_DEMO_ASSETS_SOURCE_DIR}/*)
list(FILTER DEMO_ASSETS EXCLUDE REGEX "^\\.")
list(FILTER DEMO_ASSETS EXCLUDE REGEX "/\\.")
foreach(m ${DEMO_ASSETS})
    configure_file(${VIVID_DEMO_ASSETS_SOURCE_DIR}/${m} ${CMAKE_BINARY_DIR}/assets/${m} COPYONLY)
endforeach()

# Copy font to build directory
configure_file(fonts/JetBrainsMono-Regular.ttf ${CMAKE_BINARY_DIR}/JetBrainsMono-Regular.ttf COPYONLY)

# Copy default graph template to build directory
configure_file(resources/default_graph.json ${CMAKE_BINARY_DIR}/default_graph.json COPYONLY)

# Copy self-describing .wgsl filter presets to build directory
file(GLOB WGSL_PRESETS filters/*.wgsl)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/filters)
foreach(f ${WGSL_PRESETS})
    get_filename_component(fname ${f} NAME)
    configure_file(${f} ${CMAKE_BINARY_DIR}/filters/${fname} COPYONLY)
endforeach()

# Copy locale files to build directory
file(GLOB LOCALE_FILES locales/*.json)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/locales)
foreach(f ${LOCALE_FILES})
    get_filename_component(fname ${f} NAME)
    configure_file(${f} ${CMAKE_BINARY_DIR}/locales/${fname} COPYONLY)
endforeach()

# --- Bundle resources (font, graphs, WGSL filters → Resources/) ---
if(APPLE)
    function(vivid_bundle_source_tree base_dir rel_root)
        file(GLOB_RECURSE _bundle_files CONFIGURE_DEPENDS RELATIVE ${base_dir} ${base_dir}/${rel_root}/*)
        list(FILTER _bundle_files EXCLUDE REGEX "^\\.")
        list(FILTER _bundle_files EXCLUDE REGEX "/\\.")
        foreach(rel_file ${_bundle_files})
            set(src_path ${base_dir}/${rel_file})
            if(IS_DIRECTORY ${src_path})
                continue()
            endif()
            get_filename_component(rel_dir ${rel_file} DIRECTORY)
            set(dst_dir "${CMAKE_BINARY_DIR}/vivid.app/Contents/Resources/source/${rel_dir}")
            get_filename_component(fname ${rel_file} NAME)
            add_custom_command(TARGET vivid POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${dst_dir}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${src_path}" "${dst_dir}/${fname}"
                COMMENT "Bundling ${rel_file}"
                VERBATIM)
        endforeach()
    endfunction()

    # Font → Resources/
    target_sources(vivid PRIVATE fonts/JetBrainsMono-Regular.ttf)
    set_source_files_properties(fonts/JetBrainsMono-Regular.ttf
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    # Demo graphs → Resources/graphs/(recursive)
    file(GLOB_RECURSE _BUNDLE_GRAPHS CONFIGURE_DEPENDS RELATIVE ${CMAKE_SOURCE_DIR}/graphs graphs/*.json)
    foreach(g ${_BUNDLE_GRAPHS})
        set(src_path ${CMAKE_SOURCE_DIR}/graphs/${g})
        get_filename_component(rel_dir ${g} DIRECTORY)
        if(rel_dir STREQUAL "")
            set(dst_loc Resources/graphs)
        else()
            set(dst_loc Resources/graphs/${rel_dir})
        endif()
        target_sources(vivid PRIVATE ${src_path})
        set_source_files_properties(${src_path}
            PROPERTIES MACOSX_PACKAGE_LOCATION ${dst_loc})
    endforeach()

    # Ensure graph content changes are picked up on every build (not just configure).
    foreach(g ${_BUNDLE_GRAPHS})
        set(src_path ${CMAKE_SOURCE_DIR}/graphs/${g})
        get_filename_component(rel_dir ${g} DIRECTORY)
        if(rel_dir STREQUAL "")
            set(dst_dir "${CMAKE_BINARY_DIR}/vivid.app/Contents/Resources/graphs")
        else()
            set(dst_dir "${CMAKE_BINARY_DIR}/vivid.app/Contents/Resources/graphs/${rel_dir}")
        endif()
        get_filename_component(fname ${g} NAME)
        add_custom_command(TARGET vivid POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${dst_dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${src_path}" "${dst_dir}/${fname}"
            VERBATIM)
    endforeach()

    # Demo assets → Resources/assets/(recursive)
    file(GLOB_RECURSE _BUNDLE_ASSETS RELATIVE ${VIVID_DEMO_ASSETS_SOURCE_DIR} ${VIVID_DEMO_ASSETS_SOURCE_DIR}/*)
    list(FILTER _BUNDLE_ASSETS EXCLUDE REGEX "^\\.")
    list(FILTER _BUNDLE_ASSETS EXCLUDE REGEX "/\\.")
    foreach(m ${_BUNDLE_ASSETS})
        set(src_path ${VIVID_DEMO_ASSETS_SOURCE_DIR}/${m})
        get_filename_component(rel_dir ${m} DIRECTORY)
        if(rel_dir STREQUAL "")
            set(dst_loc Resources/assets)
        else()
            set(dst_loc Resources/assets/${rel_dir})
        endif()
        target_sources(vivid PRIVATE ${src_path})
        set_source_files_properties(${src_path}
            PROPERTIES MACOSX_PACKAGE_LOCATION ${dst_loc})
    endforeach()

    # Icon → Resources/
    target_sources(vivid PRIVATE platform/macos/Vivid.icns)
    set_source_files_properties(platform/macos/Vivid.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(vivid PROPERTIES
        MACOSX_BUNDLE_ICON_FILE Vivid.icns)

    # Locale files → Resources/locales/
    file(GLOB _BUNDLE_LOCALES locales/*.json)
    foreach(f ${_BUNDLE_LOCALES})
        target_sources(vivid PRIVATE ${f})
        set_source_files_properties(${f}
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources/locales)
    endforeach()

    # WGSL filter presets → Resources/filters/
    file(GLOB _BUNDLE_FILTERS filters/*.wgsl)
    foreach(f ${_BUNDLE_FILTERS})
        target_sources(vivid PRIVATE ${f})
        set_source_files_properties(${f}
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources/filters)
    endforeach()

    # Factory presets → Resources/factory_presets/
    file(GLOB _BUNDLE_FACTORY_PRESETS ${CMAKE_BINARY_DIR}/factory_presets/*.json)
    foreach(f ${_BUNDLE_FACTORY_PRESETS})
        target_sources(vivid PRIVATE ${f})
        set_source_files_properties(${f}
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources/factory_presets)
    endforeach()

    # Default graph template → Resources/
    target_sources(vivid PRIVATE resources/default_graph.json)
    set_source_files_properties(resources/default_graph.json
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    # Bundled MCP scripts → Resources/mcp/
    file(GLOB_RECURSE _BUNDLE_MCP CONFIGURE_DEPENDS RELATIVE ${CMAKE_SOURCE_DIR}/mcp mcp/*)
    list(FILTER _BUNDLE_MCP EXCLUDE REGEX "^\\.")
    list(FILTER _BUNDLE_MCP EXCLUDE REGEX "/\\.")
    foreach(f ${_BUNDLE_MCP})
        set(src_path ${CMAKE_SOURCE_DIR}/mcp/${f})
        if(IS_DIRECTORY ${src_path})
            continue()
        endif()
        get_filename_component(rel_dir ${f} DIRECTORY)
        if(rel_dir STREQUAL "")
            set(dst_loc Resources/mcp)
        else()
            set(dst_loc Resources/mcp/${rel_dir})
        endif()
        target_sources(vivid PRIVATE ${src_path})
        set_source_files_properties(${src_path}
            PROPERTIES MACOSX_PACKAGE_LOCATION ${dst_loc})
    endforeach()

    vivid_bundle_source_tree(${CMAKE_SOURCE_DIR} src)
    vivid_bundle_source_tree(${CMAKE_SOURCE_DIR} operators)
    vivid_bundle_source_tree(${CMAKE_SOURCE_DIR} mcp)
    vivid_bundle_source_tree(${CMAKE_SOURCE_DIR} tests)
    vivid_bundle_source_tree(${CMAKE_SOURCE_DIR} docs)

    # operator_api headers → Resources/sdk/src/operator_api/
    file(GLOB _BUNDLE_API_HEADERS src/operator_api/*.h)
    foreach(f ${_BUNDLE_API_HEADERS})
        target_sources(vivid PRIVATE ${f})
        set_source_files_properties(${f}
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources/sdk/src/operator_api)
    endforeach()

    # WebGPU header → Resources/sdk/include/webgpu/
    # Derive wgpu root from WEBGPU_RUNTIME_LIB (cache variable set by FetchContent)
    get_filename_component(_wgpu_lib_dir "${WEBGPU_RUNTIME_LIB}" DIRECTORY)
    get_filename_component(_wgpu_root "${_wgpu_lib_dir}" DIRECTORY)
    set(_wgpu_header "${_wgpu_root}/include/webgpu/webgpu.h")
    if(EXISTS "${_wgpu_header}")
        target_sources(vivid PRIVATE "${_wgpu_header}")
        set_source_files_properties("${_wgpu_header}"
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources/sdk/include/webgpu)
    endif()

    # Highway headers → Resources/sdk/include/hwy/
    if(VIVID_ENABLE_HIGHWAY)
        file(GLOB_RECURSE _hwy_headers "${highway_SOURCE_DIR}/hwy/*.h")
        foreach(f ${_hwy_headers})
            file(RELATIVE_PATH _rel "${highway_SOURCE_DIR}" "${f}")
            get_filename_component(_rel_dir "${_rel}" DIRECTORY)
            target_sources(vivid PRIVATE "${f}")
            set_source_files_properties("${f}"
                PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/sdk/include/${_rel_dir}")
        endforeach()
    endif()
endif()

# Copy Dawn shared library next to the executable (if dynamically linked)
target_copy_webgpu_binaries(vivid)

# Apple framework fallback (if not already linked by glfw3webgpu)
if(APPLE)
    target_link_libraries(vivid PRIVATE
        "-framework Cocoa"
        "-framework CoreVideo"
        "-framework IOKit"
        "-framework QuartzCore"
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework VideoToolbox"
    )
endif()

# --- Generate operator manifest for standalone export ---
get_property(_manifest_entries GLOBAL PROPERTY VIVID_OPERATOR_MANIFEST)
list(JOIN _manifest_entries ",\n" _manifest_body)
file(WRITE "${CMAKE_BINARY_DIR}/operator_manifest.json" "{\n${_manifest_body}\n}\n")

# --- Copy operator plugins into bundle ---
if(APPLE)
    get_property(_op_targets GLOBAL PROPERTY VIVID_OPERATOR_TARGETS)

    # Ensure operator plugins are built before vivid's POST_BUILD copies them
    add_dependencies(vivid ${_op_targets})

    # Create PlugIns directory
    add_custom_command(TARGET vivid POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/Frameworks
    )

    if(TARGET syphon_runtime)
        add_custom_command(TARGET vivid POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:syphon_runtime>
                $<TARGET_BUNDLE_CONTENT_DIR:vivid>/Frameworks/
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_BINARY_DIR}/syphon_default.metallib
                $<TARGET_BUNDLE_CONTENT_DIR:vivid>/Resources/default.metallib
        )
    endif()

    # Keep WebGPU runtime available in Frameworks for package plugins that link
    # libwgpu_native via @rpath.
    add_custom_command(TARGET vivid POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/MacOS/libwgpu_native.dylib
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/Frameworks/libwgpu_native.dylib
    )

    foreach(op_target IN LISTS _op_targets)
        add_custom_command(TARGET vivid POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${op_target}>
                $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns/
        )
    endforeach()

    # Ad-hoc sign the bundle so Info.plist is bound to the code signature.
    # Without this, macOS TCC won't read NSCameraUsageDescription and camera
    # authorization throws an uncaught NSException.
    # Remove runtime artifacts that confuse codesign's bundle validation.
    add_custom_command(TARGET vivid POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_BUNDLE_CONTENT_DIR:vivid>/MacOS/.hot_reload
        COMMAND codesign -s - --force $<TARGET_BUNDLE_DIR:vivid>
        COMMENT "Ad-hoc signing Vivid.app bundle"
    )

    # Convenience symlink so ./build/vivid still works (canonical() resolves it)
    add_custom_command(TARGET vivid POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_BINARY_DIR}/vivid
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/MacOS/vivid
            ${CMAKE_BINARY_DIR}/vivid
        COMMENT "Creating convenience symlink build/vivid"
    )
endif()

enable_testing()
