#pragma once

/**
 * @file logic_op.h
 * @brief Logical and comparison operator
 *
 * Performs logical and comparison operations on scalar values.
 */

#include <vivid/operator.h>
#include <vivid/param.h>
#include <vivid/param_registry.h>
#include <cmath>

namespace vivid::effects {

/**
 * @brief Logic operation types
 */
enum class LogicOperation {
    GreaterThan,    ///< A > B
    LessThan,       ///< A < B
    Equal,          ///< A == B (within epsilon)
    NotEqual,       ///< A != B (within epsilon)
    GreaterOrEqual, ///< A >= B
    LessOrEqual,    ///< A <= B
    InRange,        ///< rangeMin <= A <= rangeMax
    And,            ///< A && B (values > 0.5 are true)
    Or,             ///< A || B
    Not,            ///< !A
    Toggle          ///< Flip-flop on trigger
};

/**
 * @brief Logical and comparison operations
 *
 * Performs comparison and boolean logic operations on scalar values.
 * Useful for building conditional logic in effect chains.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | inputA | float | -1000 to 1000 | 0.0 | First input value |
 * | inputB | float | -1000 to 1000 | 0.0 | Second input value |
 * | rangeMin | float | -1000 to 1000 | 0.0 | Minimum for InRange |
 * | rangeMax | float | -1000 to 1000 | 1.0 | Maximum for InRange |
 * | epsilon | float | 0-1 | 0.0001 | Tolerance for equality |
 *
 * @par Example
 * @code
 * auto& compare = chain.add<Logic>("compare");
 * compare.operation(LogicOperation::GreaterThan);
 * compare.inputA = lfo.outputValue();
 * compare.inputB = 0.5f;
 *
 * if (compare.result()) {
 *     // LFO value is above 0.5
 * }
 * @endcode
 *
 * @par Inputs
 * None (value-based)
 *
 * @par Output
 * - Boolean result via result()
 * - Float value (0 or 1) via outputValue()
 */
class Logic : public vivid::Operator, public vivid::ParamRegistry {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> inputA{"inputA", 0.0f, -1000.0f, 1000.0f};     ///< First input value
    Param<float> inputB{"inputB", 0.0f, -1000.0f, 1000.0f};     ///< Second input value
    Param<float> rangeMin{"rangeMin", 0.0f, -1000.0f, 1000.0f}; ///< Minimum for InRange
    Param<float> rangeMax{"rangeMax", 1.0f, -1000.0f, 1000.0f}; ///< Maximum for InRange
    Param<float> epsilon{"epsilon", 0.0001f, 0.0f, 1.0f};       ///< Tolerance for equality

    /// @}
    // -------------------------------------------------------------------------

    Logic() {
        registerParam(inputA);
        registerParam(inputB);
        registerParam(rangeMin);
        registerParam(rangeMax);
        registerParam(epsilon);
    }
    ~Logic() override = default;

    /// @brief Set logic operation
    void operation(LogicOperation op) { if (m_operation != op) { m_operation = op; markDirty(); } }

    /// @brief Trigger toggle (for Toggle operation)
    void trigger(bool t) {
        if (t && !m_lastTrigger) {
            m_toggleState = !m_toggleState;
        }
        m_lastTrigger = t;
    }

    // -------------------------------------------------------------------------
    /// @name Result Access
    /// @{

    /// @brief Get boolean result
    bool result() const { return m_result; }

    /// @brief Get result as float (1.0 if true, 0.0 if false)
    float value() const { return m_result ? 1.0f : 0.0f; }

    /// @brief Get output value for parameter linking
    float outputValue() const override { return m_result ? 1.0f : 0.0f; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void process(Context& ctx) override {
        if (!needsCook()) return;

        float a = static_cast<float>(inputA);
        float b = static_cast<float>(inputB);
        float eps = static_cast<float>(epsilon);
        float rMin = static_cast<float>(rangeMin);
        float rMax = static_cast<float>(rangeMax);

        switch (m_operation) {
            case LogicOperation::GreaterThan:
                m_result = a > b;
                break;
            case LogicOperation::LessThan:
                m_result = a < b;
                break;
            case LogicOperation::Equal:
                m_result = std::abs(a - b) < eps;
                break;
            case LogicOperation::NotEqual:
                m_result = std::abs(a - b) >= eps;
                break;
            case LogicOperation::GreaterOrEqual:
                m_result = a >= b;
                break;
            case LogicOperation::LessOrEqual:
                m_result = a <= b;
                break;
            case LogicOperation::InRange:
                m_result = a >= rMin && a <= rMax;
                break;
            case LogicOperation::And:
                m_result = (a > 0.5f) && (b > 0.5f);
                break;
            case LogicOperation::Or:
                m_result = (a > 0.5f) || (b > 0.5f);
                break;
            case LogicOperation::Not:
                m_result = !(a > 0.5f);
                break;
            case LogicOperation::Toggle:
                m_result = m_toggleState;
                break;
        }
        didCook();
    }

    std::string name() const override { return "Logic"; }
    OutputKind outputKind() const override { return OutputKind::Value; }

    std::vector<ParamDecl> params() override { return registeredParams(); }
    bool getParam(const std::string& name, float out[4]) override { return getRegisteredParam(name, out); }
    bool setParam(const std::string& name, const float value[4]) override {
        if (setRegisteredParam(name, value)) { markDirty(); return true; }
        return false;
    }

    /// @}

private:
    LogicOperation m_operation = LogicOperation::GreaterThan;
    bool m_result = false;
    bool m_toggleState = false;
    bool m_lastTrigger = false;
};

} // namespace vivid::effects
