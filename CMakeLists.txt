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
    set(VIVID_USE_WGPU_NATIVE_UPSTREAM OFF CACHE BOOL "" FORCE)
endif()

if(VIVID_SANITIZE_THREAD)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
    set(VIVID_USE_WGPU_NATIVE_UPSTREAM OFF CACHE BOOL "" FORCE)
endif()

# --- Modular build configuration ---
include(cmake/dependencies.cmake)
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
