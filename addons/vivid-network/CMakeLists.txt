# Vivid Network Addon
# Network communication for installations and remote control
# - UDP: Raw datagram send/receive (hardware protocols, Artnet)
# - TCP: Stream connections (custom protocols)
# - HTTP: REST API server for remote control
# - WebSocket: Real-time bidirectional communication

set(NETWORK_SOURCES
    src/udp_in.cpp
    src/udp_out.cpp
    src/osc_in.cpp
    src/osc_out.cpp
    src/web_server.cpp
)

add_library(vivid-network SHARED ${NETWORK_SOURCES})

target_include_directories(vivid-network PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core/include
)

# Link against core and ixwebsocket (already fetched in main CMakeLists.txt)
target_link_libraries(vivid-network PUBLIC
    vivid-core
    ixwebsocket
)

# Platform-specific configuration
if(APPLE)
    set_target_properties(vivid-network PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(WIN32)
    set_target_properties(vivid-network PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
endif()

# Copy library next to vivid executable
add_custom_command(TARGET vivid-network POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:vivid-network>
        $<TARGET_FILE_DIR:vivid>/
    COMMENT "Copying vivid-network library"
)
