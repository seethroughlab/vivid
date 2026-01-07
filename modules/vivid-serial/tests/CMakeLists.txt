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
    ${CMAKE_SOURCE_DIR}/modules/vivid-core/include
    ${CMAKE_SOURCE_DIR}/modules/vivid-serial/include
)

catch_discover_tests(test_serial_addon
    PROPERTIES LABELS "unit"
)

# Example existence tests (no hardware required)
add_executable(test_serial_examples
    ${CMAKE_CURRENT_LIST_DIR}/test_examples.cpp
)

target_link_libraries(test_serial_examples PRIVATE
    Catch2::Catch2WithMain
)

target_compile_definitions(test_serial_examples PRIVATE
    VIVID_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

catch_discover_tests(test_serial_examples
    PROPERTIES LABELS "smoke;examples;serial"
)
