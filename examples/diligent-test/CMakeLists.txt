# DiligentPBR standalone test
# Only built when VIVID_USE_DILIGENT is enabled

if(VIVID_USE_DILIGENT)
    add_executable(diligent-test main.cpp)

    target_compile_definitions(diligent-test PRIVATE VIVID_USE_DILIGENT=1)

    target_include_directories(diligent-test PRIVATE
        ${CMAKE_SOURCE_DIR}/runtime/include
        ${CMAKE_SOURCE_DIR}/runtime/src
    )

    target_link_libraries(diligent-test PRIVATE
        glfw
        glm::glm
        Diligent-GraphicsEngineVk-static
        Diligent-GraphicsTools
        DiligentFX
    )

    # Link platform-specific libraries
    if(APPLE)
        target_link_libraries(diligent-test PRIVATE
            "-framework Cocoa"
            "-framework IOKit"
            "-framework Metal"
            "-framework QuartzCore"
        )
    endif()

    # Need to link the compiled Diligent source files from runtime
    target_sources(diligent-test PRIVATE
        ${CMAKE_SOURCE_DIR}/runtime/src/diligent_backend.cpp
        ${CMAKE_SOURCE_DIR}/runtime/src/diligent_renderer.cpp
        ${CMAKE_SOURCE_DIR}/runtime/src/diligent_pbr.cpp
    )

    if(APPLE)
        target_sources(diligent-test PRIVATE
            ${CMAKE_SOURCE_DIR}/runtime/src/metal_layer_helper.mm
        )
        set_source_files_properties(
            ${CMAKE_SOURCE_DIR}/runtime/src/metal_layer_helper.mm
            PROPERTIES COMPILE_FLAGS "-x objective-c++"
        )
    endif()

    message(STATUS "DiligentPBR test enabled")
else()
    message(STATUS "DiligentPBR test disabled (VIVID_USE_DILIGENT=OFF)")
endif()
