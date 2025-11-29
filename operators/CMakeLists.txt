# Vivid Built-in Operators
# This file can be used as a reference for building operators as shared libraries.
# Users can include these operators in their projects by copying the source files.

# Function to add an operator as a shared library
function(add_operator name)
    add_library(${name} SHARED ${name}.cpp)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/runtime/include
    )
    target_link_libraries(${name} PRIVATE glm::glm)
    set_target_properties(${name} PROPERTIES
        PREFIX ""  # No 'lib' prefix
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/operators
    )

    # Platform-specific extension
    if(WIN32)
        set_target_properties(${name} PROPERTIES SUFFIX ".dll")
    elseif(APPLE)
        set_target_properties(${name} PROPERTIES SUFFIX ".dylib")
    else()
        set_target_properties(${name} PROPERTIES SUFFIX ".so")
    endif()
endfunction()

# Note: Built-in operators are typically copied into user projects
# rather than loaded as separate libraries. This CMakeLists.txt
# serves as a reference for how to build operators.

# Uncomment to build operators as standalone libraries:
# add_operator(lfo)
# add_operator(noise)
# add_operator(brightness)
# add_operator(hsv)
# add_operator(composite)
# add_operator(feedback)
# add_operator(blur)
# add_operator(transform)
# add_operator(gradient)
# add_operator(displacement)
# add_operator(edge)
# add_operator(shape)
# add_operator(constant)
# add_operator(math)
# add_operator(logic)
# add_operator(switch)
# add_operator(passthrough)
# add_operator(reference)
# add_operator(chromatic_aberration)
# add_operator(pixelate)
# add_operator(scanlines)
