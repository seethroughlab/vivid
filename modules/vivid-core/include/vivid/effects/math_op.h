#pragma once

/**
 * @file math_op.h
 * @brief Mathematical operations operator
 *
 * Performs mathematical operations on scalar values.
 */

#include <vivid/operator.h>
#include <vivid/param.h>
#include <vivid/param_registry.h>
#include <vivid/operator_registry.h>
#include <vivid/viz_helpers.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

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
 * auto& math = chain.add<Math>("remap");
 * math.operation(MathOperation::Remap);
 * math.inputA = lfo.outputValue();
 * math.inMin = -1.0f;
 * math.inMax = 1.0f;
 * math.outMin = 0.0f;
 * math.outMax = 1.0f;
 * @endcode
 *
 * @par Inputs
 * None (value-based)
 *
 * @par Output
 * - Float result via value()
 * - Also available via outputValue()
 */
class Math : public vivid::Operator, public vivid::ParamRegistry {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> inputA{"inputA", 0.0f, -1000.0f, 1000.0f};   ///< First input value
    Param<float> inputB{"inputB", 0.0f, -1000.0f, 1000.0f};   ///< Second input value
    Param<float> minVal{"minVal", 0.0f, -1000.0f, 1000.0f};   ///< Minimum for Clamp
    Param<float> maxVal{"maxVal", 1.0f, -1000.0f, 1000.0f};   ///< Maximum for Clamp
    Param<float> inMin{"inMin", 0.0f, -1000.0f, 1000.0f};     ///< Input min for Remap
    Param<float> inMax{"inMax", 1.0f, -1000.0f, 1000.0f};     ///< Input max for Remap
    Param<float> outMin{"outMin", 0.0f, -1000.0f, 1000.0f};   ///< Output min for Remap
    Param<float> outMax{"outMax", 1.0f, -1000.0f, 1000.0f};   ///< Output max for Remap

    /// @}
    // -------------------------------------------------------------------------

    Math() {
        registerParam(inputA);
        registerParam(inputB);
        registerParam(minVal);
        registerParam(maxVal);
        registerParam(inMin);
        registerParam(inMax);
        registerParam(outMin);
        registerParam(outMax);
    }
    ~Math() override = default;

    /// @brief Set math operation
    void operation(MathOperation op) { if (m_operation != op) { m_operation = op; markDirty(); } }

    // -------------------------------------------------------------------------
    /// @name Result Access
    /// @{

    /// @brief Get operation result
    float value() const { return m_result; }

    /// @brief Get output value for parameter linking
    float outputValue() const override { return m_result; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void process(Context& ctx) override {
        if (!needsCook()) return;

        float a = static_cast<float>(inputA);
        float b = static_cast<float>(inputB);

        switch (m_operation) {
            case MathOperation::Add:
                m_result = a + b;
                break;
            case MathOperation::Subtract:
                m_result = a - b;
                break;
            case MathOperation::Multiply:
                m_result = a * b;
                break;
            case MathOperation::Divide:
                m_result = (b != 0.0f) ? a / b : 0.0f;
                break;
            case MathOperation::Clamp:
                m_result = std::clamp(a,
                                      static_cast<float>(minVal),
                                      static_cast<float>(maxVal));
                break;
            case MathOperation::Remap: {
                float inMinVal = static_cast<float>(inMin);
                float inMaxVal = static_cast<float>(inMax);
                float outMinVal = static_cast<float>(outMin);
                float outMaxVal = static_cast<float>(outMax);
                float t = (a - inMinVal) / (inMaxVal - inMinVal);
                m_result = outMinVal + t * (outMaxVal - outMinVal);
                break;
            }
            case MathOperation::Abs:
                m_result = std::abs(a);
                break;
            case MathOperation::Sin:
                m_result = std::sin(a);
                break;
            case MathOperation::Cos:
                m_result = std::cos(a);
                break;
            case MathOperation::Pow:
                m_result = std::pow(a, b);
                break;
            case MathOperation::Sqrt:
                m_result = std::sqrt(std::max(0.0f, a));
                break;
            case MathOperation::Floor:
                m_result = std::floor(a);
                break;
            case MathOperation::Ceil:
                m_result = std::ceil(a);
                break;
            case MathOperation::Fract:
                m_result = a - std::floor(a);
                break;
            case MathOperation::Min:
                m_result = std::min(a, b);
                break;
            case MathOperation::Max:
                m_result = std::max(a, b);
                break;
        }
        didCook();
    }

    std::string name() const override { return "Math"; }
    OutputKind outputKind() const override { return OutputKind::Value; }

    std::vector<ParamDecl> params() override { return registeredParams(); }
    bool getParam(const std::string& name, float out[4]) override { return getRegisteredParam(name, out); }
    bool setParam(const std::string& name, const float value[4]) override {
        if (setRegisteredParam(name, value)) { markDirty(); return true; }
        return false;
    }

    /// @brief Draw value display visualization
    bool drawVisualization(VizDrawList* dl, float minX, float minY,
                           float maxX, float maxY) override {
        VizHelpers viz(dl);
        VizBounds bounds{minX, minY, maxX - minX, maxY - minY};

        viz.drawBackground(bounds);

        // Operation names
        const char* opNames[] = {
            "+", "-", "*", "/", "Clamp", "Remap",
            "Abs", "Sin", "Cos", "Pow", "Sqrt",
            "Floor", "Ceil", "Fract", "Min", "Max"
        };
        int opIdx = static_cast<int>(m_operation);
        if (opIdx < 0 || opIdx > 15) opIdx = 0;

        // Draw result value (large, centered)
        char valueStr[32];
        if (std::abs(m_result) >= 100.0f) {
            snprintf(valueStr, sizeof(valueStr), "%.0f", m_result);
        } else if (std::abs(m_result) >= 10.0f) {
            snprintf(valueStr, sizeof(valueStr), "%.1f", m_result);
        } else {
            snprintf(valueStr, sizeof(valueStr), "%.2f", m_result);
        }

        // Color based on sign
        uint32_t valueColor = m_result >= 0.0f
            ? VIZ_COL32(100, 255, 150, 255)   // Green for positive
            : VIZ_COL32(255, 100, 100, 255);  // Red for negative

        VizBounds valueBounds = bounds.inset(4).splitTop(0.65f);
        viz.drawLabel(valueBounds, valueStr, valueColor);

        // Draw operation name at bottom
        VizBounds opBounds = bounds.splitBottom(0.3f);
        viz.drawLabel(opBounds, opNames[opIdx], VizColors::TextSecondary);

        return true;
    }

    /// @}

private:
    MathOperation m_operation = MathOperation::Add;
    float m_result = 0.0f;
};

} // namespace vivid::effects
