# Vivid Addons Master CMakeLists.txt
# Auto-discovers and builds all vivid-* addon directories

cmake_minimum_required(VERSION 3.16)

# Output directories for built addons
set(VIVID_ADDONS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/addons")
set(VIVID_ADDONS_LIB_DIR "${VIVID_ADDONS_OUTPUT_DIR}/lib")
set(VIVID_ADDONS_INCLUDE_DIR "${VIVID_ADDONS_OUTPUT_DIR}/include")
set(VIVID_ADDONS_META_DIR "${VIVID_ADDONS_OUTPUT_DIR}/meta")

# Create output directories
file(MAKE_DIRECTORY ${VIVID_ADDONS_LIB_DIR})
file(MAKE_DIRECTORY ${VIVID_ADDONS_INCLUDE_DIR})
file(MAKE_DIRECTORY ${VIVID_ADDONS_META_DIR})

# Export paths to parent scope
set(VIVID_ADDONS_LIB_DIR "${VIVID_ADDONS_LIB_DIR}" PARENT_SCOPE)
set(VIVID_ADDONS_INCLUDE_DIR "${VIVID_ADDONS_INCLUDE_DIR}" PARENT_SCOPE)
set(VIVID_ADDONS_META_DIR "${VIVID_ADDONS_META_DIR}" PARENT_SCOPE)

# Auto-discover vivid-* addon directories
file(GLOB ADDON_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/vivid-*")

set(DISCOVERED_ADDONS "")

foreach(ADDON_DIR ${ADDON_DIRS})
    if(IS_DIRECTORY ${ADDON_DIR})
        get_filename_component(ADDON_NAME ${ADDON_DIR} NAME)

        # Check if addon has CMakeLists.txt
        if(EXISTS "${ADDON_DIR}/CMakeLists.txt")
            message(STATUS "[Addons] Configuring: ${ADDON_NAME}")
            add_subdirectory(${ADDON_DIR})
            list(APPEND DISCOVERED_ADDONS ${ADDON_NAME})
        else()
            message(WARNING "[Addons] Skipping ${ADDON_NAME}: no CMakeLists.txt")
        endif()
    endif()
endforeach()

# Generate addons registry file
if(DISCOVERED_ADDONS)
    message(STATUS "[Addons] Discovered addons: ${DISCOVERED_ADDONS}")
else()
    message(STATUS "[Addons] No addons found")
endif()
