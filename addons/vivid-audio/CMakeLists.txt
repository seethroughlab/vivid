# Vivid Audio Addon
# Audio effects for the vivid chain
# - Time-based: Delay, Echo, Reverb
# - Dynamics: Compressor, Limiter, Gate
# - Modulation: Chorus, Flanger, Phaser
# - Distortion: Overdrive, Bitcrush

set(AUDIO_SOURCES
    src/audio_file.cpp
    src/audio_effect.cpp
    src/delay.cpp
    src/echo.cpp
    src/reverb.cpp
    src/compressor.cpp
    src/limiter.cpp
    src/gate.cpp
    src/chorus.cpp
    src/flanger.cpp
    src/phaser.cpp
    src/overdrive.cpp
    src/bitcrush.cpp
)

add_library(vivid-audio SHARED ${AUDIO_SOURCES})

target_include_directories(vivid-audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(vivid-audio PUBLIC
    vivid-core
)

# Platform-specific configuration
if(APPLE)
    set_target_properties(vivid-audio PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(WIN32)
    set_target_properties(vivid-audio PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
endif()

# Copy library next to vivid executable
add_custom_command(TARGET vivid-audio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:vivid-audio>
        $<TARGET_FILE_DIR:vivid>/
    COMMENT "Copying vivid-audio library"
)
