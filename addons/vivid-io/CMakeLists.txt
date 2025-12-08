# Vivid I/O Addon
# Lightweight I/O utilities (image loading, file utilities)
# This addon has no dependencies on other vivid addons

include(FetchContent)

# Fetch stb for image loading
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb)

# Enable symbol export on Windows
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_library(vivid-io SHARED
    src/image_loader.cpp
)

target_include_directories(vivid-io PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${stb_SOURCE_DIR}
)

# No link dependencies - vivid-io is standalone

# Platform-specific settings
if(APPLE)
    set_target_properties(vivid-io PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Copy library next to vivid executable
add_custom_command(TARGET vivid-io POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:vivid-io>
        $<TARGET_FILE_DIR:vivid>/
    COMMENT "Copying vivid-io library"
)
