# 2D Physics Demo
cmake_minimum_required(VERSION 3.20)
project(2d_physics_operators)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Find Vivid headers
set(VIVID_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../build/include" CACHE PATH "Vivid include directory")

# GLM for math
find_package(glm CONFIG QUIET)
if(NOT glm_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
    )
    FetchContent_MakeAvailable(glm)
endif()

# Create shared library
add_library(operators SHARED
    chain.cpp
)

target_include_directories(operators PRIVATE
    ${VIVID_INCLUDE_DIR}
)

target_link_libraries(operators PRIVATE
    glm::glm
)

set_target_properties(operators PROPERTIES
    OUTPUT_NAME "operators"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

if(APPLE)
    target_link_options(operators PRIVATE -undefined dynamic_lookup)
endif()
