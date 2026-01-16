# Vivid Texture Sharing Addon
# Cross-platform texture sharing between applications
# - macOS: Syphon framework (auto-downloaded if not installed)
# - Windows: Spout SDK (stub)
# - Linux: Not supported

include(FetchContent)

# Common sources (all platforms)
set(TEXSHARE_SOURCES
    src/texture_share_in.cpp
    src/texture_share_out.cpp
)

# Platform-specific sources
if(APPLE)
    list(APPEND TEXSHARE_SOURCES
        src/platform/syphon_backend.mm
    )

    # Check for installed Syphon framework first
    find_library(SYPHON_FRAMEWORK Syphon
        PATHS
            /Library/Frameworks
            ~/Library/Frameworks
    )

    if(NOT SYPHON_FRAMEWORK)
        # Download pre-built Syphon framework from GitHub releases
        message(STATUS "Syphon framework not found - downloading from GitHub releases...")

        set(SYPHON_VERSION "5")
        set(SYPHON_URL "https://github.com/Syphon/Syphon-Framework/releases/download/${SYPHON_VERSION}/Syphon.framework.zip")
        set(SYPHON_DIR "${CMAKE_BINARY_DIR}/_deps/syphon")

        file(MAKE_DIRECTORY "${SYPHON_DIR}")

        if(NOT EXISTS "${SYPHON_DIR}/Syphon.framework")
            file(DOWNLOAD ${SYPHON_URL} "${SYPHON_DIR}/Syphon.framework.zip"
                STATUS SYPHON_DOWNLOAD_STATUS
                SHOW_PROGRESS
            )
            list(GET SYPHON_DOWNLOAD_STATUS 0 SYPHON_DOWNLOAD_OK)
            if(NOT SYPHON_DOWNLOAD_OK EQUAL 0)
                message(WARNING "Failed to download Syphon framework. Texture sharing will be disabled on macOS.")
                message(WARNING "Install manually: https://github.com/Syphon/Syphon-Framework/releases")
            else()
                file(ARCHIVE_EXTRACT INPUT "${SYPHON_DIR}/Syphon.framework.zip" DESTINATION "${SYPHON_DIR}")
                message(STATUS "Syphon framework downloaded to ${SYPHON_DIR}")
            endif()
        endif()

        if(EXISTS "${SYPHON_DIR}/Syphon.framework")
            set(SYPHON_FRAMEWORK "${SYPHON_DIR}/Syphon.framework")
        endif()
    endif()

elseif(WIN32)
    list(APPEND TEXSHARE_SOURCES
        src/platform/spout_backend.cpp
    )
    # Note: Spout SDK integration would go here
    # FetchContent_Declare(
    #     spout
    #     GIT_REPOSITORY https://github.com/leadedge/Spout2.git
    #     GIT_TAG        2.007j
    #     GIT_SHALLOW    TRUE
    # )
else()
    # Linux - null backend
    list(APPEND TEXSHARE_SOURCES
        src/platform/null_backend.cpp
    )
endif()

add_library(vivid-texshare SHARED ${TEXSHARE_SOURCES})

target_include_directories(vivid-texshare PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/modules/vivid-core/include
)

target_link_libraries(vivid-texshare PUBLIC
    webgpu
    glm
    glfw
    vivid-core
)

# Platform-specific configuration
if(APPLE)
    # macOS system frameworks
    target_link_libraries(vivid-texshare PRIVATE
        "-framework Metal"
        "-framework Foundation"
        "-framework IOSurface"
        "-framework CoreGraphics"
        "-framework AppKit"
    )

    # Link Syphon framework (found or downloaded above)
    if(SYPHON_FRAMEWORK)
        target_link_libraries(vivid-texshare PRIVATE ${SYPHON_FRAMEWORK})
        message(STATUS "Using Syphon framework: ${SYPHON_FRAMEWORK}")

        # Get the framework directory for include path
        get_filename_component(SYPHON_FRAMEWORK_DIR ${SYPHON_FRAMEWORK} DIRECTORY)
        target_compile_options(vivid-texshare PRIVATE
            -F${SYPHON_FRAMEWORK_DIR}
        )
    else()
        message(WARNING "Syphon framework not available - texture sharing disabled on macOS")
        target_compile_definitions(vivid-texshare PRIVATE VIVID_TEXSHARE_NO_SYPHON)
    endif()

    # Enable Objective-C++
    set_source_files_properties(
        src/platform/syphon_backend.mm
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )

    # Set install name for macOS so chains can find the library
    set_target_properties(vivid-texshare PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )

elseif(WIN32)
    # Windows - define export macro when building the DLL
    target_compile_definitions(vivid-texshare PRIVATE vivid_texshare_EXPORTS)

    # Set output directories for import library
    set_target_properties(vivid-texshare PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/Debug
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/Release
    )

else()
    # Linux - no additional configuration needed
    message(STATUS "vivid-texshare: Linux texture sharing not supported (using null backend)")
endif()

# Library is built to lib/ directly (CMAKE_LIBRARY_OUTPUT_DIRECTORY)
