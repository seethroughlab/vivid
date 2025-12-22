#!/bin/bash
# Check if a new wgpu-native release includes the memory leak fix (PR #542)
# The fix commit hash: 49b7efa (jeffcrouse/wgpu-native:fix-command-encoder-leak)
# Merged into gfx-rs/wgpu-native trunk on Dec 22, 2024

set -e

CURRENT_VERSION="v27.0.2.0"
PR_NUMBER="542"
FIX_DESCRIPTION="ensure command encoder cleanup always runs"

echo "Checking for new wgpu-native release..."
echo "Current version in Vivid: $CURRENT_VERSION"
echo ""

# Get latest release from GitHub API
LATEST=$(curl -s https://api.github.com/repos/gfx-rs/wgpu-native/releases/latest)
LATEST_TAG=$(echo "$LATEST" | grep '"tag_name"' | head -1 | sed 's/.*: "\(.*\)",/\1/')
LATEST_DATE=$(echo "$LATEST" | grep '"published_at"' | head -1 | sed 's/.*: "\(.*\)",/\1/')

echo "Latest release: $LATEST_TAG (published: $LATEST_DATE)"
echo ""

if [ "$LATEST_TAG" = "$CURRENT_VERSION" ]; then
    echo "No new release yet."
    echo ""
    echo "Checking if PR #$PR_NUMBER is in trunk (but not released)..."

    # Check if the fix is in trunk by looking at recent commits
    TRUNK_COMMITS=$(curl -s "https://api.github.com/repos/gfx-rs/wgpu-native/commits?sha=trunk&per_page=50")

    if echo "$TRUNK_COMMITS" | grep -q "$FIX_DESCRIPTION"; then
        echo "YES - PR #$PR_NUMBER is merged into trunk but not yet released."
        echo ""
        echo "Watch for new release at:"
        echo "  https://github.com/gfx-rs/wgpu-native/releases"
    else
        echo "Could not confirm PR #$PR_NUMBER in recent trunk commits."
        echo "Check manually: https://github.com/gfx-rs/wgpu-native/pull/$PR_NUMBER"
    fi
else
    echo "NEW RELEASE AVAILABLE: $LATEST_TAG"
    echo ""
    echo "Checking if it includes PR #$PR_NUMBER..."

    # Compare release date to PR merge date (Dec 22, 2024)
    # The fix was merged Dec 22, 2024, so any release after that should include it
    PR_MERGE_DATE="2024-12-22"
    RELEASE_DATE=$(echo "$LATEST_DATE" | cut -d'T' -f1)

    if [[ "$RELEASE_DATE" > "$PR_MERGE_DATE" ]] || [[ "$RELEASE_DATE" = "$PR_MERGE_DATE" ]]; then
        echo "YES - Release $LATEST_TAG (published $RELEASE_DATE) should include the fix!"
        echo ""
        echo "Next steps:"
        echo "  1. Update Vivid's wgpu-native dependency to $LATEST_TAG"
        echo "  2. Rebuild: cmake -B build && cmake --build build"
        echo "  3. Verify memory is stable: ./build/bin/vivid examples/getting-started/02-hello-noise"
        echo "  4. Mark TODO item as complete in TODO.md"
    else
        echo "NO - Release $LATEST_TAG predates the fix (merged $PR_MERGE_DATE)"
        echo "Wait for a newer release."
    fi
fi

echo ""
echo "PR #$PR_NUMBER: https://github.com/gfx-rs/wgpu-native/pull/$PR_NUMBER"
