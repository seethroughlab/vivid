# Vivid Addons - Master Build Script
#
# This CMakeLists.txt builds all addons as static libraries.
# Addons are compiled once during the main vivid build, then linked
# during hot-reload for fast iteration.
#
# Each addon provides:
#   - addon.json: Metadata for auto-detection
#   - CMakeLists.txt: Build script (creates static library)
#   - include/: Public headers
#   - src/: Implementation files

cmake_minimum_required(VERSION 3.20)

# Set output directories for all addons
set(VIVID_ADDONS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/addons")
set(VIVID_ADDONS_LIB_DIR "${VIVID_ADDONS_OUTPUT_DIR}/lib")
set(VIVID_ADDONS_INCLUDE_DIR "${VIVID_ADDONS_OUTPUT_DIR}/include")
set(VIVID_ADDONS_META_DIR "${VIVID_ADDONS_OUTPUT_DIR}/meta")

# Create output directories
file(MAKE_DIRECTORY ${VIVID_ADDONS_LIB_DIR})
file(MAKE_DIRECTORY ${VIVID_ADDONS_INCLUDE_DIR})
file(MAKE_DIRECTORY ${VIVID_ADDONS_META_DIR})

# Collect all addon subdirectories
file(GLOB ADDON_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/vivid-*")

# Track which addons were built
set(VIVID_BUILT_ADDONS "" CACHE INTERNAL "List of built addons")

foreach(ADDON_DIR ${ADDON_DIRS})
    if(IS_DIRECTORY ${ADDON_DIR})
        get_filename_component(ADDON_NAME ${ADDON_DIR} NAME)

        # Check if addon has a CMakeLists.txt
        if(EXISTS "${ADDON_DIR}/CMakeLists.txt")
            message(STATUS "[Addons] Configuring: ${ADDON_NAME}")
            add_subdirectory(${ADDON_DIR})
        else()
            message(STATUS "[Addons] Skipping ${ADDON_NAME} (no CMakeLists.txt)")
        endif()
    endif()
endforeach()

# Generate combined addons registry (addons.json)
# This lists all available addons for the runtime to use
set(ADDONS_JSON_CONTENT "{\n  \"addons\": [\n")
set(FIRST_ADDON TRUE)

foreach(ADDON_DIR ${ADDON_DIRS})
    if(IS_DIRECTORY ${ADDON_DIR})
        get_filename_component(ADDON_NAME ${ADDON_DIR} NAME)

        # Check if addon has addon.json
        if(EXISTS "${ADDON_DIR}/addon.json")
            if(NOT FIRST_ADDON)
                set(ADDONS_JSON_CONTENT "${ADDONS_JSON_CONTENT},\n")
            endif()
            set(FIRST_ADDON FALSE)

            # Read the addon.json content
            file(READ "${ADDON_DIR}/addon.json" ADDON_JSON)
            # Indent it properly
            string(REPLACE "\n" "\n    " ADDON_JSON_INDENTED "${ADDON_JSON}")
            set(ADDONS_JSON_CONTENT "${ADDONS_JSON_CONTENT}    ${ADDON_JSON_INDENTED}")
        endif()
    endif()
endforeach()

set(ADDONS_JSON_CONTENT "${ADDONS_JSON_CONTENT}\n  ],\n  \"generated\": \"${CMAKE_CURRENT_TIMESTAMP}\"\n}\n")

file(WRITE "${VIVID_ADDONS_META_DIR}/addons.json" "${ADDONS_JSON_CONTENT}")
message(STATUS "[Addons] Generated addons.json registry")
