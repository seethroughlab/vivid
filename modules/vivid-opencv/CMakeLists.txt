# vivid-opencv - OpenCV integration addon (stub)
#
# This addon requires OpenCV 4.x to be installed.
# Currently a stub/template for community contribution.

# Uncomment when implementing:
# find_package(OpenCV 4 REQUIRED)

add_library(vivid-opencv STATIC
    src/opencv.cpp
)

target_include_directories(vivid-opencv PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(vivid-opencv PRIVATE
    vivid-core
)

# Uncomment when implementing:
# target_link_libraries(vivid-opencv PRIVATE ${OpenCV_LIBS})
# target_include_directories(vivid-opencv PRIVATE ${OpenCV_INCLUDE_DIRS})
