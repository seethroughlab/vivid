#pragma once

#include <cstdint>
#include <cstring>

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

#include <tmmintrin.h>  // SSSE3 for _mm_shuffle_epi8

namespace vivid {
namespace video {

// Convert BGRA to RGBA using SSSE3 intrinsics (4 pixels at a time)
// Used by Media Foundation decoder with RGB32 output
inline void convertBGRAtoRGBA_SIMD(const uint8_t* src, uint8_t* dst, int pixelCount) {
    // Shuffle mask: swap R and B channels (BGRA -> RGBA)
    // Input:  B0 G0 R0 A0  B1 G1 R1 A1  B2 G2 R2 A2  B3 G3 R3 A3
    // Output: R0 G0 B0 A0  R1 G1 B1 A1  R2 G2 B2 A2  R3 G3 B3 A3
    __m128i shuffle = _mm_setr_epi8(2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15);

    int i = 0;
    // Process 4 pixels (16 bytes) at a time
    for (; i + 4 <= pixelCount; i += 4) {
        __m128i px = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i * 4));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i * 4), _mm_shuffle_epi8(px, shuffle));
    }

    // Handle remainder (0-3 pixels)
    for (; i < pixelCount; i++) {
        const uint8_t* s = src + i * 4;
        uint8_t* d = dst + i * 4;
        d[0] = s[2];  // R <- B
        d[1] = s[1];  // G
        d[2] = s[0];  // B <- R
        d[3] = s[3];  // A
    }
}

// Convert ARGB to RGBA using SSSE3 intrinsics (4 pixels at a time)
// Used by Media Foundation decoder with some codecs
inline void convertARGBtoRGBA_SIMD(const uint8_t* src, uint8_t* dst, int pixelCount) {
    // Shuffle mask: rotate ARGB -> RGBA
    // Input:  A0 R0 G0 B0  A1 R1 G1 B1  A2 R2 G2 B2  A3 R3 G3 B3
    // Output: R0 G0 B0 A0  R1 G1 B1 A1  R2 G2 B2 A2  R3 G3 B3 A3
    __m128i shuffle = _mm_setr_epi8(1, 2, 3, 0, 5, 6, 7, 4, 9, 10, 11, 8, 13, 14, 15, 12);

    int i = 0;
    for (; i + 4 <= pixelCount; i += 4) {
        __m128i px = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i * 4));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i * 4), _mm_shuffle_epi8(px, shuffle));
    }

    for (; i < pixelCount; i++) {
        const uint8_t* s = src + i * 4;
        uint8_t* d = dst + i * 4;
        d[0] = s[1];  // R
        d[1] = s[2];  // G
        d[2] = s[3];  // B
        d[3] = s[0];  // A
    }
}

// Convert BGR24 to RGBA using SSE2 (processes 16 source bytes -> ~5 pixels)
// Used by DirectShow decoder
// Note: BGR24 is trickier because 3-byte pixels don't align to 16-byte boundaries
inline void convertBGR24toRGBA_SIMD(const uint8_t* src, uint8_t* dst, int pixelCount) {
    int i = 0;

    // Process 4 pixels at a time using scalar loads with SIMD stores
    // BGR24 alignment makes pure SIMD tricky, so we use a hybrid approach
    for (; i + 4 <= pixelCount; i += 4) {
        // Load 4 BGR pixels (12 bytes) and convert to RGBA (16 bytes)
        alignas(16) uint8_t rgba[16];

        const uint8_t* s = src + i * 3;
        rgba[0]  = s[2];   rgba[1]  = s[1];   rgba[2]  = s[0];   rgba[3]  = 255;
        rgba[4]  = s[5];   rgba[5]  = s[4];   rgba[6]  = s[3];   rgba[7]  = 255;
        rgba[8]  = s[8];   rgba[9]  = s[7];   rgba[10] = s[6];   rgba[11] = 255;
        rgba[12] = s[11];  rgba[13] = s[10];  rgba[14] = s[9];   rgba[15] = 255;

        // Store aligned 16 bytes
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i * 4),
                         _mm_load_si128(reinterpret_cast<const __m128i*>(rgba)));
    }

    // Handle remainder
    for (; i < pixelCount; i++) {
        const uint8_t* s = src + i * 3;
        uint8_t* d = dst + i * 4;
        d[0] = s[2];  // R <- B
        d[1] = s[1];  // G
        d[2] = s[0];  // B <- R
        d[3] = 255;   // A
    }
}

// Convert RGB24 to RGBA (simple, no channel swap needed)
inline void convertRGB24toRGBA_SIMD(const uint8_t* src, uint8_t* dst, int pixelCount) {
    int i = 0;

    for (; i + 4 <= pixelCount; i += 4) {
        alignas(16) uint8_t rgba[16];

        const uint8_t* s = src + i * 3;
        rgba[0]  = s[0];   rgba[1]  = s[1];   rgba[2]  = s[2];   rgba[3]  = 255;
        rgba[4]  = s[3];   rgba[5]  = s[4];   rgba[6]  = s[5];   rgba[7]  = 255;
        rgba[8]  = s[6];   rgba[9]  = s[7];   rgba[10] = s[8];   rgba[11] = 255;
        rgba[12] = s[9];   rgba[13] = s[10];  rgba[14] = s[11];  rgba[15] = 255;

        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i * 4),
                         _mm_load_si128(reinterpret_cast<const __m128i*>(rgba)));
    }

    for (; i < pixelCount; i++) {
        const uint8_t* s = src + i * 3;
        uint8_t* d = dst + i * 4;
        d[0] = s[0];  // R
        d[1] = s[1];  // G
        d[2] = s[2];  // B
        d[3] = 255;   // A
    }
}

// Convert a full row with stride handling (handles bottom-up bitmaps)
inline void convertRowBGRAtoRGBA(const uint8_t* srcRow, uint8_t* dstRow, int width) {
    convertBGRAtoRGBA_SIMD(srcRow, dstRow, width);
}

inline void convertRowARGBtoRGBA(const uint8_t* srcRow, uint8_t* dstRow, int width) {
    convertARGBtoRGBA_SIMD(srcRow, dstRow, width);
}

inline void convertRowBGR24toRGBA(const uint8_t* srcRow, uint8_t* dstRow, int width) {
    convertBGR24toRGBA_SIMD(srcRow, dstRow, width);
}

inline void convertRowRGB24toRGBA(const uint8_t* srcRow, uint8_t* dstRow, int width) {
    convertRGB24toRGBA_SIMD(srcRow, dstRow, width);
}

// Convert NV12 (Y plane + interleaved UV plane) to RGBA using BT.709 coefficients
// yPlane: Full resolution Y plane
// uvPlane: Half resolution UV plane (interleaved U, V)
// dst: Output RGBA buffer
// width, height: Output dimensions
inline void convertNV12toRGBA(const uint8_t* yPlane, int yStride,
                               const uint8_t* uvPlane, int uvStride,
                               uint8_t* dst, int width, int height) {
    for (int y = 0; y < height; y++) {
        const uint8_t* yRow = yPlane + y * yStride;
        const uint8_t* uvRow = uvPlane + (y / 2) * uvStride;
        uint8_t* dstRow = dst + y * width * 4;

        for (int x = 0; x < width; x++) {
            // Get Y value
            int Y = yRow[x];

            // Get UV values (sampled at half resolution)
            int uvIndex = (x / 2) * 2;
            int U = uvRow[uvIndex] - 128;
            int V = uvRow[uvIndex + 1] - 128;

            // BT.709 conversion (fixed-point arithmetic for speed)
            // R = Y + 1.5748 * V
            // G = Y - 0.1873 * U - 0.4681 * V
            // B = Y + 1.8556 * U
            int R = Y + ((V * 403) >> 8);               // 1.5748 * 256 = 403
            int G = Y - ((U * 48) >> 8) - ((V * 120) >> 8);  // 0.1873 * 256 = 48, 0.4681 * 256 = 120
            int B = Y + ((U * 475) >> 8);               // 1.8556 * 256 = 475

            // Clamp to [0, 255]
            dstRow[x * 4 + 0] = static_cast<uint8_t>(R < 0 ? 0 : (R > 255 ? 255 : R));
            dstRow[x * 4 + 1] = static_cast<uint8_t>(G < 0 ? 0 : (G > 255 ? 255 : G));
            dstRow[x * 4 + 2] = static_cast<uint8_t>(B < 0 ? 0 : (B > 255 ? 255 : B));
            dstRow[x * 4 + 3] = 255;
        }
    }
}

// SIMD-optimized NV12 to RGBA conversion (processes 8 pixels at a time)
inline void convertNV12toRGBA_SIMD(const uint8_t* yPlane, int yStride,
                                    const uint8_t* uvPlane, int uvStride,
                                    uint8_t* dst, int width, int height) {
    // For now, fall back to scalar implementation
    // TODO: Implement SSE2/AVX2 optimized version
    convertNV12toRGBA(yPlane, yStride, uvPlane, uvStride, dst, width, height);
}

}  // namespace video
}  // namespace vivid
