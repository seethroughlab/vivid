#!/bin/bash
# Visual regression testing script for Vivid
# Usage: ./scripts/visual-regression-test.sh [update|test]
#   update - Generate new reference images
#   test   - Compare current output against reference images

set -e

VIVID_BIN="./build/bin/vivid"
REF_DIR="testing-fixtures/reference-images"
TMP_DIR="/tmp/vivid-visual-test"
SNAPSHOT_FRAME=30
THRESHOLD=0.01  # RMSE threshold for acceptable difference

# Examples to test
declare -A EXAMPLES=(
    ["2d-effects/chain-basics"]="examples/2d-effects/chain-basics"
    ["2d-effects/feedback"]="examples/2d-effects/feedback"
    ["2d-effects/kaleidoscope"]="examples/2d-effects/kaleidoscope"
    ["2d-effects/particles"]="examples/2d-effects/particles"
    ["2d-effects/retro-crt"]="examples/2d-effects/retro-crt"
    ["2d-effects/canvas-drawing"]="examples/2d-effects/canvas-drawing"
)

# Check dependencies
check_deps() {
    if ! command -v compare &> /dev/null; then
        echo "Error: ImageMagick 'compare' command not found."
        echo "Install with: brew install imagemagick"
        exit 1
    fi

    if [ ! -f "$VIVID_BIN" ]; then
        echo "Error: Vivid binary not found at $VIVID_BIN"
        echo "Run: cmake -B build && cmake --build build"
        exit 1
    fi
}

# Generate reference image
generate_reference() {
    local name=$1
    local example=$2
    local output="$REF_DIR/$name.png"

    echo "Generating reference: $name"
    mkdir -p "$(dirname "$output")"

    env VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json \
        VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json \
        DYLD_LIBRARY_PATH=/opt/homebrew/lib:build/lib \
        timeout 10 "$VIVID_BIN" "$example" --snapshot "$output" --snapshot-frame "$SNAPSHOT_FRAME" 2>/dev/null

    if [ -f "$output" ]; then
        echo "  Created: $output"
    else
        echo "  FAILED: Could not create $output"
        return 1
    fi
}

# Test against reference
test_reference() {
    local name=$1
    local example=$2
    local reference="$REF_DIR/$name.png"
    local test_output="$TMP_DIR/$name.png"
    local diff_output="$TMP_DIR/$name-diff.png"

    if [ ! -f "$reference" ]; then
        echo "SKIP: $name (no reference image)"
        return 0
    fi

    echo "Testing: $name"
    mkdir -p "$(dirname "$test_output")"

    # Generate test snapshot
    env VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json \
        VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json \
        DYLD_LIBRARY_PATH=/opt/homebrew/lib:build/lib \
        timeout 10 "$VIVID_BIN" "$example" --snapshot "$test_output" --snapshot-frame "$SNAPSHOT_FRAME" 2>/dev/null

    if [ ! -f "$test_output" ]; then
        echo "  FAILED: Could not generate test snapshot"
        return 1
    fi

    # Compare images
    local result
    result=$(compare -metric RMSE "$reference" "$test_output" "$diff_output" 2>&1 | grep -o '^[0-9.]*' || echo "999")

    if (( $(echo "$result < $THRESHOLD" | bc -l) )); then
        echo "  PASS (RMSE: $result)"
        return 0
    else
        echo "  FAIL (RMSE: $result > $THRESHOLD)"
        echo "  Reference: $reference"
        echo "  Test:      $test_output"
        echo "  Diff:      $diff_output"
        return 1
    fi
}

# Main
main() {
    check_deps

    local mode=${1:-test}
    local failed=0
    local passed=0
    local skipped=0

    mkdir -p "$TMP_DIR"

    case $mode in
        update)
            echo "=== Generating Reference Images ==="
            for name in "${!EXAMPLES[@]}"; do
                generate_reference "$name" "${EXAMPLES[$name]}" || ((failed++))
            done
            echo ""
            echo "Reference images updated in $REF_DIR"
            ;;
        test)
            echo "=== Visual Regression Tests ==="
            for name in "${!EXAMPLES[@]}"; do
                if [ ! -f "$REF_DIR/$name.png" ]; then
                    ((skipped++))
                    echo "SKIP: $name (no reference)"
                elif test_reference "$name" "${EXAMPLES[$name]}"; then
                    ((passed++))
                else
                    ((failed++))
                fi
            done
            echo ""
            echo "Results: $passed passed, $failed failed, $skipped skipped"
            [ $failed -eq 0 ] || exit 1
            ;;
        *)
            echo "Usage: $0 [update|test]"
            echo "  update - Generate new reference images"
            echo "  test   - Compare against reference images"
            exit 1
            ;;
    esac
}

main "$@"
