#include "cli/image_analysis_tools.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "stb_image.h"   // deps/stb (in the include path); the operators dylib has its own copy

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace vivid {
namespace {

inline double luma(const uint8_t* p) { return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]; }

// 8x8 average-hash: box-downsample luma to 8x8, threshold each cell by the 8x8 mean → 64 bits.
std::string average_hash(const uint8_t* rgba, uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return std::string(16, '0');
    double cell[64] = {0};
    for (int cy = 0; cy < 8; ++cy) for (int cx = 0; cx < 8; ++cx) {
        const uint32_t x0 = static_cast<uint32_t>(static_cast<uint64_t>(cx) * w / 8);
        const uint32_t x1 = std::max(x0 + 1, static_cast<uint32_t>(static_cast<uint64_t>(cx + 1) * w / 8));
        const uint32_t y0 = static_cast<uint32_t>(static_cast<uint64_t>(cy) * h / 8);
        const uint32_t y1 = std::max(y0 + 1, static_cast<uint32_t>(static_cast<uint64_t>(cy + 1) * h / 8));
        double s = 0.0; uint64_t n = 0;
        for (uint32_t y = y0; y < y1 && y < h; ++y) for (uint32_t x = x0; x < x1 && x < w; ++x) {
            s += luma(rgba + (static_cast<size_t>(y) * w + x) * 4); ++n;
        }
        cell[cy * 8 + cx] = n ? s / n : 0.0;
    }
    double mean = 0.0; for (double v : cell) mean += v; mean /= 64.0;
    uint64_t bits = 0;
    for (int i = 0; i < 64; ++i) if (cell[i] >= mean) bits |= (1ull << i);
    char out[17]; std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(bits));
    return std::string(out);
}

void put_u32(std::vector<uint8_t>& v, uint32_t x) { v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x); }
void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    put_u32(out, static_cast<uint32_t>(data.size()));
    const size_t typePos = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    const uLong crc = crc32(crc32(0L, Z_NULL, 0), out.data() + typePos, static_cast<uInt>(4 + data.size()));
    put_u32(out, static_cast<uint32_t>(crc));
}

}  // namespace

json analyze_rgba(const uint8_t* rgba, uint32_t w, uint32_t h) {
    if (!rgba || w == 0 || h == 0) return { {"ok", false}, {"error", "empty frame"} };
    const uint64_t n = static_cast<uint64_t>(w) * h;
    double sum = 0.0, sum2 = 0.0, activity = 0.0;
    std::array<uint32_t, 512> hist{};   // 3 bits/channel dominant-color histogram
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t* p = rgba + (static_cast<size_t>(y) * w + x) * 4;
            const double l = luma(p);
            sum += l; sum2 += l * l;
            hist[((p[0] >> 5) << 6) | ((p[1] >> 5) << 3) | (p[2] >> 5)]++;
            if (x > 0) activity += std::fabs(l - luma(p - 4));
            if (y > 0) activity += std::fabs(l - luma(p - static_cast<size_t>(w) * 4));
        }
    }
    const double mean = sum / n;
    const double var = std::max(0.0, sum2 / n - mean * mean);
    const double stdev = std::sqrt(var);
    const double brightness = mean / 255.0;
    const double contrast = stdev / 255.0;
    const double activity_norm = activity / (2.0 * n * 255.0);   // ~mean neighbour luma diff, 0..1

    // Dominant colors: top-3 occupied histogram buckets, mapped back to a representative RGB.
    std::array<int, 512> idx{}; for (int i = 0; i < 512; ++i) idx[i] = i;
    std::partial_sort(idx.begin(), idx.begin() + std::min<size_t>(3, idx.size()), idx.end(),
                      [&](int a, int b) { return hist[a] > hist[b]; });
    json dom = json::array();
    int occupied = 0; for (uint32_t c : hist) if (c) ++occupied;
    for (int k = 0; k < 3; ++k) {
        const int b = idx[k]; if (!hist[b]) break;
        const int r = ((b >> 6) & 7), g = ((b >> 3) & 7), bl = (b & 7);
        dom.push_back({ {"rgb", { r * 255 / 7, g * 255 / 7, bl * 255 / 7 }},   // 3-bit bucket -> 0..255
                        {"fraction", static_cast<double>(hist[b]) / n} });
    }

    // Blank: near-uniform (very low contrast) or near-black. Distinct from "no output" (that returns
    // no frame at all upstream) — this is "rendered, but nothing to see".
    const bool is_blank = (contrast < 0.01) || (brightness < 0.004);
    std::string reason;
    if (is_blank) reason = (brightness < 0.004) ? "near-black" : "near-uniform (flat color)";

    return {
        {"ok", true}, {"width", w}, {"height", h},
        {"is_blank", is_blank}, {"blank_reason", reason},
        {"brightness", brightness}, {"contrast", contrast}, {"activity", activity_norm},
        {"color_spread", static_cast<double>(occupied) / 512.0},
        {"dominant_colors", dom},
        {"hash", average_hash(rgba, w, h)}
    };
}

int hash_hamming(const std::string& a, const std::string& b) {
    if (a.size() != 16 || b.size() != 16) return 64;
    uint64_t ha = 0, hb = 0;
    if (std::sscanf(a.c_str(), "%016llx", reinterpret_cast<unsigned long long*>(&ha)) != 1) return 64;
    if (std::sscanf(b.c_str(), "%016llx", reinterpret_cast<unsigned long long*>(&hb)) != 1) return 64;
    uint64_t x = ha ^ hb; int d = 0; while (x) { d += static_cast<int>(x & 1); x >>= 1; }
    return d;
}

bool write_png(const std::string& path, const uint8_t* rgba, uint32_t w, uint32_t h) {
    if (!rgba || w == 0 || h == 0) return false;
    // Raw = per-scanline [filter byte 0][RGBA row]. Then zlib-deflate into IDAT.
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(h) * (1 + static_cast<size_t>(w) * 4));
    for (uint32_t y = 0; y < h; ++y) {
        raw.push_back(0);   // filter: none
        const uint8_t* row = rgba + static_cast<size_t>(y) * w * 4;
        raw.insert(raw.end(), row, row + static_cast<size_t>(w) * 4);
    }
    uLong bound = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> comp(bound);
    if (compress2(comp.data(), &bound, raw.data(), static_cast<uLong>(raw.size()), Z_BEST_SPEED) != Z_OK) return false;
    comp.resize(bound);

    std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    std::vector<uint8_t> ihdr;
    put_u32(ihdr, w); put_u32(ihdr, h);
    ihdr.push_back(8); ihdr.push_back(6); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);   // 8-bit RGBA
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", comp);
    chunk(png, "IEND", {});

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    std::fclose(f);
    return ok;
}

bool load_image(const std::string& path, std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h) {
    int iw = 0, ih = 0, ch = 0;
    unsigned char* d = stbi_load(path.c_str(), &iw, &ih, &ch, 4);   // force RGBA
    if (!d) return false;
    rgba.assign(d, d + static_cast<size_t>(iw) * ih * 4);
    stbi_image_free(d);
    w = static_cast<uint32_t>(iw); h = static_cast<uint32_t>(ih);
    return true;
}

}  // namespace vivid
