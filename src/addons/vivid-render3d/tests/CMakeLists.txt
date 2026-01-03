# vivid-render3d Tests
# Unit tests for 3D rendering operators

add_executable(test_render3d_addon
    ${CMAKE_CURRENT_LIST_DIR}/test_render3d.cpp
)

target_link_libraries(test_render3d_addon PRIVATE
    Catch2::Catch2WithMain
    vivid-core
    vivid-render3d
)

target_include_directories(test_render3d_addon PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/include
    ${CMAKE_SOURCE_DIR}/src/addons/vivid-render3d/include
)

catch_discover_tests(test_render3d_addon
    PROPERTIES LABELS "unit"
)
