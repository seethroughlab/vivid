#include "ui/rendering/renderer_2d.h"

#include <cstdio>

#include "test_helpers.h"

int main() {
    std::fprintf(stderr, "=== Test: Renderer2D scissor clamp ===\n\n");

    using vivid::ui::detail::PhysicalScissorRect;
    using vivid::ui::detail::clamp_physical_scissor_rect;

    {
        PhysicalScissorRect rect{};
        const bool ok = clamp_physical_scissor_rect(10.0f, 20.0f, 30.0f, 40.0f,
                                                    640, 360, &rect);
        check(ok, "exact in-bounds rect clamps successfully");
        if (ok) {
            check(rect.x == 10, "exact rect x preserved");
            check(rect.y == 20, "exact rect y preserved");
            check(rect.width == 30, "exact rect width preserved");
            check(rect.height == 40, "exact rect height preserved");
        }
    }

    {
        PhysicalScissorRect rect{};
        const bool ok = clamp_physical_scissor_rect(1200.0f, 680.0f, 200.0f, 100.0f,
                                                    1280, 720, &rect);
        check(ok, "HiDPI framebuffer clamp succeeds near framebuffer edge");
        if (ok) {
            check(rect.x == 1200, "HiDPI clamp preserves x origin");
            check(rect.y == 680, "HiDPI clamp preserves y origin");
            check(rect.width == 80, "HiDPI clamp trims width to framebuffer");
            check(rect.height == 40, "HiDPI clamp trims height to framebuffer");
        }
    }

    {
        PhysicalScissorRect rect{};
        const bool ok = clamp_physical_scissor_rect(-15.0f, -10.0f, 700.0f, 500.0f,
                                                    640, 360, &rect);
        check(ok, "oversized rect clamps to framebuffer bounds");
        if (ok) {
            check(rect.x == 0, "oversized rect clamps x to zero");
            check(rect.y == 0, "oversized rect clamps y to zero");
            check(rect.width == 640, "oversized rect width clamps to framebuffer width");
            check(rect.height == 360, "oversized rect height clamps to framebuffer height");
        }
    }

    {
        PhysicalScissorRect rect{};
        const bool ok = clamp_physical_scissor_rect(900.0f, 500.0f, 50.0f, 50.0f,
                                                    640, 360, &rect);
        check(!ok, "fully off-surface rect is rejected");
    }

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
