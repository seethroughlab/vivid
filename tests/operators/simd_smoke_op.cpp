// Operator fixture proving Highway compiles and runs inside the plugin path.
// Uses SIMD when available, scalar fallback otherwise.

#include "operator_api/operator.h"

#ifdef VIVID_HAS_HIGHWAY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "simd_smoke_op.cpp"
#include "hwy/foreach_target.h"
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace vivid_simd_smoke {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

float SimdSum(const float* HWY_RESTRICT data, size_t n) {
    const hn::ScalableTag<float> d;
    auto acc = hn::Zero(d);
    size_t i = 0;
    for (; i + hn::Lanes(d) <= n; i += hn::Lanes(d))
        acc = hn::Add(acc, hn::Load(d, data + i));
    float result = hn::ReduceSum(d, acc);
    // Scalar tail
    for (; i < n; ++i)
        result += data[i];
    return result;
}

}  // namespace HWY_NAMESPACE
}  // namespace vivid_simd_smoke
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace vivid_simd_smoke {
HWY_EXPORT(SimdSum);
float CallSimdSum(const float* data, size_t n) {
    return HWY_DYNAMIC_DISPATCH(SimdSum)(data, n);
}
}  // namespace vivid_simd_smoke
#endif  // HWY_ONCE

#endif  // VIVID_HAS_HIGHWAY

#if !defined(VIVID_HAS_HIGHWAY) || HWY_ONCE

struct SimdSmokeOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "SimdSmokeOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> value{"value", 1.0f, 0.0f, 10.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&value);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        float v = ctx->param_values[0];
#ifdef VIVID_HAS_HIGHWAY
        float arr[4] = {v, v, v, v};
        ctx->output_values[0] = vivid_simd_smoke::CallSimdSum(arr, 4);
#else
        ctx->output_values[0] = v * 4.0f;
#endif
    }
};

VIVID_REGISTER(SimdSmokeOp)

#endif  // HWY_ONCE
