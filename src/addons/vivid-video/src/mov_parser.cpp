// Simple QuickTime MOV parser for HAP video files
// Parses just enough of the MOV structure to extract video frame offsets

#include "mov_parser.h"
#include <cstring>
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#define fseek64 fseeko
#define ftell64 ftello
#endif

namespace vivid::video {

// Read big-endian integers
static uint32_t readU32(FILE* f) {
    uint8_t buf[4];
    if (fread(buf, 1, 4, f) != 4) return 0;
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

static uint64_t readU64(FILE* f) {
    uint8_t buf[8];
    if (fread(buf, 1, 8, f) != 8) return 0;
    return ((uint64_t)buf[0] << 56) | ((uint64_t)buf[1] << 48) |
           ((uint64_t)buf[2] << 40) | ((uint64_t)buf[3] << 32) |
           ((uint64_t)buf[4] << 24) | ((uint64_t)buf[5] << 16) |
           ((uint64_t)buf[6] << 8) | buf[7];
}

static uint16_t readU16(FILE* f) {
    uint8_t buf[2];
    if (fread(buf, 1, 2, f) != 2) return 0;
    return (buf[0] << 8) | buf[1];
}

static void readFourCC(FILE* f, char* out) {
    if (fread(out, 1, 4, f) != 4) {
        out[0] = '\0';
    }
    out[4] = '\0';
}

static void skip(FILE* f, int64_t bytes) {
    fseek64(f, bytes, SEEK_CUR);
}

// Parse sample table (stbl) atom
static bool parseSTBL(FILE* f, int64_t endPos, MOVTrack& track) {
    std::vector<uint32_t> sampleSizes;
    std::vector<uint32_t> sampleToChunk;  // first_chunk, samples_per_chunk, sample_desc_id
    std::vector<uint64_t> chunkOffsets;
    std::vector<uint32_t> sampleDurations;
    uint32_t defaultSampleSize = 0;
    uint32_t sampleCount = 0;

    while (ftell64(f) < endPos) {
        int64_t atomStart = ftell64(f);
        uint32_t atomSize = readU32(f);
        char atomType[5];
        readFourCC(f, atomType);

        if (atomSize == 0) break;
        if (atomSize == 1) {
            // 64-bit size
            atomSize = (uint32_t)readU64(f);
        }

        int64_t atomEnd = atomStart + atomSize;

        if (strcmp(atomType, "stsd") == 0) {
            // Sample description - get codec FourCC and format info
            skip(f, 4);  // version + flags
            uint32_t entryCount = readU32(f);
            if (entryCount > 0) {
                int64_t entryStart = ftell64(f);
                uint32_t descSize = readU32(f);
                readFourCC(f, track.codecFourCC);
                skip(f, 6);  // reserved
                uint16_t dataRefIndex = readU16(f);
                (void)dataRefIndex;

                // For audio tracks, parse audio sample entry
                // Common PCM formats: 'sowt' (little-endian), 'twos' (big-endian), 'lpcm', 'in24', 'in32', 'fl32', 'fl64'
                if (track.isAudio) {
                    // Audio sample entry version 0 or 1
                    uint16_t audioVersion = readU16(f);
                    (void)audioVersion;
                    skip(f, 6);  // revision level + vendor
                    track.audioChannels = readU16(f);
                    track.audioBitsPerSample = readU16(f);
                    skip(f, 4);  // compression ID + packet size

                    // Sample rate is 16.16 fixed point
                    uint32_t sampleRateFixed = readU32(f);
                    track.audioSampleRate = sampleRateFixed >> 16;

                    // Calculate bytes per frame for PCM
                    if (track.audioBitsPerSample > 0 && track.audioChannels > 0) {
                        track.audioBytesPerFrame = (track.audioBitsPerSample / 8) * track.audioChannels;
                    }
                }
                // Skip to end of sample description entry
                fseek64(f, entryStart + descSize, SEEK_SET);
            }
        }
        else if (strcmp(atomType, "stts") == 0) {
            // Time-to-sample - get durations
            skip(f, 4);  // version + flags
            uint32_t entryCount = readU32(f);
            for (uint32_t i = 0; i < entryCount; i++) {
                uint32_t count = readU32(f);
                uint32_t delta = readU32(f);
                for (uint32_t j = 0; j < count; j++) {
                    sampleDurations.push_back(delta);
                }
            }
        }
        else if (strcmp(atomType, "stsz") == 0) {
            // Sample sizes
            skip(f, 4);  // version + flags
            defaultSampleSize = readU32(f);
            sampleCount = readU32(f);
            if (defaultSampleSize == 0) {
                // Variable size samples
                sampleSizes.resize(sampleCount);
                for (uint32_t i = 0; i < sampleCount; i++) {
                    sampleSizes[i] = readU32(f);
                }
            }
        }
        else if (strcmp(atomType, "stsc") == 0) {
            // Sample-to-chunk
            skip(f, 4);  // version + flags
            uint32_t entryCount = readU32(f);
            sampleToChunk.resize(entryCount * 3);
            for (uint32_t i = 0; i < entryCount; i++) {
                sampleToChunk[i * 3 + 0] = readU32(f);  // first_chunk (1-based)
                sampleToChunk[i * 3 + 1] = readU32(f);  // samples_per_chunk
                sampleToChunk[i * 3 + 2] = readU32(f);  // sample_description_index
            }
        }
        else if (strcmp(atomType, "stco") == 0) {
            // 32-bit chunk offsets
            skip(f, 4);  // version + flags
            uint32_t entryCount = readU32(f);
            chunkOffsets.resize(entryCount);
            for (uint32_t i = 0; i < entryCount; i++) {
                chunkOffsets[i] = readU32(f);
            }
        }
        else if (strcmp(atomType, "co64") == 0) {
            // 64-bit chunk offsets
            skip(f, 4);  // version + flags
            uint32_t entryCount = readU32(f);
            chunkOffsets.resize(entryCount);
            for (uint32_t i = 0; i < entryCount; i++) {
                chunkOffsets[i] = readU64(f);
            }
        }

        fseek64(f, atomEnd, SEEK_SET);
    }

    // Build sample list from chunk information
    if (sampleCount == 0 || chunkOffsets.empty()) {
        return false;
    }

    track.samples.resize(sampleCount);

    // Expand sample-to-chunk mapping
    uint32_t sampleIndex = 0;
    uint32_t stscEntries = sampleToChunk.size() / 3;

    for (size_t chunkIndex = 0; chunkIndex < chunkOffsets.size() && sampleIndex < sampleCount; chunkIndex++) {
        // Find samples_per_chunk for this chunk
        uint32_t samplesInChunk = 1;
        for (uint32_t i = 0; i < stscEntries; i++) {
            uint32_t firstChunk = sampleToChunk[i * 3 + 0] - 1;  // Convert to 0-based
            if (chunkIndex >= firstChunk) {
                samplesInChunk = sampleToChunk[i * 3 + 1];
            }
        }

        uint64_t offset = chunkOffsets[chunkIndex];

        for (uint32_t s = 0; s < samplesInChunk && sampleIndex < sampleCount; s++) {
            uint32_t size = defaultSampleSize > 0 ? defaultSampleSize : sampleSizes[sampleIndex];
            uint32_t duration = sampleIndex < sampleDurations.size() ? sampleDurations[sampleIndex] : 1;

            track.samples[sampleIndex].offset = offset;
            track.samples[sampleIndex].size = size;
            track.samples[sampleIndex].duration = duration;

            offset += size;
            sampleIndex++;
        }
    }

    return true;
}

// Parse media information (minf) atom
static bool parseMINF(FILE* f, int64_t endPos, MOVTrack& track) {
    while (ftell64(f) < endPos) {
        int64_t atomStart = ftell64(f);
        uint32_t atomSize = readU32(f);
        char atomType[5];
        readFourCC(f, atomType);

        if (atomSize == 0) break;
        if (atomSize == 1) atomSize = (uint32_t)readU64(f);

        int64_t atomEnd = atomStart + atomSize;

        if (strcmp(atomType, "stbl") == 0) {
            if (!parseSTBL(f, atomEnd, track)) {
                return false;
            }
        }

        fseek64(f, atomEnd, SEEK_SET);
    }
    return true;
}

// Parse media (mdia) atom
static bool parseMDIA(FILE* f, int64_t endPos, MOVTrack& track) {
    while (ftell64(f) < endPos) {
        int64_t atomStart = ftell64(f);
        uint32_t atomSize = readU32(f);
        char atomType[5];
        readFourCC(f, atomType);

        if (atomSize == 0) break;
        if (atomSize == 1) atomSize = (uint32_t)readU64(f);

        int64_t atomEnd = atomStart + atomSize;

        if (strcmp(atomType, "mdhd") == 0) {
            // Media header - get timescale
            uint8_t version = 0;
            if (fread(&version, 1, 1, f) != 1) version = 0;
            skip(f, 3);  // flags

            if (version == 1) {
                skip(f, 8);  // creation_time
                skip(f, 8);  // modification_time
                track.timescale = readU32(f);
                track.duration = readU64(f);
            } else {
                skip(f, 4);  // creation_time
                skip(f, 4);  // modification_time
                track.timescale = readU32(f);
                track.duration = readU32(f);
            }
        }
        else if (strcmp(atomType, "hdlr") == 0) {
            // Handler - determine track type
            skip(f, 4);  // version + flags
            skip(f, 4);  // predefined
            char handlerType[5];
            readFourCC(f, handlerType);

            if (strcmp(handlerType, "vide") == 0) {
                track.isVideo = true;
            } else if (strcmp(handlerType, "soun") == 0) {
                track.isAudio = true;
            }
        }
        else if (strcmp(atomType, "minf") == 0) {
            if (!parseMINF(f, atomEnd, track)) {
                return false;
            }
        }

        fseek64(f, atomEnd, SEEK_SET);
    }
    return true;
}

// Parse track (trak) atom
static bool parseTRAK(FILE* f, int64_t endPos, MOVTrack& track) {
    while (ftell64(f) < endPos) {
        int64_t atomStart = ftell64(f);
        uint32_t atomSize = readU32(f);
        char atomType[5];
        readFourCC(f, atomType);

        if (atomSize == 0) break;
        if (atomSize == 1) atomSize = (uint32_t)readU64(f);

        int64_t atomEnd = atomStart + atomSize;

        if (strcmp(atomType, "tkhd") == 0) {
            // Track header - get dimensions
            uint8_t version = 0;
            if (fread(&version, 1, 1, f) != 1) version = 0;
            skip(f, 3);  // flags

            if (version == 1) {
                skip(f, 8);  // creation_time
                skip(f, 8);  // modification_time
                track.trackId = readU32(f);
                skip(f, 4);  // reserved
                skip(f, 8);  // duration
            } else {
                skip(f, 4);  // creation_time
                skip(f, 4);  // modification_time
                track.trackId = readU32(f);
                skip(f, 4);  // reserved
                skip(f, 4);  // duration
            }

            skip(f, 8);   // reserved
            skip(f, 2);   // layer
            skip(f, 2);   // alternate_group
            skip(f, 2);   // volume
            skip(f, 2);   // reserved
            skip(f, 36);  // matrix

            // Width and height are 16.16 fixed point
            uint32_t widthFixed = readU32(f);
            uint32_t heightFixed = readU32(f);
            track.width = widthFixed >> 16;
            track.height = heightFixed >> 16;
        }
        else if (strcmp(atomType, "mdia") == 0) {
            if (!parseMDIA(f, atomEnd, track)) {
                return false;
            }
        }

        fseek64(f, atomEnd, SEEK_SET);
    }
    return true;
}

// Parse movie (moov) atom
static bool parseMOOV(FILE* f, int64_t endPos, MOVFile& mov) {
    while (ftell64(f) < endPos) {
        int64_t atomStart = ftell64(f);
        uint32_t atomSize = readU32(f);
        char atomType[5];
        readFourCC(f, atomType);

        if (atomSize == 0) break;
        if (atomSize == 1) atomSize = (uint32_t)readU64(f);

        int64_t atomEnd = atomStart + atomSize;

        if (strcmp(atomType, "mvhd") == 0) {
            // Movie header
            uint8_t version = 0;
            if (fread(&version, 1, 1, f) != 1) version = 0;
            skip(f, 3);  // flags

            if (version == 1) {
                skip(f, 8);  // creation_time
                skip(f, 8);  // modification_time
                mov.timescale = readU32(f);
                mov.duration = readU64(f);
            } else {
                skip(f, 4);  // creation_time
                skip(f, 4);  // modification_time
                mov.timescale = readU32(f);
                mov.duration = readU32(f);
            }
        }
        else if (strcmp(atomType, "trak") == 0) {
            MOVTrack track;
            if (parseTRAK(f, atomEnd, track)) {
                mov.tracks.push_back(std::move(track));
            }
        }

        fseek64(f, atomEnd, SEEK_SET);
    }
    return true;
}

bool parseMOV(FILE* file, int64_t fileSize, MOVFile& out) {
    fseek64(file, 0, SEEK_SET);

    while (ftell64(file) < fileSize) {
        int64_t atomStart = ftell64(file);
        uint32_t atomSize = readU32(file);
        char atomType[5];
        readFourCC(file, atomType);

        if (atomSize == 0) {
            // Atom extends to end of file
            atomSize = (uint32_t)(fileSize - atomStart);
        }
        if (atomSize == 1) {
            // 64-bit size
            atomSize = (uint32_t)readU64(file);
        }

        int64_t atomEnd = atomStart + atomSize;

        if (strcmp(atomType, "moov") == 0) {
            if (!parseMOOV(file, atomEnd, out)) {
                return false;
            }
            // Found and parsed moov, we're done
            return out.videoTrack() != nullptr;
        }

        fseek64(file, atomEnd, SEEK_SET);
    }

    // No moov atom found
    return false;
}

} // namespace vivid::video
