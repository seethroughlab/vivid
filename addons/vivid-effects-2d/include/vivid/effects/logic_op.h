#pragma once

// Vivid Effects 2D - Logic Operator
// Logical/comparison operations

#include <vivid/operator.h>

namespace vivid::effects {

enum class LogicOperation {
    GreaterThan,
    LessThan,
    Equal,
    NotEqual,
    GreaterOrEqual,
    LessOrEqual,
    InRange,
    And,
    Or,
    Not,
    Toggle
};

class Logic : public vivid::Operator {
public:
    Logic() = default;
    ~Logic() override = default;

    // Fluent API
    Logic& inputA(float v) { m_inputA = v; return *this; }
    Logic& inputB(float v) { m_inputB = v; return *this; }
    Logic& operation(LogicOperation op) { m_operation = op; return *this; }

    // For InRange
    Logic& rangeMin(float v) { m_rangeMin = v; return *this; }
    Logic& rangeMax(float v) { m_rangeMax = v; return *this; }

    // For Toggle - trigger on rising edge
    Logic& trigger(bool t) {
        if (t && !m_lastTrigger) {
            m_toggleState = !m_toggleState;
        }
        m_lastTrigger = t;
        return *this;
    }

    // Epsilon for float comparison
    Logic& epsilon(float e) { m_epsilon = e; return *this; }

    // Get result
    bool result() const { return m_result; }
    float value() const { return m_result ? 1.0f : 0.0f; }
    float outputValue() const override { return m_result ? 1.0f : 0.0f; }

    // Operator interface
    void process(Context& ctx) override {
        switch (m_operation) {
            case LogicOperation::GreaterThan:
                m_result = m_inputA > m_inputB;
                break;
            case LogicOperation::LessThan:
                m_result = m_inputA < m_inputB;
                break;
            case LogicOperation::Equal:
                m_result = std::abs(m_inputA - m_inputB) < m_epsilon;
                break;
            case LogicOperation::NotEqual:
                m_result = std::abs(m_inputA - m_inputB) >= m_epsilon;
                break;
            case LogicOperation::GreaterOrEqual:
                m_result = m_inputA >= m_inputB;
                break;
            case LogicOperation::LessOrEqual:
                m_result = m_inputA <= m_inputB;
                break;
            case LogicOperation::InRange:
                m_result = m_inputA >= m_rangeMin && m_inputA <= m_rangeMax;
                break;
            case LogicOperation::And:
                m_result = (m_inputA > 0.5f) && (m_inputB > 0.5f);
                break;
            case LogicOperation::Or:
                m_result = (m_inputA > 0.5f) || (m_inputB > 0.5f);
                break;
            case LogicOperation::Not:
                m_result = !(m_inputA > 0.5f);
                break;
            case LogicOperation::Toggle:
                m_result = m_toggleState;
                break;
        }
    }

    std::string name() const override { return "Logic"; }
    OutputKind outputKind() const override { return OutputKind::Value; }

private:
    LogicOperation m_operation = LogicOperation::GreaterThan;
    float m_inputA = 0.0f;
    float m_inputB = 0.0f;
    float m_rangeMin = 0.0f;
    float m_rangeMax = 1.0f;
    float m_epsilon = 0.0001f;
    bool m_result = false;
    bool m_toggleState = false;
    bool m_lastTrigger = false;
};

} // namespace vivid::effects
