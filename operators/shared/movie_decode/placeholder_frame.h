#pragma once

#include <cstdint>
#include <vector>

struct MoviePlaceholderFrame {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> bgra;
};

MoviePlaceholderFrame make_movie_missing_placeholder();
