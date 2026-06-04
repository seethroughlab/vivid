#pragma once
// macro_bank.h — Shared variadic "macro" machinery for plugin host operators.
//
// A *macro* is a wire/modulation-drivable Param<float> (range [0,1]) paired with a
// Param<TextValue> ("<float>_id") naming the plugin parameter it maps to. Because the
// float knob is modulation-drivable, the number of macros is the number of plugin
// parameters that can be modulated simultaneously on one node.
//
// This replaces the per-operator fixed-8 macro arrays that used to be duplicated across
// vst3_instrument / clap_instrument / clap_effect / au_instrument. Codegen builds the
// static param descriptor from a default-constructed instance's collect_params(), so the
// COUNT is fixed at registration — kMaxMacros is a generous fixed cap, and the inspector
// shows only active slots + one empty "add" slot (the established repeat-group UX).
//
// Threading: all resolution / active-count writes happen on the MAIN thread in update();
// the AUDIO thread only does acquire-loads + per-entry atomics in emit_changes(). The
// fixed-size std::array members never allocate, so the audio callback is allocation-free.
// The separate DirectParamQueue (static one-shot MCP sets) is orthogonal and unaffected.

#include "operator_api/operator.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

// Generous fixed maximum number of macro slots per plugin node. Raise-only: lowering it
// would orphan macros saved at higher indices. 32 clears the practical ceiling for busy
// synths (Serum/Vital/Pigments) while keeping static-descriptor cost bounded.
static constexpr int kMaxMacros = 32;

// MacroBank<ParamIdT, kInvalid>
//   ParamIdT — the plugin SDK's parameter-id type (Steinberg::Vst::ParamID, clap_id,
//              AudioUnitParameterID — all unsigned 32-bit).
//   kInvalid — the SDK's "no parameter" sentinel.
template <typename ParamIdT, ParamIdT kInvalid>
struct MacroBank {
    // One macro slot: the two host-visible params plus stable name storage.
    struct Slot {
        char float_name[12];   // "macro_<n>"     — stable storage for Param::name
        char id_name[16];      // "macro_<n>_id"
        vivid::Param<float>            knob{nullptr, 0.f, 0.f, 1.f};
        vivid::Param<vivid::TextValue> id  {nullptr};
    };

    // Audio-thread cache for one resolved slot. `id` is the synchronization point:
    // min_val/max_val/units are plain fields written before id.store(release) on the
    // main thread and read after id.load(acquire) on the audio thread.
    struct Entry {
        std::atomic<ParamIdT> id        {kInvalid};
        std::atomic<float>    last_sent {-1.f};
        double                min_val = 0.0;   // CLAP/AU rescale [0,1]->native; VST3 ignores
        double                max_val = 1.0;
        char                  units[32] = {};  // AU display only
    };

    std::array<Slot, kMaxMacros>  slots_;
    std::array<Entry, kMaxMacros> entries_;
    // Highest used slot index + 1, written by update() (main) and read by emit_changes()
    // (audio) so the audio loop skips trailing empty slots instead of scanning all 32.
    std::atomic<int>              active_count_{0};

    // -- Construction (main thread, ctor) -----------------------------------------------
    // Generate stable names, descriptions, and metadata for every slot. Float knobs are
    // hidden (drawn manually by the operator's custom inspector); the id strings must NOT
    // be hidden — hidden TextValue params are stripped from file_param_values, which lets
    // _vivid_sync_params clobber str_value back to "" each frame.
    void init_params() {
        for (int i = 0; i < kMaxMacros; ++i) {
            Slot& s = slots_[i];
            std::snprintf(s.float_name, sizeof(s.float_name), "macro_%d", i);
            std::snprintf(s.id_name,    sizeof(s.id_name),    "macro_%d_id", i);
            s.knob.name = s.float_name;
            s.id.name   = s.id_name;
            vivid::description(s.knob, "Macro value (0-1 normalized), mapped to the plugin "
                                       "parameter named in the paired _id param");
            vivid::description(s.id,   "Plugin parameter name this macro controls");
            vivid::display_hint(s.knob, VIVID_DISPLAY_HIDDEN);
            vivid::repeat_group(s.knob, "macro", static_cast<uint16_t>(i));
            vivid::repeat_group(s.id,   "macro", static_cast<uint16_t>(i));
        }
    }

    // Append every slot's params in (knob, id) order. Call from collect_params().
    void collect(std::vector<vivid::ParamBase*>& out) {
        for (Slot& s : slots_) {
            out.push_back(&s.knob);
            out.push_back(&s.id);
        }
    }

    // First slot with an empty id name (the "+ Add" target), or -1 if all are taken.
    int first_empty() const {
        for (int i = 0; i < kMaxMacros; ++i)
            if (slots_[i].id.str_value.empty()) return i;
        return -1;
    }

    // Clear all resolved mappings (call on plugin change so stale ids aren't reused).
    void reset() {
        for (Entry& e : entries_) {
            e.id.store(kInvalid, std::memory_order_relaxed);
            e.last_sent.store(-1.f, std::memory_order_relaxed);
        }
    }

    // -- Main thread: resolve newly-named slots to plugin param ids ---------------------
    // resolve(name, ParamIdT& out_id, Entry& e) -> bool
    //   Looks up `name` in the active plugin's param list; on success fills out_id (and
    //   e.min_val/max_val/units for CLAP/AU) and returns true. Returns false if the
    //   plugin isn't loaded or the name is unknown.
    template <typename ResolveFn>
    void update(ResolveFn&& resolve) {
        int active = 0;
        for (int i = 0; i < kMaxMacros; ++i) {
            const std::string& name = slots_[i].id.str_value;
            if (name.empty()) {
                entries_[i].id.store(kInvalid, std::memory_order_relaxed);
                continue;
            }
            active = i + 1;
            if (entries_[i].id.load(std::memory_order_relaxed) != kInvalid) continue;

            ParamIdT out_id = kInvalid;
            if (resolve(name, out_id, entries_[i])) {
                entries_[i].last_sent.store(-1.f, std::memory_order_relaxed);
                entries_[i].id.store(out_id, std::memory_order_release);
            }
        }
        active_count_.store(active, std::memory_order_release);
    }

    // -- Audio thread: emit changed macro values ----------------------------------------
    // emit(ParamIdT id, float normalized_value, const Entry& e)
    //   Operator-supplied: performs the SDK-specific param change, rescaling from
    //   normalized [0,1] to native range via e.min_val/e.max_val where needed.
    template <typename EmitFn>
    void emit_changes(EmitFn&& emit) {
        const int n = active_count_.load(std::memory_order_acquire);
        for (int i = 0; i < n; ++i) {
            const ParamIdT id = entries_[i].id.load(std::memory_order_acquire);
            if (id == kInvalid) continue;
            const float v    = slots_[i].knob.value;
            const float last = entries_[i].last_sent.load(std::memory_order_relaxed);
            if (std::fabs(v - last) < 1e-6f) continue;
            entries_[i].last_sent.store(v, std::memory_order_relaxed);
            emit(id, v, entries_[i]);
        }
    }
};
