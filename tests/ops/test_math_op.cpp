// Unit tests for the Math control operator.
//
// Loads the math.dylib via OperatorLoader and exercises every operation
// (add, multiply, min, max, subtract, divide, modulo) including:
//   - divide by zero returns 0 (safe)
//   - modulo by zero returns 0 (safe)
//   - Euclidean modulo semantics: -1 mod 4 = 3
//
// Partition 10: pure CPU, no GPU/audio/window.

#include "runtime/operators/operator_loader.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "test_helpers.h"

namespace {

constexpr int kAdd      = 0;
constexpr int kMultiply = 1;
constexpr int kMin      = 2;
constexpr int kMax      = 3;
constexpr int kSubtract = 4;
constexpr int kDivide   = 5;
constexpr int kModulo   = 6;

struct MathHarness {
    vivid::OperatorLoader loader;
    void* instance = nullptr;
    float params[1]  = {0.0f};
    float inputs[2]  = {0.0f, 0.0f};
    float outputs[1] = {0.0f};

    bool load(const std::string& path) {
        if (!loader.load(path.c_str())) return false;
        instance = loader.create_instance();
        return instance != nullptr;
    }

    ~MathHarness() {
        if (instance) loader.destroy_instance(instance);
    }

    float run(int op, float a, float b) {
        params[0]  = static_cast<float>(op);
        inputs[0]  = a;
        inputs[1]  = b;
        outputs[0] = 0.0f;

        VividFrameContext ctx{};
        ctx.time           = 0.0;
        ctx.delta_time     = 1.0 / 60.0;
        ctx.frame          = 0;
        ctx.param_values   = params;
        ctx.input_values   = inputs;
        ctx.output_values  = outputs;

        loader.process_frame(instance, &ctx);
        return outputs[0];
    }
};

void test_descriptor(const vivid::OperatorLoader& loader) {
    std::fprintf(stderr, "\n--- Descriptor ---\n");
    const auto* desc = loader.descriptor();
    check(desc != nullptr, "descriptor not null");
    if (!desc) return;

    check(std::strcmp(desc->name, "Math") == 0, "descriptor name is \"Math\"");
    check(desc->param_count == 1, "one param (operation)");
    check(desc->port_count  == 3, "three ports (a, b, result)");

    // Confirm the operation param exposes all seven choices.
    check(desc->params[0].choice_count == 7, "operation has 7 choices");
    if (desc->params[0].choice_count >= 7) {
        const char* const* c = desc->params[0].choice_labels;
        check(std::strcmp(c[0], "add")      == 0, "choice 0 = add");
        check(std::strcmp(c[1], "multiply") == 0, "choice 1 = multiply");
        check(std::strcmp(c[2], "min")      == 0, "choice 2 = min");
        check(std::strcmp(c[3], "max")      == 0, "choice 3 = max");
        check(std::strcmp(c[4], "subtract") == 0, "choice 4 = subtract");
        check(std::strcmp(c[5], "divide")   == 0, "choice 5 = divide");
        check(std::strcmp(c[6], "modulo")   == 0, "choice 6 = modulo");
    }
}

void test_arithmetic(MathHarness& h) {
    std::fprintf(stderr, "\n--- Arithmetic ---\n");

    check_float(h.run(kAdd,       2.0f,  3.0f),  5.0f, "add(2,3) = 5");
    check_float(h.run(kAdd,      -1.5f,  0.5f), -1.0f, "add(-1.5,0.5) = -1");
    check_float(h.run(kMultiply,  2.0f,  3.0f),  6.0f, "multiply(2,3) = 6");
    check_float(h.run(kMultiply, -2.0f,  3.0f), -6.0f, "multiply(-2,3) = -6");
    check_float(h.run(kMin,       2.0f,  3.0f),  2.0f, "min(2,3) = 2");
    check_float(h.run(kMin,      -2.0f, -3.0f), -3.0f, "min(-2,-3) = -3");
    check_float(h.run(kMax,       2.0f,  3.0f),  3.0f, "max(2,3) = 3");
    check_float(h.run(kSubtract,  5.0f,  3.0f),  2.0f, "subtract(5,3) = 2");
    check_float(h.run(kSubtract,  3.0f,  5.0f), -2.0f, "subtract(3,5) = -2 (a-b order)");
}

void test_divide(MathHarness& h) {
    std::fprintf(stderr, "\n--- Divide ---\n");

    check_float(h.run(kDivide, 6.0f,  2.0f),  3.0f, "divide(6,2) = 3");
    check_float(h.run(kDivide, 1.0f,  4.0f),  0.25f, "divide(1,4) = 0.25");
    check_float(h.run(kDivide, -6.0f, 2.0f), -3.0f, "divide(-6,2) = -3");
    check_float(h.run(kDivide, 6.0f,  0.0f),  0.0f, "divide by zero returns 0");
    check_float(h.run(kDivide, 6.0f,  1e-12f), 0.0f, "divide by near-zero returns 0");
    check_float(h.run(kDivide, 6.0f, -1e-12f), 0.0f, "divide by negative near-zero returns 0");
}

void test_modulo_euclidean(MathHarness& h) {
    std::fprintf(stderr, "\n--- Modulo (Euclidean) ---\n");

    // Basic positive cases.
    check_float(h.run(kModulo, 7.0f,  3.0f),  1.0f, "modulo(7,3) = 1");
    check_float(h.run(kModulo, 6.0f,  3.0f),  0.0f, "modulo(6,3) = 0");
    check_float(h.run(kModulo, 0.0f,  5.0f),  0.0f, "modulo(0,5) = 0");
    check_float(h.run(kModulo, 5.5f,  2.0f),  1.5f, "modulo(5.5,2) = 1.5 (fractional)");

    // Euclidean semantics: the key win — negative dividends wrap non-negative
    // for the common positive-divisor case. This is what lets `step % N` always
    // land in [0, N) for cycle indexing.
    check_float(h.run(kModulo, -1.0f, 4.0f),  3.0f, "modulo(-1,4) = 3 (Euclidean)");
    check_float(h.run(kModulo, -5.0f, 3.0f),  1.0f, "modulo(-5,3) = 1 (Euclidean)");
    check_float(h.run(kModulo, -4.0f, 4.0f),  0.0f, "modulo(-4,4) = 0 (exact boundary)");

    // Divide-by-zero safety.
    check_float(h.run(kModulo,  7.0f, 0.0f),  0.0f, "modulo by zero returns 0");
    check_float(h.run(kModulo,  7.0f, 1e-12f), 0.0f, "modulo by near-zero returns 0");

    // Counter-like usage: monotonic input, fixed divisor N — expected cell
    // index after wrap. Mirrors the arpeggiator step-counter use case.
    for (int step = 0; step < 33; ++step) {
        float got = h.run(kModulo, static_cast<float>(step), 16.0f);
        float want = static_cast<float>(step % 16);
        if (std::fabs(got - want) > 1e-4f) {
            std::fprintf(stderr, "  FAIL: step=%d got=%f want=%f\n", step, got, want);
            failures++;
            return;
        }
    }
    std::fprintf(stderr, "  PASS: step-counter wrap matches integer modulo for 0..32\n");
}

void test_default_operation(MathHarness& h) {
    std::fprintf(stderr, "\n--- Default operation ---\n");
    // Default (operation=0) must remain "add" for backward compatibility with
    // every existing graph JSON on disk.
    check_float(h.run(kAdd, 2.0f, 3.0f), 5.0f, "default op is add (backward compat)");
}

} // namespace

int main(int argc, char** argv) {
    const std::string build_dir = (argc > 1) ? argv[1] : ".";
    const std::string path = build_dir + "/math.dylib";

    if (!std::filesystem::exists(path)) {
        std::fprintf(stderr, "FATAL: %s not found (build math.dylib first)\n", path.c_str());
        return 1;
    }

    std::fprintf(stderr, "=== Test: Math operator ===\n");

    MathHarness h;
    if (!h.load(path)) {
        std::fprintf(stderr, "FATAL: could not load %s\n", path.c_str());
        return 1;
    }

    test_descriptor(h.loader);
    test_default_operation(h);
    test_arithmetic(h);
    test_divide(h);
    test_modulo_euclidean(h);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
