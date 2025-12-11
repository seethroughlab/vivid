#pragma once

// Simple QuickTime MOV parser for HAP video files
// Only extracts what we need: video track sample offsets and sizes

#include <cstdint>
#include <vector>
#include <string>
#include <cstdio>

namespace vivid::video {

struct MOVSample {
    uint64_t offset;    // File offset of sample data
    uint32_t size;      // Size in bytes
    uint32_t duration;  // Duration in timescale units
};

struct MOVTrack {
    uint32_t trackId = 0;
    uint32_t timescale = 0;
    uint64_t duration = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    char codecFourCC[5] = {0};  // e.g., "Hap1", "HapY", "avc1", "sowt", "twos", "lpcm"
    bool isVideo = false;
    bool isAudio = false;
    std::vector<MOVSample> samples;

    // Audio-specific fields (only valid when isAudio is true)
    uint32_t audioSampleRate = 0;
    uint16_t audioChannels = 0;
    uint16_t audioBitsPerSample = 0;
    uint32_t audioBytesPerFrame = 0;  // For calculating sample counts
};

struct MOVFile {
    uint32_t timescale = 0;     // Movie timescale
    uint64_t duration = 0;      // Movie duration
    std::vector<MOVTrack> tracks;

    // Find first video track
    const MOVTrack* videoTrack() const {
        for (const auto& t : tracks) {
            if (t.isVideo) return &t;
        }
        return nullptr;
    }

    // Find first audio track
    const MOVTrack* audioTrack() const {
        for (const auto& t : tracks) {
            if (t.isAudio) return &t;
        }
        return nullptr;
    }
};

// Parse a MOV file and extract track information
// Returns true on success
bool parseMOV(FILE* file, int64_t fileSize, MOVFile& out);

} // namespace vivid::video
