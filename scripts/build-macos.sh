#!/bin/bash
# Build script for macOS
# Builds the Vivid runtime and VS Code extension

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "=== Building Vivid for macOS ==="
echo "Project root: $PROJECT_ROOT"

# Check prerequisites
echo ""
echo "Checking prerequisites..."
command -v cmake >/dev/null 2>&1 || { echo "Error: cmake is required but not installed."; exit 1; }
command -v clang++ >/dev/null 2>&1 || { echo "Error: clang++ is required but not installed."; exit 1; }
command -v node >/dev/null 2>&1 || { echo "Error: node is required but not installed."; exit 1; }
command -v npm >/dev/null 2>&1 || { echo "Error: npm is required but not installed."; exit 1; }

echo "All prerequisites found."

# Build runtime
echo ""
echo "=== Building Runtime ==="
cd "$PROJECT_ROOT"

BUILD_TYPE="${1:-Release}"
echo "Build type: $BUILD_TYPE"

cmake -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build build -j$(sysctl -n hw.ncpu)

echo ""
echo "Runtime built: $PROJECT_ROOT/build/bin/vivid-runtime"

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
echo "Runtime: $PROJECT_ROOT/build/bin/vivid-runtime"
echo "Extension: $PROJECT_ROOT/extension (run 'npm run package' to create .vsix)"
echo ""
echo "To test:"
echo "  1. Open extension folder in VS Code and press F5"
echo "  2. In the Extension Development Host, open an example project"
echo "  3. Run: $PROJECT_ROOT/build/bin/vivid-runtime examples/hello"
