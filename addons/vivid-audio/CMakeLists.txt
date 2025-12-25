# Vivid Audio Addon
# Audio effects and analysis for the vivid chain
# - Time-based: Delay, Echo, Reverb
# - Dynamics: Compressor, Limiter, Gate
# - Modulation: Chorus, Flanger, Phaser
# - Distortion: Overdrive, Bitcrush
# - Analysis: Levels, FFT, BandSplit, BeatDetect

include(FetchContent)

# KissFFT for frequency analysis
set(KISSFFT_TEST OFF CACHE BOOL "" FORCE)
set(KISSFFT_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    kissfft
    GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
    GIT_TAG        131.1.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(kissfft)

set(AUDIO_SOURCES
    # Sources
    src/audio_in.cpp
    src/audio_file.cpp
    # Effects
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
    src/tape_effect.cpp
    src/audio_filter.cpp
    # Analysis
    src/audio_analyzer.cpp
    src/levels.cpp
    src/fft.cpp
    src/band_split.cpp
    src/beat_detect.cpp
    # Synthesis
    src/oscillator.cpp
    src/envelope.cpp
    src/synth.cpp
    src/poly_synth.cpp
    src/wavetable_synth.cpp
    src/fm_synth.cpp
    src/granular.cpp
    src/noise_gen.cpp
    src/crackle.cpp
    src/pitch_env.cpp
    src/formant.cpp
    # Envelopes
    src/decay.cpp
    src/ar.cpp
    # Drums
    src/kick.cpp
    src/snare.cpp
    src/hihat.cpp
    src/clap.cpp
    # Sequencing
    src/clock.cpp
    src/sequencer.cpp
    src/euclidean.cpp
    src/song.cpp
    # Mixing
    src/audio_mixer.cpp
    src/audio_gain.cpp
    # Sampling
    src/sample_bank.cpp
    src/sample_player.cpp
    src/sampler.cpp
    src/multi_sampler.cpp
    # Presets
    src/preset.cpp
)

add_library(vivid-audio SHARED ${AUDIO_SOURCES})

target_include_directories(vivid-audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core/include
    ${kissfft_SOURCE_DIR}
)

target_link_libraries(vivid-audio PUBLIC
    vivid-core
    kissfft
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
