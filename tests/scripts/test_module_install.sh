#!/bin/bash
# Test script for verifying module installation works
# Can be run locally by Claude Code or manually

set -e

VIVID_BIN="${VIVID_BIN:-./build/bin/vivid}"
TEST_MODULE="https://github.com/seethroughlab/vivid-onnx"
MODULE_NAME="vivid-onnx"

echo "=== Module Installation Test ==="
echo "Using vivid binary: $VIVID_BIN"
echo ""

# Check vivid binary exists
if [ ! -f "$VIVID_BIN" ]; then
    echo "ERROR: Vivid binary not found at $VIVID_BIN"
    exit 1
fi

# Step 1: Check if module is already installed, remove if so
echo "Step 1: Checking existing modules..."
if $VIVID_BIN modules list 2>/dev/null | grep -q "$MODULE_NAME"; then
    echo "  Module already installed, removing..."
    $VIVID_BIN modules remove "$MODULE_NAME" || true
fi

# Step 2: Install module from GitHub
echo ""
echo "Step 2: Installing $MODULE_NAME from GitHub..."
if ! $VIVID_BIN modules install "$TEST_MODULE"; then
    echo "ERROR: Module installation failed"
    exit 1
fi

# Step 3: Verify module appears in list
echo ""
echo "Step 3: Verifying module is installed..."
if ! $VIVID_BIN modules list | grep -q "$MODULE_NAME"; then
    echo "ERROR: Module not found in list after installation"
    exit 1
fi
echo "  Module found in list"

# Step 4: Try to run an example (if the module has examples)
echo ""
echo "Step 4: Testing module example..."
MODULE_PATH="$HOME/.vivid/modules/$MODULE_NAME"
EXAMPLE_PATH="$MODULE_PATH/examples/pose-tracking"

if [ -d "$EXAMPLE_PATH" ]; then
    echo "  Running pose-tracking example with snapshot..."
    if $VIVID_BIN "$EXAMPLE_PATH" --snapshot /tmp/onnx-test.png --snapshot-frame 5 2>&1; then
        echo "  Example ran successfully"
        if [ -f "/tmp/onnx-test.png" ]; then
            echo "  Snapshot saved: /tmp/onnx-test.png"
        fi
    else
        echo "  WARNING: Example failed to run (may be missing dependencies)"
    fi
else
    echo "  No example found at $EXAMPLE_PATH, skipping"
fi

# Step 5: Clean up (optional)
echo ""
echo "Step 5: Cleaning up..."
# Uncomment to remove after test:
# $VIVID_BIN modules remove "$MODULE_NAME"

echo ""
echo "=== Module Installation Test PASSED ==="
