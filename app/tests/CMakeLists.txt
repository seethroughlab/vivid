# Headless, portable test suite — no GUI/GPU/audio/VST deps, so it configures and
# runs anywhere (Linux CI included) and stays fast. Each test is a standalone
# executable that returns its failure count; ctest groups them by label tier.
find_package(Threads REQUIRED)   # std::thread in test_hot_reload

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

# Pre-P1 session migration (legacy VOp-int -> op-name). persist.h pulls nlohmann.
vivid_add_test(test_persist_chain_migration)
target_link_libraries(test_persist_chain_migration PRIVATE nlohmann_json::nlohmann_json)

# P4.1: session-version load guard (classify_session_version is a pure header fn).
vivid_add_test(test_version_guard)
target_link_libraries(test_version_guard PRIVATE nlohmann_json::nlohmann_json)

# P4.3: runtime-health rollup + serialization (severity()/to_json() are App-free).
vivid_add_test(test_runtime_health)
target_sources(test_runtime_health PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/runtime_health.cpp)
target_link_libraries(test_runtime_health PRIVATE nlohmann_json::nlohmann_json)

# --- P2.1: dlopen operator loader + LoadedOperator adapter ---
# GPU-free fixture operators built as loadable modules; the tests dlopen them by
# explicit path (so they need no wgpu device and stay headless/portable).
add_library(test_fixture_op MODULE fixtures/test_fixture_op.cpp)
target_include_directories(test_fixture_op PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_library(test_fixture_bad MODULE fixtures/test_fixture_bad.cpp)
target_include_directories(test_fixture_bad PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# Loader: dlopen + ABI/symbol/descriptor validation (operator_loader is wgpu-free).
vivid_add_test(test_operator_loader)
target_sources(test_operator_loader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/operator_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)
target_link_libraries(test_operator_loader PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(test_operator_loader test_fixture_op test_fixture_bad)
target_compile_definitions(test_operator_loader PRIVATE
    FIXTURE_OP_PATH="$<TARGET_FILE:test_fixture_op>"
    FIXTURE_BAD_PATH="$<TARGET_FILE:test_fixture_bad>")

# Adapter: LoadedOperator mirrors a dylib descriptor through OpRegistry losslessly.
vivid_add_test(test_loaded_operator)
target_sources(test_loaded_operator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/operator_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/loaded_operator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)
target_link_libraries(test_loaded_operator PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(test_loaded_operator test_fixture_op)
target_compile_definitions(test_loaded_operator PRIVATE
    FIXTURE_OP_PATH="$<TARGET_FILE:test_fixture_op>")

# P2.2′: operator discovery metadata (display_name/keywords/summary) reaches the
# descriptor for both a compiled-in op and a loaded dylib op.
vivid_add_test(test_op_metadata)
target_sources(test_op_metadata PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/operator_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/loaded_operator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)
target_link_libraries(test_op_metadata PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(test_op_metadata test_fixture_op)
target_compile_definitions(test_op_metadata PRIVATE
    FIXTURE_OP_PATH="$<TARGET_FILE:test_fixture_op>")

# P2.3: package pipeline — parse manifest, compile the operator via clang++, load it.
# Compiles a GPU-free fixture op so it needs no wgpu at test time (portable/headless).
vivid_add_test(test_package_compile)
target_sources(test_package_compile PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/package_manifest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/package_compiler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/package_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/operator_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/loaded_operator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/operator_scan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/op_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/platform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp)
target_link_libraries(test_package_compile PRIVATE ${CMAKE_DL_LIBS} nlohmann_json::nlohmann_json)
target_compile_definitions(test_package_compile PRIVATE
    PKG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/pkg"
    VIVID_PKG_CXX="${CMAKE_CXX_COMPILER}"
    VIVID_PKG_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../src"
    VIVID_PKG_ARCH="${CMAKE_OSX_ARCHITECTURES}")

# P2.4: hot-reload infrastructure — classify_hot_reload + FileWatcher + HotReloader.
vivid_add_test(test_hot_reload)
target_sources(test_hot_reload PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gpu/operator_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/operator_api/operator_descriptor_validation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/file_watcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/packages/hot_reload.cpp)
target_link_libraries(test_hot_reload PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
