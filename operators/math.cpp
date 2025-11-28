// Math Operator
// Performs arithmetic operations on numeric values from other operators

#include <vivid/vivid.h>
#include <algorithm>
#include <cmath>

using namespace vivid;

// Math operation modes
enum class MathOp {
    Add = 0,
    Subtract,
    Multiply,
    Divide,
    Min,
    Max,
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
    Mix
};

class Math : public Operator {
public:
    Math() = default;

    // Fluent API for inputs
    Math& a(const std::string& node) { inputA_ = node; return *this; }
    Math& b(const std::string& node) { inputB_ = node; return *this; }
    Math& a(float val) { constA_ = val; useConstA_ = true; return *this; }
    Math& b(float val) { constB_ = val; useConstB_ = true; return *this; }

    // Operation selection
    Math& op(MathOp operation) { op_ = static_cast<int>(operation); return *this; }
    Math& add() { op_ = static_cast<int>(MathOp::Add); return *this; }
    Math& subtract() { op_ = static_cast<int>(MathOp::Subtract); return *this; }
    Math& multiply() { op_ = static_cast<int>(MathOp::Multiply); return *this; }
    Math& divide() { op_ = static_cast<int>(MathOp::Divide); return *this; }
    Math& min() { op_ = static_cast<int>(MathOp::Min); return *this; }
    Math& max() { op_ = static_cast<int>(MathOp::Max); return *this; }
    Math& clamp() { op_ = static_cast<int>(MathOp::Clamp); return *this; }
    Math& remap() { op_ = static_cast<int>(MathOp::Remap); return *this; }

    // Remap parameters
    Math& inMin(float v) { inMin_ = v; return *this; }
    Math& inMax(float v) { inMax_ = v; return *this; }
    Math& outMin(float v) { outMin_ = v; return *this; }
    Math& outMax(float v) { outMax_ = v; return *this; }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        // Get input values
        float valA = useConstA_ ? constA_ : ctx.getInputValue(inputA_, "out", 0.0f);
        float valB = useConstB_ ? constB_ : ctx.getInputValue(inputB_, "out", 0.0f);

        float result = 0.0f;
        MathOp operation = static_cast<MathOp>(op_);

        switch (operation) {
            case MathOp::Add:
                result = valA + valB;
                break;
            case MathOp::Subtract:
                result = valA - valB;
                break;
            case MathOp::Multiply:
                result = valA * valB;
                break;
            case MathOp::Divide:
                result = (valB != 0.0f) ? valA / valB : 0.0f;
                break;
            case MathOp::Min:
                result = std::min(valA, valB);
                break;
            case MathOp::Max:
                result = std::max(valA, valB);
                break;
            case MathOp::Clamp:
                result = std::clamp(valA, valB, constB_);  // clamp(value, min, max)
                break;
            case MathOp::Remap: {
                // Remap valA from [inMin, inMax] to [outMin, outMax]
                float t = (valA - inMin_) / (inMax_ - inMin_ + 0.0001f);
                result = outMin_ + t * (outMax_ - outMin_);
                break;
            }
            case MathOp::Abs:
                result = std::abs(valA);
                break;
            case MathOp::Sin:
                result = std::sin(valA);
                break;
            case MathOp::Cos:
                result = std::cos(valA);
                break;
            case MathOp::Pow:
                result = std::pow(valA, valB);
                break;
            case MathOp::Sqrt:
                result = std::sqrt(std::max(0.0f, valA));
                break;
            case MathOp::Floor:
                result = std::floor(valA);
                break;
            case MathOp::Ceil:
                result = std::ceil(valA);
                break;
            case MathOp::Fract:
                result = valA - std::floor(valA);
                break;
            case MathOp::Mix:
                // Mix between A and B using constB_ as factor
                result = valA + (valB - valA) * constB_;
                break;
        }

        ctx.setOutput("out", result);
    }

    std::vector<ParamDecl> params() override {
        return {
            intParam("op", op_, 0, 16),
            floatParam("constA", constA_, -1000.0f, 1000.0f),
            floatParam("constB", constB_, -1000.0f, 1000.0f),
            floatParam("inMin", inMin_, -1000.0f, 1000.0f),
            floatParam("inMax", inMax_, -1000.0f, 1000.0f),
            floatParam("outMin", outMin_, -1000.0f, 1000.0f),
            floatParam("outMax", outMax_, -1000.0f, 1000.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Value; }

private:
    std::string inputA_;
    std::string inputB_;
    float constA_ = 0.0f;
    float constB_ = 1.0f;
    bool useConstA_ = false;
    bool useConstB_ = false;
    int op_ = 0;  // MathOp::Add

    // Remap parameters
    float inMin_ = 0.0f;
    float inMax_ = 1.0f;
    float outMin_ = 0.0f;
    float outMax_ = 1.0f;
};

VIVID_OPERATOR(Math)
