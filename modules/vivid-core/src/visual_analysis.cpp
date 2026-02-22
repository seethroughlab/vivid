// Vivid - Visual Analysis Tools
// Color harmony, symmetry, and spatial balance analysis
// Algorithms extracted from MCP tool handlers for shared use

#include <vivid/visual_analysis.h>
#include <cmath>
#include <algorithm>
#include <array>
#include <vector>
#include <limits>
#include <sstream>

namespace vivid {

namespace {

// sRGB linearization for color space conversion
float srgbToLinear(float c) {
    return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// RGB (0-1, linear) to CIE Lab color space via XYZ (D65 illuminant)
void rgbToLab(float r, float g, float b, float& L, float& a, float& labB) {
    float lr = srgbToLinear(r);
    float lg = srgbToLinear(g);
    float lb = srgbToLinear(b);

    // RGB to XYZ (sRGB D65)
    float x = 0.4124564f * lr + 0.3575761f * lg + 0.1804375f * lb;
    float y = 0.2126729f * lr + 0.7151522f * lg + 0.0721750f * lb;
    float z = 0.0193339f * lr + 0.1191920f * lg + 0.9503041f * lb;

    // Normalize to D65 white point
    x /= 0.95047f;
    z /= 1.08883f;

    // XYZ to Lab
    auto f = [](float t) -> float {
        const float delta = 6.0f / 29.0f;
        return (t > delta * delta * delta) ? std::cbrt(t) : t / (3.0f * delta * delta) + 4.0f / 29.0f;
    };

    float fx = f(x), fy = f(y), fz = f(z);
    L = 116.0f * fy - 16.0f;
    a = 500.0f * (fx - fy);
    labB = 200.0f * (fy - fz);
}

// RGB (0-1) to hex string "#rrggbb"
std::string rgbToHexString(float r, float g, float b) {
    int ri = std::clamp(static_cast<int>(r * 255.0f + 0.5f), 0, 255);
    int gi = std::clamp(static_cast<int>(g * 255.0f + 0.5f), 0, 255);
    int bi = std::clamp(static_cast<int>(b * 255.0f + 0.5f), 0, 255);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", ri, gi, bi);
    return std::string(buf);
}

// Downsample RGBA8 pixels to 64x64 RGB float buffer (box filter)
std::vector<std::array<float, 3>> downsampleToRGB64(const uint8_t* pixels, uint32_t width, uint32_t height) {
    const int DS = 64;
    std::vector<std::array<float, 3>> out(DS * DS);
    float cellW = static_cast<float>(width) / DS;
    float cellH = static_cast<float>(height) / DS;

    for (int dy = 0; dy < DS; dy++) {
        int sy0 = static_cast<int>(dy * cellH);
        int sy1 = std::min(static_cast<int>((dy + 1) * cellH), static_cast<int>(height));
        for (int dx = 0; dx < DS; dx++) {
            int sx0 = static_cast<int>(dx * cellW);
            int sx1 = std::min(static_cast<int>((dx + 1) * cellW), static_cast<int>(width));
            float sumR = 0, sumG = 0, sumB = 0;
            int count = 0;
            for (int y = sy0; y < sy1; y++) {
                for (int x = sx0; x < sx1; x++) {
                    int idx = (y * static_cast<int>(width) + x) * 4;
                    sumR += pixels[idx] / 255.0f;
                    sumG += pixels[idx + 1] / 255.0f;
                    sumB += pixels[idx + 2] / 255.0f;
                    count++;
                }
            }
            if (count > 0) {
                out[dy * DS + dx] = {sumR / count, sumG / count, sumB / count};
            }
        }
    }
    return out;
}

// Downsample RGBA8 pixels to 64x64 luminance buffer (box filter)
std::vector<float> downsampleToLuminance64(const uint8_t* pixels, uint32_t width, uint32_t height) {
    const int DS = 64;
    std::vector<float> out(DS * DS);
    float cellW = static_cast<float>(width) / DS;
    float cellH = static_cast<float>(height) / DS;

    for (int dy = 0; dy < DS; dy++) {
        int sy0 = static_cast<int>(dy * cellH);
        int sy1 = std::min(static_cast<int>((dy + 1) * cellH), static_cast<int>(height));
        for (int dx = 0; dx < DS; dx++) {
            int sx0 = static_cast<int>(dx * cellW);
            int sx1 = std::min(static_cast<int>((dx + 1) * cellW), static_cast<int>(width));
            float sum = 0;
            int count = 0;
            for (int y = sy0; y < sy1; y++) {
                for (int x = sx0; x < sx1; x++) {
                    int idx = (y * static_cast<int>(width) + x) * 4;
                    float r = pixels[idx] / 255.0f;
                    float g = pixels[idx + 1] / 255.0f;
                    float b = pixels[idx + 2] / 255.0f;
                    sum += 0.2126f * r + 0.7152f * g + 0.0722f * b;
                    count++;
                }
            }
            out[dy * DS + dx] = (count > 0) ? sum / count : 0.0f;
        }
    }
    return out;
}

// Compute spatial balance from a 3x3 region brightness grid
SpatialBalanceAnalysis computeSpatialBalance(const std::array<float, 9>& rb) {
    SpatialBalanceAnalysis result;

    // Overall mean brightness
    float mean = 0.0f;
    for (int i = 0; i < 9; i++) mean += rb[i];
    mean /= 9.0f;

    // Rule-of-thirds power points (intersections of 3x3 grid)
    float pp1 = (rb[0] + rb[1] + rb[3] + rb[4]) / 4.0f;
    float pp2 = (rb[1] + rb[2] + rb[4] + rb[5]) / 4.0f;
    float pp3 = (rb[3] + rb[4] + rb[6] + rb[7]) / 4.0f;
    float pp4 = (rb[4] + rb[5] + rb[7] + rb[8]) / 4.0f;

    float ppMean = (pp1 + pp2 + pp3 + pp4) / 4.0f;
    result.thirdsScore = (mean > 0.001f) ? std::clamp(ppMean / mean, 0.0f, 2.0f) / 2.0f : 0.5f;

    // Edge bias
    float leftWeight = (rb[0] + rb[3] + rb[6]) / 3.0f;
    float rightWeight = (rb[2] + rb[5] + rb[8]) / 3.0f;
    float topWeight = (rb[0] + rb[1] + rb[2]) / 3.0f;
    float bottomWeight = (rb[6] + rb[7] + rb[8]) / 3.0f;

    float epsilon = 0.001f;
    result.horizontalBias = (rightWeight - leftWeight) / (rightWeight + leftWeight + epsilon);
    result.verticalBias = (bottomWeight - topWeight) / (bottomWeight + topWeight + epsilon);

    // Quadrant distribution
    float q1 = (rb[0] + rb[1] + rb[3] + rb[4]) / 4.0f;
    float q2 = (rb[1] + rb[2] + rb[4] + rb[5]) / 4.0f;
    float q3 = (rb[3] + rb[4] + rb[6] + rb[7]) / 4.0f;
    float q4 = (rb[4] + rb[5] + rb[7] + rb[8]) / 4.0f;

    float qMean = (q1 + q2 + q3 + q4) / 4.0f;
    float qVar = ((q1 - qMean) * (q1 - qMean) + (q2 - qMean) * (q2 - qMean) +
                   (q3 - qMean) * (q3 - qMean) + (q4 - qMean) * (q4 - qMean)) / 4.0f;
    float qStd = std::sqrt(qVar);
    result.balanceScore = (qMean > 0.001f) ? std::clamp(1.0f - qStd / qMean, 0.0f, 1.0f) : 1.0f;

    return result;
}

} // anonymous namespace

// =============================================================================
// Color Harmony Analysis
// =============================================================================

ColorHarmonyAnalysis analyzeColorHarmony(const uint8_t* pixels, uint32_t width, uint32_t height) {
    ColorHarmonyAnalysis result;

    auto rgbPixels = downsampleToRGB64(pixels, width, height);
    const int N = static_cast<int>(rgbPixels.size());

    // K-means clustering (k=5, Lab space)
    const int K = 5;
    const int MAX_ITER = 10;

    // Convert to Lab
    struct LabPixel { float L, a, b; float r, g, bVal; };
    std::vector<LabPixel> labPixels(N);
    for (int i = 0; i < N; i++) {
        labPixels[i].r = rgbPixels[i][0];
        labPixels[i].g = rgbPixels[i][1];
        labPixels[i].bVal = rgbPixels[i][2];
        rgbToLab(rgbPixels[i][0], rgbPixels[i][1], rgbPixels[i][2],
                 labPixels[i].L, labPixels[i].a, labPixels[i].b);
    }

    // Initialize centroids by evenly sampling
    struct Centroid { float L, a, b; float sumR, sumG, sumB; int count; };
    std::vector<Centroid> centroids(K);
    for (int k = 0; k < K; k++) {
        int idx = (k * N) / K;
        centroids[k] = {labPixels[idx].L, labPixels[idx].a, labPixels[idx].b, 0, 0, 0, 0};
    }

    std::vector<int> assignments(N, 0);

    for (int iter = 0; iter < MAX_ITER; iter++) {
        // Assign pixels to nearest centroid
        for (int i = 0; i < N; i++) {
            float bestDist = std::numeric_limits<float>::max();
            for (int k = 0; k < K; k++) {
                float dL = labPixels[i].L - centroids[k].L;
                float da = labPixels[i].a - centroids[k].a;
                float db = labPixels[i].b - centroids[k].b;
                float dist = dL * dL + da * da + db * db;
                if (dist < bestDist) {
                    bestDist = dist;
                    assignments[i] = k;
                }
            }
        }

        // Update centroids
        for (int k = 0; k < K; k++) {
            centroids[k] = {0, 0, 0, 0, 0, 0, 0};
        }
        for (int i = 0; i < N; i++) {
            int k = assignments[i];
            centroids[k].L += labPixels[i].L;
            centroids[k].a += labPixels[i].a;
            centroids[k].b += labPixels[i].b;
            centroids[k].sumR += labPixels[i].r;
            centroids[k].sumG += labPixels[i].g;
            centroids[k].sumB += labPixels[i].bVal;
            centroids[k].count++;
        }
        for (int k = 0; k < K; k++) {
            if (centroids[k].count > 0) {
                centroids[k].L /= centroids[k].count;
                centroids[k].a /= centroids[k].count;
                centroids[k].b /= centroids[k].count;
                centroids[k].sumR /= centroids[k].count;
                centroids[k].sumG /= centroids[k].count;
                centroids[k].sumB /= centroids[k].count;
            }
        }
    }

    // Sort centroids by population (largest first)
    std::sort(centroids.begin(), centroids.end(),
              [](const Centroid& a, const Centroid& b) { return a.count > b.count; });

    // Build palette and compute hue angles
    std::vector<float> hues;
    float maxL = -1e9f, minL = 1e9f;

    for (int k = 0; k < K; k++) {
        if (centroids[k].count == 0) continue;
        result.palette.push_back(rgbToHexString(centroids[k].sumR, centroids[k].sumG, centroids[k].sumB));

        float hue = std::atan2(centroids[k].b, centroids[k].a) * 180.0f / 3.14159265f;
        if (hue < 0) hue += 360.0f;
        hues.push_back(hue);

        maxL = std::max(maxL, centroids[k].L);
        minL = std::min(minL, centroids[k].L);
    }

    // Palette contrast
    result.paletteContrast = (maxL > minL) ? std::clamp((maxL - minL) / 100.0f, 0.0f, 1.0f) : 0.0f;

    // Score against harmony models
    std::vector<float> pairDiffs;
    for (size_t i = 0; i < hues.size(); i++) {
        for (size_t j = i + 1; j < hues.size(); j++) {
            float diff = std::abs(hues[i] - hues[j]);
            if (diff > 180.0f) diff = 360.0f - diff;
            pairDiffs.push_back(diff);
        }
    }

    float bestScore = 0.0f;
    std::string bestModel = "none";

    if (!pairDiffs.empty()) {
        // Complementary: pairs near 180 degrees
        {
            float score = 0.0f;
            for (float d : pairDiffs) {
                float proximity = 1.0f - std::abs(d - 180.0f) / 180.0f;
                score = std::max(score, proximity);
            }
            if (score > bestScore) { bestScore = score; bestModel = "complementary"; }
        }

        // Analogous: all pairs within 60 degrees
        {
            float maxDiff = 0.0f;
            for (float d : pairDiffs) maxDiff = std::max(maxDiff, d);
            float score = (maxDiff <= 60.0f) ? 1.0f - maxDiff / 60.0f : 0.0f;
            if (score > bestScore) { bestScore = score; bestModel = "analogous"; }
        }

        // Triadic: pairs near 120 degrees
        {
            float score = 0.0f;
            int triadCount = 0;
            for (float d : pairDiffs) {
                float proximity = 1.0f - std::abs(d - 120.0f) / 120.0f;
                if (proximity > 0.5f) { score += proximity; triadCount++; }
            }
            if (triadCount >= 2) {
                score /= triadCount;
                if (score > bestScore) { bestScore = score; bestModel = "triadic"; }
            }
        }

        // Split-complementary
        {
            float score = 0.0f;
            for (float d : pairDiffs) {
                float compProx = 1.0f - std::abs(d - 180.0f) / 180.0f;
                float splitProx = 1.0f - std::abs(d - 150.0f) / 150.0f;
                score = std::max(score, (compProx + splitProx) / 2.0f);
            }
            if (score > bestScore) { bestScore = score; bestModel = "split-complementary"; }
        }
    }

    result.harmonyScore = bestScore;
    result.harmonyType = bestModel;

    return result;
}

// =============================================================================
// Symmetry Analysis
// =============================================================================

SymmetryAnalysis analyzeSymmetry(const uint8_t* pixels, uint32_t width, uint32_t height) {
    SymmetryAnalysis result;

    auto lum = downsampleToLuminance64(pixels, width, height);
    const int DS = 64;

    // Horizontal symmetry
    float hDiff = 0.0f;
    int hCount = 0;
    for (int y = 0; y < DS; y++) {
        for (int x = 0; x < DS / 2; x++) {
            hDiff += std::abs(lum[y * DS + x] - lum[y * DS + (DS - 1 - x)]);
            hCount++;
        }
    }
    result.horizontalSymmetry = (hCount > 0) ? 1.0f - hDiff / hCount : 0.0f;

    // Vertical symmetry
    float vDiff = 0.0f;
    int vCount = 0;
    for (int y = 0; y < DS / 2; y++) {
        for (int x = 0; x < DS; x++) {
            vDiff += std::abs(lum[y * DS + x] - lum[(DS - 1 - y) * DS + x]);
            vCount++;
        }
    }
    result.verticalSymmetry = (vCount > 0) ? 1.0f - vDiff / vCount : 0.0f;

    // Radial (4-fold) symmetry
    float rDiff = 0.0f;
    int rCount = 0;
    int half = DS / 2;
    for (int y = 0; y < half; y++) {
        for (int x = 0; x < half; x++) {
            float tl = lum[y * DS + x];
            float tr = lum[x * DS + (DS - 1 - y)];
            float br = lum[(DS - 1 - y) * DS + (DS - 1 - x)];
            float bl = lum[(DS - 1 - x) * DS + y];

            float mean4 = (tl + tr + br + bl) / 4.0f;
            rDiff += std::abs(tl - mean4) + std::abs(tr - mean4) +
                     std::abs(br - mean4) + std::abs(bl - mean4);
            rCount += 4;
        }
    }
    result.radialSymmetry = (rCount > 0) ? 1.0f - rDiff / rCount : 0.0f;

    return result;
}

// =============================================================================
// Spatial Balance Analysis
// =============================================================================

SpatialBalanceAnalysis analyzeSpatialBalance(const uint8_t* pixels, uint32_t width, uint32_t height) {
    auto fa = analyzePixels(pixels, width, height);
    return analyzeSpatialBalance(fa);
}

SpatialBalanceAnalysis analyzeSpatialBalance(const FrameAnalysis& fa) {
    return computeSpatialBalance(fa.regionBrightness);
}

// =============================================================================
// Composite Analysis
// =============================================================================

VisualAnalysis analyzeVisual(const uint8_t* pixels, uint32_t width, uint32_t height) {
    VisualAnalysis result;
    result.harmony = analyzeColorHarmony(pixels, width, height);
    result.symmetry = analyzeSymmetry(pixels, width, height);
    // Use the pixel-based overload (no pre-computed FrameAnalysis available here)
    result.balance = analyzeSpatialBalance(pixels, width, height);
    return result;
}

// =============================================================================
// JSON Serialization
// =============================================================================

std::string ColorHarmonyAnalysis::toJSON() const {
    std::ostringstream ss;
    ss << "{";
    ss << "\"harmonyScore\":" << harmonyScore;
    ss << ",\"harmonyType\":\"" << harmonyType << "\"";
    ss << ",\"paletteContrast\":" << paletteContrast;
    ss << ",\"palette\":[";
    for (size_t i = 0; i < palette.size(); i++) {
        if (i > 0) ss << ",";
        ss << "\"" << palette[i] << "\"";
    }
    ss << "]";
    ss << "}";
    return ss.str();
}

std::string SymmetryAnalysis::toJSON() const {
    std::ostringstream ss;
    ss << "{";
    ss << "\"horizontalSymmetry\":" << horizontalSymmetry;
    ss << ",\"verticalSymmetry\":" << verticalSymmetry;
    ss << ",\"radialSymmetry\":" << radialSymmetry;
    ss << "}";
    return ss.str();
}

std::string SpatialBalanceAnalysis::toJSON() const {
    std::ostringstream ss;
    ss << "{";
    ss << "\"thirdsScore\":" << thirdsScore;
    ss << ",\"horizontalBias\":" << horizontalBias;
    ss << ",\"verticalBias\":" << verticalBias;
    ss << ",\"balanceScore\":" << balanceScore;
    ss << "}";
    return ss.str();
}

std::string VisualAnalysis::toJSON() const {
    std::ostringstream ss;
    ss << "{";
    ss << "\"harmony\":" << harmony.toJSON();
    ss << ",\"symmetry\":" << symmetry.toJSON();
    ss << ",\"balance\":" << balance.toJSON();
    ss << "}";
    return ss.str();
}

} // namespace vivid
