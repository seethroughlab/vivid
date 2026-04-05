# Inspector layout normalization tests
add_executable(test_inspector_layout
    tests/ui/test_inspector_layout.cpp
)
target_include_directories(test_inspector_layout PRIVATE src tests)
target_link_libraries(test_inspector_layout PRIVATE vivid_runtime_testlib)
add_test(NAME test_inspector_layout COMMAND test_inspector_layout WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# UI overlay interaction regressions (example/package/meta overlay callbacks)
add_executable(test_ui_overlay_interactions
    tests/ui/test_ui_overlay_interactions.cpp
)
target_include_directories(test_ui_overlay_interactions PRIVATE src tests)
target_link_libraries(test_ui_overlay_interactions PRIVATE vivid_runtime_testlib vivid_ui webgpu glfw nlohmann_json::nlohmann_json stb_truetype)
if(APPLE)
    target_link_libraries(test_ui_overlay_interactions PRIVATE
        "-framework Cocoa" "-framework Foundation")
endif()
add_test(NAME test_ui_overlay_interactions COMMAND test_ui_overlay_interactions WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_ui_overlay_interactions PROPERTIES LABELS "UI_SMOKE")

add_executable(test_ui_editor_interactions
    tests/ui/test_ui_editor_interactions.cpp
)
target_include_directories(test_ui_editor_interactions PRIVATE src tests)
target_link_libraries(test_ui_editor_interactions PRIVATE vivid_runtime_testlib vivid_ui webgpu glfw nlohmann_json::nlohmann_json stb_truetype)
if(APPLE)
    target_link_libraries(test_ui_editor_interactions PRIVATE
        "-framework Cocoa" "-framework Foundation")
endif()
add_test(NAME test_ui_editor_interactions COMMAND test_ui_editor_interactions WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_ui_editor_interactions PROPERTIES LABELS "UI_SMOKE")

add_executable(test_ui_widget_interactions
    tests/ui/test_ui_widget_interactions.cpp
)
target_include_directories(test_ui_widget_interactions PRIVATE src tests)
target_link_libraries(test_ui_widget_interactions PRIVATE vivid_runtime_testlib vivid_ui webgpu glfw nlohmann_json::nlohmann_json stb_truetype)
if(APPLE)
    target_link_libraries(test_ui_widget_interactions PRIVATE
        "-framework Cocoa" "-framework Foundation")
endif()
add_test(NAME test_ui_widget_interactions COMMAND test_ui_widget_interactions WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_ui_widget_interactions PROPERTIES LABELS "UI_SMOKE")

# Windowed GUI smoke for inspector and editor-drop regressions.
add_executable(test_ui_screenshot_smoke
    tests/ui/test_ui_screenshot_smoke.cpp
)
target_include_directories(test_ui_screenshot_smoke PRIVATE src tests deps/stb)
target_link_libraries(test_ui_screenshot_smoke PRIVATE vivid_runtime_testlib nlohmann_json::nlohmann_json)
add_dependencies(test_ui_screenshot_smoke vivid file_drop_test_op file_drop_test_op_alt)
if(APPLE)
    add_custom_command(TARGET test_ui_screenshot_smoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:file_drop_test_op>
            $<TARGET_FILE:file_drop_test_op_alt>
            $<TARGET_BUNDLE_CONTENT_DIR:vivid>/PlugIns/
        COMMENT "Staging GUI smoke file-drop fixtures into Vivid.app bundle"
    )
endif()
add_test(NAME test_ui_screenshot_smoke
    COMMAND test_ui_screenshot_smoke ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_ui_screenshot_smoke PROPERTIES
    LABELS "GUI_SMOKE"
    TIMEOUT 300
    ENVIRONMENT
        "VIVID_ENABLE_UI_SCREENSHOT_SMOKE=1;VIVID_UI_SMOKE_LANE=gui_smoke;HOME=${CMAKE_BINARY_DIR}/.test_ui_screenshot_smoke/gui_smoke/home")

add_test(NAME test_ui_screenshot_smoke_env
    COMMAND test_ui_screenshot_smoke ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_ui_screenshot_smoke_env PROPERTIES
    LABELS "GUI_ENV"
    TIMEOUT 180
    ENVIRONMENT
        "VIVID_ENABLE_UI_SCREENSHOT_SMOKE=1;VIVID_ENABLE_GUI_ENV_SMOKE=1;VIVID_UI_SMOKE_LANE=gui_env;HOME=${CMAKE_BINARY_DIR}/.test_ui_screenshot_smoke/gui_env/home")

add_test(NAME test_ui_screenshot_smoke_harness
    COMMAND test_ui_screenshot_smoke ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_ui_screenshot_smoke_harness PROPERTIES
    LABELS "UI_SMOKE"
    TIMEOUT 30
    ENVIRONMENT
        "VIVID_ENABLE_UI_SCREENSHOT_SMOKE=1;VIVID_UI_SMOKE_HARNESS_SELFTEST=1;VIVID_UI_SMOKE_LANE=harness_selftest;HOME=${CMAKE_BINARY_DIR}/.test_ui_screenshot_smoke/harness_selftest/home")

# Architecture guard: UI layer must not directly include runtime package catalog
add_executable(test_ui_arch_guard tests/ui/test_ui_arch_guard.cpp)
target_include_directories(test_ui_arch_guard PRIVATE src tests)
target_link_libraries(test_ui_arch_guard PRIVATE vivid_runtime_testlib)
add_test(NAME test_ui_arch_guard COMMAND test_ui_arch_guard ${CMAKE_SOURCE_DIR} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Repo hygiene guard: removed runtime-polymorphic composition must not return outside explicit anti-return docs.
add_executable(test_removed_pattern_guard tests/ui/test_removed_pattern_guard.cpp)
target_include_directories(test_removed_pattern_guard PRIVATE src tests)
target_link_libraries(test_removed_pattern_guard PRIVATE vivid_runtime_testlib)
add_test(NAME test_removed_pattern_guard COMMAND test_removed_pattern_guard ${CMAKE_SOURCE_DIR} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# WGSL header parser unit test
add_executable(test_wgsl_header
    tests/common/test_wgsl_header.cpp
)
target_include_directories(test_wgsl_header PRIVATE src tests)
target_link_libraries(test_wgsl_header PRIVATE vivid_runtime_testlib nlohmann_json::nlohmann_json)
add_test(NAME test_wgsl_header COMMAND test_wgsl_header WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# WGSL include preprocessor diagnostics (missing include/cycle/include chain)
add_executable(test_wgsl_preprocessor
    tests/common/test_wgsl_preprocessor.cpp
)
target_include_directories(test_wgsl_preprocessor PRIVATE src tests)
target_link_libraries(test_wgsl_preprocessor PRIVATE vivid_runtime_testlib)
add_test(NAME test_wgsl_preprocessor COMMAND test_wgsl_preprocessor WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# String utility unit test (header-only, no dependencies)
add_executable(test_string_util tests/common/test_string_util.cpp)
target_include_directories(test_string_util PRIVATE src tests)
target_link_libraries(test_string_util PRIVATE vivid_runtime_testlib)
add_test(NAME test_string_util COMMAND test_string_util WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Text editing unit test (header-only, no dependencies)
add_executable(test_text_edit tests/ui/test_text_edit.cpp)
target_include_directories(test_text_edit PRIVATE src tests)
target_link_libraries(test_text_edit PRIVATE vivid_runtime_testlib)
add_test(NAME test_text_edit COMMAND test_text_edit WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# audio_dsp operator API compatibility test
add_executable(test_audio_dsp_api tests/audio/test_audio_dsp_api.cpp)
target_include_directories(test_audio_dsp_api PRIVATE src tests)
target_link_libraries(test_audio_dsp_api PRIVATE vivid_runtime_testlib)
add_test(NAME test_audio_dsp_api COMMAND test_audio_dsp_api WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
