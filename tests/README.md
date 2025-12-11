# Vivid Test Suite

Unit tests, integration tests, and visual regression tests for the Vivid framework.

## Building Tests

Tests are disabled by default. Enable them with:

```bash
cmake -B build -DBUILD_TESTS=ON
cmake --build build
```

## Running Tests

```bash
# Run all tests
ctest --test-dir build

# Run with verbose output
ctest --test-dir build -V

# Run specific test executable
./build/tests/test_operators

# Run tests matching a pattern
./build/tests/test_operators "[noise]"
```

## Test Structure

```
tests/
├── unit/
│   ├── operators/      # Operator parameter and API tests
│   │   ├── test_noise.cpp
│   │   ├── test_blur.cpp
│   │   └── test_composite.cpp
│   ├── audio/          # Audio operator tests
│   │   ├── test_clock.cpp
│   │   └── test_sequencer.cpp
│   └── render3d/       # 3D rendering tests (TODO)
├── integration/        # Chain composition and workflow tests
│   └── test_chain.cpp
└── visual/             # Visual regression tests (TODO)
    └── reference-images/
```

## Test Categories

### Unit Tests (`unit/`)

Test individual operator behavior:
- Parameter defaults and ranges
- Fluent API chaining
- `getParam`/`setParam` correctness
- `params()` declarations

These tests don't require GPU context.

### Integration Tests (`integration/`)

Test chain composition:
- Adding and retrieving operators
- Operator connections
- Output configuration

### Visual Regression Tests (`visual/`)

**TODO**: Requires headless rendering mode.

Will test:
- Operator output against reference images
- Pixel-diff comparison with tolerance

## Adding Tests

Use Catch2 syntax:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <vivid/effects/my_operator.h>

TEST_CASE("MyOperator defaults", "[operators][myoperator]") {
    MyOperator op;
    float out[4] = {0};

    SECTION("param defaults to expected value") {
        REQUIRE(op.getParam("param", out));
        REQUIRE(out[0] == 1.0f);
    }
}
```

## Test Tags

- `[operators]` - Operator tests
- `[audio]` - Audio operator tests
- `[integration]` - Integration tests
- `[chain]` - Chain-related tests
- `[noise]`, `[blur]`, etc. - Specific operator tests
