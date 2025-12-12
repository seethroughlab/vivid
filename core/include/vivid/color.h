#pragma once

/**
 * @file color.h
 * @brief Color class with named colors, HSV conversion, and hex parsing
 *
 * Provides a convenient Color type that implicitly converts to glm::vec4,
 * enabling readable code with named colors while maintaining compatibility
 * with the existing API.
 *
 * @par Example
 * @code
 * // Before:
 * particles.color(1.0f, 0.8f, 0.2f, 1.0f);
 *
 * // After:
 * particles.color(Color::Gold);
 * particles.color(Color::fromHex("#FF7F50"));
 * Color gradient = Color::Red.lerp(Color::Blue, t);
 * @endcode
 */

#include <glm/glm.hpp>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace vivid {

class ColorParam;  // Forward declaration

/**
 * @brief RGBA color with named colors, HSV conversion, and hex parsing
 *
 * Color stores RGBA values in 0-1 range and provides:
 * - Implicit conversion to glm::vec4 for API compatibility
 * - Static constants for all 140+ CSS/X11 named colors
 * - Factory methods for HSV and hex color creation
 * - Color blending and interpolation
 */
class Color {
public:
    float r, g, b, a;

    // =========================================================================
    // Constructors
    // =========================================================================

    /**
     * @brief Default constructor (opaque white)
     */
    constexpr Color() : r(1.0f), g(1.0f), b(1.0f), a(1.0f) {}

    /**
     * @brief Construct from RGBA values (0-1 range)
     * @param r Red component
     * @param g Green component
     * @param b Blue component
     * @param a Alpha component (default 1.0 = opaque)
     */
    constexpr Color(float r, float g, float b, float a = 1.0f)
        : r(r), g(g), b(b), a(a) {}

    /**
     * @brief Construct from glm::vec4
     * @param v Vector with RGBA components
     */
    constexpr Color(const glm::vec4& v)
        : r(v.r), g(v.g), b(v.b), a(v.a) {}

    /**
     * @brief Construct from glm::vec3 (alpha defaults to 1.0)
     * @param v Vector with RGB components
     */
    constexpr Color(const glm::vec3& v)
        : r(v.r), g(v.g), b(v.b), a(1.0f) {}

    // =========================================================================
    // Conversion Operators
    // =========================================================================

    /**
     * @brief Implicit conversion to glm::vec4
     *
     * Enables seamless use with existing vivid APIs:
     * @code
     * particles.color(Color::Coral);  // Implicitly converts
     * @endcode
     */
    constexpr operator glm::vec4() const {
        return glm::vec4(r, g, b, a);
    }

    /**
     * @brief Explicit conversion to glm::vec3 (discards alpha)
     */
    constexpr explicit operator glm::vec3() const {
        return glm::vec3(r, g, b);
    }

    // =========================================================================
    // Factory Methods
    // =========================================================================

    /**
     * @brief Create color from HSV values
     * @param h Hue (0-1, wraps)
     * @param s Saturation (0-1)
     * @param v Value/brightness (0-1)
     * @param a Alpha (default 1.0)
     * @return Color in RGB space
     *
     * @par Example
     * @code
     * Color rainbow = Color::fromHSV(time * 0.1f, 0.8f, 1.0f);
     * @endcode
     */
    static Color fromHSV(float h, float s, float v, float a = 1.0f) {
        // Wrap hue to 0-1 range
        h = h - std::floor(h);

        float c = v * s;
        float x = c * (1.0f - std::abs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
        float m = v - c;

        float ri, gi, bi;
        if (h < 1.0f/6.0f)      { ri = c; gi = x; bi = 0; }
        else if (h < 2.0f/6.0f) { ri = x; gi = c; bi = 0; }
        else if (h < 3.0f/6.0f) { ri = 0; gi = c; bi = x; }
        else if (h < 4.0f/6.0f) { ri = 0; gi = x; bi = c; }
        else if (h < 5.0f/6.0f) { ri = x; gi = 0; bi = c; }
        else                    { ri = c; gi = 0; bi = x; }

        return Color(ri + m, gi + m, bi + m, a);
    }

    /**
     * @brief Create color from hex integer (0xRRGGBB or 0xRRGGBBAA)
     * @param hex Hex value
     * @return Color
     *
     * @par Example
     * @code
     * Color coral = Color::fromHex(0xFF7F50);
     * Color semiTransparent = Color::fromHex(0xFF7F5080);
     * @endcode
     */
    static constexpr Color fromHex(uint32_t hex) {
        // Detect if alpha is included (value > 0xFFFFFF)
        if (hex > 0xFFFFFF) {
            // 0xRRGGBBAA format
            return Color(
                ((hex >> 24) & 0xFF) / 255.0f,
                ((hex >> 16) & 0xFF) / 255.0f,
                ((hex >> 8) & 0xFF) / 255.0f,
                (hex & 0xFF) / 255.0f
            );
        } else {
            // 0xRRGGBB format (opaque)
            return Color(
                ((hex >> 16) & 0xFF) / 255.0f,
                ((hex >> 8) & 0xFF) / 255.0f,
                (hex & 0xFF) / 255.0f,
                1.0f
            );
        }
    }

    /**
     * @brief Create color from hex string
     * @param hex Hex string ("#RRGGBB", "#RRGGBBAA", "RRGGBB", "RRGGBBAA")
     * @return Color (returns magenta on parse error for visibility)
     *
     * @par Example
     * @code
     * Color coral = Color::fromHex("#FF7F50");
     * Color withAlpha = Color::fromHex("#FF7F5080");
     * @endcode
     */
    static Color fromHex(const std::string& hex) {
        std::string s = hex;

        // Strip leading #
        if (!s.empty() && s[0] == '#') {
            s = s.substr(1);
        }

        // Parse based on length
        try {
            if (s.length() == 6) {
                // RRGGBB
                uint32_t val = static_cast<uint32_t>(std::stoul(s, nullptr, 16));
                return fromHex(val);
            } else if (s.length() == 8) {
                // RRGGBBAA
                uint32_t val = static_cast<uint32_t>(std::stoul(s, nullptr, 16));
                return fromHex(val);
            }
        } catch (...) {
            // Fall through to error color
        }

        // Parse error - return visible magenta
        return Color(1.0f, 0.0f, 1.0f, 1.0f);
    }

    /**
     * @brief Create color from 0-255 byte values
     * @param r Red (0-255)
     * @param g Green (0-255)
     * @param b Blue (0-255)
     * @param a Alpha (0-255, default 255)
     * @return Color
     *
     * @par Example
     * @code
     * Color c = Color::fromBytes(255, 127, 80);  // Coral
     * @endcode
     */
    static constexpr Color fromBytes(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return Color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    }

    // =========================================================================
    // HSV Conversion (to HSV)
    // =========================================================================

    /**
     * @brief Convert to HSV representation
     * @return glm::vec3 with (hue, saturation, value) in 0-1 range
     */
    glm::vec3 toHSV() const {
        float maxC = std::max({r, g, b});
        float minC = std::min({r, g, b});
        float delta = maxC - minC;

        float h = 0.0f;
        float s = (maxC > 0.0f) ? (delta / maxC) : 0.0f;
        float v = maxC;

        if (delta > 0.0f) {
            if (maxC == r) {
                h = (g - b) / delta;
                if (h < 0.0f) h += 6.0f;
            } else if (maxC == g) {
                h = 2.0f + (b - r) / delta;
            } else {
                h = 4.0f + (r - g) / delta;
            }
            h /= 6.0f;
        }

        return glm::vec3(h, s, v);
    }

    /**
     * @brief Get hue component (0-1)
     */
    float hue() const { return toHSV().x; }

    /**
     * @brief Get saturation component (0-1)
     */
    float saturation() const { return toHSV().y; }

    /**
     * @brief Get value/brightness component (0-1)
     */
    float value() const { return toHSV().z; }

    // =========================================================================
    // Color Manipulation
    // =========================================================================

    /**
     * @brief Return color with modified alpha
     * @param newAlpha New alpha value (0-1)
     * @return New Color with changed alpha
     *
     * @par Example
     * @code
     * particles.color(Color::Coral.withAlpha(0.5f));
     * @endcode
     */
    constexpr Color withAlpha(float newAlpha) const {
        return Color(r, g, b, newAlpha);
    }

    /**
     * @brief Return color with hue shifted
     * @param amount Amount to shift hue (0-1 wraps)
     * @return New Color with shifted hue
     */
    Color withHueShift(float amount) const {
        glm::vec3 hsv = toHSV();
        return fromHSV(hsv.x + amount, hsv.y, hsv.z, a);
    }

    /**
     * @brief Return color with adjusted saturation
     * @param factor Multiplier for saturation (0 = grayscale, 1 = unchanged)
     * @return New Color with adjusted saturation
     */
    Color withSaturation(float factor) const {
        glm::vec3 hsv = toHSV();
        return fromHSV(hsv.x, std::clamp(hsv.y * factor, 0.0f, 1.0f), hsv.z, a);
    }

    /**
     * @brief Return color with adjusted brightness
     * @param factor Multiplier for value (0 = black, 1 = unchanged)
     * @return New Color with adjusted brightness
     */
    Color withBrightness(float factor) const {
        glm::vec3 hsv = toHSV();
        return fromHSV(hsv.x, hsv.y, std::clamp(hsv.z * factor, 0.0f, 1.0f), a);
    }

    /**
     * @brief Return lightened color
     * @param amount Amount to lighten (0-1, default 0.2)
     * @return Lighter color
     */
    Color lighter(float amount = 0.2f) const {
        return Color(
            std::min(1.0f, r + amount),
            std::min(1.0f, g + amount),
            std::min(1.0f, b + amount),
            a
        );
    }

    /**
     * @brief Return darkened color
     * @param amount Amount to darken (0-1, default 0.2)
     * @return Darker color
     */
    Color darker(float amount = 0.2f) const {
        return Color(
            std::max(0.0f, r - amount),
            std::max(0.0f, g - amount),
            std::max(0.0f, b - amount),
            a
        );
    }

    /**
     * @brief Return inverted color (1-r, 1-g, 1-b)
     * @return Inverted color (alpha unchanged)
     */
    constexpr Color inverted() const {
        return Color(1.0f - r, 1.0f - g, 1.0f - b, a);
    }

    /**
     * @brief Return grayscale version (luminance)
     * @return Grayscale color
     */
    constexpr Color grayscale() const {
        float lum = 0.299f * r + 0.587f * g + 0.114f * b;
        return Color(lum, lum, lum, a);
    }

    /**
     * @brief Get luminance (perceived brightness)
     * @return Luminance value (0-1)
     */
    constexpr float luminance() const {
        return 0.299f * r + 0.587f * g + 0.114f * b;
    }

    // =========================================================================
    // Blending / Interpolation
    // =========================================================================

    /**
     * @brief Linear interpolation between two colors
     * @param other Target color
     * @param t Interpolation factor (0 = this, 1 = other)
     * @return Interpolated color
     *
     * @par Example
     * @code
     * Color gradient = Color::Red.lerp(Color::Blue, t);
     * @endcode
     */
    Color lerp(const Color& other, float t) const {
        return Color(
            r + (other.r - r) * t,
            g + (other.g - g) * t,
            b + (other.b - b) * t,
            a + (other.a - a) * t
        );
    }

    /**
     * @brief Mix two colors (alias for lerp with t=0.5)
     * @param other Color to mix with
     * @return Average of both colors
     */
    Color mix(const Color& other) const {
        return lerp(other, 0.5f);
    }

    /**
     * @brief Mix with weight
     * @param other Color to mix with
     * @param weight Weight of other color (0-1)
     * @return Blended color
     */
    Color mix(const Color& other, float weight) const {
        return lerp(other, weight);
    }

    // =========================================================================
    // Data Access
    // =========================================================================

    /**
     * @brief Get pointer to RGBA data (for GPU upload)
     * @return Pointer to contiguous float[4] array
     */
    const float* data() const { return &r; }

    /**
     * @brief Get mutable pointer to RGBA data
     * @return Pointer to contiguous float[4] array
     */
    float* data() { return &r; }

    /**
     * @brief Convert to hex integer (0xRRGGBB)
     * @return Hex value (alpha discarded)
     */
    uint32_t toHex() const {
        uint8_t ri = static_cast<uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
        uint8_t gi = static_cast<uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
        uint8_t bi = static_cast<uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
        return (static_cast<uint32_t>(ri) << 16) | (static_cast<uint32_t>(gi) << 8) | bi;
    }

    /**
     * @brief Convert to hex integer with alpha (0xRRGGBBAA)
     * @return Hex value including alpha
     */
    uint32_t toHexAlpha() const {
        uint8_t ri = static_cast<uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
        uint8_t gi = static_cast<uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
        uint8_t bi = static_cast<uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
        uint8_t ai = static_cast<uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
        return (static_cast<uint32_t>(ri) << 24) | (static_cast<uint32_t>(gi) << 16) |
               (static_cast<uint32_t>(bi) << 8) | ai;
    }

    // =========================================================================
    // Comparison Operators
    // =========================================================================

    constexpr bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }

    constexpr bool operator!=(const Color& other) const {
        return !(*this == other);
    }

    // =========================================================================
    // Static Color Constants - Declarations
    // =========================================================================
    // CSS/X11 Named Colors - defined after class is complete

    // --- Reds ---
    static const Color IndianRed;
    static const Color LightCoral;
    static const Color Salmon;
    static const Color DarkSalmon;
    static const Color LightSalmon;
    static const Color Crimson;
    static const Color Red;
    static const Color FireBrick;
    static const Color DarkRed;

    // --- Pinks ---
    static const Color Pink;
    static const Color LightPink;
    static const Color HotPink;
    static const Color DeepPink;
    static const Color MediumVioletRed;
    static const Color PaleVioletRed;

    // --- Oranges ---
    static const Color Coral;
    static const Color Tomato;
    static const Color OrangeRed;
    static const Color DarkOrange;
    static const Color Orange;

    // --- Yellows ---
    static const Color Gold;
    static const Color Yellow;
    static const Color LightYellow;
    static const Color LemonChiffon;
    static const Color LightGoldenrodYellow;
    static const Color PapayaWhip;
    static const Color Moccasin;
    static const Color PeachPuff;
    static const Color PaleGoldenrod;
    static const Color Khaki;
    static const Color DarkKhaki;

    // --- Purples ---
    static const Color Lavender;
    static const Color Thistle;
    static const Color Plum;
    static const Color Violet;
    static const Color Orchid;
    static const Color Fuchsia;
    static const Color Magenta;
    static const Color MediumOrchid;
    static const Color MediumPurple;
    static const Color RebeccaPurple;
    static const Color BlueViolet;
    static const Color DarkViolet;
    static const Color DarkOrchid;
    static const Color DarkMagenta;
    static const Color Purple;
    static const Color Indigo;
    static const Color SlateBlue;
    static const Color DarkSlateBlue;
    static const Color MediumSlateBlue;

    // --- Greens ---
    static const Color GreenYellow;
    static const Color Chartreuse;
    static const Color LawnGreen;
    static const Color Lime;
    static const Color LimeGreen;
    static const Color PaleGreen;
    static const Color LightGreen;
    static const Color MediumSpringGreen;
    static const Color SpringGreen;
    static const Color MediumSeaGreen;
    static const Color SeaGreen;
    static const Color ForestGreen;
    static const Color Green;
    static const Color DarkGreen;
    static const Color YellowGreen;
    static const Color OliveDrab;
    static const Color Olive;
    static const Color DarkOliveGreen;
    static const Color MediumAquamarine;
    static const Color DarkSeaGreen;
    static const Color LightSeaGreen;
    static const Color DarkCyan;
    static const Color Teal;

    // --- Blues/Cyans ---
    static const Color Aqua;
    static const Color Cyan;
    static const Color LightCyan;
    static const Color PaleTurquoise;
    static const Color Aquamarine;
    static const Color Turquoise;
    static const Color MediumTurquoise;
    static const Color DarkTurquoise;
    static const Color CadetBlue;
    static const Color SteelBlue;
    static const Color LightSteelBlue;
    static const Color PowderBlue;
    static const Color LightBlue;
    static const Color SkyBlue;
    static const Color LightSkyBlue;
    static const Color DeepSkyBlue;
    static const Color DodgerBlue;
    static const Color CornflowerBlue;
    static const Color RoyalBlue;
    static const Color Blue;
    static const Color MediumBlue;
    static const Color DarkBlue;
    static const Color Navy;
    static const Color MidnightBlue;

    // --- Browns ---
    static const Color Cornsilk;
    static const Color BlanchedAlmond;
    static const Color Bisque;
    static const Color NavajoWhite;
    static const Color Wheat;
    static const Color BurlyWood;
    static const Color Tan;
    static const Color RosyBrown;
    static const Color SandyBrown;
    static const Color Goldenrod;
    static const Color DarkGoldenrod;
    static const Color Peru;
    static const Color Chocolate;
    static const Color SaddleBrown;
    static const Color Sienna;
    static const Color Brown;
    static const Color Maroon;

    // --- Whites ---
    static const Color White;
    static const Color Snow;
    static const Color Honeydew;
    static const Color MintCream;
    static const Color Azure;
    static const Color AliceBlue;
    static const Color GhostWhite;
    static const Color WhiteSmoke;
    static const Color Seashell;
    static const Color Beige;
    static const Color OldLace;
    static const Color FloralWhite;
    static const Color Ivory;
    static const Color AntiqueWhite;
    static const Color Linen;
    static const Color LavenderBlush;
    static const Color MistyRose;

    // --- Grays/Blacks ---
    static const Color Gainsboro;
    static const Color LightGray;
    static const Color Silver;
    static const Color DarkGray;
    static const Color Gray;
    static const Color DimGray;
    static const Color LightSlateGray;
    static const Color SlateGray;
    static const Color DarkSlateGray;
    static const Color Black;

    // --- Transparent ---
    static const Color Transparent;
};

// =========================================================================
// Static Color Constant Definitions - CSS/X11 Named Colors
// =========================================================================

// --- Reds ---
inline constexpr Color Color::IndianRed        {0.804f, 0.361f, 0.361f};
inline constexpr Color Color::LightCoral       {0.941f, 0.502f, 0.502f};
inline constexpr Color Color::Salmon           {0.980f, 0.502f, 0.447f};
inline constexpr Color Color::DarkSalmon       {0.914f, 0.588f, 0.478f};
inline constexpr Color Color::LightSalmon      {1.000f, 0.627f, 0.478f};
inline constexpr Color Color::Crimson          {0.863f, 0.078f, 0.235f};
inline constexpr Color Color::Red              {1.000f, 0.000f, 0.000f};
inline constexpr Color Color::FireBrick        {0.698f, 0.133f, 0.133f};
inline constexpr Color Color::DarkRed          {0.545f, 0.000f, 0.000f};

// --- Pinks ---
inline constexpr Color Color::Pink             {1.000f, 0.753f, 0.796f};
inline constexpr Color Color::LightPink        {1.000f, 0.714f, 0.757f};
inline constexpr Color Color::HotPink          {1.000f, 0.412f, 0.706f};
inline constexpr Color Color::DeepPink         {1.000f, 0.078f, 0.576f};
inline constexpr Color Color::MediumVioletRed  {0.780f, 0.082f, 0.522f};
inline constexpr Color Color::PaleVioletRed    {0.859f, 0.439f, 0.576f};

// --- Oranges ---
inline constexpr Color Color::Coral            {1.000f, 0.498f, 0.314f};
inline constexpr Color Color::Tomato           {1.000f, 0.388f, 0.278f};
inline constexpr Color Color::OrangeRed        {1.000f, 0.271f, 0.000f};
inline constexpr Color Color::DarkOrange       {1.000f, 0.549f, 0.000f};
inline constexpr Color Color::Orange           {1.000f, 0.647f, 0.000f};

// --- Yellows ---
inline constexpr Color Color::Gold             {1.000f, 0.843f, 0.000f};
inline constexpr Color Color::Yellow           {1.000f, 1.000f, 0.000f};
inline constexpr Color Color::LightYellow      {1.000f, 1.000f, 0.878f};
inline constexpr Color Color::LemonChiffon     {1.000f, 0.980f, 0.804f};
inline constexpr Color Color::LightGoldenrodYellow {0.980f, 0.980f, 0.824f};
inline constexpr Color Color::PapayaWhip       {1.000f, 0.937f, 0.835f};
inline constexpr Color Color::Moccasin         {1.000f, 0.894f, 0.710f};
inline constexpr Color Color::PeachPuff        {1.000f, 0.855f, 0.725f};
inline constexpr Color Color::PaleGoldenrod    {0.933f, 0.910f, 0.667f};
inline constexpr Color Color::Khaki            {0.941f, 0.902f, 0.549f};
inline constexpr Color Color::DarkKhaki        {0.741f, 0.718f, 0.420f};

// --- Purples ---
inline constexpr Color Color::Lavender         {0.902f, 0.902f, 0.980f};
inline constexpr Color Color::Thistle          {0.847f, 0.749f, 0.847f};
inline constexpr Color Color::Plum             {0.867f, 0.627f, 0.867f};
inline constexpr Color Color::Violet           {0.933f, 0.510f, 0.933f};
inline constexpr Color Color::Orchid           {0.855f, 0.439f, 0.839f};
inline constexpr Color Color::Fuchsia          {1.000f, 0.000f, 1.000f};
inline constexpr Color Color::Magenta          {1.000f, 0.000f, 1.000f};
inline constexpr Color Color::MediumOrchid     {0.729f, 0.333f, 0.827f};
inline constexpr Color Color::MediumPurple     {0.576f, 0.439f, 0.859f};
inline constexpr Color Color::RebeccaPurple    {0.400f, 0.200f, 0.600f};
inline constexpr Color Color::BlueViolet       {0.541f, 0.169f, 0.886f};
inline constexpr Color Color::DarkViolet       {0.580f, 0.000f, 0.827f};
inline constexpr Color Color::DarkOrchid       {0.600f, 0.196f, 0.800f};
inline constexpr Color Color::DarkMagenta      {0.545f, 0.000f, 0.545f};
inline constexpr Color Color::Purple           {0.502f, 0.000f, 0.502f};
inline constexpr Color Color::Indigo           {0.294f, 0.000f, 0.510f};
inline constexpr Color Color::SlateBlue        {0.416f, 0.353f, 0.804f};
inline constexpr Color Color::DarkSlateBlue    {0.282f, 0.239f, 0.545f};
inline constexpr Color Color::MediumSlateBlue  {0.482f, 0.408f, 0.933f};

// --- Greens ---
inline constexpr Color Color::GreenYellow      {0.678f, 1.000f, 0.184f};
inline constexpr Color Color::Chartreuse       {0.498f, 1.000f, 0.000f};
inline constexpr Color Color::LawnGreen        {0.486f, 0.988f, 0.000f};
inline constexpr Color Color::Lime             {0.000f, 1.000f, 0.000f};
inline constexpr Color Color::LimeGreen        {0.196f, 0.804f, 0.196f};
inline constexpr Color Color::PaleGreen        {0.596f, 0.984f, 0.596f};
inline constexpr Color Color::LightGreen       {0.565f, 0.933f, 0.565f};
inline constexpr Color Color::MediumSpringGreen{0.000f, 0.980f, 0.604f};
inline constexpr Color Color::SpringGreen      {0.000f, 1.000f, 0.498f};
inline constexpr Color Color::MediumSeaGreen   {0.235f, 0.702f, 0.443f};
inline constexpr Color Color::SeaGreen         {0.180f, 0.545f, 0.341f};
inline constexpr Color Color::ForestGreen      {0.133f, 0.545f, 0.133f};
inline constexpr Color Color::Green            {0.000f, 0.502f, 0.000f};
inline constexpr Color Color::DarkGreen        {0.000f, 0.392f, 0.000f};
inline constexpr Color Color::YellowGreen      {0.604f, 0.804f, 0.196f};
inline constexpr Color Color::OliveDrab        {0.420f, 0.557f, 0.137f};
inline constexpr Color Color::Olive            {0.502f, 0.502f, 0.000f};
inline constexpr Color Color::DarkOliveGreen   {0.333f, 0.420f, 0.184f};
inline constexpr Color Color::MediumAquamarine {0.400f, 0.804f, 0.667f};
inline constexpr Color Color::DarkSeaGreen     {0.561f, 0.737f, 0.561f};
inline constexpr Color Color::LightSeaGreen    {0.125f, 0.698f, 0.667f};
inline constexpr Color Color::DarkCyan         {0.000f, 0.545f, 0.545f};
inline constexpr Color Color::Teal             {0.000f, 0.502f, 0.502f};

// --- Blues/Cyans ---
inline constexpr Color Color::Aqua             {0.000f, 1.000f, 1.000f};
inline constexpr Color Color::Cyan             {0.000f, 1.000f, 1.000f};
inline constexpr Color Color::LightCyan        {0.878f, 1.000f, 1.000f};
inline constexpr Color Color::PaleTurquoise    {0.686f, 0.933f, 0.933f};
inline constexpr Color Color::Aquamarine       {0.498f, 1.000f, 0.831f};
inline constexpr Color Color::Turquoise        {0.251f, 0.878f, 0.816f};
inline constexpr Color Color::MediumTurquoise  {0.282f, 0.820f, 0.800f};
inline constexpr Color Color::DarkTurquoise    {0.000f, 0.808f, 0.820f};
inline constexpr Color Color::CadetBlue        {0.373f, 0.620f, 0.627f};
inline constexpr Color Color::SteelBlue        {0.275f, 0.510f, 0.706f};
inline constexpr Color Color::LightSteelBlue   {0.690f, 0.769f, 0.871f};
inline constexpr Color Color::PowderBlue       {0.690f, 0.878f, 0.902f};
inline constexpr Color Color::LightBlue        {0.678f, 0.847f, 0.902f};
inline constexpr Color Color::SkyBlue          {0.529f, 0.808f, 0.922f};
inline constexpr Color Color::LightSkyBlue     {0.529f, 0.808f, 0.980f};
inline constexpr Color Color::DeepSkyBlue      {0.000f, 0.749f, 1.000f};
inline constexpr Color Color::DodgerBlue       {0.118f, 0.565f, 1.000f};
inline constexpr Color Color::CornflowerBlue   {0.392f, 0.584f, 0.929f};
inline constexpr Color Color::RoyalBlue        {0.255f, 0.412f, 0.882f};
inline constexpr Color Color::Blue             {0.000f, 0.000f, 1.000f};
inline constexpr Color Color::MediumBlue       {0.000f, 0.000f, 0.804f};
inline constexpr Color Color::DarkBlue         {0.000f, 0.000f, 0.545f};
inline constexpr Color Color::Navy             {0.000f, 0.000f, 0.502f};
inline constexpr Color Color::MidnightBlue     {0.098f, 0.098f, 0.439f};

// --- Browns ---
inline constexpr Color Color::Cornsilk         {1.000f, 0.973f, 0.863f};
inline constexpr Color Color::BlanchedAlmond   {1.000f, 0.922f, 0.804f};
inline constexpr Color Color::Bisque           {1.000f, 0.894f, 0.769f};
inline constexpr Color Color::NavajoWhite      {1.000f, 0.871f, 0.678f};
inline constexpr Color Color::Wheat            {0.961f, 0.871f, 0.702f};
inline constexpr Color Color::BurlyWood        {0.871f, 0.722f, 0.529f};
inline constexpr Color Color::Tan              {0.824f, 0.706f, 0.549f};
inline constexpr Color Color::RosyBrown        {0.737f, 0.561f, 0.561f};
inline constexpr Color Color::SandyBrown       {0.957f, 0.643f, 0.376f};
inline constexpr Color Color::Goldenrod        {0.855f, 0.647f, 0.125f};
inline constexpr Color Color::DarkGoldenrod    {0.722f, 0.525f, 0.043f};
inline constexpr Color Color::Peru             {0.804f, 0.522f, 0.247f};
inline constexpr Color Color::Chocolate        {0.824f, 0.412f, 0.118f};
inline constexpr Color Color::SaddleBrown      {0.545f, 0.271f, 0.075f};
inline constexpr Color Color::Sienna           {0.627f, 0.322f, 0.176f};
inline constexpr Color Color::Brown            {0.647f, 0.165f, 0.165f};
inline constexpr Color Color::Maroon           {0.502f, 0.000f, 0.000f};

// --- Whites ---
inline constexpr Color Color::White            {1.000f, 1.000f, 1.000f};
inline constexpr Color Color::Snow             {1.000f, 0.980f, 0.980f};
inline constexpr Color Color::Honeydew         {0.941f, 1.000f, 0.941f};
inline constexpr Color Color::MintCream        {0.961f, 1.000f, 0.980f};
inline constexpr Color Color::Azure            {0.941f, 1.000f, 1.000f};
inline constexpr Color Color::AliceBlue        {0.941f, 0.973f, 1.000f};
inline constexpr Color Color::GhostWhite       {0.973f, 0.973f, 1.000f};
inline constexpr Color Color::WhiteSmoke       {0.961f, 0.961f, 0.961f};
inline constexpr Color Color::Seashell         {1.000f, 0.961f, 0.933f};
inline constexpr Color Color::Beige            {0.961f, 0.961f, 0.863f};
inline constexpr Color Color::OldLace          {0.992f, 0.961f, 0.902f};
inline constexpr Color Color::FloralWhite      {1.000f, 0.980f, 0.941f};
inline constexpr Color Color::Ivory            {1.000f, 1.000f, 0.941f};
inline constexpr Color Color::AntiqueWhite     {0.980f, 0.922f, 0.843f};
inline constexpr Color Color::Linen            {0.980f, 0.941f, 0.902f};
inline constexpr Color Color::LavenderBlush    {1.000f, 0.941f, 0.961f};
inline constexpr Color Color::MistyRose        {1.000f, 0.894f, 0.882f};

// --- Grays/Blacks ---
inline constexpr Color Color::Gainsboro        {0.863f, 0.863f, 0.863f};
inline constexpr Color Color::LightGray        {0.827f, 0.827f, 0.827f};
inline constexpr Color Color::Silver           {0.753f, 0.753f, 0.753f};
inline constexpr Color Color::DarkGray         {0.663f, 0.663f, 0.663f};
inline constexpr Color Color::Gray             {0.502f, 0.502f, 0.502f};
inline constexpr Color Color::DimGray          {0.412f, 0.412f, 0.412f};
inline constexpr Color Color::LightSlateGray   {0.467f, 0.533f, 0.600f};
inline constexpr Color Color::SlateGray        {0.439f, 0.502f, 0.565f};
inline constexpr Color Color::DarkSlateGray    {0.184f, 0.310f, 0.310f};
inline constexpr Color Color::Black            {0.000f, 0.000f, 0.000f};

// --- Transparent ---
inline constexpr Color Color::Transparent      {0.000f, 0.000f, 0.000f, 0.000f};

// =========================================================================
// Free Function for Static Lerp
// =========================================================================

/**
 * @brief Linear interpolation between two colors (free function)
 * @param a Start color
 * @param b End color
 * @param t Interpolation factor (0-1)
 * @return Interpolated color
 */
inline Color lerp(const Color& a, const Color& b, float t) {
    return a.lerp(b, t);
}

} // namespace vivid

// =========================================================================
// ColorParam Integration (requires color.h to be included after param.h)
// =========================================================================
#include <vivid/param.h>

namespace vivid {

inline void ColorParam::set(const Color& c) {
    m_r = c.r;
    m_g = c.g;
    m_b = c.b;
    m_a = c.a;
}

inline ColorParam::operator Color() const {
    return Color(m_r, m_g, m_b, m_a);
}

} // namespace vivid
