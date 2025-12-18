# vivid-serial Tests
# Unit tests for serial operators

add_executable(test_serial_addon
    ${CMAKE_CURRENT_LIST_DIR}/test_serial.cpp
)

target_link_libraries(test_serial_addon PRIVATE
    Catch2::Catch2WithMain
    vivid-core
    vivid-serial
)

target_include_directories(test_serial_addon PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/addons/vivid-serial/include
)

catch_discover_tests(test_serial_addon)
