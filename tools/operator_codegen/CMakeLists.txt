cmake_minimum_required(VERSION 3.10)
project(operator_codegen LANGUAGES CXX)

add_executable(operator_codegen 
    main.cpp
    descriptor_builder.cpp
    uniform_codegen.cpp
)

target_include_directories(operator_codegen PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(operator_codegen PRIVATE 
    vivid_source_syntax
    vivid_wgsl_uniform_layout
)
