#pragma once

// Vivid Effects 2D - Math Operator
// Mathematical operations on values

#include <vivid/operator.h>
#include <algorithm>
#include <cmath>

namespace vivid::effects {

enum class MathOperation {
    Add,
    Subtract,
    Multiply,
    Divide,
    Clamp,
    Remap,
    Abs,
    Sin,
    Cos,
    Pow,
    Sqrt,
    Floor,
    Ceil,
    Fract,
    Min,
    Max
};

class Math : public vivid::Operator {
public:
    Math() = default;
    ~Math() override = default;

    // Fluent API
    Math& inputA(float v) { m_inputA = v; return *this; }
    Math& inputB(float v) { m_inputB = v; return *this; }
    Math& operation(MathOperation op) { m_operation = op; return *this; }

    // For clamp
    Math& minVal(float v) { m_minVal = v; return *this; }
    Math& maxVal(float v) { m_maxVal = v; return *this; }

    // For remap
    Math& inMin(float v) { m_inMin = v; return *this; }
    Math& inMax(float v) { m_inMax = v; return *this; }
    Math& outMin(float v) { m_outMin = v; return *this; }
    Math& outMax(float v) { m_outMax = v; return *this; }

    // Get result
    float value() const { return m_result; }

    // Operator interface
    void process(Context& ctx) override {
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
                m_result = std::clamp(m_inputA, m_minVal, m_maxVal);
                break;
            case MathOperation::Remap: {
                float t = (m_inputA - m_inMin) / (m_inMax - m_inMin);
                m_result = m_outMin + t * (m_outMax - m_outMin);
                break;
            }
            case MathOperation::Abs:
                m_result = std::abs(m_inputA);
                break;
            case MathOperation::Sin:
                m_result = std::sin(m_inputA);
                break;
            case MathOperation::Cos:
                m_result = std::cos(m_inputA);
                break;
            case MathOperation::Pow:
                m_result = std::pow(m_inputA, m_inputB);
                break;
            case MathOperation::Sqrt:
                m_result = std::sqrt(std::max(0.0f, m_inputA));
                break;
            case MathOperation::Floor:
                m_result = std::floor(m_inputA);
                break;
            case MathOperation::Ceil:
                m_result = std::ceil(m_inputA);
                break;
            case MathOperation::Fract:
                m_result = m_inputA - std::floor(m_inputA);
                break;
            case MathOperation::Min:
                m_result = std::min(m_inputA, m_inputB);
                break;
            case MathOperation::Max:
                m_result = std::max(m_inputA, m_inputB);
                break;
        }
    }

    std::string name() const override { return "Math"; }
    OutputKind outputKind() const override { return OutputKind::Value; }

private:
    MathOperation m_operation = MathOperation::Add;
    float m_inputA = 0.0f;
    float m_inputB = 0.0f;
    float m_minVal = 0.0f;
    float m_maxVal = 1.0f;
    float m_inMin = 0.0f;
    float m_inMax = 1.0f;
    float m_outMin = 0.0f;
    float m_outMax = 1.0f;
    float m_result = 0.0f;
};

} // namespace vivid::effects
