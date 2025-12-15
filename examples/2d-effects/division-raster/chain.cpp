// Division Raster - Vivid Port
// Original: http://paperjs.org/examples/division-raster/
// Progressive image reveal through recursive subdivision
// Each rectangle is filled with the average color of that region

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>
#include <vector>
#include <algorithm>

using namespace vivid;
using namespace vivid::effects;

struct Rect {
    float x, y, w, h;
    bool canDivide() const { return w > 4 && h > 4; }
};

// Global state
std::vector<Rect> rects;
Image* raster = nullptr;
int frameCount = 0;
int maxDivisions = 500;  // Limit total subdivisions

void divideRect(size_t index) {
    if (index >= rects.size()) return;

    Rect& r = rects[index];
    if (!r.canDivide()) return;

    bool isLandscape = r.w > r.h;

    if (isLandscape) {
        // Split horizontally
        float halfW = r.w / 2.0f;
        Rect left = {r.x, r.y, halfW, r.h};
        Rect right = {r.x + halfW, r.y, halfW, r.h};
        rects[index] = left;
        rects.push_back(right);
    } else {
        // Split vertically
        float halfH = r.h / 2.0f;
        Rect top = {r.x, r.y, r.w, halfH};
        Rect bottom = {r.x, r.y + halfH, r.w, halfH};
        rects[index] = top;
        rects.push_back(bottom);
    }
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Load source image with CPU data for pixel sampling
    auto& image = chain.add<Image>("raster");
    image.file = "assets/images/nature.jpg";
    image.keepCpuData = true;  // Enable pixel sampling
    raster = &image;

    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(1280, 720);

    chain.output("canvas");

    if (chain.hasError()) {
        ctx.setError(chain.error());
        return;
    }

    // Initialize with one rectangle covering the canvas
    rects.clear();
    rects.push_back({0, 0, 1280, 720});
    frameCount = 0;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& canvas = chain.get<Canvas>("canvas");

    // Clear canvas
    canvas.clear(0.0f, 0.0f, 0.0f, 1.0f);

    if (!raster || !raster->hasCpuData()) {
        // Image not loaded yet or CPU data not available
        canvas.fillStyle({1, 0, 0, 1});
        canvas.fillRect(0, 0, 200, 50);
        return;
    }

    // Get image dimensions for coordinate mapping
    int imgW = raster->imageWidth();
    int imgH = raster->imageHeight();

    if (imgW == 0 || imgH == 0) return;

    // Subdivide some rectangles each frame (automatic animation)
    int divisionsPerFrame = 3;
    for (int i = 0; i < divisionsPerFrame && rects.size() < maxDivisions; ++i) {
        // Pick a random rectangle to divide (prefer larger ones)
        if (!rects.empty()) {
            // Find largest rectangle that can still be divided
            size_t bestIdx = 0;
            float bestArea = 0;
            for (size_t j = 0; j < rects.size(); ++j) {
                if (rects[j].canDivide()) {
                    float area = rects[j].w * rects[j].h;
                    if (area > bestArea) {
                        bestArea = area;
                        bestIdx = j;
                    }
                }
            }
            if (bestArea > 0) {
                divideRect(bestIdx);
            }
        }
    }

    // Draw all rectangles with their average colors from the source image
    for (const auto& r : rects) {
        // Map canvas coordinates to image coordinates
        int imgX = static_cast<int>(r.x / 1280.0f * imgW);
        int imgY = static_cast<int>(r.y / 720.0f * imgH);
        int imgRectW = static_cast<int>(r.w / 1280.0f * imgW);
        int imgRectH = static_cast<int>(r.h / 720.0f * imgH);

        // Get average color for this region
        glm::vec4 color = raster->getAverageColor(imgX, imgY, imgRectW, imgRectH);

        // Draw filled rectangle
        canvas.fillStyle(color);
        canvas.fillRect(r.x, r.y, r.w, r.h);

        // Optional: draw subtle border for visibility
        if (r.w > 10 && r.h > 10) {
            canvas.strokeStyle({0, 0, 0, 0.1f});
            canvas.lineWidth(0.5f);
            canvas.strokeRect(r.x, r.y, r.w, r.h);
        }
    }

    frameCount++;
}

VIVID_CHAIN(setup, update)
