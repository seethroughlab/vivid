// ADR-0053 Phase B: the migration gate. Proves the new typed control-edge resolver
// (control_shape.h::visual_control_resolve / visual_control_advance) is BYTE-IDENTICAL to the Phase-A
// pair it supersedes — NodeGraph::apply_params combined with MappingRegistry::dest_value (mapping.h) —
// across the full shaping-parameter space (amount / curve / invert / out range / declared range) AND
// across the envelope-follower's attack/release smoothing over a signal sequence. If either formula
// ever drifts, a migrated project would render different pixels; this test catches that drift.
#include "control_shape.h"
#include "mapping.h"
#include "test_helpers.h"

#include <cmath>
#include <utility>
#include <vector>

using namespace vivid;

// The OLD resolve (no smoothing): mapping_shaped -> [out_lo,out_hi] -> *amount -> base + mod*range, clamp.
// Mirrors MappingRegistry::dest_value (unsmoothed branch) fed into apply_params' clamp(base+mod*(hi-lo)).
static float old_resolve(float base, float src, const Mapping& m, float pmin, float pmax) {
    const float shaped = mapping_shaped(m, src);                 // clamp01 -> invert -> curve
    const float mod = (m.out_lo + (m.out_hi - m.out_lo) * shaped) * m.amount;   // range, then gain
    const float v = base + mod * (pmax - pmin);
    return v < pmin ? pmin : (v > pmax ? pmax : v);
}

int main() {
    const float amounts[]  = { 0.f, 0.25f, 0.6f, 1.f, 2.f };
    const float curves[]   = { -1.f, -0.5f, 0.f, 0.5f, 1.f };
    const bool  inverts[]  = { false, true };
    const std::pair<float,float> outRanges[] = { {0.f,1.f}, {0.2f,0.8f}, {0.5f,0.5f}, {-1.f,1.f} };
    const std::pair<float,float> pRanges[]   = { {0.f,1.f}, {0.f,10.f}, {-5.f,5.f}, {1.f,1.f} };
    const float bases[]    = { 0.f, 0.3f, 1.f, -2.f };
    const float srcs[]     = { 0.f, 0.13f, 0.5f, 0.87f, 1.f, -0.2f, 1.4f };   // incl. out-of-[0,1]

    // --- Static (unsmoothed) parity across the full shaping space ---------------------------------
    long grid = 0;
    for (float amount : amounts)
      for (float curve : curves)
        for (bool invert : inverts)
          for (auto lohi : outRanges)
            for (auto pr : pRanges)
              for (float base : bases)
                for (float src : srcs) {
                    Mapping m; m.amount = amount; m.curve = curve; m.invert = invert;
                    m.out_lo = lohi.first; m.out_hi = lohi.second;
                    VisualControlShape sh; sh.amount = amount; sh.curve = curve; sh.invert = invert;
                    sh.out_lo = lohi.first; sh.out_hi = lohi.second;
                    const float a = old_resolve(base, src, m, pr.first, pr.second);
                    const float b = visual_control_resolve(base, src, sh, pr.first, pr.second);
                    CHECK_NEAR(a, b, 1e-6);
                    ++grid;
                }
    CHECK(grid > 5000);   // the space was actually swept

    // --- Envelope parity: step-by-step against MappingRegistry::advance over a signal sequence -----
    // A control edge's per-frame smoothing must track the registry's one-pole exactly, so a project
    // migrated mid-motion doesn't visibly jump.
    {
        const float dst_base = 0.4f, pmin = 0.f, pmax = 8.f;
        Mapping m; m.amount = 1.3f; m.curve = 0.5f; m.invert = true;
        m.out_lo = 0.1f; m.out_hi = 0.9f; m.attack = 0.02f; m.release = 0.25f;
        MappingRegistry reg;
        reg.set_source("s", 0.f);
        reg.connect("s", "d", m.amount);
        Mapping* rm = reg.find("d");
        rm->curve = m.curve; rm->invert = m.invert; rm->out_lo = m.out_lo; rm->out_hi = m.out_hi;
        rm->attack = m.attack; rm->release = m.release;

        VisualControlShape sh; sh.amount = m.amount; sh.curve = m.curve; sh.invert = m.invert;
        sh.out_lo = m.out_lo; sh.out_hi = m.out_hi; sh.attack = m.attack; sh.release = m.release;

        // A source sequence with rises and falls (exercises both attack and release constants).
        const float seq[] = { 0.f, 1.f, 1.f, 0.6f, 0.6f, 0.2f, 0.9f, 0.9f, 0.0f, 0.0f, 0.55f };
        const float dt = 1.f / 60.f;
        for (float s : seq) {
            reg.set_source("s", s);
            reg.advance(dt);
            visual_control_advance(sh, s, dt);
            CHECK_NEAR(rm->smoothed, sh.smoothed, 1e-6);
            const float regv = std::clamp(dst_base + reg.dest_value("d") * (pmax - pmin), pmin, pmax);
            const float edgev = visual_control_resolve(dst_base, s, sh, pmin, pmax);
            CHECK_NEAR(regv, edgev, 1e-6);
        }
    }

    return 0;
}
