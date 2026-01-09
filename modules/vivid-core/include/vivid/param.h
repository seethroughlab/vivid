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
#include <magic_enum/magic_enum.hpp>
#include <string>
#include <functional>
#include <type_traits>

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

    /// @brief Assignment operator (clears any binding, marks owner dirty)
    Param& operator=(T v) {
        if (m_value != v || m_binding) {
            m_value = v;
            m_binding = nullptr;
            m_boundOperator = nullptr;
            if (m_owner) m_owner->markDirty();
        }
        return *this;
    }

    /// @brief Set owner operator (called by registerParam)
    void setOwner(Operator* owner) { m_owner = owner; }

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
        m_boundOperator = nullptr;  // Lambda binding not trackable
    }

    /**
     * @brief Bind to a value operator with explicit range mapping (trackable)
     * @param source Value operator to bind to
     * @param inMin Input minimum (operator's output range)
     * @param inMax Input maximum (operator's output range)
     * @param outMin Output minimum (parameter's range)
     * @param outMax Output maximum (parameter's range)
     *
     * This binding is trackable and will appear in the chain visualizer.
     *
     * Example:
     * @code
     * // LFO outputs -1 to 1, map to scale 5 to 20
     * noise.scale.bind(lfo, -1.0f, 1.0f, 5.0f, 20.0f);
     * @endcode
     */
    void bind(Operator& source, float inMin, float inMax, T outMin, T outMax) {
        m_boundOperator = &source;
        m_binding = [&source, inMin, inMax, outMin, outMax]() {
            float v = source.outputValue();
            float t = (v - inMin) / (inMax - inMin);  // Normalize to 0..1
            return static_cast<T>(outMin + t * (outMax - outMin));
        };
    }

    /**
     * @brief Bind directly to a value operator (no range mapping, trackable)
     * @param source Value operator to bind to
     *
     * This binding is trackable and will appear in the chain visualizer.
     *
     * Example:
     * @code
     * myParam.bindDirect(mathOp);  // Use operator output directly
     * @endcode
     */
    void bindDirect(Operator& source) {
        m_boundOperator = &source;
        m_binding = [&source]() {
            return static_cast<T>(source.outputValue());
        };
    }

    /**
     * @brief Clear any binding
     */
    void unbind() {
        m_binding = nullptr;
        m_boundOperator = nullptr;
    }

    /**
     * @brief Check if parameter has a binding
     */
    bool isBound() const { return m_binding != nullptr; }

    /**
     * @brief Get bound operator (for visualization)
     * @return Pointer to bound operator, or nullptr if not bound to an operator
     */
    Operator* boundOperator() const { return m_boundOperator; }

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
     * @return ParamDecl with name, type, range, default, and bound operator
     */
    ParamDecl decl() const {
        ParamDecl d;
        d.name = m_name;
        d.type = ParamTypeFor<T>::value;
        d.minVal = static_cast<float>(m_min);
        d.maxVal = static_cast<float>(m_max);
        d.defaultVal[0] = static_cast<float>(m_value);
        d.boundOperator = m_boundOperator;
        return d;
    }

private:
    const char* m_name;
    T m_value;
    T m_min, m_max;
    std::function<T()> m_binding;
    Operator* m_boundOperator = nullptr;  ///< Bound value operator (for visualization)
    Operator* m_owner = nullptr;          ///< Owning operator (for dirty tracking)
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

    /// @brief Set both components (clears bindings, marks owner dirty)
    void set(float x, float y) {
        bool changed = (m_x != x || m_y != y || m_bindingX || m_bindingY || m_bindingUniform);
        m_x = x; m_y = y;
        m_bindingX = nullptr;
        m_bindingY = nullptr;
        m_bindingUniform = nullptr;
        m_boundOperatorX = nullptr;
        m_boundOperatorY = nullptr;
        m_boundOperatorUniform = nullptr;
        if (changed && m_owner) m_owner->markDirty();
    }

    /// @brief Set owner operator (called by registerParam)
    void setOwner(Operator* owner) { m_owner = owner; }

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
        m_boundOperatorX = nullptr;
        m_boundOperatorY = nullptr;
        m_boundOperatorUniform = nullptr;  // Lambda not trackable
    }

    /// @brief Bind both components uniformly to a value operator (trackable)
    void bind(Operator& source, float inMin, float inMax, float outMin, float outMax) {
        m_boundOperatorUniform = &source;
        m_boundOperatorX = nullptr;
        m_boundOperatorY = nullptr;
        m_bindingX = nullptr;
        m_bindingY = nullptr;
        m_bindingUniform = [&source, inMin, inMax]() {
            float v = source.outputValue();
            return (v - inMin) / (inMax - inMin);  // Return normalized 0..1
        };
        m_uniformMin = outMin;
        m_uniformMax = outMax;
    }

    /// @brief Bind X component to a 0-1 source with range
    void bindX(std::function<float()> source, float outMin, float outMax) {
        m_bindingX = [source = std::move(source), outMin, outMax]() {
            return outMin + source() * (outMax - outMin);
        };
        m_boundOperatorX = nullptr;  // Lambda not trackable
    }

    /// @brief Bind X component to a value operator (trackable)
    void bindX(Operator& source, float inMin, float inMax, float outMin, float outMax) {
        m_boundOperatorX = &source;
        m_bindingX = [&source, inMin, inMax, outMin, outMax]() {
            float v = source.outputValue();
            float t = (v - inMin) / (inMax - inMin);
            return outMin + t * (outMax - outMin);
        };
    }

    /// @brief Bind Y component to a 0-1 source with range
    void bindY(std::function<float()> source, float outMin, float outMax) {
        m_bindingY = [source = std::move(source), outMin, outMax]() {
            return outMin + source() * (outMax - outMin);
        };
        m_boundOperatorY = nullptr;  // Lambda not trackable
    }

    /// @brief Bind Y component to a value operator (trackable)
    void bindY(Operator& source, float inMin, float inMax, float outMin, float outMax) {
        m_boundOperatorY = &source;
        m_bindingY = [&source, inMin, inMax, outMin, outMax]() {
            float v = source.outputValue();
            float t = (v - inMin) / (inMax - inMin);
            return outMin + t * (outMax - outMin);
        };
    }

    /// @brief Clear all bindings
    void unbind() {
        m_bindingX = nullptr;
        m_bindingY = nullptr;
        m_bindingUniform = nullptr;
        m_boundOperatorX = nullptr;
        m_boundOperatorY = nullptr;
        m_boundOperatorUniform = nullptr;
    }

    /// @brief Check if any binding is set
    bool isBound() const {
        return m_bindingX || m_bindingY || m_bindingUniform;
    }

    /// @brief Get bound operator for X component (for visualization)
    Operator* boundOperatorX() const { return m_boundOperatorX; }

    /// @brief Get bound operator for Y component (for visualization)
    Operator* boundOperatorY() const { return m_boundOperatorY; }

    /// @brief Get bound operator for uniform binding (for visualization)
    Operator* boundOperatorUniform() const { return m_boundOperatorUniform; }

    /// @}
    // -------------------------------------------------------------------------

    const char* name() const { return m_name; }

    ParamDecl decl() const {
        ParamDecl d;
        d.name = m_name;
        d.type = ParamType::Vec2;
        d.minVal = m_min;
        d.maxVal = m_max;
        d.defaultVal[0] = m_x;
        d.defaultVal[1] = m_y;
        // Report first bound operator found (uniform > X > Y)
        d.boundOperator = m_boundOperatorUniform ? m_boundOperatorUniform
                        : (m_boundOperatorX ? m_boundOperatorX : m_boundOperatorY);
        return d;
    }

private:
    const char* m_name;
    float m_x, m_y;
    float m_min, m_max;
    std::function<float()> m_bindingX;
    std::function<float()> m_bindingY;
    std::function<float()> m_bindingUniform;
    float m_uniformMin = 0.0f, m_uniformMax = 1.0f;
    Operator* m_boundOperatorX = nullptr;
    Operator* m_boundOperatorY = nullptr;
    Operator* m_boundOperatorUniform = nullptr;
    Operator* m_owner = nullptr;
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

    /// @brief Set all components (clears bindings, marks owner dirty)
    void set(float x, float y, float z) {
        bool changed = (m_x != x || m_y != y || m_z != z || m_bindingX || m_bindingY || m_bindingZ);
        m_x = x; m_y = y; m_z = z;
        m_bindingX = nullptr;
        m_bindingY = nullptr;
        m_bindingZ = nullptr;
        m_boundOperatorX = nullptr;
        m_boundOperatorY = nullptr;
        m_boundOperatorZ = nullptr;
        if (changed && m_owner) m_owner->markDirty();
    }

    /// @brief Set owner operator (called by registerParam)
    void setOwner(Operator* owner) { m_owner = owner; }

    // -------------------------------------------------------------------------
    /// @name Binding
    /// @{

    /// @brief Bind X component to a 0-1 source with range
    void bindX(std::function<float()> source, float outMin, float outMax) {
        m_bindingX = [source = std::move(source), outMin, outMax]() {
            return outMin + source() * (outMax - outMin);
        };
        m_boundOperatorX = nullptr;  // Lambda not trackable
    }

    /// @brief Bind X component to a value operator (trackable)
    void bindX(Operator& source, float inMin, float inMax, float outMin, float outMax) {
        m_boundOperatorX = &source;
        m_bindingX = [&source, inMin, inMax, outMin, outMax]() {
            float v = source.outputValue();
            float t = (v - inMin) / (inMax - inMin);
            return outMin + t * (outMax - outMin);
        };
    }

    /// @brief Bind Y component to a 0-1 source with range
    void bindY(std::function<float()> source, float outMin, float outMax) {
        m_bindingY = [source = std::move(source), outMin, outMax]() {
            return outMin + source() * (outMax - outMin);
        };
        m_boundOperatorY = nullptr;  // Lambda not trackable
    }

    /// @brief Bind Y component to a value operator (trackable)
    void bindY(Operator& source, float inMin, float inMax, float outMin, float outMax) {
        m_boundOperatorY = &source;
        m_bindingY = [&source, inMin, inMax, outMin, outMax]() {
            float v = source.outputValue();
            float t = (v - inMin) / (inMax - inMin);
            return outMin + t * (outMax - outMin);
        };
    }

    /// @brief Bind Z component to a 0-1 source with range
    void bindZ(std::function<float()> source, float outMin, float outMax) {
        m_bindingZ = [source = std::move(source), outMin, outMax]() {
            return outMin + source() * (outMax - outMin);
        };
        m_boundOperatorZ = nullptr;  // Lambda not trackable
    }

    /// @brief Bind Z component to a value operator (trackable)
    void bindZ(Operator& source, float inMin, float inMax, float outMin, float outMax) {
        m_boundOperatorZ = &source;
        m_bindingZ = [&source, inMin, inMax, outMin, outMax]() {
            float v = source.outputValue();
            float t = (v - inMin) / (inMax - inMin);
            return outMin + t * (outMax - outMin);
        };
    }

    /// @brief Clear all bindings
    void unbind() {
        m_bindingX = nullptr;
        m_bindingY = nullptr;
        m_bindingZ = nullptr;
        m_boundOperatorX = nullptr;
        m_boundOperatorY = nullptr;
        m_boundOperatorZ = nullptr;
    }

    /// @brief Check if any binding is set
    bool isBound() const {
        return m_bindingX || m_bindingY || m_bindingZ;
    }

    /// @brief Get bound operator for X component
    Operator* boundOperatorX() const { return m_boundOperatorX; }

    /// @brief Get bound operator for Y component
    Operator* boundOperatorY() const { return m_boundOperatorY; }

    /// @brief Get bound operator for Z component
    Operator* boundOperatorZ() const { return m_boundOperatorZ; }

    /// @}
    // -------------------------------------------------------------------------

    const char* name() const { return m_name; }

    ParamDecl decl() const {
        ParamDecl d;
        d.name = m_name;
        d.type = ParamType::Vec3;
        d.minVal = m_min;
        d.maxVal = m_max;
        d.defaultVal[0] = m_x;
        d.defaultVal[1] = m_y;
        d.defaultVal[2] = m_z;
        // Report first bound operator found (X > Y > Z)
        d.boundOperator = m_boundOperatorX ? m_boundOperatorX
                        : (m_boundOperatorY ? m_boundOperatorY : m_boundOperatorZ);
        return d;
    }

private:
    const char* m_name;
    float m_x, m_y, m_z;
    float m_min, m_max;
    std::function<float()> m_bindingX;
    std::function<float()> m_bindingY;
    std::function<float()> m_bindingZ;
    Operator* m_boundOperatorX = nullptr;
    Operator* m_boundOperatorY = nullptr;
    Operator* m_boundOperatorZ = nullptr;
    Operator* m_owner = nullptr;
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

    /// @brief Set all components (clears bindings, marks owner dirty)
    void set(float r, float g, float b, float a = 1.0f) {
        bool changed = (m_r != r || m_g != g || m_b != b || m_a != a ||
                        m_bindingR || m_bindingG || m_bindingB || m_bindingA);
        m_r = r; m_g = g; m_b = b; m_a = a;
        m_bindingR = nullptr;
        m_bindingG = nullptr;
        m_bindingB = nullptr;
        m_bindingA = nullptr;
        m_boundOperatorR = nullptr;
        m_boundOperatorG = nullptr;
        m_boundOperatorB = nullptr;
        m_boundOperatorA = nullptr;
        if (changed && m_owner) m_owner->markDirty();
    }

    /// @brief Set owner operator (called by registerParam)
    void setOwner(Operator* owner) { m_owner = owner; }

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
        m_boundOperatorR = nullptr;  // Lambda not trackable
    }

    /// @brief Bind red component to a value operator (trackable)
    void bindR(Operator& source, float inMin, float inMax, float outMin, float outMax) {
        m_boundOperatorR = &source;
        m_bindingR = [&source, inMin, inMax, outMin, outMax]() {
            float v = source.outputValue();
            float t = (v - inMin) / (inMax - inMin);
            return outMin + t * (outMax - outMin);
        };
    }

    /// @brief Bind green component to a 0-1 source with range
    void bindG(std::function<float()> source, float outMin, float outMax) {
        m_bindingG = [source = std::move(source), outMin, outMax]() {
            return outMin + source() * (outMax - outMin);
        };
        m_boundOperatorG = nullptr;  // Lambda not trackable
    }

    /// @brief Bind green component to a value operator (trackable)
    void bindG(Operator& source, float inMin, float inMax, float outMin, float outMax) {
        m_boundOperatorG = &source;
        m_bindingG = [&source, inMin, inMax, outMin, outMax]() {
            float v = source.outputValue();
            float t = (v - inMin) / (inMax - inMin);
            return outMin + t * (outMax - outMin);
        };
    }

    /// @brief Bind blue component to a 0-1 source with range
    void bindB(std::function<float()> source, float outMin, float outMax) {
        m_bindingB = [source = std::move(source), outMin, outMax]() {
            return outMin + source() * (outMax - outMin);
        };
        m_boundOperatorB = nullptr;  // Lambda not trackable
    }

    /// @brief Bind blue component to a value operator (trackable)
    void bindB(Operator& source, float inMin, float inMax, float outMin, float outMax) {
        m_boundOperatorB = &source;
        m_bindingB = [&source, inMin, inMax, outMin, outMax]() {
            float v = source.outputValue();
            float t = (v - inMin) / (inMax - inMin);
            return outMin + t * (outMax - outMin);
        };
    }

    /// @brief Bind alpha component to a 0-1 source with range
    void bindA(std::function<float()> source, float outMin, float outMax) {
        m_bindingA = [source = std::move(source), outMin, outMax]() {
            return outMin + source() * (outMax - outMin);
        };
        m_boundOperatorA = nullptr;  // Lambda not trackable
    }

    /// @brief Bind alpha component to a value operator (trackable)
    void bindA(Operator& source, float inMin, float inMax, float outMin, float outMax) {
        m_boundOperatorA = &source;
        m_bindingA = [&source, inMin, inMax, outMin, outMax]() {
            float v = source.outputValue();
            float t = (v - inMin) / (inMax - inMin);
            return outMin + t * (outMax - outMin);
        };
    }

    /// @brief Clear all bindings
    void unbind() {
        m_bindingR = nullptr;
        m_bindingG = nullptr;
        m_bindingB = nullptr;
        m_bindingA = nullptr;
        m_boundOperatorR = nullptr;
        m_boundOperatorG = nullptr;
        m_boundOperatorB = nullptr;
        m_boundOperatorA = nullptr;
    }

    /// @brief Check if any binding is set
    bool isBound() const {
        return m_bindingR || m_bindingG || m_bindingB || m_bindingA;
    }

    /// @brief Get bound operator for R component
    Operator* boundOperatorR() const { return m_boundOperatorR; }

    /// @brief Get bound operator for G component
    Operator* boundOperatorG() const { return m_boundOperatorG; }

    /// @brief Get bound operator for B component
    Operator* boundOperatorB() const { return m_boundOperatorB; }

    /// @brief Get bound operator for A component
    Operator* boundOperatorA() const { return m_boundOperatorA; }

    /// @}
    // -------------------------------------------------------------------------

    const char* name() const { return m_name; }

    ParamDecl decl() const {
        ParamDecl d;
        d.name = m_name;
        d.type = ParamType::Color;
        d.minVal = 0.0f;
        d.maxVal = 1.0f;
        d.defaultVal[0] = m_r;
        d.defaultVal[1] = m_g;
        d.defaultVal[2] = m_b;
        d.defaultVal[3] = m_a;
        // Report first bound operator found (R > G > B > A)
        d.boundOperator = m_boundOperatorR ? m_boundOperatorR
                        : (m_boundOperatorG ? m_boundOperatorG
                        : (m_boundOperatorB ? m_boundOperatorB : m_boundOperatorA));
        return d;
    }

private:
    const char* m_name;
    float m_r, m_g, m_b, m_a;
    std::function<float()> m_bindingR;
    std::function<float()> m_bindingG;
    std::function<float()> m_bindingB;
    std::function<float()> m_bindingA;
    Operator* m_boundOperatorR = nullptr;
    Operator* m_boundOperatorG = nullptr;
    Operator* m_boundOperatorB = nullptr;
    Operator* m_boundOperatorA = nullptr;
    Operator* m_owner = nullptr;
};

/**
 * @brief ADSR envelope parameter wrapper
 *
 * Stores Attack, Decay, Sustain, Release values for envelope generators.
 * Values are stored as: attack (seconds), decay (seconds), sustain (0-1 level), release (seconds).
 *
 * @par Example
 * @code
 * ADSRParam m_envelope{"envelope", 0.01f, 0.2f, 0.7f, 0.3f};
 *
 * void applyEnvelope(float& amp) {
 *     // Use envelope values for amplitude shaping
 * }
 * @endcode
 */
class ADSRParam {
public:
    /**
     * @brief Construct an ADSR envelope parameter
     * @param name Display name
     * @param attack Attack time in seconds (default 0.01)
     * @param decay Decay time in seconds (default 0.2)
     * @param sustain Sustain level 0-1 (default 0.7)
     * @param release Release time in seconds (default 0.3)
     * @param maxTime Maximum time for A/D/R sliders (default 2.0)
     */
    ADSRParam(const char* name,
              float attack = 0.01f, float decay = 0.2f,
              float sustain = 0.7f, float release = 0.3f,
              float maxTime = 2.0f)
        : m_name(name), m_attack(attack), m_decay(decay),
          m_sustain(sustain), m_release(release), m_maxTime(maxTime) {}

    /// @brief Get attack time in seconds
    float attack() const { return m_attack; }

    /// @brief Get decay time in seconds
    float decay() const { return m_decay; }

    /// @brief Get sustain level (0-1)
    float sustain() const { return m_sustain; }

    /// @brief Get release time in seconds
    float release() const { return m_release; }

    /// @brief Get maximum time for sliders
    float maxTime() const { return m_maxTime; }

    /// @brief Get attack reference for direct binding
    float& attackRef() { return m_attack; }

    /// @brief Get decay reference for direct binding
    float& decayRef() { return m_decay; }

    /// @brief Get sustain reference for direct binding
    float& sustainRef() { return m_sustain; }

    /// @brief Get release reference for direct binding
    float& releaseRef() { return m_release; }

    /// @brief Set all ADSR values
    void set(float a, float d, float s, float r) {
        bool changed = (m_attack != a || m_decay != d || m_sustain != s || m_release != r);
        m_attack = a;
        m_decay = d;
        m_sustain = s;
        m_release = r;
        if (changed && m_owner) m_owner->markDirty();
    }

    /// @brief Set owner operator (called by registerParam)
    void setOwner(Operator* owner) { m_owner = owner; }

    /// @brief Get parameter name
    const char* name() const { return m_name; }

    /// @brief Generate parameter declaration for introspection
    ParamDecl decl() const {
        ParamDecl d;
        d.name = m_name;
        d.type = ParamType::ADSR;
        d.minVal = 0.0f;
        d.maxVal = m_maxTime;
        d.defaultVal[0] = m_attack;
        d.defaultVal[1] = m_decay;
        d.defaultVal[2] = m_sustain;
        d.defaultVal[3] = m_release;
        return d;
    }

private:
    const char* m_name;
    float m_attack, m_decay, m_sustain, m_release;
    float m_maxTime;
    Operator* m_owner = nullptr;
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

    /// @brief Assignment from string (marks owner dirty)
    FilePathParam& operator=(const std::string& path) {
        if (m_path != path) {
            m_path = path;
            if (m_owner) m_owner->markDirty();
        }
        return *this;
    }

    /// @brief Assignment from C-string (marks owner dirty)
    FilePathParam& operator=(const char* path) {
        if (m_path != path) {
            m_path = path;
            if (m_owner) m_owner->markDirty();
        }
        return *this;
    }

    /// @brief Set owner operator (called by registerParam)
    void setOwner(Operator* owner) { m_owner = owner; }

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
    Operator* m_owner = nullptr;
};

/**
 * @brief Base class for type-erased enum parameter access
 *
 * Provides virtual interface for ParamRef to access enum parameters
 * without knowing the concrete enum type.
 */
class EnumParamBase {
public:
    virtual ~EnumParamBase() = default;
    virtual const char* name() const = 0;
    virtual int index() const = 0;
    virtual void setIndex(int i) = 0;
    virtual ParamDecl decl() const = 0;
    virtual void setOwner(Operator* owner) = 0;
};

/**
 * @brief Enumeration parameter wrapper with automatic label extraction
 * @tparam E Enum type (must be a scoped enum)
 *
 * Uses magic_enum for compile-time enum reflection. Labels are automatically
 * extracted from enum value names.
 *
 * @par Example
 * @code
 * enum class BlendMode { Over, Add, Multiply, Screen };
 *
 * class Composite : public TextureOperator {
 *     EnumParam<BlendMode> m_mode{"mode", BlendMode::Over};
 *
 *     std::vector<ParamDecl> params() override {
 *         return { m_mode.decl() };  // Labels auto-populated: ["Over", "Add", "Multiply", "Screen"]
 *     }
 * };
 * @endcode
 */
template<typename E>
class EnumParam : public EnumParamBase {
    static_assert(std::is_enum_v<E>, "EnumParam requires an enum type");
public:
    /**
     * @brief Construct an enum parameter
     * @param name Display name for UI
     * @param defaultVal Default enum value
     */
    EnumParam(const char* name, E defaultVal)
        : m_name(name), m_value(defaultVal), m_default(defaultVal) {}

    /// @brief Implicit conversion to enum type
    operator E() const { return m_value; }

    /// @brief Get value explicitly
    E get() const { return m_value; }

    /// @brief Assignment operator (marks owner dirty)
    EnumParam& operator=(E v) {
        if (m_value != v) {
            m_value = v;
            if (m_owner) m_owner->markDirty();
        }
        return *this;
    }

    /// @brief Set owner operator (called by registerParam)
    void setOwner(Operator* owner) override { m_owner = owner; }

    /// @brief Get parameter name
    const char* name() const override { return m_name; }

    /// @brief Get current value as index (for UI)
    int index() const override {
        auto idx = magic_enum::enum_index(m_value);
        return idx.has_value() ? static_cast<int>(idx.value()) : 0;
    }

    /// @brief Set value by index (from UI, marks owner dirty)
    void setIndex(int i) override {
        if (i >= 0 && static_cast<size_t>(i) < magic_enum::enum_count<E>()) {
            E newVal = magic_enum::enum_value<E>(static_cast<size_t>(i));
            if (m_value != newVal) {
                m_value = newVal;
                if (m_owner) m_owner->markDirty();
            }
        }
    }

    /// @brief Get current value as string
    std::string_view valueName() const {
        return magic_enum::enum_name(m_value);
    }

    /**
     * @brief Generate ParamDecl for introspection
     * @return ParamDecl with type=Enum and auto-populated labels
     */
    ParamDecl decl() const override {
        ParamDecl d;
        d.name = m_name;
        d.type = ParamType::Enum;
        auto defaultIdx = magic_enum::enum_index(m_default);
        d.defaultVal[0] = defaultIdx.has_value() ? static_cast<float>(defaultIdx.value()) : 0.0f;
        d.minVal = 0.0f;
        d.maxVal = static_cast<float>(magic_enum::enum_count<E>() - 1);
        // Auto-populate labels from enum names
        for (auto name : magic_enum::enum_names<E>()) {
            d.enumLabels.emplace_back(name);
        }
        return d;
    }

private:
    const char* m_name;
    E m_value;
    E m_default;
    Operator* m_owner = nullptr;
};

} // namespace vivid
