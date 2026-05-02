cmake_minimum_required(VERSION 3.20)
project(vivid VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

set(VIVID_APPCAST_URL "https://vivid.seethroughlab.com/appcast.xml" CACHE STRING
    "Sparkle appcast URL for core app updates")
set(VIVID_SPARKLE_PUBLIC_KEY "" CACHE STRING
    "Sparkle EdDSA public key (SUPublicEDKey)")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Compiler warnings ---
if(NOT MSVC)
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# --- Sanitizer instrumentation (opt-in) ---
option(VIVID_SANITIZE "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(VIVID_SANITIZE_THREAD "Enable ThreadSanitizer (mutually exclusive with VIVID_SANITIZE)" OFF)

if(VIVID_SANITIZE AND VIVID_SANITIZE_THREAD)
    message(FATAL_ERROR "Cannot enable both VIVID_SANITIZE and VIVID_SANITIZE_THREAD")
endif()

if(VIVID_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-optimize-sibling-calls)
    add_link_options(-fsanitize=address,undefined)
endif()

if(VIVID_SANITIZE_THREAD)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

# --- Optional macOS Accelerate framework ---
if(APPLE)
    option(VIVID_ENABLE_ACCELERATE "Enable optional macOS Accelerate framework paths" ON)
endif()

# --- Modular build configuration ---
include(cmake/git_version.cmake)    # defines VIVID_CORE_COMMIT, VIVID_CORE_REPO_URL
include(cmake/dependencies.cmake)

add_library(vivid_source_syntax STATIC
    src/runtime/core/source_syntax_parser.cpp
)
target_include_directories(vivid_source_syntax PUBLIC src)
target_link_libraries(vivid_source_syntax PUBLIC tree_sitter_runtime tree_sitter_cpp_lib)

add_library(vivid_wgsl_uniform_layout STATIC
    src/runtime/gpu/wgsl_uniform_layout.cpp
)
target_include_directories(vivid_wgsl_uniform_layout PUBLIC src)

include(cmake/tools.cmake)
include(cmake/operators.cmake)
include(cmake/app.cmake)
include(cmake/tests.cmake)

# --- reset target: wipe user data directories (opt-in) ---
if(APPLE)
    set(VIVID_CONFIG_DIR "$ENV{HOME}/Library/Application Support/Vivid")
elseif(WIN32)
    set(VIVID_CONFIG_DIR "$ENV{APPDATA}/Vivid")
else()
    if(DEFINED ENV{XDG_CONFIG_HOME})
        set(VIVID_CONFIG_DIR "$ENV{XDG_CONFIG_HOME}/vivid")
    else()
        set(VIVID_CONFIG_DIR "$ENV{HOME}/.config/vivid")
    endif()
endif()
set(VIVID_WORKSPACE_DIR "$ENV{HOME}/Documents/Vivid")

add_custom_target(reset
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${VIVID_CONFIG_DIR}" "${VIVID_WORKSPACE_DIR}"
    COMMENT "Removing Vivid user data: ${VIVID_CONFIG_DIR} and ${VIVID_WORKSPACE_DIR}"
    VERBATIM
)
