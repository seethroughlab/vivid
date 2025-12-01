#!/bin/bash
# Build script for Linux
# Builds the Vivid runtime and VS Code extension

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "=== Building Vivid for Linux ==="
echo "Project root: $PROJECT_ROOT"

# Check prerequisites
echo ""
echo "Checking prerequisites..."
command -v cmake >/dev/null 2>&1 || { echo "Error: cmake is required. Install with: sudo apt install cmake"; exit 1; }
command -v g++ >/dev/null 2>&1 || { echo "Error: g++ is required. Install with: sudo apt install build-essential"; exit 1; }
command -v node >/dev/null 2>&1 || { echo "Error: node is required. Install from https://nodejs.org"; exit 1; }
command -v npm >/dev/null 2>&1 || { echo "Error: npm is required. Install from https://nodejs.org"; exit 1; }

# Check for required system libraries
echo "Checking system libraries..."
pkg-config --exists glfw3 2>/dev/null || echo "Warning: glfw3 not found. Install with: sudo apt install libglfw3-dev"
pkg-config --exists x11 2>/dev/null || echo "Warning: X11 not found. Install with: sudo apt install libx11-dev"

echo "All prerequisites found."

# Build runtime
echo ""
echo "=== Building Runtime ==="
cd "$PROJECT_ROOT"

BUILD_TYPE="${1:-Release}"
echo "Build type: $BUILD_TYPE"

cmake -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build build -j$(nproc)

echo ""
echo "Runtime built: $PROJECT_ROOT/build/bin/vivid"

# Build VS Code extension
echo ""
echo "=== Building VS Code Extension ==="
cd "$PROJECT_ROOT/extension"

npm install
npm run compile

# Build native module
echo ""
echo "=== Building Native Module ==="
cd "$PROJECT_ROOT/extension/native"

npm install
npm run build

echo ""
echo "=== Build Complete ==="
echo ""
echo "Runtime: $PROJECT_ROOT/build/bin/vivid"
echo "Extension: $PROJECT_ROOT/extension (run 'npm run package' to create .vsix)"
echo ""
echo "To test:"
echo "  1. Open extension folder in VS Code and press F5"
echo "  2. In the Extension Development Host, open an example project"
echo "  3. Run: $PROJECT_ROOT/build/bin/vivid examples/hello"
