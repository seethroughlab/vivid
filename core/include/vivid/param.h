#pragma once

/**
 * @file param.h
 * @brief Parameter wrapper classes for operators
 *
 * These wrappers combine parameter values with metadata (name, range, default)
 * to reduce redundancy. Parameters automatically generate ParamDecl for
 * introspection and UI.
 */

#include <vivid/operator.h>
#include <string>

namespace vivid {

// Forward declaration for Color integration
class Color;

/**
 * @brief Type traits mapping C++ types to ParamType enum
 * @tparam T C++ type
 */
template<typename T> struct ParamTypeFor;
template<> struct ParamTypeFor<float> { static constexpr ParamType value = ParamType::Float; };
template<> struct ParamTypeFor<int>   { static constexpr ParamType value = ParamType::Int; };
template<> struct ParamTypeFor<bool>  { static constexpr ParamType value = ParamType::Bool; };

/**
 * @brief Scalar parameter wrapper (float, int, bool)
 * @tparam T Value type (float, int, or bool)
 *
 * Combines a value with metadata. Supports implicit conversion so it can
 * be used like a regular value.
 *
 * @par Example
 * @code
 * class MyEffect : public TextureOperator {
 *     Param<float> m_intensity{"intensity", 1.0f, 0.0f, 2.0f};
 *
 *     MyEffect& intensity(float v) { m_intensity = v; return *this; }
 *
 *     std::vector<ParamDecl> params() override {
 *         return { m_intensity.decl() };
 *     }
 * };
 * @endcode
 */
template<typename T>
class Param {
public:
    /**
     * @brief Construct a parameter
     * @param name Display name for UI
     * @param defaultVal Default value
     * @param minVal Minimum allowed value
     * @param maxVal Maximum allowed value
     */
    constexpr Param(const char* name, T defaultVal, T minVal = T{}, T maxVal = T{1})
        : m_name(name), m_value(defaultVal), m_min(minVal), m_max(maxVal) {}

    /// @brief Implicit conversion to value type
    operator T() const { return m_value; }

    /// @brief Get value explicitly
    T get() const { return m_value; }

    /// @brief Assignment for fluent setters
    Param& operator=(T v) { m_value = v; return *this; }

    /// @brief Get parameter name
    const char* name() const { return m_name; }

    /// @brief Get minimum value
    T min() const { return m_min; }

    /// @brief Get maximum value
    T max() const { return m_max; }

    /**
     * @brief Generate ParamDecl for introspection
     * @return ParamDecl with name, type, range, and default
     */
    ParamDecl decl() const {
        return {m_name, ParamTypeFor<T>::value,
                static_cast<float>(m_min), static_cast<float>(m_max),
                {static_cast<float>(m_value)}};
    }

private:
    const char* m_name;
    T m_value;
    T m_min, m_max;
};

/**
 * @brief 2D vector parameter wrapper
 *
 * @par Example
 * @code
 * Vec2Param m_offset{"offset", 0.0f, 0.0f, -1.0f, 1.0f};
 *
 * MyEffect& offset(float x, float y) { m_offset.set(x, y); return *this; }
 * @endcode
 */
class Vec2Param {
public:
    /**
     * @brief Construct a Vec2 parameter
     * @param name Display name
     * @param x Default X value
     * @param y Default Y value
     * @param minVal Minimum for both components
     * @param maxVal Maximum for both components
     */
    constexpr Vec2Param(const char* name, float x, float y, float minVal = -1.0f, float maxVal = 1.0f)
        : m_name(name), m_x(x), m_y(y), m_min(minVal), m_max(maxVal) {}

    /// @brief Get X component
    float x() const { return m_x; }

    /// @brief Get Y component
    float y() const { return m_y; }

    /**
     * @brief Set both components
     * @param x New X value
     * @param y New Y value
     */
    void set(float x, float y) { m_x = x; m_y = y; }

    /// @brief Get parameter name
    const char* name() const { return m_name; }

    /// @brief Generate ParamDecl
    ParamDecl decl() const {
        return {m_name, ParamType::Vec2, m_min, m_max, {m_x, m_y}};
    }

private:
    const char* m_name;
    float m_x, m_y;
    float m_min, m_max;
};

/**
 * @brief 3D vector parameter wrapper
 *
 * @par Example
 * @code
 * Vec3Param m_offset{"offset", 0.0f, 0.0f, 0.0f, -10.0f, 10.0f};
 *
 * MyEffect& offset(float x, float y, float z) { m_offset.set(x, y, z); return *this; }
 * @endcode
 */
class Vec3Param {
public:
    /**
     * @brief Construct a Vec3 parameter
     * @param name Display name
     * @param x Default X value
     * @param y Default Y value
     * @param z Default Z value
     * @param minVal Minimum for all components
     * @param maxVal Maximum for all components
     */
    constexpr Vec3Param(const char* name, float x, float y, float z, float minVal = -1.0f, float maxVal = 1.0f)
        : m_name(name), m_x(x), m_y(y), m_z(z), m_min(minVal), m_max(maxVal) {}

    /// @brief Get X component
    float x() const { return m_x; }

    /// @brief Get Y component
    float y() const { return m_y; }

    /// @brief Get Z component
    float z() const { return m_z; }

    /**
     * @brief Set all components
     * @param x New X value
     * @param y New Y value
     * @param z New Z value
     */
    void set(float x, float y, float z) { m_x = x; m_y = y; m_z = z; }

    /// @brief Get parameter name
    const char* name() const { return m_name; }

    /// @brief Generate ParamDecl
    ParamDecl decl() const {
        return {m_name, ParamType::Vec3, m_min, m_max, {m_x, m_y, m_z}};
    }

private:
    const char* m_name;
    float m_x, m_y, m_z;
    float m_min, m_max;
};

/**
 * @brief RGBA color parameter wrapper
 *
 * @par Example
 * @code
 * ColorParam m_color{"color", 1.0f, 1.0f, 1.0f, 1.0f};
 *
 * MyEffect& color(float r, float g, float b, float a = 1.0f) {
 *     m_color.set(r, g, b, a);
 *     return *this;
 * }
 * @endcode
 */
class ColorParam {
public:
    /**
     * @brief Construct a color parameter
     * @param name Display name
     * @param r Red (0-1)
     * @param g Green (0-1)
     * @param b Blue (0-1)
     * @param a Alpha (0-1, default 1)
     */
    constexpr ColorParam(const char* name, float r, float g, float b, float a = 1.0f)
        : m_name(name), m_r(r), m_g(g), m_b(b), m_a(a) {}

    /// @brief Get red component
    float r() const { return m_r; }

    /// @brief Get green component
    float g() const { return m_g; }

    /// @brief Get blue component
    float b() const { return m_b; }

    /// @brief Get alpha component
    float a() const { return m_a; }

    /// @brief Get pointer to RGBA array (for GPU upload)
    const float* data() const { return &m_r; }

    /**
     * @brief Set all components
     * @param r Red
     * @param g Green
     * @param b Blue
     * @param a Alpha (default 1)
     */
    void set(float r, float g, float b, float a = 1.0f) { m_r = r; m_g = g; m_b = b; m_a = a; }

    /**
     * @brief Set from Color
     * @param c Color to copy from
     */
    void set(const Color& c);  // Defined in color.h after Color is complete

    /**
     * @brief Implicit conversion to Color
     */
    operator Color() const;  // Defined in color.h after Color is complete

    /// @brief Get parameter name
    const char* name() const { return m_name; }

    /// @brief Generate ParamDecl
    ParamDecl decl() const {
        return {m_name, ParamType::Color, 0.0f, 1.0f, {m_r, m_g, m_b, m_a}};
    }

private:
    const char* m_name;
    float m_r, m_g, m_b, m_a;
};

} // namespace vivid
