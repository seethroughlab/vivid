#include "livery_gen.h"
#include <cmath>
#include <algorithm>
#include <iostream>

// Include stb_image for loading grime textures
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace livery {

// Team palettes
const TeamPalette FEISAR = {
    {0.17f, 0.36f, 0.69f},  // Blue primary
    {1.00f, 1.00f, 1.00f},  // White secondary
    {1.00f, 0.84f, 0.00f},  // Gold accent
    {0.05f, 0.08f, 0.15f}   // Dark blue
};

const TeamPalette AG_SYS = {
    {1.00f, 0.84f, 0.00f},  // Yellow primary
    {0.00f, 0.40f, 0.80f},  // Blue secondary
    {1.00f, 1.00f, 1.00f},  // White accent
    {0.15f, 0.12f, 0.00f}   // Dark yellow
};

const TeamPalette AURICOM = {
    {0.85f, 0.12f, 0.12f},  // Red primary
    {1.00f, 1.00f, 1.00f},  // White secondary
    {0.20f, 0.20f, 0.25f},  // Dark gray accent
    {0.25f, 0.05f, 0.05f}   // Dark red
};

const TeamPalette QIREX = {
    {0.45f, 0.00f, 0.65f},  // Purple primary
    {0.00f, 0.85f, 0.85f},  // Cyan secondary
    {0.10f, 0.10f, 0.12f},  // Near black accent
    {0.15f, 0.00f, 0.20f}   // Dark purple
};

const TeamPalette PIRANHA = {
    {0.12f, 0.12f, 0.14f},  // Black primary
    {1.00f, 0.45f, 0.00f},  // Orange secondary
    {0.75f, 0.75f, 0.78f},  // Silver accent
    {0.05f, 0.05f, 0.06f}   // Pure black
};

// UV regions for texture atlas
namespace regions {
    const UVRegion BODY_TOP   = {0.0f, 0.0f, 0.5f, 0.25f};
    const UVRegion BODY_SIDE  = {0.5f, 0.0f, 1.0f, 0.25f};
    const UVRegion NOSE       = {0.0f, 0.25f, 0.25f, 0.5f};
    const UVRegion COCKPIT    = {0.25f, 0.25f, 0.5f, 0.5f};
    const UVRegion POD_OUTER  = {0.0f, 0.5f, 0.5f, 0.75f};
    const UVRegion POD_INNER  = {0.5f, 0.5f, 1.0f, 0.75f};
    const UVRegion WING       = {0.0f, 0.75f, 0.5f, 1.0f};
    const UVRegion FIN        = {0.5f, 0.75f, 0.75f, 1.0f};
    const UVRegion ENGINE     = {0.75f, 0.75f, 1.0f, 1.0f};
}

LiveryGenerator::LiveryGenerator(int width, int height)
    : width_(width), height_(height), teamNumber_(1) {
    pixels_.resize(width * height * 4);
    palette_ = FEISAR;
}

void LiveryGenerator::setPalette(const TeamPalette& palette) {
    palette_ = palette;
}

void LiveryGenerator::setTeamNumber(int number) {
    teamNumber_ = std::clamp(number, 0, 99);
}

void LiveryGenerator::setPixel(int x, int y, glm::vec3 color, float alpha) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;

    int idx = (y * width_ + x) * 4;
    pixels_[idx + 0] = static_cast<uint8_t>(std::clamp(color.r * 255.0f, 0.0f, 255.0f));
    pixels_[idx + 1] = static_cast<uint8_t>(std::clamp(color.g * 255.0f, 0.0f, 255.0f));
    pixels_[idx + 2] = static_cast<uint8_t>(std::clamp(color.b * 255.0f, 0.0f, 255.0f));
    pixels_[idx + 3] = static_cast<uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f));
}

void LiveryGenerator::clear(glm::vec3 color) {
    for (int y = 0; y < height_; y++) {
        for (int x = 0; x < width_; x++) {
            setPixel(x, y, color, 1.0f);
        }
    }
}

void LiveryGenerator::fillRect(int x, int y, int w, int h, glm::vec3 color, float alpha) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            setPixel(px, py, color, alpha);
        }
    }
}

void LiveryGenerator::fillRegion(const UVRegion& region, glm::vec3 color, float alpha) {
    int x0 = static_cast<int>(region.u0 * width_);
    int y0 = static_cast<int>(region.v0 * height_);
    int x1 = static_cast<int>(region.u1 * width_);
    int y1 = static_cast<int>(region.v1 * height_);
    fillRect(x0, y0, x1 - x0, y1 - y0, color, alpha);
}

void LiveryGenerator::drawHorizontalStripe(int y, int height, glm::vec3 color) {
    fillRect(0, y, width_, height, color, 1.0f);
}

void LiveryGenerator::drawVerticalStripe(int x, int width, glm::vec3 color) {
    fillRect(x, 0, width, height_, color, 1.0f);
}

void LiveryGenerator::drawDiagonalStripes(int x, int y, int w, int h, int stripeWidth, int gap, glm::vec3 color) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            int diag = (px - x) + (py - y);
            if ((diag / stripeWidth) % 2 == 0) {
                setPixel(px, py, color, 1.0f);
            }
        }
    }
}

void LiveryGenerator::drawPanelLines(int spacing, glm::vec3 color) {
    // Horizontal panel lines
    for (int y = spacing; y < height_; y += spacing) {
        for (int x = 0; x < width_; x++) {
            setPixel(x, y, color, 0.7f);
            if (y + 1 < height_) setPixel(x, y + 1, color, 0.3f);
        }
    }
    // Vertical panel lines
    for (int x = spacing; x < width_; x += spacing) {
        for (int y = 0; y < height_; y++) {
            setPixel(x, y, color, 0.7f);
            if (x + 1 < width_) setPixel(x + 1, y, color, 0.3f);
        }
    }
}

void LiveryGenerator::drawChevron(int cx, int cy, int size, glm::vec3 color) {
    int thickness = std::max(2, size / 8);
    for (int i = 0; i < size; i++) {
        int x1 = cx - size / 2 + i;
        int x2 = cx + size / 2 - i;
        int y = cy - size / 2 + i;

        for (int t = 0; t < thickness; t++) {
            setPixel(x1, y + t, color, 1.0f);
            setPixel(x2, y + t, color, 1.0f);
        }
    }
}

void LiveryGenerator::drawHazardStripes(int x, int y, int w, int h, glm::vec3 color1, glm::vec3 color2) {
    int stripeWidth = std::max(4, w / 8);
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            int diag = (px - x) - (py - y);
            bool stripe1 = ((diag / stripeWidth) % 2 == 0);
            setPixel(px, py, stripe1 ? color1 : color2, 1.0f);
        }
    }
}

// Simple 5x7 pixel digit patterns
static const uint8_t digitPatterns[10][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, // 2
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 5
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, // 9
};

void LiveryGenerator::drawNumber(int x, int y, int digit, int scale, glm::vec3 color) {
    if (digit < 0 || digit > 9) return;

    for (int row = 0; row < 7; row++) {
        uint8_t pattern = digitPatterns[digit][row];
        for (int col = 0; col < 5; col++) {
            if (pattern & (0x10 >> col)) {
                // Draw scaled pixel
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        setPixel(x + col * scale + sx, y + row * scale + sy, color, 1.0f);
                    }
                }
            }
        }
    }
}

void LiveryGenerator::drawTeamNumber(int x, int y, int scale, glm::vec3 color) {
    if (teamNumber_ >= 10) {
        drawNumber(x, y, teamNumber_ / 10, scale, color);
        drawNumber(x + 6 * scale, y, teamNumber_ % 10, scale, color);
    } else {
        drawNumber(x + 3 * scale, y, teamNumber_, scale, color);
    }
}

void LiveryGenerator::generateBodyTop() {
    // Main body top - primary color with racing stripe
    int x0 = static_cast<int>(regions::BODY_TOP.u0 * width_);
    int y0 = static_cast<int>(regions::BODY_TOP.v0 * height_);
    int w = static_cast<int>((regions::BODY_TOP.u1 - regions::BODY_TOP.u0) * width_);
    int h = static_cast<int>((regions::BODY_TOP.v1 - regions::BODY_TOP.v0) * height_);

    // Fill with primary
    fillRect(x0, y0, w, h, palette_.primary, 1.0f);

    // Center racing stripe (secondary color)
    int stripeWidth = w / 6;
    int stripeX = x0 + (w - stripeWidth) / 2;
    fillRect(stripeX, y0, stripeWidth, h, palette_.secondary, 1.0f);

    // Thin accent lines on stripe edges
    int lineWidth = 2;
    fillRect(stripeX - lineWidth, y0, lineWidth, h, palette_.accent, 1.0f);
    fillRect(stripeX + stripeWidth, y0, lineWidth, h, palette_.accent, 1.0f);

    // Team number
    int numX = x0 + w / 4 - 12;
    int numY = y0 + h / 3;
    drawTeamNumber(numX, numY, 3, palette_.secondary);
}

void LiveryGenerator::generateBodySide() {
    int x0 = static_cast<int>(regions::BODY_SIDE.u0 * width_);
    int y0 = static_cast<int>(regions::BODY_SIDE.v0 * height_);
    int w = static_cast<int>((regions::BODY_SIDE.u1 - regions::BODY_SIDE.u0) * width_);
    int h = static_cast<int>((regions::BODY_SIDE.v1 - regions::BODY_SIDE.v0) * height_);

    // Fill with primary
    fillRect(x0, y0, w, h, palette_.primary, 1.0f);

    // Lower section in secondary color
    int splitY = y0 + h * 2 / 3;
    fillRect(x0, splitY, w, y0 + h - splitY, palette_.secondary, 1.0f);

    // Dividing line
    fillRect(x0, splitY - 2, w, 4, palette_.accent, 1.0f);
}

void LiveryGenerator::generateNose() {
    int x0 = static_cast<int>(regions::NOSE.u0 * width_);
    int y0 = static_cast<int>(regions::NOSE.v0 * height_);
    int w = static_cast<int>((regions::NOSE.u1 - regions::NOSE.u0) * width_);
    int h = static_cast<int>((regions::NOSE.v1 - regions::NOSE.v0) * height_);

    // Nose - gradient from primary to dark
    for (int py = y0; py < y0 + h; py++) {
        float t = static_cast<float>(py - y0) / h;
        glm::vec3 color = glm::mix(palette_.primary, palette_.dark, t * 0.5f);
        for (int px = x0; px < x0 + w; px++) {
            setPixel(px, py, color, 1.0f);
        }
    }

    // Chevron arrows pointing forward
    int chevronSize = w / 3;
    drawChevron(x0 + w / 2, y0 + h / 3, chevronSize, palette_.accent);
}

void LiveryGenerator::generateCockpit() {
    int x0 = static_cast<int>(regions::COCKPIT.u0 * width_);
    int y0 = static_cast<int>(regions::COCKPIT.v0 * height_);
    int w = static_cast<int>((regions::COCKPIT.u1 - regions::COCKPIT.u0) * width_);
    int h = static_cast<int>((regions::COCKPIT.v1 - regions::COCKPIT.v0) * height_);

    // Cockpit glass - dark with subtle gradient
    for (int py = y0; py < y0 + h; py++) {
        float t = static_cast<float>(py - y0) / h;
        glm::vec3 color = glm::mix(
            glm::vec3(0.05f, 0.08f, 0.12f),
            glm::vec3(0.02f, 0.03f, 0.05f),
            t
        );
        for (int px = x0; px < x0 + w; px++) {
            setPixel(px, py, color, 0.9f);
        }
    }

    // Frame lines
    fillRect(x0, y0, w, 2, palette_.dark, 1.0f);
    fillRect(x0, y0 + h - 2, w, 2, palette_.dark, 1.0f);
}

void LiveryGenerator::generatePodOuter() {
    int x0 = static_cast<int>(regions::POD_OUTER.u0 * width_);
    int y0 = static_cast<int>(regions::POD_OUTER.v0 * height_);
    int w = static_cast<int>((regions::POD_OUTER.u1 - regions::POD_OUTER.u0) * width_);
    int h = static_cast<int>((regions::POD_OUTER.v1 - regions::POD_OUTER.v0) * height_);

    // Pod outer surface - secondary color
    fillRect(x0, y0, w, h, palette_.secondary, 1.0f);

    // Large team number
    int numX = x0 + w / 4;
    int numY = y0 + h / 4;
    drawTeamNumber(numX, numY, 4, palette_.primary);

    // Diagonal accent stripes at rear
    int stripeArea = w / 4;
    drawDiagonalStripes(x0 + w - stripeArea, y0, stripeArea, h, 8, 8, palette_.accent);
}

void LiveryGenerator::generatePodInner() {
    int x0 = static_cast<int>(regions::POD_INNER.u0 * width_);
    int y0 = static_cast<int>(regions::POD_INNER.v0 * height_);
    int w = static_cast<int>((regions::POD_INNER.u1 - regions::POD_INNER.u0) * width_);
    int h = static_cast<int>((regions::POD_INNER.v1 - regions::POD_INNER.v0) * height_);

    // Pod inner surface - darker, more technical look
    fillRect(x0, y0, w, h, palette_.dark * 1.5f, 1.0f);

    // Tech panel pattern
    int panelW = w / 4;
    int panelH = h / 3;
    for (int py = 0; py < 3; py++) {
        for (int px = 0; px < 4; px++) {
            if ((px + py) % 2 == 0) {
                fillRect(x0 + px * panelW + 2, y0 + py * panelH + 2,
                         panelW - 4, panelH - 4, palette_.dark * 2.0f, 1.0f);
            }
        }
    }
}

void LiveryGenerator::generateWing() {
    int x0 = static_cast<int>(regions::WING.u0 * width_);
    int y0 = static_cast<int>(regions::WING.v0 * height_);
    int w = static_cast<int>((regions::WING.u1 - regions::WING.u0) * width_);
    int h = static_cast<int>((regions::WING.v1 - regions::WING.v0) * height_);

    // Wing surface - accent color
    fillRect(x0, y0, w, h, palette_.accent, 1.0f);

    // Racing stripes across wing
    int stripeH = h / 5;
    fillRect(x0, y0 + h / 2 - stripeH / 2, w, stripeH, palette_.primary, 1.0f);

    // Edge detail
    fillRect(x0, y0, w, 3, palette_.dark, 1.0f);
    fillRect(x0, y0 + h - 3, w, 3, palette_.dark, 1.0f);
}

void LiveryGenerator::generateFin() {
    int x0 = static_cast<int>(regions::FIN.u0 * width_);
    int y0 = static_cast<int>(regions::FIN.v0 * height_);
    int w = static_cast<int>((regions::FIN.u1 - regions::FIN.u0) * width_);
    int h = static_cast<int>((regions::FIN.v1 - regions::FIN.v0) * height_);

    // Fin - primary with accent stripe
    fillRect(x0, y0, w, h, palette_.primary, 1.0f);

    // Vertical accent stripe
    int stripeW = w / 3;
    fillRect(x0 + stripeW, y0, stripeW, h, palette_.accent, 1.0f);
}

void LiveryGenerator::generateEngine() {
    int x0 = static_cast<int>(regions::ENGINE.u0 * width_);
    int y0 = static_cast<int>(regions::ENGINE.v0 * height_);
    int w = static_cast<int>((regions::ENGINE.u1 - regions::ENGINE.u0) * width_);
    int h = static_cast<int>((regions::ENGINE.v1 - regions::ENGINE.v0) * height_);

    // Engine exhaust - metallic with glow center
    fillRect(x0, y0, w, h, glm::vec3(0.3f, 0.3f, 0.35f), 1.0f);

    // Concentric rings
    int cx = x0 + w / 2;
    int cy = y0 + h / 2;
    int maxR = std::min(w, h) / 2 - 2;

    for (int r = maxR; r > 0; r -= 4) {
        float t = 1.0f - static_cast<float>(r) / maxR;
        glm::vec3 color = glm::mix(
            glm::vec3(0.4f, 0.4f, 0.45f),
            glm::vec3(1.0f, 0.6f, 0.2f),  // Orange glow center
            t * t
        );

        for (int py = y0; py < y0 + h; py++) {
            for (int px = x0; px < x0 + w; px++) {
                float dist = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
                if (dist < r && dist > r - 3) {
                    setPixel(px, py, color, 1.0f);
                }
            }
        }
    }

    // Bright center
    for (int py = y0; py < y0 + h; py++) {
        for (int px = x0; px < x0 + w; px++) {
            float dist = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
            if (dist < 5) {
                setPixel(px, py, glm::vec3(1.0f, 0.8f, 0.5f), 1.0f);
            }
        }
    }
}

void LiveryGenerator::generate() {
    // Create a full-coverage livery texture (not atlas-based)
    // This works with simple 0-1 UV mapping on any mesh

    // Fill with primary team color
    clear(palette_.primary);

    // === HORIZONTAL RACING STRIPE (center) ===
    int stripeY = height_ / 2 - height_ / 8;
    int stripeH = height_ / 4;
    fillRect(0, stripeY, width_, stripeH, palette_.secondary, 1.0f);

    // Accent lines on stripe edges
    fillRect(0, stripeY - 4, width_, 4, palette_.accent, 1.0f);
    fillRect(0, stripeY + stripeH, width_, 4, palette_.accent, 1.0f);

    // === DIAGONAL STRIPES (corners for visual interest) ===
    int cornerSize = width_ / 4;
    // Top-left corner
    drawDiagonalStripes(0, 0, cornerSize, cornerSize, 12, 12, palette_.accent);
    // Bottom-right corner
    drawDiagonalStripes(width_ - cornerSize, height_ - cornerSize, cornerSize, cornerSize, 12, 12, palette_.accent);

    // === HAZARD STRIPES (bottom edge) ===
    int hazardH = height_ / 16;
    drawHazardStripes(0, height_ - hazardH, width_, hazardH, palette_.dark, palette_.accent);

    // === CHEVRONS (pointing right/forward) ===
    int chevronSize = height_ / 6;
    // Multiple chevrons along the stripe
    drawChevron(width_ / 6, height_ / 2, chevronSize, palette_.primary);
    drawChevron(width_ / 3, height_ / 2, chevronSize, palette_.primary);

    // === TEAM NUMBER (large, centered in stripe) ===
    int numX = width_ / 2;
    int numY = stripeY + stripeH / 4;
    int numScale = 6;
    drawTeamNumber(numX, numY, numScale, palette_.primary);

    // === PANEL LINES (subtle tech detail) ===
    drawPanelLines(64, palette_.dark * 0.7f);

    // === VERTICAL ACCENT STRIPE (side detail) ===
    int vStripeW = width_ / 20;
    fillRect(width_ - vStripeW * 2, 0, vStripeW, height_, palette_.secondary, 0.8f);

    // === GRIME OVERLAY (if path is set) ===
    if (!grimePath_.empty()) {
        blendGrimeOverlay(grimePath_, 0.8f);  // Strong grime effect with overlay blend
    }
}

void LiveryGenerator::blendGrimeOverlay(const std::string& grimePath, float intensity) {
    // Load the grime texture
    int grimeW, grimeH, grimeChannels;
    unsigned char* grimeData = stbi_load(grimePath.c_str(), &grimeW, &grimeH, &grimeChannels, 0);
    if (!grimeData) {
        std::cerr << "[livery] Failed to load grime texture: " << grimePath << std::endl;
        return;
    }
    std::cout << "[livery] Loaded grime texture: " << grimePath << " (" << grimeW << "x" << grimeH << ")" << std::endl;

    // Blend grime over the procedural livery using multiply blend
    // Scale the grime texture to cover the entire livery (not tiled)
    for (int y = 0; y < height_; y++) {
        for (int x = 0; x < width_; x++) {
            // Sample grime texture (scaled to fit, not tiled)
            int gx = (x * grimeW) / width_;
            int gy = (y * grimeH) / height_;
            int grimeIdx = (gy * grimeW + gx) * grimeChannels;

            // Get grime luminance (grayscale value)
            float grimeLum;
            if (grimeChannels >= 3) {
                // RGB - convert to luminance
                float gr = grimeData[grimeIdx] / 255.0f;
                float gg = grimeData[grimeIdx + 1] / 255.0f;
                float gb = grimeData[grimeIdx + 2] / 255.0f;
                grimeLum = 0.299f * gr + 0.587f * gg + 0.114f * gb;
            } else {
                // Grayscale
                grimeLum = grimeData[grimeIdx] / 255.0f;
            }

            // Get current pixel
            int idx = (y * width_ + x) * 4;
            float r = pixels_[idx] / 255.0f;
            float g = pixels_[idx + 1] / 255.0f;
            float b = pixels_[idx + 2] / 255.0f;

            // Overlay blend mode: preserves contrast while adding grime detail
            // Dark grime pixels darken, light grime pixels slightly lighten
            // This creates visible weathering patterns

            // Apply overlay formula (standard Photoshop overlay blend)
            auto overlay = [](float base, float blend) {
                if (base < 0.5f)
                    return 2.0f * base * blend;
                else
                    return 1.0f - 2.0f * (1.0f - base) * (1.0f - blend);
            };

            // Blend between original and overlay based on intensity
            float ro = overlay(r, grimeLum);
            float go = overlay(g, grimeLum);
            float bo = overlay(b, grimeLum);

            r = r * (1.0f - intensity) + ro * intensity;
            g = g * (1.0f - intensity) + go * intensity;
            b = b * (1.0f - intensity) + bo * intensity;

            // Write back
            pixels_[idx] = static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f));
            pixels_[idx + 1] = static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f));
            pixels_[idx + 2] = static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f));
        }
    }

    stbi_image_free(grimeData);
}

void LiveryGenerator::uploadTo(vivid::Context& ctx, vivid::Texture& tex) {
    tex = ctx.createTexture(width_, height_);
    ctx.uploadTexturePixels(tex, pixels_.data(), width_, height_);
}

// Convenience function
vivid::Texture generateLiveryTexture(vivid::Context& ctx, const TeamPalette& palette, int teamNumber) {
    LiveryGenerator gen(512, 512);
    gen.setPalette(palette);
    gen.setTeamNumber(teamNumber);
    gen.generate();

    vivid::Texture tex;
    gen.uploadTo(ctx, tex);
    return tex;
}

} // namespace livery
