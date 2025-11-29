#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <cmath>
#include <memory>
#include <string>

namespace vivid {

/// Logic operation modes
enum class LogicOp {
    GreaterThan = 0,
    LessThan,
    Equal,
    NotEqual,
    GreaterOrEqual,
    LessOrEqual,
    And,
    Or,
    Not,
    Xor,
    Threshold,
    InRange,
    Toggle,
    Trigger,
    Gate
};

/// State for Logic operator
struct LogicState : OperatorState {
    bool toggleState = false;
    float prevValue = 0.0f;
};

/**
 * @brief Logic and comparison operations.
 *
 * Performs comparisons, boolean logic, and trigger/gate operations.
 *
 * Example:
 * @code
 * Logic gate_;
 * gate_.a("signal").b("enable").gate();  // Pass signal when enable > 0.5
 *
 * Logic trigger_;
 * trigger_.a("lfo").threshold(0.5f).trigger();  // Pulse on rising edge
 * @endcode
 */
class Logic : public Operator {
public:
    Logic() = default;

    /// Set input A from another operator
    Logic& a(const std::string& node) { inputA_ = node; useConstA_ = false; return *this; }
    /// Set input B from another operator
    Logic& b(const std::string& node) { inputB_ = node; useConstB_ = false; return *this; }
    /// Set input A to constant value
    Logic& a(float val) { constA_ = val; useConstA_ = true; return *this; }
    /// Set input B to constant value
    Logic& b(float val) { constB_ = val; useConstB_ = true; return *this; }

    /// Set operation by enum
    Logic& op(LogicOp operation) { op_ = static_cast<int>(operation); return *this; }
    /// A > B
    Logic& greaterThan() { op_ = static_cast<int>(LogicOp::GreaterThan); return *this; }
    /// A < B
    Logic& lessThan() { op_ = static_cast<int>(LogicOp::LessThan); return *this; }
    /// A == B (within epsilon)
    Logic& equal() { op_ = static_cast<int>(LogicOp::Equal); return *this; }
    /// Output 1 if A > threshold
    Logic& threshold(float t) { threshold_ = t; op_ = static_cast<int>(LogicOp::Threshold); return *this; }
    /// Output 1 if min <= A <= max
    Logic& inRange(float min, float max) { rangeMin_ = min; rangeMax_ = max; op_ = static_cast<int>(LogicOp::InRange); return *this; }
    /// Toggle state on rising edge
    Logic& toggle() { op_ = static_cast<int>(LogicOp::Toggle); return *this; }
    /// Pulse on rising edge
    Logic& trigger() { op_ = static_cast<int>(LogicOp::Trigger); return *this; }
    /// Pass A when B > 0.5
    Logic& gate() { op_ = static_cast<int>(LogicOp::Gate); return *this; }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        float valA = useConstA_ ? constA_ : ctx.getInputValue(inputA_, "out", 0.0f);
        float valB = useConstB_ ? constB_ : ctx.getInputValue(inputB_, "out", 0.0f);

        float result = 0.0f;
        LogicOp operation = static_cast<LogicOp>(op_);

        switch (operation) {
            case LogicOp::GreaterThan:    result = (valA > valB) ? 1.0f : 0.0f; break;
            case LogicOp::LessThan:       result = (valA < valB) ? 1.0f : 0.0f; break;
            case LogicOp::Equal:          result = (std::abs(valA - valB) < epsilon_) ? 1.0f : 0.0f; break;
            case LogicOp::NotEqual:       result = (std::abs(valA - valB) >= epsilon_) ? 1.0f : 0.0f; break;
            case LogicOp::GreaterOrEqual: result = (valA >= valB) ? 1.0f : 0.0f; break;
            case LogicOp::LessOrEqual:    result = (valA <= valB) ? 1.0f : 0.0f; break;
            case LogicOp::And:            result = (valA > 0.5f && valB > 0.5f) ? 1.0f : 0.0f; break;
            case LogicOp::Or:             result = (valA > 0.5f || valB > 0.5f) ? 1.0f : 0.0f; break;
            case LogicOp::Not:            result = (valA <= 0.5f) ? 1.0f : 0.0f; break;
            case LogicOp::Xor:            result = ((valA > 0.5f) != (valB > 0.5f)) ? 1.0f : 0.0f; break;
            case LogicOp::Threshold:      result = (valA > threshold_) ? 1.0f : 0.0f; break;
            case LogicOp::InRange:        result = (valA >= rangeMin_ && valA <= rangeMax_) ? 1.0f : 0.0f; break;
            case LogicOp::Toggle: {
                bool wasAbove = prevValue_ > threshold_;
                bool isAbove = valA > threshold_;
                if (!wasAbove && isAbove) toggleState_ = !toggleState_;
                result = toggleState_ ? 1.0f : 0.0f;
                break;
            }
            case LogicOp::Trigger: {
                bool wasAbove = prevValue_ > threshold_;
                bool isAbove = valA > threshold_;
                result = (!wasAbove && isAbove) ? 1.0f : 0.0f;
                break;
            }
            case LogicOp::Gate:
                result = (valB > 0.5f) ? valA : 0.0f;
                break;
        }

        prevValue_ = valA;
        ctx.setOutput("out", result);
    }

    std::unique_ptr<OperatorState> saveState() override {
        auto state = std::make_unique<LogicState>();
        state->toggleState = toggleState_;
        state->prevValue = prevValue_;
        return state;
    }

    void loadState(std::unique_ptr<OperatorState> state) override {
        if (auto* s = dynamic_cast<LogicState*>(state.get())) {
            toggleState_ = s->toggleState;
            prevValue_ = s->prevValue;
        }
    }

    std::vector<ParamDecl> params() override {
        return {
            intParam("op", op_, 0, 14),
            floatParam("threshold", threshold_, -1000.0f, 1000.0f),
            floatParam("rangeMin", rangeMin_, -1000.0f, 1000.0f),
            floatParam("rangeMax", rangeMax_, -1000.0f, 1000.0f),
            floatParam("constA", constA_, -1000.0f, 1000.0f),
            floatParam("constB", constB_, -1000.0f, 1000.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Value; }

private:
    std::string inputA_;
    std::string inputB_;
    float constA_ = 0.0f;
    float constB_ = 0.0f;
    bool useConstA_ = false;
    bool useConstB_ = false;
    int op_ = 0;
    float threshold_ = 0.5f;
    float rangeMin_ = 0.0f;
    float rangeMax_ = 1.0f;
    float epsilon_ = 0.0001f;
    bool toggleState_ = false;
    float prevValue_ = 0.0f;
};

} // namespace vivid
