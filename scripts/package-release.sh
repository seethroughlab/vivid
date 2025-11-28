#!/bin/bash
# Package Vivid for release distribution
#
# Creates a distributable package containing:
# - Runtime executable
# - Headers for operator development
# - Shaders
# - VS Code extension
# - Documentation
# - Example projects
#
# Usage: ./scripts/package-release.sh [version]
#        ./scripts/package-release.sh 0.1.0

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

VERSION="${1:-0.1.0}"
PLATFORM="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"

# Normalize platform names
case "$PLATFORM" in
    darwin) PLATFORM="macos" ;;
    linux) PLATFORM="linux" ;;
    mingw*|msys*|cygwin*) PLATFORM="windows" ;;
esac

RELEASE_NAME="vivid-${VERSION}-${PLATFORM}-${ARCH}"
RELEASE_DIR="${PROJECT_ROOT}/release/${RELEASE_NAME}"

echo "=== Packaging Vivid ${VERSION} for ${PLATFORM}-${ARCH} ==="

# Clean previous release
rm -rf "${PROJECT_ROOT}/release/${RELEASE_NAME}"*
mkdir -p "${RELEASE_DIR}"

# Build runtime in Release mode
echo ""
echo "=== Building Runtime ==="
cd "$PROJECT_ROOT"
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)

# Copy runtime executable
echo ""
echo "=== Copying Runtime ==="
mkdir -p "${RELEASE_DIR}/bin"
if [ "$PLATFORM" = "windows" ]; then
    cp "${PROJECT_ROOT}/build/bin/Release/vivid-runtime.exe" "${RELEASE_DIR}/bin/"
else
    cp "${PROJECT_ROOT}/build/bin/vivid-runtime" "${RELEASE_DIR}/bin/"
fi

# Copy headers
echo "=== Copying Headers ==="
mkdir -p "${RELEASE_DIR}/include"
cp -r "${PROJECT_ROOT}/build/include/vivid" "${RELEASE_DIR}/include/"

# Copy shaders
echo "=== Copying Shaders ==="
mkdir -p "${RELEASE_DIR}/shaders"
cp "${PROJECT_ROOT}/shaders/"*.wgsl "${RELEASE_DIR}/shaders/"

# Build and copy VS Code extension
echo ""
echo "=== Building VS Code Extension ==="
cd "${PROJECT_ROOT}/extension"
npm install --silent
npm run compile --silent

# Build native module if not already built
if [ ! -f "${PROJECT_ROOT}/extension/native/build/Release/shared_preview.node" ]; then
    echo "Building native module..."
    cd "${PROJECT_ROOT}/extension/native"
    npm install --silent
    npm run build --silent
fi

# Package extension
cd "${PROJECT_ROOT}/extension"
npm run package --silent 2>/dev/null || true

mkdir -p "${RELEASE_DIR}/extension"
cp "${PROJECT_ROOT}/extension/"*.vsix "${RELEASE_DIR}/extension/" 2>/dev/null || echo "Warning: No .vsix found"

# Copy documentation
echo "=== Copying Documentation ==="
mkdir -p "${RELEASE_DIR}/docs"
cp "${PROJECT_ROOT}/README.md" "${RELEASE_DIR}/"
cp "${PROJECT_ROOT}/docs/"*.md "${RELEASE_DIR}/docs/"

# Copy examples
echo "=== Copying Examples ==="
mkdir -p "${RELEASE_DIR}/examples"
for example in hello feedback gradient-blend lfo-modulation shapes; do
    if [ -d "${PROJECT_ROOT}/examples/${example}" ]; then
        mkdir -p "${RELEASE_DIR}/examples/${example}"
        cp "${PROJECT_ROOT}/examples/${example}/CMakeLists.txt" "${RELEASE_DIR}/examples/${example}/"
        cp "${PROJECT_ROOT}/examples/${example}/chain.cpp" "${RELEASE_DIR}/examples/${example}/"
        # Copy shaders if present
        if [ -d "${PROJECT_ROOT}/examples/${example}/shaders" ]; then
            cp -r "${PROJECT_ROOT}/examples/${example}/shaders" "${RELEASE_DIR}/examples/${example}/"
        fi
    fi
done

# Create a quick start script
echo "=== Creating Quick Start Script ==="
if [ "$PLATFORM" = "windows" ]; then
    cat > "${RELEASE_DIR}/run-example.bat" << 'EOF'
@echo off
bin\vivid-runtime.exe examples\hello
EOF
else
    cat > "${RELEASE_DIR}/run-example.sh" << 'EOF'
#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
"${SCRIPT_DIR}/bin/vivid-runtime" "${SCRIPT_DIR}/examples/hello"
EOF
    chmod +x "${RELEASE_DIR}/run-example.sh"
fi

# Create archive
echo ""
echo "=== Creating Archive ==="
cd "${PROJECT_ROOT}/release"

if [ "$PLATFORM" = "windows" ]; then
    zip -r "${RELEASE_NAME}.zip" "${RELEASE_NAME}"
    ARCHIVE="${RELEASE_NAME}.zip"
else
    tar -czf "${RELEASE_NAME}.tar.gz" "${RELEASE_NAME}"
    ARCHIVE="${RELEASE_NAME}.tar.gz"
fi

# Calculate checksums
echo ""
echo "=== Checksums ==="
if command -v shasum &> /dev/null; then
    shasum -a 256 "${ARCHIVE}" | tee "${RELEASE_NAME}.sha256"
elif command -v sha256sum &> /dev/null; then
    sha256sum "${ARCHIVE}" | tee "${RELEASE_NAME}.sha256"
fi

echo ""
echo "=== Release Package Complete ==="
echo ""
echo "Package: ${PROJECT_ROOT}/release/${ARCHIVE}"
echo "Size: $(du -h "${ARCHIVE}" | cut -f1)"
echo ""
echo "Contents:"
if [ "$PLATFORM" = "windows" ]; then
    unzip -l "${ARCHIVE}" | tail -20
else
    tar -tzf "${ARCHIVE}" | head -30
fi
echo ""
echo "To test the release:"
echo "  cd ${RELEASE_DIR}"
echo "  ./run-example.sh"
