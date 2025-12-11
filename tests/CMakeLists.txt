# Vivid Test Suite
# =================
# Unit tests, integration tests, and visual regression tests

cmake_minimum_required(VERSION 3.16)

# Fetch Catch2 v3
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(Catch2)

# Include Catch2's CMake helpers
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)

# -----------------------------------------------------------------------------
# Unit Tests - Operators
# -----------------------------------------------------------------------------

add_executable(test_operators
    unit/operators/test_noise.cpp
    unit/operators/test_blur.cpp
    unit/operators/test_composite.cpp
)

target_link_libraries(test_operators PRIVATE
    Catch2::Catch2WithMain
    vivid-core
    vivid-effects-2d
)

target_include_directories(test_operators PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/addons/vivid-effects-2d/include
)

catch_discover_tests(test_operators)

# -----------------------------------------------------------------------------
# Unit Tests - Audio
# -----------------------------------------------------------------------------

add_executable(test_audio
    unit/audio/test_clock.cpp
    unit/audio/test_sequencer.cpp
)

target_link_libraries(test_audio PRIVATE
    Catch2::Catch2WithMain
    vivid-core
    vivid-audio
)

target_include_directories(test_audio PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/addons/vivid-audio/include
)

catch_discover_tests(test_audio)

# -----------------------------------------------------------------------------
# Integration Tests
# -----------------------------------------------------------------------------

add_executable(test_integration
    integration/test_chain.cpp
)

target_link_libraries(test_integration PRIVATE
    Catch2::Catch2WithMain
    vivid-core
    vivid-effects-2d
)

target_include_directories(test_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/addons/vivid-effects-2d/include
)

catch_discover_tests(test_integration)

# -----------------------------------------------------------------------------
# Visual Regression Tests (requires headless mode)
# -----------------------------------------------------------------------------

# TODO: Enable when headless rendering is implemented
# add_executable(test_visual
#     visual/test_visual_regression.cpp
# )
