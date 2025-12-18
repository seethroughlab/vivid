# Vivid Serial Addon
# Serial communication for Arduino, sensors, and DMX lighting
#
# Note: This addon is skipped for Emscripten/web builds since
# serial ports are not available in browsers.

set(SERIAL_SOURCES
    src/serial_port.cpp
    src/serial_out.cpp
    src/serial_in.cpp
    src/dmx_out.cpp
)

add_library(vivid-serial SHARED ${SERIAL_SOURCES})

target_include_directories(vivid-serial PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(vivid-serial PUBLIC vivid-core)

# Platform-specific serial dependencies
if(WIN32)
    # Windows uses native Win32 serial API
    target_compile_definitions(vivid-serial PRIVATE VIVID_SERIAL_WIN32)
    set_target_properties(vivid-serial PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
elseif(APPLE)
    # macOS uses POSIX + IOKit for serial enumeration
    target_link_libraries(vivid-serial PRIVATE "-framework IOKit" "-framework CoreFoundation")
    target_compile_definitions(vivid-serial PRIVATE VIVID_SERIAL_MACOS)
    set_target_properties(vivid-serial PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
else()
    # Linux
    target_compile_definitions(vivid-serial PRIVATE VIVID_SERIAL_LINUX)

    # Optionally link libserialport if available for better enumeration
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(SERIALPORT QUIET libserialport)
        if(SERIALPORT_FOUND)
            target_link_libraries(vivid-serial PRIVATE ${SERIALPORT_LIBRARIES})
            target_include_directories(vivid-serial PRIVATE ${SERIALPORT_INCLUDE_DIRS})
            target_compile_definitions(vivid-serial PRIVATE USE_LIBSERIALPORT)
        endif()
    endif()
endif()

# Copy library next to vivid executable
add_custom_command(TARGET vivid-serial POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:vivid-serial>
        $<TARGET_FILE_DIR:vivid>/
    COMMENT "Copying vivid-serial library"
)
