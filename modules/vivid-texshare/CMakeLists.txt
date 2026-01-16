# Vivid Texture Sharing Addon
# Cross-platform texture sharing between applications
# - macOS: Syphon framework (built from source for Metal support)
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

    # Build Syphon framework from source (required for Metal support)
    # The official SDK release doesn't include Metal - must build from main branch
    set(SYPHON_DIR "${CMAKE_BINARY_DIR}/_deps/syphon-src")
    set(SYPHON_BUILD_DIR "${CMAKE_BINARY_DIR}/_deps/syphon-build")
    set(SYPHON_FRAMEWORK_PATH "${SYPHON_BUILD_DIR}/Syphon.framework")

    # Check if we already have a built framework with the binary
    if(NOT EXISTS "${SYPHON_FRAMEWORK_PATH}/Syphon")
        message(STATUS "Syphon: Fetching source from GitHub...")

        FetchContent_Declare(
            syphon
            GIT_REPOSITORY https://github.com/Syphon/Syphon-Framework.git
            GIT_TAG        main
            GIT_SHALLOW    TRUE
            SOURCE_DIR     "${SYPHON_DIR}"
        )

        FetchContent_Populate(syphon)

        if(EXISTS "${SYPHON_DIR}/Syphon.xcodeproj")
            # Build Syphon framework using xcodebuild
            # Build universal binary (arm64 + x86_64) for CI compatibility
            message(STATUS "Syphon: Building universal framework (arm64 + x86_64)...")
            file(MAKE_DIRECTORY "${SYPHON_BUILD_DIR}")

            execute_process(
                COMMAND xcodebuild
                    -project Syphon.xcodeproj
                    -scheme Syphon
                    -configuration Release
                    -derivedDataPath "${SYPHON_BUILD_DIR}/DerivedData"
                    CONFIGURATION_BUILD_DIR=${SYPHON_BUILD_DIR}
                    ONLY_ACTIVE_ARCH=NO
                    BUILD_LIBRARY_FOR_DISTRIBUTION=YES
                    ARCHS=arm64\ x86_64
                WORKING_DIRECTORY "${SYPHON_DIR}"
                RESULT_VARIABLE SYPHON_BUILD_RESULT
                OUTPUT_VARIABLE SYPHON_BUILD_OUTPUT
                ERROR_VARIABLE SYPHON_BUILD_ERROR
            )

            if(NOT SYPHON_BUILD_RESULT EQUAL 0)
                message(WARNING "Syphon: Build failed:")
                message(WARNING "${SYPHON_BUILD_ERROR}")
                message(WARNING "Syphon: Texture sharing will be disabled on macOS.")
                set(SYPHON_BUILD_SUCCESS FALSE)
            else()
                message(STATUS "Syphon: Framework built successfully at ${SYPHON_FRAMEWORK_PATH}")
                set(SYPHON_BUILD_SUCCESS TRUE)
            endif()
        else()
            message(WARNING "Syphon: Failed to fetch source (Syphon.xcodeproj not found)")
            set(SYPHON_BUILD_SUCCESS FALSE)
        endif()
    else()
        message(STATUS "Syphon: Using cached framework from ${SYPHON_BUILD_DIR}")
        set(SYPHON_BUILD_SUCCESS TRUE)
    endif()

    # Only set SYPHON_FRAMEWORK if build succeeded AND framework exists
    if(SYPHON_BUILD_SUCCESS AND EXISTS "${SYPHON_FRAMEWORK_PATH}/Syphon")
        set(SYPHON_FRAMEWORK "${SYPHON_FRAMEWORK_PATH}")
    else()
        # Ensure we don't accidentally use any system framework
        unset(SYPHON_FRAMEWORK)
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

    # Link Syphon framework (built from source above)
    if(SYPHON_FRAMEWORK)
        # Use explicit path - don't let linker search system framework paths
        target_link_libraries(vivid-texshare PRIVATE "${SYPHON_FRAMEWORK}")
        message(STATUS "Syphon: Linking framework: ${SYPHON_FRAMEWORK}")

        # Add framework search path for headers
        target_compile_options(vivid-texshare PRIVATE
            -F${SYPHON_BUILD_DIR}
        )
    else()
        message(WARNING "Syphon: Framework not available - texture sharing disabled on macOS")
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
