# Vivid Video Addon
# Video playback with HAP codec support (GPU-compressed textures)

# macOS-only for now (uses AVFoundation)
if(NOT APPLE)
    message(WARNING "vivid-video addon is currently macOS-only")
    return()
endif()

add_library(vivid-video SHARED
    src/audio_player.cpp
    src/hap.c
    src/hap_decoder.mm
    src/video_player.cpp
)

target_include_directories(vivid-video PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(vivid-video PUBLIC
    webgpu
    glm
    glfw
)

# macOS frameworks for AVFoundation
if(APPLE)
    target_link_libraries(vivid-video PRIVATE
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreAudio"
        "-framework AudioToolbox"
    )

    # Enable Objective-C++
    set_source_files_properties(src/hap_decoder.mm PROPERTIES
        COMPILE_FLAGS "-x objective-c++"
    )

    # Set install name for macOS so chains can find the library
    set_target_properties(vivid-video PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Copy library next to vivid executable
add_custom_command(TARGET vivid-video POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:vivid-video>
        $<TARGET_FILE_DIR:vivid>/
    COMMENT "Copying vivid-video library"
)
