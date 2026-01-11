#!/bin/bash
# Test script for verifying module linking works (faster than install)
# Uses a local module directory instead of cloning from GitHub
# Ideal for Claude Code to run during development

set -e

VIVID_BIN="${VIVID_BIN:-./build/bin/vivid}"
MODULE_PATH="${MODULE_PATH:-$HOME/Developer/vivid-onnx}"
MODULE_NAME="vivid-onnx"

echo "=== Module Link Test ==="
echo "Using vivid binary: $VIVID_BIN"
echo "Module path: $MODULE_PATH"
echo ""

# Check vivid binary exists
if [ ! -f "$VIVID_BIN" ]; then
    echo "ERROR: Vivid binary not found at $VIVID_BIN"
    exit 1
fi

# Check module directory exists
if [ ! -d "$MODULE_PATH" ]; then
    echo "ERROR: Module directory not found at $MODULE_PATH"
    echo "Clone it with: git clone https://github.com/seethroughlab/vivid-onnx $MODULE_PATH"
    exit 1
fi

# Check module has built library
LIB_PATH="$MODULE_PATH/lib/libvivid-onnx.dylib"
if [ ! -f "$LIB_PATH" ]; then
    # Try build directory
    BUILD_LIB="$MODULE_PATH/build/libvivid-onnx.dylib"
    if [ -f "$BUILD_LIB" ]; then
        echo "Copying built library to lib/"
        mkdir -p "$MODULE_PATH/lib"
        cp "$BUILD_LIB" "$LIB_PATH"
    else
        echo "ERROR: Module library not built. Run:"
        echo "  cd $MODULE_PATH && cmake -B build -DVIVID_ROOT=$HOME/Developer/vivid && cmake --build build"
        exit 1
    fi
fi

# Step 1: Unlink if already linked
echo "Step 1: Checking existing links..."
if $VIVID_BIN modules list 2>/dev/null | grep -q "$MODULE_NAME"; then
    echo "  Module already linked, unlinking..."
    $VIVID_BIN modules unlink "$MODULE_NAME" || true
fi

# Step 2: Link module
echo ""
echo "Step 2: Linking module..."
if ! $VIVID_BIN modules link "$MODULE_PATH"; then
    echo "ERROR: Module linking failed"
    exit 1
fi

# Step 3: Verify module appears in list
echo ""
echo "Step 3: Verifying module is linked..."
if ! $VIVID_BIN modules list | grep -q "$MODULE_NAME"; then
    echo "ERROR: Module not found in list after linking"
    exit 1
fi
echo "  Module found in list"

# Step 4: Run pose-tracking example
echo ""
echo "Step 4: Testing pose-tracking example..."
EXAMPLE_PATH="$MODULE_PATH/examples/pose-tracking"
if [ -d "$EXAMPLE_PATH" ]; then
    if timeout 30 $VIVID_BIN "$EXAMPLE_PATH" --snapshot /tmp/pose-link-test.png --snapshot-frame 60 2>&1; then
        echo "  Pose tracking example ran successfully"
        [ -f "/tmp/pose-link-test.png" ] && echo "  Snapshot: /tmp/pose-link-test.png"
    else
        echo "  WARNING: Pose tracking example failed"
    fi
fi

# Step 5: Run face-detection example
echo ""
echo "Step 5: Testing face-detection example..."
EXAMPLE_PATH="$MODULE_PATH/examples/face-detection"
if [ -d "$EXAMPLE_PATH" ]; then
    if timeout 30 $VIVID_BIN "$EXAMPLE_PATH" --snapshot /tmp/face-link-test.png --snapshot-frame 60 2>&1; then
        echo "  Face detection example ran successfully"
        [ -f "/tmp/face-link-test.png" ] && echo "  Snapshot: /tmp/face-link-test.png"
    else
        echo "  WARNING: Face detection example failed"
    fi
fi

echo ""
echo "=== Module Link Test PASSED ==="
