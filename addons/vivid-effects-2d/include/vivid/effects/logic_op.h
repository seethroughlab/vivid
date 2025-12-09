#pragma once

/**
 * @file logic_op.h
 * @brief Logical and comparison operator
 *
 * Performs logical and comparison operations on scalar values.
 */

#include <vivid/operator.h>
#include <vivid/param.h>
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
 * chain.add<Logic>("compare")
 *     .operation(LogicOperation::GreaterThan)
 *     .inputA(lfo.outputValue())
 *     .inputB(0.5f);
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
class Logic : public vivid::Operator {
public:
    Logic() = default;
    ~Logic() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set first input value
     * @param v Input A value
     * @return Reference for chaining
     */
    Logic& inputA(float v) { m_inputA = v; return *this; }

    /**
     * @brief Set second input value
     * @param v Input B value
     * @return Reference for chaining
     */
    Logic& inputB(float v) { m_inputB = v; return *this; }

    /**
     * @brief Set logic operation
     * @param op Operation type
     * @return Reference for chaining
     */
    Logic& operation(LogicOperation op) { m_operation = op; return *this; }

    /**
     * @brief Set minimum for InRange operation
     * @param v Minimum value
     * @return Reference for chaining
     */
    Logic& rangeMin(float v) { m_rangeMin = v; return *this; }

    /**
     * @brief Set maximum for InRange operation
     * @param v Maximum value
     * @return Reference for chaining
     */
    Logic& rangeMax(float v) { m_rangeMax = v; return *this; }

    /**
     * @brief Trigger toggle (for Toggle operation)
     * @param t Trigger on rising edge
     * @return Reference for chaining
     */
    Logic& trigger(bool t) {
        if (t && !m_lastTrigger) {
            m_toggleState = !m_toggleState;
        }
        m_lastTrigger = t;
        return *this;
    }

    /**
     * @brief Set epsilon for float comparison
     * @param e Epsilon value (default 0.0001)
     * @return Reference for chaining
     */
    Logic& epsilon(float e) { m_epsilon = e; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Result Access
    /// @{

    /**
     * @brief Get boolean result
     * @return Operation result as bool
     */
    bool result() const { return m_result; }

    /**
     * @brief Get result as float
     * @return 1.0 if true, 0.0 if false
     */
    float value() const { return m_result ? 1.0f : 0.0f; }

    /**
     * @brief Get output value for parameter linking
     * @return 1.0 if true, 0.0 if false
     */
    float outputValue() const override { return m_result ? 1.0f : 0.0f; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::vector<ParamDecl> params() override {
        return { m_inputA.decl(), m_inputB.decl(), m_rangeMin.decl(),
                 m_rangeMax.decl(), m_epsilon.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "inputA") { out[0] = m_inputA; return true; }
        if (name == "inputB") { out[0] = m_inputB; return true; }
        if (name == "rangeMin") { out[0] = m_rangeMin; return true; }
        if (name == "rangeMax") { out[0] = m_rangeMax; return true; }
        if (name == "epsilon") { out[0] = m_epsilon; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "inputA") { m_inputA = value[0]; return true; }
        if (name == "inputB") { m_inputB = value[0]; return true; }
        if (name == "rangeMin") { m_rangeMin = value[0]; return true; }
        if (name == "rangeMax") { m_rangeMax = value[0]; return true; }
        if (name == "epsilon") { m_epsilon = value[0]; return true; }
        return false;
    }

    void process(Context& ctx) override {
        float a = static_cast<float>(m_inputA);
        float b = static_cast<float>(m_inputB);
        float eps = static_cast<float>(m_epsilon);
        float rMin = static_cast<float>(m_rangeMin);
        float rMax = static_cast<float>(m_rangeMax);

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
    }

    std::string name() const override { return "Logic"; }
    OutputKind outputKind() const override { return OutputKind::Value; }

    /// @}

private:
    LogicOperation m_operation = LogicOperation::GreaterThan;
    Param<float> m_inputA{"inputA", 0.0f, -1000.0f, 1000.0f};
    Param<float> m_inputB{"inputB", 0.0f, -1000.0f, 1000.0f};
    Param<float> m_rangeMin{"rangeMin", 0.0f, -1000.0f, 1000.0f};
    Param<float> m_rangeMax{"rangeMax", 1.0f, -1000.0f, 1000.0f};
    Param<float> m_epsilon{"epsilon", 0.0001f, 0.0f, 1.0f};
    bool m_result = false;
    bool m_toggleState = false;
    bool m_lastTrigger = false;
};

} // namespace vivid::effects
