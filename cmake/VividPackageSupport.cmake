# VividPackageSupport.cmake — include in cmake-based Vivid packages
#
# Provides vivid_package_operator(name source [GPU] [EXTRA_LIBS ...])
# which runs operator_codegen and builds the operator dylib correctly.
#
#   name        cmake target + dylib name (matches the operator dir).
#   source      the operator .cpp/.mm (codegen scans it for the operator class).
#   GPU         set for GPU operators — links wgpu-native + adds the WebGPU
#               include dir (from VIVID_WEBGPU_* below). Required for any operator
#               that inherits GpuProcessable / WgslFilterBase / includes webgpu.h.
#   EXTRA_LIBS  additional libraries to link (rarely needed).
#
# Variables expected from the package manager:
#   VIVID_SRC_DIR        — path to Vivid source root (for operator_api/ headers)
#   VIVID_BUILD_DIR      — path to Vivid build dir (for operator_codegen tool)
#   VIVID_PLUGIN_SUFFIX  — dylib suffix (.dylib / .so)
#
# Optional (injected by the package manager when available):
#   VIVID_WEBGPU_INCLUDE_DIR / VIVID_WEBGPU_LIB_DIR  (used by the GPU flag)
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

    # GPU operators: add the WebGPU include dir and link wgpu-native. Paths come
    # from the package manager (PackageCompiler::managed_webgpu_paths), so the
    # cmake path links against the same host runtime binary as the clang++ path.
    if(ARG_GPU)
        if(VIVID_WEBGPU_INCLUDE_DIR)
            target_include_directories(${name} PRIVATE "${VIVID_WEBGPU_INCLUDE_DIR}")
        endif()
        if(VIVID_WEBGPU_LIB_DIR)
            target_link_directories(${name} PRIVATE "${VIVID_WEBGPU_LIB_DIR}")
            target_link_libraries(${name} PRIVATE wgpu_native)
        else()
            message(WARNING "vivid_package_operator(${name} GPU): no VIVID_WEBGPU_LIB_DIR "
                            "provided — wgpu-native will not be linked. Build via the Vivid "
                            "package manager so it can inject the WebGPU paths.")
        endif()
    endif()

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
