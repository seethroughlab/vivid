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
# Custom target to generate reference images
# -----------------------------------------------------------------------------

add_custom_target(generate-reference-images
    COMMAND ${CMAKE_COMMAND} -E echo "Generating reference images..."
    COMMAND $<TARGET_FILE:vivid> ${CMAKE_SOURCE_DIR}/examples/2d-effects/chain-basics
            --snapshot ${CMAKE_SOURCE_DIR}/tests/fixtures/reference-images/2d-effects/chain-basics.png
            --snapshot-frame 30
    COMMAND $<TARGET_FILE:vivid> ${CMAKE_SOURCE_DIR}/examples/2d-effects/kaleidoscope
            --snapshot ${CMAKE_SOURCE_DIR}/tests/fixtures/reference-images/2d-effects/kaleidoscope.png
            --snapshot-frame 30
    COMMAND $<TARGET_FILE:vivid> ${CMAKE_SOURCE_DIR}/examples/2d-effects/retro-crt
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
