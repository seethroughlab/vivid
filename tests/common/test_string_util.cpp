#include "test_helpers.h"
#include "common/string_util.h"
#include <string>
#include <cstdint>

int main() {
    // =================================================================
    // Test 1: format_float basics
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 1: format_float basics ===\n");
        check(vivid::format_float(0.0f) == "0.0000", "zero");
        check(vivid::format_float(1.0f) == "1.0000", "one");
        check(vivid::format_float(-1.0f) == "-1.0000", "negative one");
        check(vivid::format_float(3.14159f) == "3.1416", "pi rounds to 4 decimals");
    }

    // =================================================================
    // Test 2: format_float precision parameter
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 2: format_float precision ===\n");
        check(vivid::format_float(1.23456f, 2) == "1.23", "precision 2");
        check(vivid::format_float(1.23456f, 0) == "1", "precision 0");
        check(vivid::format_float(1.23456f, 6) == "1.234560", "precision 6");
    }

    // =================================================================
    // Test 3: format_float edge cases
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 3: format_float edge cases ===\n");
        check(vivid::format_float(0.00001f) == "0.0000", "very small rounds to 0.0000");
        check(vivid::format_float(-0.0f) == "0.0000" || vivid::format_float(-0.0f) == "-0.0000",
              "negative zero");
        check(vivid::format_float(99999.5f, 1) == "99999.5", "large value");
    }

    // =================================================================
    // Test 4: format_uint
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 4: format_uint ===\n");
        check(vivid::format_uint(0) == "0", "uint zero");
        check(vivid::format_uint(1) == "1", "uint one");
        check(vivid::format_uint(42) == "42", "uint 42");
        check(vivid::format_uint(UINT32_MAX) == "4294967295", "uint max");
    }

    // =================================================================
    // Test 5: format_int
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 5: format_int ===\n");
        check(vivid::format_int(0) == "0", "int zero");
        check(vivid::format_int(1) == "1", "int one");
        check(vivid::format_int(-1) == "-1", "int negative one");
        check(vivid::format_int(42) == "42", "int 42");
        check(vivid::format_int(-42) == "-42", "int negative 42");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
