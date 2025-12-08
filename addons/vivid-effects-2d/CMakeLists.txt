# Vivid Effects 2D Addon

# Fetch stb for image loading
include(FetchContent)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb)

# Enable symbol export on Windows
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_library(vivid-effects-2d SHARED
    src/texture_operator.cpp
    src/noise.cpp
    src/solid_color.cpp
    src/composite.cpp
    src/ramp.cpp
    src/feedback.cpp
    src/output.cpp
    src/image.cpp
    src/displace.cpp
    src/blur.cpp
    src/transform.cpp
    src/shape.cpp
    src/lfo.cpp
    src/hsv.cpp
    src/brightness.cpp
    src/mirror.cpp
    src/edge.cpp
    src/pixelate.cpp
    src/tile.cpp
    src/chromatic_aberration.cpp
    src/switch_op.cpp
    src/bloom.cpp
    src/gradient.cpp
    src/dither.cpp
    src/quantize.cpp
    src/scanlines.cpp
    src/crt_effect.cpp
    src/downsample.cpp
    # Particle systems (Phase 5)
    src/particle_renderer.cpp
    src/particles.cpp
    src/point_sprites.cpp
    # Canvas (Phase 5)
    src/font_atlas.cpp
    src/canvas_renderer.cpp
    src/canvas.cpp
)

target_include_directories(vivid-effects-2d PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core/include
    ${stb_SOURCE_DIR}
)

target_link_libraries(vivid-effects-2d PUBLIC
    webgpu
    glm
    glfw
)

# Platform-specific settings
if(APPLE)
    # Set install name for macOS so chains can find the library
    set_target_properties(vivid-effects-2d PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Copy library next to vivid executable
add_custom_command(TARGET vivid-effects-2d POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:vivid-effects-2d>
        $<TARGET_FILE_DIR:vivid>/
    COMMENT "Copying vivid-effects-2d library"
)

# Copy shaders to build directory
file(GLOB EFFECT_SHADERS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.wgsl)
if(EFFECT_SHADERS)
    add_custom_command(TARGET vivid-effects-2d POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:vivid>/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EFFECT_SHADERS} $<TARGET_FILE_DIR:vivid>/shaders/
        COMMENT "Copying effect shaders"
    )
endif()
