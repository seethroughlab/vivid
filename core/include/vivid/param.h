#pragma once

// Vivid - Parameter Wrappers
// Combine parameter values with metadata (name, range) to avoid redundancy

#include <vivid/operator.h>
#include <string>

namespace vivid {

// Type traits for mapping C++ types to ParamType
template<typename T> struct ParamTypeFor;
template<> struct ParamTypeFor<float> { static constexpr ParamType value = ParamType::Float; };
template<> struct ParamTypeFor<int>   { static constexpr ParamType value = ParamType::Int; };
template<> struct ParamTypeFor<bool>  { static constexpr ParamType value = ParamType::Bool; };

// Scalar parameter (float, int, bool)
template<typename T>
class Param {
public:
    constexpr Param(const char* name, T defaultVal, T minVal = T{}, T maxVal = T{1})
        : m_name(name), m_value(defaultVal), m_min(minVal), m_max(maxVal) {}

    // Implicit conversion - use param like a regular value
    operator T() const { return m_value; }
    T get() const { return m_value; }

    // Assignment for fluent setters
    Param& operator=(T v) { m_value = v; return *this; }

    // Metadata access
    const char* name() const { return m_name; }
    T min() const { return m_min; }
    T max() const { return m_max; }

    // Generate ParamDecl for params() method
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

// Vec2 parameter
class Vec2Param {
public:
    constexpr Vec2Param(const char* name, float x, float y, float minVal = -1.0f, float maxVal = 1.0f)
        : m_name(name), m_x(x), m_y(y), m_min(minVal), m_max(maxVal) {}

    float x() const { return m_x; }
    float y() const { return m_y; }
    void set(float x, float y) { m_x = x; m_y = y; }

    const char* name() const { return m_name; }

    ParamDecl decl() const {
        return {m_name, ParamType::Vec2, m_min, m_max, {m_x, m_y}};
    }

private:
    const char* m_name;
    float m_x, m_y;
    float m_min, m_max;
};

// Color parameter (RGBA)
class ColorParam {
public:
    constexpr ColorParam(const char* name, float r, float g, float b, float a = 1.0f)
        : m_name(name), m_r(r), m_g(g), m_b(b), m_a(a) {}

    float r() const { return m_r; }
    float g() const { return m_g; }
    float b() const { return m_b; }
    float a() const { return m_a; }
    const float* data() const { return &m_r; }
    void set(float r, float g, float b, float a = 1.0f) { m_r = r; m_g = g; m_b = b; m_a = a; }

    const char* name() const { return m_name; }

    ParamDecl decl() const {
        return {m_name, ParamType::Color, 0.0f, 1.0f, {m_r, m_g, m_b, m_a}};
    }

private:
    const char* m_name;
    float m_r, m_g, m_b, m_a;
};

} // namespace vivid
