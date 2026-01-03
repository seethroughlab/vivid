#pragma once

// VizDrawList - ImDrawList-compatible wrapper for OverlayCanvas
// Allows operators to provide visualizations without direct ImGui dependency

#include <vivid/overlay_canvas.h>
#include <glm/glm.hpp>
#include <cstdint>

namespace vivid {

// Color helpers (matching ImGui's IM_COL32)
constexpr uint32_t VIZ_COL32(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(r);
}

// Convert uint32_t color (ABGR format) to glm::vec4 (normalized RGBA)
inline glm::vec4 col32ToVec4(uint32_t col) {
    return glm::vec4(
        static_cast<float>((col >> 0) & 0xFF) / 255.0f,  // R
        static_cast<float>((col >> 8) & 0xFF) / 255.0f,  // G
        static_cast<float>((col >> 16) & 0xFF) / 255.0f, // B
        static_cast<float>((col >> 24) & 0xFF) / 255.0f  // A
    );
}

// Simple 2D vector (matching ImVec2)
struct VizVec2 {
    float x = 0, y = 0;
    VizVec2() = default;
    VizVec2(float x_, float y_) : x(x_), y(y_) {}
};

// Text size result
struct VizTextSize {
    float x = 0, y = 0;
};

/**
 * @brief ImDrawList-compatible drawing interface
 *
 * Wraps OverlayCanvas to provide an API similar to ImDrawList.
 * Used by operators to draw custom visualizations in the chain visualizer.
 *
 * Color format: ABGR (same as ImGui's IM_COL32)
 */
class VizDrawList {
public:
    explicit VizDrawList(OverlayCanvas& canvas) : m_canvas(canvas) {}

    // Filled shapes
    void AddRectFilled(VizVec2 min, VizVec2 max, uint32_t col, float rounding = 0.0f) {
        (void)rounding; // OverlayCanvas doesn't support rounding yet
        m_canvas.fillRect(min.x, min.y, max.x - min.x, max.y - min.y, col32ToVec4(col));
    }

    // Outlined shapes
    void AddRect(VizVec2 min, VizVec2 max, uint32_t col, float rounding = 0.0f,
                 int flags = 0, float thickness = 1.0f) {
        (void)rounding; (void)flags;
        m_canvas.strokeRect(min.x, min.y, max.x - min.x, max.y - min.y, thickness, col32ToVec4(col));
    }

    // Circles
    void AddCircleFilled(VizVec2 center, float radius, uint32_t col, int segments = 0) {
        (void)segments;
        m_canvas.fillCircle(center.x, center.y, radius, col32ToVec4(col));
    }

    void AddCircle(VizVec2 center, float radius, uint32_t col, int segments = 0, float thickness = 1.0f) {
        (void)segments;
        m_canvas.strokeCircle(center.x, center.y, radius, thickness, col32ToVec4(col));
    }

    // Lines
    void AddLine(VizVec2 p1, VizVec2 p2, uint32_t col, float thickness = 1.0f) {
        m_canvas.line(p1.x, p1.y, p2.x, p2.y, thickness, col32ToVec4(col));
    }

    // Triangle (filled)
    void AddTriangleFilled(VizVec2 p1, VizVec2 p2, VizVec2 p3, uint32_t col) {
        m_canvas.fillTriangle(glm::vec2(p1.x, p1.y), glm::vec2(p2.x, p2.y),
                              glm::vec2(p3.x, p3.y), col32ToVec4(col));
    }

    // Text
    void AddText(VizVec2 pos, uint32_t col, const char* textStr) {
        m_canvas.text(textStr, pos.x, pos.y, col32ToVec4(col));
    }

    void AddText(VizVec2 pos, uint32_t col, const char* textStr, float fontSize) {
        (void)fontSize; // OverlayCanvas uses font index, not size
        m_canvas.text(textStr, pos.x, pos.y, col32ToVec4(col));
    }

    // Image (texture ID is WGPUTextureView cast to void*)
    // Assumes 16:9 aspect ratio for preview textures (256x144)
    void AddImage(void* texId, VizVec2 min, VizVec2 max, float srcAspect = 256.0f / 144.0f) {
        float areaW = max.x - min.x;
        float areaH = max.y - min.y;

        if (texId) {
            WGPUTextureView view = reinterpret_cast<WGPUTextureView>(texId);

            // Preserve aspect ratio - fit image within area
            float areaAspect = areaW / areaH;
            float drawW, drawH, drawX, drawY;

            if (srcAspect > areaAspect) {
                // Image is wider than area - fit to width, center vertically
                drawW = areaW;
                drawH = areaW / srcAspect;
                drawX = min.x;
                drawY = min.y + (areaH - drawH) * 0.5f;
            } else {
                // Image is taller than area - fit to height, center horizontally
                drawH = areaH;
                drawW = areaH * srcAspect;
                drawX = min.x + (areaW - drawW) * 0.5f;
                drawY = min.y;
            }

            m_canvas.texturedRect(drawX, drawY, drawW, drawH, view);
        } else {
            // No texture - draw placeholder rectangle
            m_canvas.strokeRect(min.x, min.y, areaW, areaH,
                               1.0f, col32ToVec4(VIZ_COL32(100, 100, 100, 255)));
        }
    }

    // Text size calculation (approximate)
    VizTextSize CalcTextSize(const char* textStr, float fontSize = 12.0f) const {
        // Approximate: 7 pixels per character width, fontSize for height
        size_t len = 0;
        while (textStr && textStr[len]) len++;
        return { static_cast<float>(len) * fontSize * 0.6f, fontSize };
    }

private:
    OverlayCanvas& m_canvas;
};

} // namespace vivid
