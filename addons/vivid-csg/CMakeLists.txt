# vivid-csg addon CMakeLists.txt

cmake_minimum_required(VERSION 3.16)

project(vivid-csg CXX)

include(FetchContent)

# Fetch Manifold library
FetchContent_Declare(
    manifold
    GIT_REPOSITORY https://github.com/elalish/manifold.git
    GIT_TAG        v3.0.1
    GIT_SHALLOW    TRUE
)

# Manifold options
set(MANIFOLD_EXPORT OFF CACHE BOOL "" FORCE)
set(MANIFOLD_TEST OFF CACHE BOOL "" FORCE)
set(MANIFOLD_CROSS_SECTION OFF CACHE BOOL "" FORCE)
set(MANIFOLD_PAR OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(manifold)

# Sources
set(CSG_ADDON_SOURCES
    src/csg.cpp
)

set(CSG_ADDON_HEADERS
    include/vivid/csg/csg.h
)

# Create shared library
add_library(vivid-csg SHARED
    ${CSG_ADDON_SOURCES}
    ${CSG_ADDON_HEADERS}
)

# Include directories
target_include_directories(vivid-csg
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/runtime/include
        ${CMAKE_SOURCE_DIR}/external/glm
)

# Link against Manifold
target_link_libraries(vivid-csg
    PRIVATE
        manifold
)

# Platform-specific settings
if(PLATFORM_MACOS)
    target_compile_definitions(vivid-csg PRIVATE PLATFORM_MACOS=1)
    set_target_properties(vivid-csg PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
        LINK_FLAGS "-undefined dynamic_lookup"
    )
elseif(PLATFORM_WIN32)
    target_compile_definitions(vivid-csg PRIVATE PLATFORM_WIN32=1)
elseif(PLATFORM_LINUX)
    target_compile_definitions(vivid-csg PRIVATE PLATFORM_LINUX=1)
endif()

# Set C++ standard
target_compile_features(vivid-csg PUBLIC cxx_std_17)

# Set output directory
set_target_properties(vivid-csg PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${VIVID_ADDONS_LIB_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${VIVID_ADDONS_LIB_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${VIVID_ADDONS_LIB_DIR}"
    FOLDER "Vivid/Addons"
)

# Copy headers to output directory
add_custom_command(TARGET vivid-csg POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${VIVID_ADDONS_INCLUDE_DIR}"
)

# Copy addon.json to meta directory
add_custom_command(TARGET vivid-csg POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/addon.json"
        "${VIVID_ADDONS_META_DIR}/csg.json"
)

message(STATUS "[vivid-csg] Configured")
