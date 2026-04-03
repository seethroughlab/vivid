// Unit tests for Path Animate operator: bezier math, easing functions, loop modes.

#include <cstdio>
#include <cmath>
#include <cstring>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

static void check_near(float actual, float expected, float tol, const char* msg) {
    if (std::fabs(actual - expected) > tol) {
        std::fprintf(stderr, "  FAIL: %s (got %f, expected %f)\n", msg, actual, expected);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

// ---------------------------------------------------------------------------
// Replicate the bezier/easing math from path_animate.cpp for unit testing
// ---------------------------------------------------------------------------

static double bezier(double t, double p0, double p1, double p2, double p3) {
    double u = 1.0 - t;
    return u * u * u * p0
         + 3.0 * u * u * t * p1
         + 3.0 * u * t * t * p2
         + t * t * t * p3;
}

static double bezier_deriv(double t, double p0, double p1, double p2, double p3) {
    double u = 1.0 - t;
    return 3.0 * u * u * (p1 - p0)
         + 6.0 * u * t * (p2 - p1)
         + 3.0 * t * t * (p3 - p2);
}

static double apply_easing(double t, int mode) {
    switch (mode) {
        case 0: return t;
        case 1: return t * t;
        case 2: return t * (2.0 - t);
        case 3: return t < 0.5 ? 2.0 * t * t : -1.0 + (4.0 - 2.0 * t) * t;
        case 4: return t * t * (3.0 - 2.0 * t);
        default: return t;
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_bezier_endpoints() {
    std::fprintf(stderr, "\n--- Bezier Endpoints ---\n");

    // B(0) should equal P0
    check_near(static_cast<float>(bezier(0.0, 0.0, 0.3, 0.7, 1.0)), 0.0f, 1e-6f,
               "B(0) x = P0.x");
    check_near(static_cast<float>(bezier(0.0, 0.0, 0.5, 0.5, 0.0)), 0.0f, 1e-6f,
               "B(0) y = P0.y");

    // B(1) should equal P3
    check_near(static_cast<float>(bezier(1.0, 0.0, 0.3, 0.7, 1.0)), 1.0f, 1e-6f,
               "B(1) x = P3.x");
    check_near(static_cast<float>(bezier(1.0, 0.0, 0.5, 0.5, 0.0)), 0.0f, 1e-6f,
               "B(1) y = P3.y");
}

static void test_bezier_midpoint() {
    std::fprintf(stderr, "\n--- Bezier Midpoint ---\n");

    // For a straight line P0=(0,0), P1=(0.33,0.33), P2=(0.66,0.66), P3=(1,1)
    // B(0.5) should be approximately (0.5, 0.5)
    double mx = bezier(0.5, 0.0, 0.33, 0.66, 1.0);
    double my = bezier(0.5, 0.0, 0.33, 0.66, 1.0);
    check_near(static_cast<float>(mx), 0.5f, 0.02f, "Linear bezier midpoint x ≈ 0.5");
    check_near(static_cast<float>(my), 0.5f, 0.02f, "Linear bezier midpoint y ≈ 0.5");
}

static void test_bezier_derivative() {
    std::fprintf(stderr, "\n--- Bezier Derivative ---\n");

    // For a straight line, derivative should be approximately constant
    double d0 = bezier_deriv(0.0, 0.0, 0.333, 0.667, 1.0);
    double d1 = bezier_deriv(0.5, 0.0, 0.333, 0.667, 1.0);
    double d2 = bezier_deriv(1.0, 0.0, 0.333, 0.667, 1.0);
    check_near(static_cast<float>(d0), static_cast<float>(d1), 0.01f,
               "Linear bezier: derivative at 0 ≈ derivative at 0.5");
    check_near(static_cast<float>(d1), static_cast<float>(d2), 0.01f,
               "Linear bezier: derivative at 0.5 ≈ derivative at 1.0");

    // Derivative at t=0 should be 3*(P1-P0)
    double deriv_at_0 = bezier_deriv(0.0, 0.0, 0.5, 0.7, 1.0);
    check_near(static_cast<float>(deriv_at_0), 3.0f * 0.5f, 1e-6f,
               "B'(0) = 3*(P1-P0)");
}

static void test_easing_boundaries() {
    std::fprintf(stderr, "\n--- Easing Boundaries ---\n");

    // All easing functions should map 0→0 and 1→1
    for (int mode = 0; mode <= 4; ++mode) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "easing[%d](0) = 0", mode);
        check_near(static_cast<float>(apply_easing(0.0, mode)), 0.0f, 1e-6f, buf);
        std::snprintf(buf, sizeof(buf), "easing[%d](1) = 1", mode);
        check_near(static_cast<float>(apply_easing(1.0, mode)), 1.0f, 1e-6f, buf);
    }
}

static void test_easing_monotonic() {
    std::fprintf(stderr, "\n--- Easing Monotonicity ---\n");

    // All easing modes should be monotonically increasing on [0,1]
    for (int mode = 0; mode <= 4; ++mode) {
        bool monotonic = true;
        double prev = 0.0;
        for (int i = 1; i <= 100; ++i) {
            double t = static_cast<double>(i) / 100.0;
            double v = apply_easing(t, mode);
            if (v < prev - 1e-10) {
                monotonic = false;
                break;
            }
            prev = v;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "easing[%d] is monotonic", mode);
        check(monotonic, buf);
    }
}

static void test_easing_shapes() {
    std::fprintf(stderr, "\n--- Easing Shapes ---\n");

    // Ease-in at 0.5 should be < 0.5 (starts slow)
    check(apply_easing(0.5, 1) < 0.5, "Ease-In(0.5) < 0.5");

    // Ease-out at 0.5 should be > 0.5 (starts fast)
    check(apply_easing(0.5, 2) > 0.5, "Ease-Out(0.5) > 0.5");

    // SmoothStep at 0.5 should equal 0.5 (symmetric)
    check_near(static_cast<float>(apply_easing(0.5, 4)), 0.5f, 1e-6f,
               "SmoothStep(0.5) = 0.5");

    // Ease-In-Out at 0.5 should equal 0.5 (symmetric)
    check_near(static_cast<float>(apply_easing(0.5, 3)), 0.5f, 1e-6f,
               "Ease-In-Out(0.5) = 0.5");
}

static void test_loop_modes() {
    std::fprintf(stderr, "\n--- Loop Modes ---\n");

    // Loop mode: phase wraps around
    {
        double phase = 2.7;
        double t = std::fmod(phase, 1.0);
        if (t < 0.0) t += 1.0;
        check_near(static_cast<float>(t), 0.7f, 1e-6f, "Loop: 2.7 wraps to 0.7");
    }

    // Ping-Pong mode
    {
        // phase 0.3 → t = 0.3
        double cycle = std::fmod(0.3, 2.0);
        double t = cycle <= 1.0 ? cycle : 2.0 - cycle;
        check_near(static_cast<float>(t), 0.3f, 1e-6f, "Ping-Pong: 0.3 → 0.3");

        // phase 1.3 → t = 0.7 (bouncing back)
        cycle = std::fmod(1.3, 2.0);
        t = cycle <= 1.0 ? cycle : 2.0 - cycle;
        check_near(static_cast<float>(t), 0.7f, 1e-6f, "Ping-Pong: 1.3 → 0.7");

        // phase 1.0 → t = 1.0
        cycle = std::fmod(1.0, 2.0);
        t = cycle <= 1.0 ? cycle : 2.0 - cycle;
        check_near(static_cast<float>(t), 1.0f, 1e-6f, "Ping-Pong: 1.0 → 1.0");
    }

    // Once+Hold: clamp at 1.0
    {
        double phase = 5.0;
        double t = phase < 0.0 ? 0.0 : (phase > 1.0 ? 1.0 : phase);
        check_near(static_cast<float>(t), 1.0f, 1e-6f, "Once+Hold: 5.0 → 1.0");

        phase = 0.4;
        t = phase < 0.0 ? 0.0 : (phase > 1.0 ? 1.0 : phase);
        check_near(static_cast<float>(t), 0.4f, 1e-6f, "Once+Hold: 0.4 → 0.4");
    }
}

static void test_bezier_angle() {
    std::fprintf(stderr, "\n--- Bezier Angle ---\n");

    // Straight horizontal line P0=(0,0) → P3=(1,0) with control points on the line
    double dx = bezier_deriv(0.5, 0.0, 0.33, 0.67, 1.0);
    double dy = bezier_deriv(0.5, 0.0, 0.0, 0.0, 0.0);
    double angle = std::atan2(dy, dx);
    check_near(static_cast<float>(angle), 0.0f, 0.01f,
               "Horizontal bezier: angle ≈ 0");

    // Straight vertical line P0=(0,0) → P3=(0,1)
    dx = bezier_deriv(0.5, 0.0, 0.0, 0.0, 0.0);
    dy = bezier_deriv(0.5, 0.0, 0.33, 0.67, 1.0);
    angle = std::atan2(dy, dx);
    check_near(static_cast<float>(angle), static_cast<float>(M_PI / 2.0), 0.01f,
               "Vertical bezier: angle ≈ π/2");
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main() {
    std::fprintf(stderr, "=== Path Animate Unit Tests ===\n");

    test_bezier_endpoints();
    test_bezier_midpoint();
    test_bezier_derivative();
    test_easing_boundaries();
    test_easing_monotonic();
    test_easing_shapes();
    test_loop_modes();
    test_bezier_angle();

    std::fprintf(stderr, "\n%s (%d failure%s)\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures > 0 ? 1 : 0;
}
