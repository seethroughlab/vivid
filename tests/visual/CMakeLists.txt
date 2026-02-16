# Visual Regression Tests
# =======================
# Tests that compare rendered output against reference images

add_executable(test_visual_regression
    test_visual_regression.cpp
)

target_link_libraries(test_visual_regression PRIVATE
    Catch2::Catch2WithMain
)

target_compile_definitions(test_visual_regression PRIVATE
    VIVID_BINARY_PATH="$<TARGET_FILE:vivid>"
    VIVID_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# stb_image is fetched via FetchContent
target_include_directories(test_visual_regression PRIVATE
    ${CMAKE_BINARY_DIR}/_deps/stb-src
)

# Ensure vivid is built before running visual tests
add_dependencies(test_visual_regression vivid)

catch_discover_tests(test_visual_regression
    PROPERTIES
        LABELS "visual"
        TIMEOUT 120
)

# -----------------------------------------------------------------------------
# GUI Visual Regression Tests
# -----------------------------------------------------------------------------

add_executable(test_gui_visual_regression
    test_gui_visual_regression.cpp
)

target_link_libraries(test_gui_visual_regression PRIVATE
    Catch2::Catch2WithMain
)

target_compile_definitions(test_gui_visual_regression PRIVATE
    VIVID_BINARY_PATH="$<TARGET_FILE:vivid>"
    VIVID_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_include_directories(test_gui_visual_regression PRIVATE
    ${CMAKE_BINARY_DIR}/_deps/stb-src
)

add_dependencies(test_gui_visual_regression vivid)

catch_discover_tests(test_gui_visual_regression
    PROPERTIES
        LABELS "visual;gui"
        TIMEOUT 120
)

# -----------------------------------------------------------------------------
# Custom target to generate reference images
# -----------------------------------------------------------------------------

add_custom_target(generate-reference-images
    COMMAND ${CMAKE_COMMAND} -E echo "Generating reference images..."
    COMMAND $<TARGET_FILE:vivid> ${CMAKE_SOURCE_DIR}/projects/2d-effects/chain-basics
            --snapshot ${CMAKE_SOURCE_DIR}/tests/fixtures/reference-images/2d-effects/chain-basics.png
            --snapshot-frame 30
    COMMAND $<TARGET_FILE:vivid> ${CMAKE_SOURCE_DIR}/projects/2d-effects/kaleidoscope
            --snapshot ${CMAKE_SOURCE_DIR}/tests/fixtures/reference-images/2d-effects/kaleidoscope.png
            --snapshot-frame 30
    COMMAND $<TARGET_FILE:vivid> ${CMAKE_SOURCE_DIR}/projects/2d-effects/retro-crt
            --snapshot ${CMAKE_SOURCE_DIR}/tests/fixtures/reference-images/2d-effects/retro-crt.png
            --snapshot-frame 30
    COMMAND $<TARGET_FILE:vivid> ${CMAKE_SOURCE_DIR}/tests/fixtures/retro-suite
            --snapshot ${CMAKE_SOURCE_DIR}/tests/fixtures/reference-images/retro-suite.png
            --snapshot-frame 30
    COMMAND $<TARGET_FILE:vivid> ${CMAKE_SOURCE_DIR}/tests/fixtures/blend-modes-all
            --snapshot ${CMAKE_SOURCE_DIR}/tests/fixtures/reference-images/blend-modes-all.png
            --snapshot-frame 30
    DEPENDS vivid
    COMMENT "Generating reference images for visual regression tests"
    VERBATIM
)

# -----------------------------------------------------------------------------
# Custom target to generate GUI reference images
# -----------------------------------------------------------------------------

add_custom_target(generate-gui-reference-images
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/tests/fixtures/reference-images/gui
    # Static UI snapshot (no interaction)
    COMMAND $<TARGET_FILE:vivid> ${CMAKE_SOURCE_DIR}/tests/fixtures/gui-visual-test
            --snapshot-ui
            --snapshot ${CMAKE_SOURCE_DIR}/tests/fixtures/reference-images/gui/gui-default.png
            --snapshot-frame 30
    # Puppeteered: click on a node (opens inspector)
    COMMAND $<TARGET_FILE:vivid> ${CMAKE_SOURCE_DIR}/tests/fixtures/gui-visual-test
            --snapshot-ui
            --snapshot ${CMAKE_SOURCE_DIR}/tests/fixtures/reference-images/gui/gui-click-node.png
            --snapshot-frame 35
            --script ${CMAKE_SOURCE_DIR}/tests/fixtures/gui-scripts/click-node.json
    # Puppeteered: scroll to zoom out on node graph
    COMMAND $<TARGET_FILE:vivid> ${CMAKE_SOURCE_DIR}/tests/fixtures/gui-visual-test
            --snapshot-ui
            --snapshot ${CMAKE_SOURCE_DIR}/tests/fixtures/reference-images/gui/gui-zoom-out.png
            --snapshot-frame 35
            --script ${CMAKE_SOURCE_DIR}/tests/fixtures/gui-scripts/zoom-out.json
    DEPENDS vivid
    COMMENT "Generating GUI reference images for visual regression tests"
    VERBATIM
)
