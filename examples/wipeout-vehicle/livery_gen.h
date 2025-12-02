#pragma once
// Procedural Livery Texture Generator
// Generates Wipeout 2097-style team livery textures

#include <vivid/vivid.h>
#include <vector>
#include <cstdint>

namespace livery {

// Team color palette
struct TeamPalette {
    glm::vec3 primary;      // Main body color
    glm::vec3 secondary;    // Accent/pod color
    glm::vec3 accent;       // Stripe/detail color
    glm::vec3 dark;         // Panel lines, shadows
};

// Pre-defined team palettes
extern const TeamPalette FEISAR;   // Blue/White
extern const TeamPalette AG_SYS;   // Yellow/Blue
extern const TeamPalette AURICOM;  // Red/White
extern const TeamPalette QIREX;    // Purple/Cyan
extern const TeamPalette PIRANHA;  // Black/Orange

// Texture atlas regions (UV coordinates for different parts)
struct UVRegion {
    float u0, v0, u1, v1;
};

// Standard atlas layout
namespace regions {
    // Body regions (top half of atlas)
    extern const UVRegion BODY_TOP;      // 0.0-0.5, 0.0-0.25
    extern const UVRegion BODY_SIDE;     // 0.5-1.0, 0.0-0.25
    extern const UVRegion NOSE;          // 0.0-0.25, 0.25-0.5
    extern const UVRegion COCKPIT;       // 0.25-0.5, 0.25-0.5

    // Pod/wing regions (bottom half)
    extern const UVRegion POD_OUTER;     // 0.0-0.5, 0.5-0.75
    extern const UVRegion POD_INNER;     // 0.5-1.0, 0.5-0.75
    extern const UVRegion WING;          // 0.0-0.5, 0.75-1.0
    extern const UVRegion FIN;           // 0.5-0.75, 0.75-1.0
    extern const UVRegion ENGINE;        // 0.75-1.0, 0.75-1.0
}

// Livery generator class
class LiveryGenerator {
public:
    LiveryGenerator(int width = 512, int height = 512);

    // Set the team palette
    void setPalette(const TeamPalette& palette);

    // Set team number (for decals)
    void setTeamNumber(int number);

    // Set path to grime texture for weathering effect
    void setGrimePath(const std::string& path) { grimePath_ = path; }

    // Generate the complete livery texture
    void generate();

    // Get the pixel data (RGBA, 4 bytes per pixel)
    const std::vector<uint8_t>& getPixels() const { return pixels_; }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    // Upload to Vivid texture
    void uploadTo(vivid::Context& ctx, vivid::Texture& tex);

private:
    int width_;
    int height_;
    std::vector<uint8_t> pixels_;
    TeamPalette palette_;
    int teamNumber_;

    // Drawing primitives
    void clear(glm::vec3 color);
    void setPixel(int x, int y, glm::vec3 color, float alpha = 1.0f);
    void fillRect(int x, int y, int w, int h, glm::vec3 color, float alpha = 1.0f);
    void fillRegion(const UVRegion& region, glm::vec3 color, float alpha = 1.0f);

    // Pattern generators
    void drawHorizontalStripe(int y, int height, glm::vec3 color);
    void drawVerticalStripe(int x, int width, glm::vec3 color);
    void drawDiagonalStripes(int x, int y, int w, int h, int stripeWidth, int gap, glm::vec3 color);
    void drawPanelLines(int spacing, glm::vec3 color);
    void drawChevron(int cx, int cy, int size, glm::vec3 color);
    void drawHazardStripes(int x, int y, int w, int h, glm::vec3 color1, glm::vec3 color2);
    void drawNumber(int x, int y, int digit, int scale, glm::vec3 color);
    void drawTeamNumber(int x, int y, int scale, glm::vec3 color);

    // Region-specific generation
    void generateBodyTop();
    void generateBodySide();
    void generateNose();
    void generateCockpit();
    void generatePodOuter();
    void generatePodInner();
    void generateWing();
    void generateFin();
    void generateEngine();

    // Grime overlay blending
    void blendGrimeOverlay(const std::string& grimePath, float intensity = 0.4f);
    std::string grimePath_;  // Path to grime texture
};

// Convenience function to generate a livery texture
vivid::Texture generateLiveryTexture(vivid::Context& ctx, const TeamPalette& palette, int teamNumber = 1);

} // namespace livery
