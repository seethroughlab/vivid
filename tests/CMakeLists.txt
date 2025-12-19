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
# Unit Tests (core 2D effects)
# -----------------------------------------------------------------------------

add_executable(test_effects_2d
    unit/test_effects_2d.cpp
)

target_link_libraries(test_effects_2d PRIVATE
    Catch2::Catch2WithMain
    vivid-core
)

target_include_directories(test_effects_2d PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

catch_discover_tests(test_effects_2d)

# -----------------------------------------------------------------------------
# Integration Tests (core)
# -----------------------------------------------------------------------------

add_executable(test_integration
    integration/test_chain.cpp
)

target_link_libraries(test_integration PRIVATE
    Catch2::Catch2WithMain
    vivid-core
)

target_include_directories(test_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

catch_discover_tests(test_integration)

# -----------------------------------------------------------------------------
# Addon Tests (included from addon directories)
# -----------------------------------------------------------------------------
# Unit tests are in their respective addon directories:
#   - vivid-audio/tests/ (synthesis, effects, analysis)
#   - vivid-network/tests/ (OSC, UDP)
#   - vivid-serial/tests/ (SerialIn, SerialOut, DMXOut)
#
# To build addon tests, include their CMakeLists.txt:

option(BUILD_ADDON_TESTS "Build addon test suites" ON)

if(BUILD_ADDON_TESTS)
    include(${CMAKE_SOURCE_DIR}/addons/vivid-audio/tests/CMakeLists.txt)

    include(${CMAKE_SOURCE_DIR}/addons/vivid-network/tests/CMakeLists.txt)
    include(${CMAKE_SOURCE_DIR}/addons/vivid-serial/tests/CMakeLists.txt)
endif()

# -----------------------------------------------------------------------------
# Visual Regression Tests (requires headless mode)
# -----------------------------------------------------------------------------

# TODO: Enable when headless rendering is implemented
# add_executable(test_visual
#     visual/test_visual_regression.cpp
# )
