#pragma once

/**
 * @file param.h
 * @brief Parameter wrapper classes for operators
 *
 * These wrappers combine parameter values with metadata (name, range, default)
 * to reduce redundancy. Parameters automatically generate ParamDecl for
 * introspection and UI.
 *
 * Parameters support optional bindings for reactive updates:
 * @code
 * // Bind to normalized source (0-1) with output range
 * noise.scale.bind([&]() { return bands.bass(); }, 5.0f, 20.0f);
 *
 * // Bind direct (no range mapping)
 * noise.scale.bindDirect([&]() { return mouseX * 20.0f; });
 * @endcode
 */

#include <vivid/operator.h>
#include <string>
#include <functional>

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
 *     void setIntensity(float v) { m_intensity = v; }
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
    Param(const char* name, T defaultVal, T minVal = T{}, T maxVal = T{1})
        : m_name(name), m_value(defaultVal), m_min(minVal), m_max(maxVal) {}

    /// @brief Implicit conversion to value type (evaluates binding if set)
    operator T() const { return get(); }

    /// @brief Get value explicitly (evaluates binding if set)
    T get() const {
        if (m_binding) {
            return m_binding();
        }
        return m_value;
    }

    /// @brief Assignment operator (clears any binding)
    Param& operator=(T v) {
        m_value = v;
        m_binding = nullptr;
        return *this;
    }

    // -------------------------------------------------------------------------
    /// @name Binding
    /// @{

    /**
     * @brief Bind to a normalized source (0-1) with output range
     * @param source Function returning 0-1 normalized value
     * @param outMin Output minimum (when source returns 0)
     * @param outMax Output maximum (when source returns 1)
     *
     * Example:
     * @code
     * noise.scale.bind([&]() { return bands.bass(); }, 5.0f, 20.0f);
     * @endcode
     */
    void bind(std::function<float()> source, T outMin, T outMax) {
        m_binding = [source = std::move(source), outMin, outMax]() {
            float t = source();
            return static_cast<T>(outMin + t * (outMax - outMin));
        };
    }

    /**
     * @brief Bind directly to a source (no range mapping)
     * @param source Function returning the exact value
     *
     * Example:
     * @code
     * noise.scale.bindDirect([&]() { return mouseX * 20.0f; });
     * @endcode
     */
    void bindDirect(std::function<T()> source) {
        m_binding = std::move(source);
    }

    /**
     * @brief Clear any binding
     */
    void unbind() {
        m_binding = nullptr;
    }

    /**
     * @brief Check if parameter has a binding
     */
    bool isBound() const { return m_binding != nullptr; }

    /// @}
    // -------------------------------------------------------------------------

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
    std::function<T()> m_binding;
};

/**
 * @brief 2D vector parameter wrapper with binding support
 *
 * @par Example
 * @code
 * Vec2Param m_size{"size", 0.5f, 0.5f, 0.0f, 1.0f};
 *
 * // Uniform binding - both components scale together
 * m_size.bind([&]() { return bands.bass(); }, 0.1f, 0.5f);
 *
 * // Per-component binding
 * m_size.bindX([&]() { return bands.bass(); }, 0.1f, 0.5f);
 * m_size.bindY([&]() { return bands.mid(); }, 0.1f, 0.5f);
 * @endcode
 */
class Vec2Param {
public:
    Vec2Param(const char* name, float x, float y, float minVal = -1.0f, float maxVal = 1.0f)
        : m_name(name), m_x(x), m_y(y), m_min(minVal), m_max(maxVal) {}

    /// @brief Get X component (evaluates binding if set)
    float x() const {
        if (m_bindingX) return m_bindingX();
        if (m_bindingUniform) {
            float t = m_bindingUniform();
            return m_uniformMin + t * (m_uniformMax - m_uniformMin);
        }
        return m_x;
    }

    /// @brief Get Y component (evaluates binding if set)
    float y() const {
        if (m_bindingY) return m_bindingY();
        if (m_bindingUniform) {
            float t = m_bindingUniform();
            return m_uniformMin + t * (m_uniformMax - m_uniformMin);
        }
        return m_y;
    }

    /// @brief Set both components (clears bindings)
    void set(float x, float y) {
        m_x = x; m_y = y;
        m_bindingX = nullptr;
        m_bindingY = nullptr;
        m_bindingUniform = nullptr;
    }

    // -------------------------------------------------------------------------
    /// @name Binding
    /// @{

    /// @brief Bind both components uniformly to a 0-1 source
    void bind(std::function<float()> source, float outMin, float outMax) {
        m_bindingUniform = std::move(source);
        m_uniformMin = outMin;
        m_uniformMax = outMax;
        m_bindingX = nullptr;
        m_bindingY = nullptr;
    }

    /// @brief Bind X component to a 0-1 source with range
    void bindX(std::function<float()> source, float outMin, float outMax) {
        m_bindingX = [source = std::move(source), outMin, outMax]() {
            return outMin + source() * (outMax - outMin);
        };
    }

    /// @brief Bind Y component to a 0-1 source with range
    void bindY(std::function<float()> source, float outMin, float outMax) {
        m_bindingY = [source = std::move(source), outMin, outMax]() {
            return outMin + source() * (outMax - outMin);
        };
    }

    /// @brief Clear all bindings
    void unbind() {
        m_bindingX = nullptr;
        m_bindingY = nullptr;
        m_bindingUniform = nullptr;
    }

    /// @brief Check if any binding is set
    bool isBound() const {
        return m_bindingX || m_bindingY || m_bindingUniform;
    }

    /// @}
    // -------------------------------------------------------------------------

    const char* name() const { return m_name; }

    ParamDecl decl() const {
        return {m_name, ParamType::Vec2, m_min, m_max, {m_x, m_y}};
    }

private:
    const char* m_name;
    float m_x, m_y;
    float m_min, m_max;
    std::function<float()> m_bindingX;
    std::function<float()> m_bindingY;
    std::function<float()> m_bindingUniform;
    float m_uniformMin = 0.0f, m_uniformMax = 1.0f;
};

/**
 * @brief 3D vector parameter wrapper with binding support
 *
 * @par Example
 * @code
 * Vec3Param m_position{"position", 0.0f, 0.0f, 0.0f, -10.0f, 10.0f};
 *
 * // Bind individual components
 * m_position.bindX([&]() { return lfo.value(); }, -5.0f, 5.0f);
 * @endcode
 */
class Vec3Param {
public:
    Vec3Param(const char* name, float x, float y, float z, float minVal = -1.0f, float maxVal = 1.0f)
        : m_name(name), m_x(x), m_y(y), m_z(z), m_min(minVal), m_max(maxVal) {}

    /// @brief Get X component (evaluates binding if set)
    float x() const {
        if (m_bindingX) return m_bindingX();
        return m_x;
    }

    /// @brief Get Y component (evaluates binding if set)
    float y() const {
        if (m_bindingY) return m_bindingY();
        return m_y;
    }

    /// @brief Get Z component (evaluates binding if set)
    float z() const {
        if (m_bindingZ) return m_bindingZ();
        return m_z;
    }

    /// @brief Set all components (clears bindings)
    void set(float x, float y, float z) {
        m_x = x; m_y = y; m_z = z;
        m_bindingX = nullptr;
        m_bindingY = nullptr;
        m_bindingZ = nullptr;
    }

    // -------------------------------------------------------------------------
    /// @name Binding
    /// @{

    /// @brief Bind X component to a 0-1 source with range
    void bindX(std::function<float()> source, float outMin, float outMax) {
        m_bindingX = [source = std::move(source), outMin, outMax]() {
            return outMin + source() * (outMax - outMin);
        };
    }

    /// @brief Bind Y component to a 0-1 source with range
    void bindY(std::function<float()> source, float outMin, float outMax) {
        m_bindingY = [source = std::move(source), outMin, outMax]() {
            return outMin + source() * (outMax - outMin);
        };
    }

    /// @brief Bind Z component to a 0-1 source with range
    void bindZ(std::function<float()> source, float outMin, float outMax) {
        m_bindingZ = [source = std::move(source), outMin, outMax]() {
            return outMin + source() * (outMax - outMin);
        };
    }

    /// @brief Clear all bindings
    void unbind() {
        m_bindingX = nullptr;
        m_bindingY = nullptr;
        m_bindingZ = nullptr;
    }

    /// @brief Check if any binding is set
    bool isBound() const {
        return m_bindingX || m_bindingY || m_bindingZ;
    }

    /// @}
    // -------------------------------------------------------------------------

    const char* name() const { return m_name; }

    ParamDecl decl() const {
        return {m_name, ParamType::Vec3, m_min, m_max, {m_x, m_y, m_z}};
    }

private:
    const char* m_name;
    float m_x, m_y, m_z;
    float m_min, m_max;
    std::function<float()> m_bindingX;
    std::function<float()> m_bindingY;
    std::function<float()> m_bindingZ;
};

/**
 * @brief RGBA color parameter wrapper with binding support
 *
 * @par Example
 * @code
 * ColorParam m_color{"color", 1.0f, 1.0f, 1.0f, 1.0f};
 *
 * // Bind red channel to audio
 * m_color.bindR([&]() { return bands.bass(); }, 0.0f, 1.0f);
 *
 * // Bind alpha to fade
 * m_color.bindA([&]() { return levels.rms(); }, 0.5f, 1.0f);
 * @endcode
 */
class ColorParam {
public:
    ColorParam(const char* name, float r, float g, float b, float a = 1.0f)
        : m_name(name), m_r(r), m_g(g), m_b(b), m_a(a) {}

    /// @brief Get red component (evaluates binding if set)
    float r() const {
        if (m_bindingR) return m_bindingR();
        return m_r;
    }

    /// @brief Get green component (evaluates binding if set)
    float g() const {
        if (m_bindingG) return m_bindingG();
        return m_g;
    }

    /// @brief Get blue component (evaluates binding if set)
    float b() const {
        if (m_bindingB) return m_bindingB();
        return m_b;
    }

    /// @brief Get alpha component (evaluates binding if set)
    float a() const {
        if (m_bindingA) return m_bindingA();
        return m_a;
    }

    /// @brief Get RGBA as array (evaluates bindings) - NOTE: returns temp, don't store pointer
    void getData(float out[4]) const {
        out[0] = r(); out[1] = g(); out[2] = b(); out[3] = a();
    }

    /// @brief Set all components (clears bindings)
    void set(float r, float g, float b, float a = 1.0f) {
        m_r = r; m_g = g; m_b = b; m_a = a;
        m_bindingR = nullptr;
        m_bindingG = nullptr;
        m_bindingB = nullptr;
        m_bindingA = nullptr;
    }

    void set(const Color& c);  // Defined in color.h

    operator Color() const;  // Defined in color.h

    // -------------------------------------------------------------------------
    /// @name Binding
    /// @{

    /// @brief Bind red component to a 0-1 source with range
    void bindR(std::function<float()> source, float outMin, float outMax) {
        m_bindingR = [source = std::move(source), outMin, outMax]() {
            return outMin + source() * (outMax - outMin);
        };
    }

    /// @brief Bind green component to a 0-1 source with range
    void bindG(std::function<float()> source, float outMin, float outMax) {
        m_bindingG = [source = std::move(source), outMin, outMax]() {
            return outMin + source() * (outMax - outMin);
        };
    }

    /// @brief Bind blue component to a 0-1 source with range
    void bindB(std::function<float()> source, float outMin, float outMax) {
        m_bindingB = [source = std::move(source), outMin, outMax]() {
            return outMin + source() * (outMax - outMin);
        };
    }

    /// @brief Bind alpha component to a 0-1 source with range
    void bindA(std::function<float()> source, float outMin, float outMax) {
        m_bindingA = [source = std::move(source), outMin, outMax]() {
            return outMin + source() * (outMax - outMin);
        };
    }

    /// @brief Clear all bindings
    void unbind() {
        m_bindingR = nullptr;
        m_bindingG = nullptr;
        m_bindingB = nullptr;
        m_bindingA = nullptr;
    }

    /// @brief Check if any binding is set
    bool isBound() const {
        return m_bindingR || m_bindingG || m_bindingB || m_bindingA;
    }

    /// @}
    // -------------------------------------------------------------------------

    const char* name() const { return m_name; }

    ParamDecl decl() const {
        return {m_name, ParamType::Color, 0.0f, 1.0f, {m_r, m_g, m_b, m_a}};
    }

private:
    const char* m_name;
    float m_r, m_g, m_b, m_a;
    std::function<float()> m_bindingR;
    std::function<float()> m_bindingG;
    std::function<float()> m_bindingB;
    std::function<float()> m_bindingA;
};

/**
 * @brief File path parameter wrapper for textures, videos, models, etc.
 *
 * @par Example
 * @code
 * FilePathParam m_texture{"texture", "", "*.png;*.jpg;*.exr", "image"};
 *
 * void setTexture(const std::string& path) {
 *     m_texture = path;
 * }
 * @endcode
 */
class FilePathParam {
public:
    /**
     * @brief Construct a file path parameter
     * @param name Display name
     * @param defaultPath Default file path (empty string for none)
     * @param filter File filter pattern (e.g., "*.png;*.jpg;*.exr")
     * @param category Category hint for UI ("image", "video", "audio", "model")
     */
    FilePathParam(const char* name, const char* defaultPath = "",
                  const char* filter = "*.*", const char* category = "")
        : m_name(name), m_path(defaultPath), m_filter(filter), m_category(category) {}

    /// @brief Get the current path
    const std::string& get() const { return m_path; }

    /// @brief Implicit conversion to string reference
    operator const std::string&() const { return m_path; }

    /// @brief Assignment from string
    FilePathParam& operator=(const std::string& path) { m_path = path; return *this; }

    /// @brief Assignment from C-string
    FilePathParam& operator=(const char* path) { m_path = path; return *this; }

    /// @brief Get parameter name
    const char* name() const { return m_name; }

    /// @brief Get file filter pattern
    const char* filter() const { return m_filter; }

    /// @brief Get category hint
    const char* category() const { return m_category; }

    /// @brief Check if path is empty
    bool empty() const { return m_path.empty(); }

    /// @brief Generate ParamDecl
    ParamDecl decl() const {
        ParamDecl d;
        d.name = m_name;
        d.type = ParamType::FilePath;
        d.stringDefault = m_path;
        d.fileFilter = m_filter;
        d.fileCategory = m_category;
        return d;
    }

private:
    const char* m_name;
    std::string m_path;
    const char* m_filter;
    const char* m_category;
};

} // namespace vivid
