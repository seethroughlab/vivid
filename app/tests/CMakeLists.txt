# Headless, portable test suite — no GUI/GPU/audio/VST deps, so it configures and
# runs anywhere (Linux CI included) and stays fast. Each test is a standalone
# executable that returns its failure count; ctest groups them by label tier.
add_library(vivid_test_helpers INTERFACE)
target_include_directories(vivid_test_helpers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# vivid_add_test(<name> [LABEL])  — adds tests/<name>.cpp as a ctest case.
function(vivid_add_test name)
    set(label "HEADLESS_SMOKE")
    if(ARGV1)
        set(label "${ARGV1}")
    endif()
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE vivid_test_helpers)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "${label}")
endfunction()

vivid_add_test(test_mapping)
vivid_add_test(test_control_parse)
vivid_add_test(test_control_errors)
target_link_libraries(test_control_errors PRIVATE nlohmann_json::nlohmann_json)

# Operator descriptor validation (lifted operator_api) — compiles the validator
# .cpp into the test (header + stdlib only, so still headless/portable).
vivid_add_test(test_descriptor_validation)
target_sources(test_descriptor_validation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)

# In-process operator runtime (gpu/op_runtime is wgpu-free → headless).
vivid_add_test(test_op_registry)
target_sources(test_op_registry PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)
