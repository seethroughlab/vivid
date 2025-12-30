# check_file_exists.cmake
# Usage: cmake -P check_file_exists.cmake <file_path>
# Returns success (0) if file exists, failure (1) if not

# Get the file path from command line arguments
# CMake passes arguments after -P script.cmake as CMAKE_ARGV3, CMAKE_ARGV4, etc.
set(FILE_PATH "${CMAKE_ARGV3}")

if(NOT FILE_PATH)
    message(FATAL_ERROR "Usage: cmake -P check_file_exists.cmake <file_path>")
endif()

if(EXISTS "${FILE_PATH}")
    message(STATUS "File exists: ${FILE_PATH}")
else()
    message(FATAL_ERROR "File does not exist: ${FILE_PATH}")
endif()
