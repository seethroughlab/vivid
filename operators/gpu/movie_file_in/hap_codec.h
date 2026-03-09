#pragma once

#include "video_decoder.h"
#include <cstdint>
#include <cstddef>

inline bool vivid_is_hap_fourcc(uint32_t codec) {
    return codec == 'Hap1' || codec == 'Hap5' || codec == 'HapY' ||
           codec == 'HapM' || codec == 'HapA';
}

inline bool vivid_is_notchlc_fourcc(uint32_t codec) {
    return codec == 'nclc' || codec == 'NCLC';
}

inline VideoCompressedFormat vivid_hap_to_compressed_format(unsigned int hap_fmt) {
    // Values mirror HapTextureFormat constants in deps/hap/hap.h
    constexpr unsigned int kHapRGBDXT1 = 0x83F0;
    constexpr unsigned int kHapRGBADXT5 = 0x83F3;
    constexpr unsigned int kHapYCoCgDXT5 = 0x01;
    constexpr unsigned int kHapARGTC1 = 0x8DBB;
    switch (hap_fmt) {
        case kHapRGBDXT1:
            return VideoCompressedFormat::BC1;
        case kHapRGBADXT5:
        case kHapYCoCgDXT5:
            return VideoCompressedFormat::BC3;
        case kHapARGTC1:
            return VideoCompressedFormat::BC4;
        default:
            return VideoCompressedFormat::None;
    }
}

inline size_t vivid_compressed_bytes_per_block(VideoCompressedFormat fmt) {
    switch (fmt) {
        case VideoCompressedFormat::BC1:
        case VideoCompressedFormat::BC4:
            return 8;
        case VideoCompressedFormat::BC3:
            return 16;
        default:
            return 0;
    }
}
