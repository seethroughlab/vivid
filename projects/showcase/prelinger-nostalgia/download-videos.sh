#!/bin/bash
# Download public domain videos from the Prelinger Archive (archive.org)
# These films are available under Creative Commons licenses for reuse.
#
# Usage:
#   ./download-videos.sh              # Download all videos
#   ./download-videos.sh --quick      # Download just one video (fastest)
#   ./download-videos.sh [output_dir] # Specify output directory
#
# Source: https://archive.org/details/prelinger

set -e

# Parse arguments
QUICK_MODE=false
OUTPUT_DIR="assets"

for arg in "$@"; do
    case $arg in
        --quick|-q)
            QUICK_MODE=true
            ;;
        *)
            OUTPUT_DIR="$arg"
            ;;
    esac
done

mkdir -p "$OUTPUT_DIR"

# Curated selection of nostalgic educational/industrial films
# Good for ambient visuals - varied imagery, public domain
# Format: identifier:filename:description:size_mb
VIDEOS=(
    "AboutBan1935:AboutBan1935.mp4:About Bananas (1935) - Tropical industrial:69"
    "HealthYo1953:HealthYo1953.mp4:Health Your Posture (1953) - Educational:42"
    "FromtheG1954:FromtheG1954.mp4:From the Ground Up (1954) - Construction:85"
    "Sleepfor1950:Sleepfor1950.mp4:Sleep for Health (1950) - Dreamy health film:38"
    "EatforHe1954:EatforHe1954.mp4:Eat for Health (1954) - Food/nutrition:51"
    "Automoti1940:Automoti1940.mp4:Automotive Service (1940) - Machines/industry:112"
    "ParkCons1938:ParkCons1938.mp4:Park Conscious (1938) - Nature/parks:45"
    "Usingthe1947:Usingthe1947.mp4:Using the Bank (1947) - Mid-century life:35"
)

# In quick mode, just download one small video
if [ "$QUICK_MODE" = true ]; then
    VIDEOS=("Usingthe1947:Usingthe1947.mp4:Using the Bank (1947) - Mid-century life:35")
fi

BASE_URL="https://archive.org/download"

echo "========================================"
echo "Prelinger Archive Video Downloader"
echo "========================================"
echo "Output directory: $OUTPUT_DIR"
if [ "$QUICK_MODE" = true ]; then
    echo "Mode: Quick (1 video)"
else
    echo "Mode: Full (${#VIDEOS[@]} videos)"
fi
echo ""
echo "Note: archive.org servers can be slow."
echo "      Downloads may take several minutes."
echo ""

download_video() {
    local identifier="$1"
    local filename="$2"
    local description="$3"
    local size_mb="$4"
    local url="$BASE_URL/$identifier/$filename"
    local output="$OUTPUT_DIR/$filename"

    if [ -f "$output" ]; then
        echo "[SKIP] $description (already exists)"
        return 0
    fi

    echo "[DOWNLOAD] $description (~${size_mb}MB)"
    echo "  URL: $url"

    # Use curl with extended timeout (archive.org can be slow)
    # --retry 3: retry up to 3 times
    # --retry-delay 5: wait 5 seconds between retries
    # --connect-timeout 60: wait up to 60s for connection
    # --max-time 600: max 10 minutes total per file
    if curl -L -o "$output" \
        --retry 3 \
        --retry-delay 5 \
        --connect-timeout 60 \
        --max-time 600 \
        --progress-bar \
        "$url"; then
        local actual_size=$(ls -lh "$output" | awk '{print $5}')
        echo "  Saved: $output ($actual_size)"
        return 0
    else
        echo "  [ERROR] Failed to download $filename"
        rm -f "$output"  # Clean up partial download
        return 1
    fi
}

# Download each video
downloaded=0
failed=0

for entry in "${VIDEOS[@]}"; do
    IFS=':' read -r identifier filename description size_mb <<< "$entry"
    echo "----------------------------------------"
    if download_video "$identifier" "$filename" "$description" "$size_mb"; then
        ((downloaded++)) || true
    else
        ((failed++)) || true
    fi
    echo ""
done

echo "========================================"
echo "Download complete!"
echo "  Downloaded: $downloaded"
if [ $failed -gt 0 ]; then
    echo "  Failed: $failed"
fi
echo "========================================"
echo ""

# List downloaded files with sizes
if [ -d "$OUTPUT_DIR" ] && ls "$OUTPUT_DIR"/*.mp4 &>/dev/null; then
    echo "Downloaded files:"
    ls -lh "$OUTPUT_DIR"/*.mp4
    echo ""
    echo "To use in your chain.cpp:"
    first_file=$(ls "$OUTPUT_DIR"/*.mp4 | head -1)
    echo "  video.load(\"$first_file\");"
fi
