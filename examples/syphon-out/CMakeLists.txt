# Syphon Output Example - Custom CMakeLists.txt
# Demonstrates sharing textures with other apps via Syphon (macOS only)

cmake_minimum_required(VERSION 3.20)
project(syphon_out_operators)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Vivid headers (passed by runtime)
set(VIVID_INCLUDE_DIR "" CACHE PATH "Vivid include directory")
set(STB_INCLUDE_DIR "" CACHE PATH "STB include directory")

# GLM for math
include(FetchContent)

find_package(glm CONFIG QUIET)
if(NOT glm_FOUND)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
    )
    FetchContent_MakeAvailable(glm)
endif()

# Source files
add_library(operators SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/chain.cpp"
)

target_include_directories(operators PRIVATE
    ${VIVID_INCLUDE_DIR}
    ${STB_INCLUDE_DIR}
)

target_link_libraries(operators PRIVATE
    glm::glm
)

# Include Syphon addon
include("${CMAKE_CURRENT_SOURCE_DIR}/../../addons/vivid-syphon/addon.cmake")
vivid_use_syphon(operators)

set_target_properties(operators PROPERTIES
    OUTPUT_NAME "operators"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

if(APPLE)
    target_link_options(operators PRIVATE -undefined dynamic_lookup)
endif()
