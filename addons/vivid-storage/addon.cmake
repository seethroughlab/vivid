# vivid-storage addon
# Provides simple JSON-based key/value persistent storage
#
# Usage in project CMakeLists.txt:
#   include(path/to/vivid-storage/addon.cmake)
#   vivid_use_storage(your_target)

cmake_minimum_required(VERSION 3.20)

set(VIVID_STORAGE_DIR ${CMAKE_CURRENT_LIST_DIR})

include(FetchContent)

# nlohmann/json for JSON parsing
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Function to add vivid-storage to a target
function(vivid_use_storage TARGET)
    target_sources(${TARGET} PRIVATE
        ${VIVID_STORAGE_DIR}/src/storage.cpp
    )
    target_include_directories(${TARGET} PRIVATE
        ${VIVID_STORAGE_DIR}/include
    )
    target_link_libraries(${TARGET} PRIVATE
        nlohmann_json::nlohmann_json
    )
endfunction()
