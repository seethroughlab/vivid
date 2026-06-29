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
