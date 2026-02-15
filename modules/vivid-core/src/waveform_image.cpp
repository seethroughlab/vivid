#include <vivid/waveform_image.h>
#include "stb_image_write.h"

#include <vector>
#include <cmath>
#include <algorithm>

namespace vivid {

bool renderWaveformPNG(const std::string& outputPath,
                       const float* samples,
                       uint32_t frameCount,
                       uint32_t channels,
                       int width,
                       int height) {
    if (!samples || frameCount == 0 || width <= 0 || height <= 0) {
        return false;
    }

    // RGBA image buffer (dark background)
    std::vector<uint8_t> pixels(width * height * 4, 0);

    // Fill with dark background (20, 20, 30, 255)
    for (int i = 0; i < width * height; i++) {
        pixels[i * 4 + 0] = 20;
        pixels[i * 4 + 1] = 20;
        pixels[i * 4 + 2] = 30;
        pixels[i * 4 + 3] = 255;
    }

    // Draw center line (dim gray)
    int centerY = height / 2;
    for (int x = 0; x < width; x++) {
        int idx = (centerY * width + x) * 4;
        pixels[idx + 0] = 60;
        pixels[idx + 1] = 60;
        pixels[idx + 2] = 70;
    }

    // For each pixel column, compute min/max amplitude from the corresponding sample range
    auto drawChannel = [&](int channel, uint8_t r, uint8_t g, uint8_t b) {
        for (int x = 0; x < width; x++) {
            // Map pixel column to sample range
            uint32_t sampleStart = static_cast<uint32_t>(
                static_cast<float>(x) / width * frameCount);
            uint32_t sampleEnd = static_cast<uint32_t>(
                static_cast<float>(x + 1) / width * frameCount);
            sampleEnd = std::min(sampleEnd, frameCount);

            if (sampleStart >= sampleEnd) continue;

            float minVal = 0.0f, maxVal = 0.0f;
            for (uint32_t s = sampleStart; s < sampleEnd; s++) {
                float v = samples[s * channels + channel];
                minVal = std::min(minVal, v);
                maxVal = std::max(maxVal, v);
            }

            // Map amplitude [-1, 1] to pixel Y [height-1, 0]
            int yMin = centerY - static_cast<int>(maxVal * (height / 2 - 2));
            int yMax = centerY - static_cast<int>(minVal * (height / 2 - 2));
            yMin = std::max(0, std::min(height - 1, yMin));
            yMax = std::max(0, std::min(height - 1, yMax));

            // Draw vertical line for this column
            for (int y = yMin; y <= yMax; y++) {
                int idx = (y * width + x) * 4;
                // Alpha blend for overlapping stereo channels
                pixels[idx + 0] = std::min(255, static_cast<int>(pixels[idx + 0]) + r / 2);
                pixels[idx + 1] = std::min(255, static_cast<int>(pixels[idx + 1]) + g / 2);
                pixels[idx + 2] = std::min(255, static_cast<int>(pixels[idx + 2]) + b / 2);
            }
        }
    };

    if (channels == 1) {
        drawChannel(0, 0, 220, 220);  // Cyan for mono
    } else if (channels >= 2) {
        drawChannel(0, 0, 200, 220);  // Cyan for left
        drawChannel(1, 220, 80, 200); // Magenta for right
    }

    return stbi_write_png(outputPath.c_str(), width, height, 4, pixels.data(), width * 4) != 0;
}

} // namespace vivid
