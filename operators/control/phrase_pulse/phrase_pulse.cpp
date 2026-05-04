#include "operator_api/metronome_sync.h"
#include "operator_api/operator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

/**
 * @brief Emits a one-frame gate at the start of every N-bar phrase.
 *
 * Reads the graph metronome and fires a 1.0 pulse on its output the
 * frame the transport crosses an N-bar phrase boundary (e.g. with
 * phrase_bars=4 and 4/4 time, fires at beats 0, 16, 32, ...). Wire the
 * `pulse` output into a sequencer's `reset` input to align downstream
 * patterns with musical phrase starts.
 *
 * @param phrase_bars Length of the phrase in bars (1-16).
 * @see DrumSequencer (bar_sync param), Euclidean (bar_sync param)
 */
struct PhrasePulse : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "PhrasePulse";
    static constexpr bool kTimeDependent = true;

    vivid::Param<int> phrase_bars{"phrase_bars", 4, 1, 16};

    PhrasePulse() {
        vivid::semantic_tag(phrase_bars, "count");
        vivid::semantic_shape(phrase_bars, "int");
        vivid::description(phrase_bars,
            "Number of bars per phrase. Pulse fires on every Nth bar boundary.");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phrase_bars);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"pulse", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        const auto m = vivid::metronome_transport(ctx);
        const int N = std::max(1, phrase_bars.int_value());
        const int beats_per_bar = std::max(1, m.beats_per_bar);
        const double phrase_beats = static_cast<double>(beats_per_bar) * N;

        const int64_t idx = static_cast<int64_t>(std::floor(m.beats_elapsed / phrase_beats));
        const bool fire = initialized_ && (idx != prev_idx_);
        ctx->output_values[0] = fire ? 1.0f : 0.0f;
        prev_idx_ = idx;
        initialized_ = true;
    }

private:
    int64_t prev_idx_ = 0;
    bool initialized_ = false;
};

VIVID_DEFINE_OP(PhrasePulse) {
    name = "PhrasePulse";
    keywords = {"phrase", "pulse", "bar", "sync", "metronome", "reset", "clock"};
    summary = "Emits a one-frame gate at the start of every N-bar phrase.";
}

VIVID_REGISTER(PhrasePulse)
