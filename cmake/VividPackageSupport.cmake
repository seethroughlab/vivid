# VividPackageSupport.cmake — include in cmake-based Vivid packages
#
# Provides vivid_package_operator(name source [GPU] [EXTRA_LIBS ...])
# which runs operator_codegen and builds the operator dylib correctly.
#
# Variables expected from the package manager:
#   VIVID_SRC_DIR        — path to Vivid source root (for operator_api/ headers)
#   VIVID_BUILD_DIR      — path to Vivid build dir (for operator_codegen tool)
#   VIVID_PLUGIN_SUFFIX  — dylib suffix (.dylib / .so)
#
# Optional:
#   VIVID_HIGHWAY_INCLUDE_DIR / VIVID_HIGHWAY_LIBRARY
#   VIVID_DRAGONBOX_INCLUDE_DIR / VIVID_DRAGONBOX_LIBRARY

cmake_minimum_required(VERSION 3.16)

function(vivid_package_operator name source)
    cmake_parse_arguments(ARG "GPU" "" "EXTRA_LIBS" ${ARGN})

    get_filename_component(_source_abs "${source}" ABSOLUTE
                           BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    get_filename_component(_source_ext "${_source_abs}" EXT)

    if("${_source_ext}" STREQUAL ".mm")
        set(_generated "${CMAKE_CURRENT_BINARY_DIR}/${name}_generated_registration.mm")
    else()
        set(_generated "${CMAKE_CURRENT_BINARY_DIR}/${name}_generated_registration.cpp")
    endif()

    set(_codegen_tool "${VIVID_BUILD_DIR}/tools/operator_codegen/operator_codegen")
    if(NOT EXISTS "${_codegen_tool}")
        message(FATAL_ERROR "vivid_package_operator: operator_codegen not found at ${_codegen_tool}. Set VIVID_BUILD_DIR correctly.")
    endif()

    add_custom_command(
        OUTPUT  "${_generated}"
        COMMAND "${_codegen_tool}" --input "${_source_abs}" --output "${_generated}"
        DEPENDS "${_source_abs}"
        COMMENT "Generating operator registration for ${name}"
        VERBATIM
    )

    add_library(${name} MODULE "${_generated}")
    set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "${VIVID_PLUGIN_SUFFIX}")

    target_include_directories(${name} PRIVATE
        "${VIVID_SRC_DIR}/src"
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_compile_features(${name} PRIVATE cxx_std_17)

    if(VIVID_HIGHWAY_INCLUDE_DIR)
        target_include_directories(${name} PRIVATE "${VIVID_HIGHWAY_INCLUDE_DIR}")
        target_compile_definitions(${name} PRIVATE VIVID_HAS_HIGHWAY=1)
    endif()
    if(VIVID_HIGHWAY_LIBRARY)
        target_link_libraries(${name} PRIVATE "${VIVID_HIGHWAY_LIBRARY}")
    endif()

    if(ARG_EXTRA_LIBS)
        target_link_libraries(${name} PRIVATE ${ARG_EXTRA_LIBS})
    endif()
endfunction()
