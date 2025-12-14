# vivid-effects-2d Tests
# Unit tests for 2D texture operators

add_executable(test_effects_2d
    ${CMAKE_CURRENT_LIST_DIR}/test_noise.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_blur.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_composite.cpp
)

target_link_libraries(test_effects_2d PRIVATE
    Catch2::Catch2WithMain
    vivid-core
    vivid-effects-2d
)

target_include_directories(test_effects_2d PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/addons/vivid-effects-2d/include
)

catch_discover_tests(test_effects_2d)
