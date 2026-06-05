#pragma once
// plugin_slot.h — triple-buffer plugin-handle swap shared by the VST3/CLAP/AU
// instrument + effect operators (audit 05-R2-F2/F3).
//
// Threading model (extracted verbatim from the operators' hand-rolled logic):
//   - MAIN thread stages a freshly-loaded plugin handle (stage()).
//   - The AUDIO thread swaps it in between process callbacks (swap_in_pending())
//     and retires the previously-active handle into a 2-deep "dying" queue. It
//     NEVER deletes on the audio thread.
//   - MAIN thread reclaims (destroys) retired handles each tick (reclaim()).
//   - A third retire before a main-thread reclaim overflows: the oldest dying
//     handle is dropped (leaked) and on_overflow() is invoked, rather than
//     blocking the audio thread with a delete.
//
// The SDK-specific operations (stop/start processing, validity, destroy) are
// passed as callables so one mechanism serves all plugin standards. The callables
// invoked on the audio path (stop/is_valid/start/destroy/on_overflow) must be
// real-time safe — no allocation, locking, or blocking — exactly as the inlined
// per-operator code is today.
#include <atomic>

namespace vivid::plugin_common {

template <class HandleT>
class PluginSlot {
public:
    PluginSlot() = default;
    PluginSlot(const PluginSlot&) = delete;
    PluginSlot& operator=(const PluginSlot&) = delete;

    // MAIN thread: stage a handle to be swapped in by the next audio callback.
    // Overwrites any not-yet-consumed pending handle (caller owns that case).
    void stage(HandleT* h) { pending_.store(h, std::memory_order_release); }

    // AUDIO thread: install any pending handle, retiring the previous active one.
    //   stop(old)        — stop the outgoing handle's processing before retiring it
    //   is_valid(pend)   — false ⇒ the staged handle failed to load; it is handed to
    //                      destroy() immediately and `active` becomes null
    //   start(pend)      — enable the incoming handle's processing
    //   destroy(pend)    — dispose of an invalid staged handle (only the trivial,
    //                      RT-safe teardown the operators do inline for this case)
    //   on_overflow(h)   — a third retire with no intervening reclaim; `h` is leaked
    // Returns true iff a pending handle was consumed this call.
    template <class StopFn, class IsValidFn, class StartFn, class DestroyFn, class OverflowFn>
    bool swap_in_pending(StopFn&& stop, IsValidFn&& is_valid, StartFn&& start,
                         DestroyFn&& destroy, OverflowFn&& on_overflow) {
        HandleT* pend = pending_.load(std::memory_order_acquire);
        if (!pend) return false;
        pend = pending_.exchange(nullptr, std::memory_order_acq_rel);
        if (!pend) return false;

        HandleT* old = active_.exchange(pend, std::memory_order_acq_rel);
        if (old) {
            stop(old);
            HandleT* prev = dying_.exchange(old, std::memory_order_acq_rel);
            if (prev) {
                HandleT* prev2 = dying2_.exchange(prev, std::memory_order_acq_rel);
                if (prev2) on_overflow(prev2);  // dropped/leaked — see header note
            }
        }
        if (!is_valid(pend)) {
            active_.store(nullptr, std::memory_order_release);
            destroy(pend);
            return true;
        }
        start(pend);
        return true;
    }

    // MAIN thread: destroy any retired handles. Returns the number destroyed.
    template <class DestroyFn>
    int reclaim(DestroyFn&& destroy) {
        int n = 0;
        if (HandleT* d = dying_.exchange(nullptr, std::memory_order_acq_rel)) { destroy(d); ++n; }
        if (HandleT* d = dying2_.exchange(nullptr, std::memory_order_acq_rel)) { destroy(d); ++n; }
        return n;
    }

    // MAIN thread (teardown): destroy active + pending + all retired handles.
    template <class DestroyFn>
    void destroy_all(DestroyFn&& destroy) {
        if (HandleT* a = active_.exchange(nullptr, std::memory_order_acq_rel)) destroy(a);
        if (HandleT* p = pending_.exchange(nullptr, std::memory_order_acq_rel)) destroy(p);
        reclaim(destroy);
    }

    HandleT* active() const { return active_.load(std::memory_order_acquire); }
    bool has_pending() const { return pending_.load(std::memory_order_acquire) != nullptr; }

private:
    std::atomic<HandleT*> active_  {nullptr};
    std::atomic<HandleT*> pending_ {nullptr};
    std::atomic<HandleT*> dying_   {nullptr};
    std::atomic<HandleT*> dying2_  {nullptr};
};

}  // namespace vivid::plugin_common
