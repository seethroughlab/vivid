// Unit tests for the unified value-storage substrate (lane-value clean-break,
// Phase 3): ValueBuffer / ValueRef / ValueArena / BridgeValueSlot.
//
// These represent scalar and many values of any CPU payload (float/string/custom)
// in one buffer + a per-domain arena, with the RT-safety contract (fixed-capacity
// arenas/buffers never allocate on overflow) and bounded bridge overflow. Nothing
// executes this yet (Phases 4-5 wire it) — the tests are the validation.

#include "runtime/graph/value_buffer.h"
#include "runtime/graph/value_arena.h"
#include "runtime/graph/snapshot_types.h"
#include <cstdio>
#include <vector>
#include "test_helpers.h"

using namespace vivid;

int main() {
    std::fprintf(stderr, "--- test_value_buffer ---\n");

    // ---- ValueBuffer: float (scalar + many) -------------------------------
    {
        ValueBuffer vb(VIVID_VALUE_FLOAT, 8);
        check(vb.capacity() == 8, "float buffer capacity");
        check(vb.ensure(3), "ensure within capacity");
        float* w = vb.floats_ptr();
        check(w != nullptr, "typed float accessor");
        w[0] = 1; w[1] = 2; w[2] = 3;
        vb.commit(3);
        check(vb.committed_count == 3, "committed 3");
        check(vb.envelope.multiplicity == VIVID_MULTIPLICITY_MANY, "3 values => Many");
        check(vb.floats[0] == 1 && vb.floats[2] == 3, "float data retained");

        vb.commit(1);
        check(vb.envelope.multiplicity == VIVID_MULTIPLICITY_SCALAR, "1 value => Scalar");
        check(vb.bytes_ptr() == nullptr, "byte accessor rejects a Float buffer");
    }

    // ---- ValueBuffer: string ----------------------------------------------
    {
        ValueBuffer vb(VIVID_VALUE_STRING, 4);
        check(vb.ensure(2), "string ensure");
        vb.set_string(0, "alpha");
        vb.set_string(1, "beta");
        vb.commit(2);
        check(vb.strings[0] == "alpha" && vb.strings[1] == "beta", "string data retained");
        check(vb.floats_ptr() == nullptr, "float accessor rejects a String buffer");
    }

    // ---- ValueBuffer: custom bytes ----------------------------------------
    {
        ValueBuffer vb(VIVID_VALUE_CUSTOM, 16);
        check(vb.ensure(4), "bytes ensure");
        uint8_t* b = vb.bytes_ptr();
        check(b != nullptr, "byte accessor");
        b[0] = 0xAB; b[3] = 0xCD;
        vb.commit(4);
        check(vb.bytes[0] == 0xAB && vb.bytes[3] == 0xCD, "byte data retained");
    }

    // ---- RT-safety: fixed (audio) buffer never allocates on overflow ------
    {
        ValueBuffer vb(VIVID_VALUE_FLOAT, 4);
        vb.pool_owned = true;
        vb.allow_grow = false;                 // audio-thread semantics
        check(!vb.ensure(5), "fixed buffer: ensure beyond capacity returns false");
        check(vb.capacity() == 4, "fixed buffer: capacity unchanged (no alloc)");

        ValueBuffer fb(VIVID_VALUE_FLOAT, 4);
        fb.pool_owned = true;
        fb.allow_grow = true;                  // frame-thread semantics
        check(fb.ensure(9), "growable buffer: ensure beyond capacity grows");
        check(fb.capacity() >= 9, "growable buffer: capacity grew");
    }

    // ---- ValueRef: intrusive refcount + RAII ------------------------------
    {
        ValueBuffer vb(VIVID_VALUE_FLOAT, 4);
        vb.ensure(2); vb.floats[0] = 7; vb.commit(2);
        {
            ValueRef r(&vb);
            check(vb.ref_count.load() == 1, "ValueRef retains");
            ValueRef r2 = r;  // copy = retain
            check(vb.ref_count.load() == 2, "copy retains");
            check(r2.count() == 2 && r2.floats()[0] == 7, "ref reads buffer");
        }
        check(vb.ref_count.load() == 0, "RAII released both refs");
    }

    // ---- ValueArena: fixed (audio) vs growable (frame) --------------------
    {
        ValueArena fixed(VIVID_VALUE_FLOAT, 16, /*growable=*/false);
        fixed.prewarm(2);
        check(fixed.total_count() == 2 && fixed.free_count() == 2, "fixed arena prewarmed 2");
        ValueBuffer* a = fixed.acquire();
        ValueBuffer* b = fixed.acquire();
        check(a && b, "acquired 2 from fixed arena");
        check(fixed.acquire() == nullptr, "fixed arena exhausted: acquire returns null (no alloc)");
        check(fixed.total_count() == 2, "fixed arena did not allocate on exhaustion");
        // Returning a buffer (ref_count back to 0) lets sweep reclaim it.
        a->retain(); a->release();             // net 0
        fixed.sweep();
        check(fixed.free_count() >= 1, "sweep reclaims a ref_count==0 buffer");

        ValueArena grow(VIVID_VALUE_FLOAT, 16, /*growable=*/true);
        ValueBuffer* g = grow.acquire();       // allocates on demand
        check(g != nullptr && grow.total_count() == 1, "growable arena allocates on acquire");
    }

    // ---- BridgeValueSlot: clamp + overflow accounting (01-R2-F7) -----------
    {
        std::vector<float> storage(4, 0.0f);
        BridgeValueSlot slot;
        slot.data = storage.data();
        slot.capacity = 4;
        ValueHealthCounters health;

        const float in_ok[3] = {1, 2, 3};
        bool of1 = slot.write_clamped(in_ok, 3);
        check(!of1 && slot.length == 3, "within capacity: no overflow, length=3");
        check(storage[0] == 1 && storage[2] == 3, "data copied");

        const float in_big[6] = {9, 9, 9, 9, 9, 9};
        bool of2 = slot.write_clamped(in_big, 6);
        if (of2) ++health.bridge_overflow;
        check(of2 && slot.length == 4, "over capacity: clamped to 4, overflow reported");
        check(health.bridge_overflow == 1, "bridge_overflow counter incremented");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
