// Standalone Highway SIMD smoke test — proves the shared dependency path works
// outside any operator or package context. No GPU, no audio, no window.

#ifdef VIVID_HAS_HIGHWAY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "test_highway_smoke.cpp"
#include "hwy/foreach_target.h"
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace vivid_test {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

void VectorMul(const float* HWY_RESTRICT a, const float* HWY_RESTRICT b,
               float* HWY_RESTRICT out, size_t n) {
    const hn::ScalableTag<float> d;
    size_t i = 0;
    for (; i + hn::Lanes(d) <= n; i += hn::Lanes(d)) {
        auto va = hn::Load(d, a + i);
        auto vb = hn::Load(d, b + i);
        hn::Store(hn::Mul(va, vb), d, out + i);
    }
    // Scalar tail
    for (; i < n; ++i)
        out[i] = a[i] * b[i];
}

}  // namespace HWY_NAMESPACE
}  // namespace vivid_test
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace vivid_test {
HWY_EXPORT(VectorMul);
void CallVectorMul(const float* a, const float* b, float* out, size_t n) {
    HWY_DYNAMIC_DISPATCH(VectorMul)(a, b, out, n);
}
}  // namespace vivid_test

#include <cstdio>
#include <cmath>

static bool approx_eq(float a, float b) {
    return std::fabs(a - b) < 1e-6f;
}

int main() {
    constexpr size_t N = 8;
    float a[N] = {1, 2, 3, 4, 5, 6, 7, 8};
    float b[N] = {8, 7, 6, 5, 4, 3, 2, 1};
    float c[N] = {};
    float expected[N] = {8, 14, 18, 20, 20, 18, 14, 8};

    vivid_test::CallVectorMul(a, b, c, N);

    bool ok = true;
    for (size_t i = 0; i < N; ++i) {
        if (!approx_eq(c[i], expected[i])) {
            std::fprintf(stderr, "FAIL: c[%zu] = %f, expected %f\n", i, c[i], expected[i]);
            ok = false;
        }
    }

    std::printf("Highway SIMD smoke: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
#endif  // HWY_ONCE

#else  // !VIVID_HAS_HIGHWAY

#include <cstdio>

int main() {
    std::printf("Highway disabled (scalar-only build): PASS\n");
    return 0;
}

#endif  // VIVID_HAS_HIGHWAY
