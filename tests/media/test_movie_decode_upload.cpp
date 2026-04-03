#include "operators/shared/movie_decode/texture_upload.h"
#include <cstdio>
#include "test_helpers.h"

static int g_fail = 0;

int main() {
    check(movie_aligned_bpr(320 * 4) == 1280, "aligned bpr keeps already aligned row");
    check(movie_aligned_bpr(1920 * 4) == 7680, "aligned bpr for 1080p row");
    check(movie_aligned_bpr(13) == 256, "aligned bpr rounds small rows to 256");

    uint32_t blocks_w = (1920 + 3) / 4;
    uint32_t bc1_row = blocks_w * 8;
    uint32_t bc3_row = blocks_w * 16;
    check(movie_aligned_bpr(bc1_row) % 256 == 0, "bc1 row alignment is 256 multiple");
    check(movie_aligned_bpr(bc3_row) % 256 == 0, "bc3 row alignment is 256 multiple");

    return g_fail == 0 ? 0 : 1;
}
