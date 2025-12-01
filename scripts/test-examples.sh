#!/bin/bash
# Test all Vivid examples
# Runs each example for a few seconds and reports any errors

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
RUNTIME="$PROJECT_ROOT/build/bin/vivid"
EXAMPLES_DIR="$PROJECT_ROOT/examples"
RUN_SECONDS=5

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Track results
declare -a PASSED
declare -a FAILED
declare -a SKIPPED

# Check runtime exists
if [ ! -f "$RUNTIME" ]; then
    echo -e "${RED}Error: Runtime not found at $RUNTIME${NC}"
    echo "Run 'make' in the project root first"
    exit 1
fi

echo "========================================"
echo "        Vivid Examples Test Suite       "
echo "========================================"
echo ""
echo "Runtime: $RUNTIME"
echo "Run time: ${RUN_SECONDS}s per example"
echo ""

# Get all examples
EXAMPLES=$(find "$EXAMPLES_DIR" -maxdepth 1 -type d | sort | tail -n +2)

for example_path in $EXAMPLES; do
    example_name=$(basename "$example_path")

    # Skip if no chain.cpp
    if [ ! -f "$example_path/chain.cpp" ]; then
        echo -e "${YELLOW}[SKIP]${NC} $example_name (no chain.cpp)"
        SKIPPED+=("$example_name")
        continue
    fi

    echo -n "Testing $example_name... "

    # Clean build directory to force recompile
    rm -rf "$example_path/build"

    # Run the example and capture output
    OUTPUT_FILE=$(mktemp)

    # Run in background, capture stderr+stdout
    "$RUNTIME" "$example_path" --windowed > "$OUTPUT_FILE" 2>&1 &
    PID=$!

    # Wait a bit for startup and initial render
    sleep $RUN_SECONDS

    # Check if process is still running (good) or died early (bad)
    if kill -0 $PID 2>/dev/null; then
        # Still running after RUN_SECONDS seconds - likely working
        # Kill it and move on
        kill $PID 2>/dev/null || true
        wait $PID 2>/dev/null || true

        # Check output for real error indicators
        # Exclude: "error" in paths, PreviewServer port binding (expected when running multiple tests)
        if grep -i "error\|failed\|exception\|segfault\|abort" "$OUTPUT_FILE" | grep -qv "error.wgsl\|error.cpp\|PreviewServer.*Failed to start\|Address already in use"; then
            echo -e "${YELLOW}[WARN]${NC} (errors in output)"
            head -20 "$OUTPUT_FILE"
            FAILED+=("$example_name")
        else
            echo -e "${GREEN}[PASS]${NC}"
            PASSED+=("$example_name")
        fi
    else
        # Process died early - check exit code
        wait $PID 2>/dev/null
        EXIT_CODE=$?

        echo -e "${RED}[FAIL]${NC} (exit code: $EXIT_CODE)"
        head -20 "$OUTPUT_FILE"
        FAILED+=("$example_name")
    fi

    rm -f "$OUTPUT_FILE"
done

# Kill any remaining vivid processes
killall vivid 2>/dev/null || true

echo ""
echo "========================================"
echo "               Results                  "
echo "========================================"
echo ""
echo -e "${GREEN}Passed:${NC}  ${#PASSED[@]}"
echo -e "${RED}Failed:${NC}  ${#FAILED[@]}"
echo -e "${YELLOW}Skipped:${NC} ${#SKIPPED[@]}"

if [ ${#FAILED[@]} -gt 0 ]; then
    echo ""
    echo "Failed examples:"
    for ex in "${FAILED[@]}"; do
        echo "  - $ex"
    done
    exit 1
fi

echo ""
echo "All tests passed!"
