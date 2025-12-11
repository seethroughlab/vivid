# Contributing to Vivid

Thank you for your interest in contributing to Vivid! This document provides guidelines and instructions for contributing.

## Getting Started

### Prerequisites

- CMake 3.20 or higher
- C++17 compatible compiler
- Platform-specific requirements:
  - **macOS**: Xcode Command Line Tools
  - **Windows**: Visual Studio 2019+ with C++ workload
  - **Linux**: GCC 9+ or Clang 10+, plus X11/Wayland development libraries

### Building from Source

```bash
# Clone the repository
git clone https://github.com/seethroughlab/vivid.git
cd vivid

# Configure and build
cmake -B build
cmake --build build

# Run an example
./build/bin/vivid examples/getting-started/hello-noise
```

### Running Tests

```bash
# Configure with tests enabled
cmake -B build -DBUILD_TESTS=ON
cmake --build build

# Run all tests
ctest --test-dir build --output-on-failure
```

## Code Style

### C++ Guidelines

- **Standard**: C++17
- **Naming**:
  - Classes: `PascalCase`
  - Methods/functions: `camelCase`
  - Member variables: `m_camelCase`
  - Constants: `UPPER_SNAKE_CASE`
- **Formatting**: 4-space indentation, braces on same line
- **Headers**: Use `#pragma once`

### Shader Guidelines (WGSL)

- Use descriptive names for uniforms and varyings
- Comment complex algorithms
- Follow WebGPU best practices

### Documentation

- All public APIs should have Doxygen comments
- Include `@brief`, `@param`, and `@return` tags
- Add `@code` examples for complex features

## Making Changes

### Workflow

1. **Fork** the repository
2. **Create a branch** for your feature or fix:
   ```bash
   git checkout -b feature/my-new-feature
   ```
3. **Make your changes** with clear, focused commits
4. **Test** your changes locally
5. **Push** to your fork and create a Pull Request

### Commit Messages

- Use present tense ("Add feature" not "Added feature")
- Keep the first line under 72 characters
- Reference issues when applicable: "Fix #123"

### Pull Request Guidelines

- Describe what your PR does and why
- Include screenshots/videos for visual changes
- Ensure CI passes before requesting review
- Keep PRs focused - one feature or fix per PR

## Project Structure

```
vivid/
├── core/           # Runtime engine
├── addons/         # Modular features
│   ├── vivid-effects-2d/
│   ├── vivid-render3d/
│   ├── vivid-video/
│   ├── vivid-audio/
│   └── vivid-io/
├── examples/       # Demo projects
├── tests/          # Test suite
└── docs/           # Documentation
```

## Adding a New Operator

1. Create header in `addons/vivid-effects-2d/include/vivid/effects/`
2. Create implementation in `addons/vivid-effects-2d/src/`
3. Add to CMakeLists.txt
4. Include in `effects.h`
5. Add example usage in `examples/`
6. Document with Doxygen comments

## Questions?

- Open an issue for bugs or feature requests
- Check existing issues before creating new ones

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
