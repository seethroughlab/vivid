#pragma once

/**
 * @file math_op.h
 * @brief Mathematical operations operator
 *
 * Performs mathematical operations on scalar values.
 */

#include <vivid/operator.h>
#include <vivid/param.h>
#include <algorithm>
#include <cmath>

namespace vivid::effects {

/**
 * @brief Math operation types
 */
enum class MathOperation {
    Add,        ///< A + B
    Subtract,   ///< A - B
    Multiply,   ///< A * B
    Divide,     ///< A / B (safe, returns 0 for B=0)
    Clamp,      ///< Clamp A to [minVal, maxVal]
    Remap,      ///< Remap A from [inMin,inMax] to [outMin,outMax]
    Abs,        ///< |A|
    Sin,        ///< sin(A)
    Cos,        ///< cos(A)
    Pow,        ///< A^B
    Sqrt,       ///< sqrt(A) (safe, returns 0 for A<0)
    Floor,      ///< floor(A)
    Ceil,       ///< ceil(A)
    Fract,      ///< A - floor(A)
    Min,        ///< min(A, B)
    Max         ///< max(A, B)
};

/**
 * @brief Mathematical operations on values
 *
 * Performs arithmetic, trigonometric, and utility math operations
 * on scalar values. Useful for transforming and combining values
 * in effect chains.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | inputA | float | -1000 to 1000 | 0.0 | First input value |
 * | inputB | float | -1000 to 1000 | 0.0 | Second input value |
 * | minVal | float | -1000 to 1000 | 0.0 | Minimum for Clamp |
 * | maxVal | float | -1000 to 1000 | 1.0 | Maximum for Clamp |
 * | inMin | float | -1000 to 1000 | 0.0 | Input min for Remap |
 * | inMax | float | -1000 to 1000 | 1.0 | Input max for Remap |
 * | outMin | float | -1000 to 1000 | 0.0 | Output min for Remap |
 * | outMax | float | -1000 to 1000 | 1.0 | Output max for Remap |
 *
 * @par Example
 * @code
 * // Remap LFO from [-1,1] to [0,1]
 * chain.add<Math>("remap")
 *     .operation(MathOperation::Remap)
 *     .inputA(lfo.outputValue())
 *     .inMin(-1.0f).inMax(1.0f)
 *     .outMin(0.0f).outMax(1.0f);
 * @endcode
 *
 * @par Inputs
 * None (value-based)
 *
 * @par Output
 * - Float result via value()
 * - Also available via outputValue()
 */
class Math : public vivid::Operator {
public:
    Math() = default;
    ~Math() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set first input value
     * @param v Input A value
     * @return Reference for chaining
     */
    Math& inputA(float v) { if (m_inputA != v) { m_inputA = v; markDirty(); } return *this; }

    /**
     * @brief Set second input value
     * @param v Input B value
     * @return Reference for chaining
     */
    Math& inputB(float v) { if (m_inputB != v) { m_inputB = v; markDirty(); } return *this; }

    /**
     * @brief Set math operation
     * @param op Operation type
     * @return Reference for chaining
     */
    Math& operation(MathOperation op) { if (m_operation != op) { m_operation = op; markDirty(); } return *this; }

    /**
     * @brief Set minimum for Clamp operation
     * @param v Minimum value
     * @return Reference for chaining
     */
    Math& minVal(float v) { if (m_minVal != v) { m_minVal = v; markDirty(); } return *this; }

    /**
     * @brief Set maximum for Clamp operation
     * @param v Maximum value
     * @return Reference for chaining
     */
    Math& maxVal(float v) { if (m_maxVal != v) { m_maxVal = v; markDirty(); } return *this; }

    /**
     * @brief Set input minimum for Remap
     * @param v Input range minimum
     * @return Reference for chaining
     */
    Math& inMin(float v) { if (m_inMin != v) { m_inMin = v; markDirty(); } return *this; }

    /**
     * @brief Set input maximum for Remap
     * @param v Input range maximum
     * @return Reference for chaining
     */
    Math& inMax(float v) { if (m_inMax != v) { m_inMax = v; markDirty(); } return *this; }

    /**
     * @brief Set output minimum for Remap
     * @param v Output range minimum
     * @return Reference for chaining
     */
    Math& outMin(float v) { if (m_outMin != v) { m_outMin = v; markDirty(); } return *this; }

    /**
     * @brief Set output maximum for Remap
     * @param v Output range maximum
     * @return Reference for chaining
     */
    Math& outMax(float v) { if (m_outMax != v) { m_outMax = v; markDirty(); } return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Result Access
    /// @{

    /**
     * @brief Get operation result
     * @return Computed value
     */
    float value() const { return m_result; }

    /**
     * @brief Get output value for parameter linking
     * @return Computed value
     */
    float outputValue() const override { return m_result; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::vector<ParamDecl> params() override {
        return { m_inputA.decl(), m_inputB.decl(), m_minVal.decl(), m_maxVal.decl(),
                 m_inMin.decl(), m_inMax.decl(), m_outMin.decl(), m_outMax.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "inputA") { out[0] = m_inputA; return true; }
        if (name == "inputB") { out[0] = m_inputB; return true; }
        if (name == "minVal") { out[0] = m_minVal; return true; }
        if (name == "maxVal") { out[0] = m_maxVal; return true; }
        if (name == "inMin") { out[0] = m_inMin; return true; }
        if (name == "inMax") { out[0] = m_inMax; return true; }
        if (name == "outMin") { out[0] = m_outMin; return true; }
        if (name == "outMax") { out[0] = m_outMax; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "inputA") { inputA(value[0]); return true; }
        if (name == "inputB") { inputB(value[0]); return true; }
        if (name == "minVal") { minVal(value[0]); return true; }
        if (name == "maxVal") { maxVal(value[0]); return true; }
        if (name == "inMin") { inMin(value[0]); return true; }
        if (name == "inMax") { inMax(value[0]); return true; }
        if (name == "outMin") { outMin(value[0]); return true; }
        if (name == "outMax") { outMax(value[0]); return true; }
        return false;
    }

    void process(Context& ctx) override {
        if (!needsCook()) return;

        switch (m_operation) {
            case MathOperation::Add:
                m_result = m_inputA + m_inputB;
                break;
            case MathOperation::Subtract:
                m_result = m_inputA - m_inputB;
                break;
            case MathOperation::Multiply:
                m_result = m_inputA * m_inputB;
                break;
            case MathOperation::Divide:
                m_result = (m_inputB != 0.0f) ? m_inputA / m_inputB : 0.0f;
                break;
            case MathOperation::Clamp:
                m_result = std::clamp(static_cast<float>(m_inputA),
                                      static_cast<float>(m_minVal),
                                      static_cast<float>(m_maxVal));
                break;
            case MathOperation::Remap: {
                float a = static_cast<float>(m_inputA);
                float inMin = static_cast<float>(m_inMin);
                float inMax = static_cast<float>(m_inMax);
                float outMin = static_cast<float>(m_outMin);
                float outMax = static_cast<float>(m_outMax);
                float t = (a - inMin) / (inMax - inMin);
                m_result = outMin + t * (outMax - outMin);
                break;
            }
            case MathOperation::Abs:
                m_result = std::abs(static_cast<float>(m_inputA));
                break;
            case MathOperation::Sin:
                m_result = std::sin(static_cast<float>(m_inputA));
                break;
            case MathOperation::Cos:
                m_result = std::cos(static_cast<float>(m_inputA));
                break;
            case MathOperation::Pow:
                m_result = std::pow(static_cast<float>(m_inputA), static_cast<float>(m_inputB));
                break;
            case MathOperation::Sqrt:
                m_result = std::sqrt(std::max(0.0f, static_cast<float>(m_inputA)));
                break;
            case MathOperation::Floor:
                m_result = std::floor(static_cast<float>(m_inputA));
                break;
            case MathOperation::Ceil:
                m_result = std::ceil(static_cast<float>(m_inputA));
                break;
            case MathOperation::Fract: {
                float a = static_cast<float>(m_inputA);
                m_result = a - std::floor(a);
                break;
            }
            case MathOperation::Min:
                m_result = std::min(static_cast<float>(m_inputA), static_cast<float>(m_inputB));
                break;
            case MathOperation::Max:
                m_result = std::max(static_cast<float>(m_inputA), static_cast<float>(m_inputB));
                break;
        }
        didCook();
    }

    std::string name() const override { return "Math"; }
    OutputKind outputKind() const override { return OutputKind::Value; }

    /// @}

private:
    MathOperation m_operation = MathOperation::Add;
    Param<float> m_inputA{"inputA", 0.0f, -1000.0f, 1000.0f};
    Param<float> m_inputB{"inputB", 0.0f, -1000.0f, 1000.0f};
    Param<float> m_minVal{"minVal", 0.0f, -1000.0f, 1000.0f};
    Param<float> m_maxVal{"maxVal", 1.0f, -1000.0f, 1000.0f};
    Param<float> m_inMin{"inMin", 0.0f, -1000.0f, 1000.0f};
    Param<float> m_inMax{"inMax", 1.0f, -1000.0f, 1000.0f};
    Param<float> m_outMin{"outMin", 0.0f, -1000.0f, 1000.0f};
    Param<float> m_outMax{"outMax", 1.0f, -1000.0f, 1000.0f};
    float m_result = 0.0f;
};

} // namespace vivid::effects
