# Loadable visual operators — each built as a standalone MODULE .dylib that the
# app dlopen's at runtime (P2). The dylib links the SAME shared libwgpu_native as
# the host (target `webgpu` → `-lwgpu_native`), so a host-created WGPUDevice handed
# in via VividGpuContext is valid inside the operator: one wgpu instance, two images.
#
# Self-contained against operator_api/ headers only — operators must NOT depend on
# app/src/gpu/*.cpp (those live in the executable). P2.0 has one spike operator;
# P2.2/P2.3 grow this into an add_vivid_operator() helper + a package compiler.

# add_vivid_operator(<name> <source>...) — a MODULE .dylib copied into the app bundle's
# PlugIns/ next to the host + libwgpu_native.dylib.
function(add_vivid_operator name)
    add_library(${name} MODULE ${ARGN})
    set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX ".dylib")
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_link_libraries(${name} PRIVATE webgpu)
    if(APPLE)
        # wgpu symbols import from @rpath/libwgpu_native.dylib (already loaded by the
        # host); the rpath lets a fresh dlopen also resolve it from Contents/MacOS/.
        # -undefined dynamic_lookup future-proofs operator→host runtime callbacks.
        target_link_options(${name} PRIVATE
            -undefined dynamic_lookup
            "LINKER:-rpath,@loader_path/../MacOS")
    endif()
    # Drop the dylib into the host bundle's PlugIns/ so the startup scan finds it.
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:vivid_poc>/PlugIns"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${name}>"
                "$<TARGET_BUNDLE_CONTENT_DIR:vivid_poc>/PlugIns/"
        VERBATIM)
endfunction()

add_vivid_operator(vivid_spike_solid spike/spike_solid.cpp)
