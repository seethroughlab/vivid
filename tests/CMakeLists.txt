# Vivid Test Suite
# =================
# Integration tests (unit tests moved to respective addons)

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
# Integration Tests (core)
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
# Addon Tests (included from addon directories)
# -----------------------------------------------------------------------------
# Unit tests have been moved to their respective addon directories:
#   - vivid-effects-2d/tests/ (test_noise, test_blur, test_composite)
#   - vivid-audio/tests/ (test_clock, test_sequencer)
#
# To build addon tests, include their CMakeLists.txt:

option(BUILD_ADDON_TESTS "Build addon test suites" ON)

if(BUILD_ADDON_TESTS)
    include(${CMAKE_SOURCE_DIR}/addons/vivid-effects-2d/tests/CMakeLists.txt)
    include(${CMAKE_SOURCE_DIR}/addons/vivid-audio/tests/CMakeLists.txt)
endif()

# -----------------------------------------------------------------------------
# Visual Regression Tests (requires headless mode)
# -----------------------------------------------------------------------------

# TODO: Enable when headless rendering is implemented
# add_executable(test_visual
#     visual/test_visual_regression.cpp
# )
