#include "operators/gpu/movie_file_in/hap_codec.h"
#include <cstdio>

static int g_failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        g_failures++;
    } else {
        std::fprintf(stderr, "PASS: %s\n", msg);
    }
}

int main() {
    check(vivid_is_hap_fourcc('Hap1'), "Hap1 is recognized");
    check(vivid_is_hap_fourcc('Hap5'), "Hap5 is recognized");
    check(vivid_is_hap_fourcc('HapY'), "HapY is recognized");
    check(vivid_is_hap_fourcc('HapM'), "HapM is recognized");
    check(vivid_is_hap_fourcc('HapA'), "HapA is recognized");
    check(!vivid_is_hap_fourcc('avc1'), "avc1 is not HAP");
    check(vivid_is_notchlc_fourcc('nclc'), "nclc is recognized as NotchLC");
    check(vivid_is_notchlc_fourcc('NCLC'), "NCLC is recognized as NotchLC");
    check(!vivid_is_notchlc_fourcc('Hap1'), "Hap1 is not NotchLC");

    check(vivid_hap_to_compressed_format(0x83F0) == VideoCompressedFormat::BC1,
          "RGB_DXT1 maps to BC1");
    check(vivid_hap_to_compressed_format(0x83F3) == VideoCompressedFormat::BC3,
          "RGBA_DXT5 maps to BC3");
    check(vivid_hap_to_compressed_format(0x01) == VideoCompressedFormat::BC3,
          "YCoCg_DXT5 maps to BC3");
    check(vivid_hap_to_compressed_format(0x8DBB) == VideoCompressedFormat::BC4,
          "A_RGTC1 maps to BC4");
    check(vivid_hap_to_compressed_format(0xDEADBEEF) == VideoCompressedFormat::None,
          "Unknown HAP format maps to none");

    check(vivid_compressed_bytes_per_block(VideoCompressedFormat::BC1) == 8,
          "BC1 bytes-per-block");
    check(vivid_compressed_bytes_per_block(VideoCompressedFormat::BC3) == 16,
          "BC3 bytes-per-block");
    check(vivid_compressed_bytes_per_block(VideoCompressedFormat::BC4) == 8,
          "BC4 bytes-per-block");

    return g_failures == 0 ? 0 : 1;
}
