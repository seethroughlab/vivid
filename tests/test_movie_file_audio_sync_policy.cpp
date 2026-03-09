#include "operators/audio/movie_file_audio_in/sync_policy.h"

#include <cmath>
#include <cstdio>

static int g_fail = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        g_fail++;
    } else {
        std::fprintf(stderr, "PASS: %s\n", msg);
    }
}

int main() {
    constexpr AVSyncThresholds t{};

    {
        const auto d = decide_av_sync(0.010, t);
        check(d.action == AVSyncAction::None, "small drift => no correction");
    }

    {
        const auto d = decide_av_sync(0.080, t);
        check(d.action == AVSyncAction::Skip, "audio behind => skip");
        check(d.skip_media_s > 0.0, "skip has positive media duration");
    }

    {
        const auto d = decide_av_sync(-0.080, t);
        check(d.action == AVSyncAction::Silence, "audio ahead => silence");
    }

    {
        const auto d = decide_av_sync(1.250, t);
        check(d.action == AVSyncAction::Resync, "large positive drift => hard resync");
    }

    {
        const auto d = decide_av_sync(-1.250, t);
        check(d.action == AVSyncAction::Resync, "large negative drift => hard resync");
    }

    {
        const auto d_warn = decide_av_sync(0.140, t);
        const auto d_crit = decide_av_sync(0.320, t);
        check(d_warn.action == AVSyncAction::Skip, "warn region still skip");
        check(d_crit.action == AVSyncAction::Skip, "critical region still skip");
        check(d_crit.skip_media_s > d_warn.skip_media_s, "critical skip more aggressive");
        check(d_crit.severity >= d_warn.severity, "severity non-decreasing");
    }

    {
        AVSyncCorrectionMode mode = AVSyncCorrectionMode::Locked;
        const auto d1 = decide_av_sync_stateful(0.033, t, mode);
        check(d1.action == AVSyncAction::None, "hysteresis: below enter threshold stays locked");
        check(mode == AVSyncCorrectionMode::Locked, "hysteresis: mode remains locked below enter");
    }

    {
        AVSyncCorrectionMode mode = AVSyncCorrectionMode::Locked;
        const auto d1 = decide_av_sync_stateful(0.040, t, mode);
        check(d1.action == AVSyncAction::Skip, "hysteresis: above enter threshold starts correction");
        check(mode == AVSyncCorrectionMode::CorrectingBehind, "hysteresis: enters correcting-behind");

        const auto d2 = decide_av_sync_stateful(0.028, t, mode);
        check(d2.action == AVSyncAction::Skip, "hysteresis: remains correcting above exit threshold");

        const auto d3 = decide_av_sync_stateful(0.018, t, mode);
        check(d3.action == AVSyncAction::None, "hysteresis: exits correcting below exit threshold");
        check(mode == AVSyncCorrectionMode::Locked, "hysteresis: returns locked below exit");
    }

    {
        AVSyncCorrectionMode mode = AVSyncCorrectionMode::Locked;
        const auto d1 = decide_av_sync_stateful(-0.040, t, mode);
        check(d1.action == AVSyncAction::Silence, "hysteresis: above enter negative starts ahead correction");
        check(mode == AVSyncCorrectionMode::CorrectingAhead, "hysteresis: enters correcting-ahead");

        const auto d2 = decide_av_sync_stateful(-0.025, t, mode);
        check(d2.action == AVSyncAction::Silence, "hysteresis: ahead correction held above exit");

        const auto d3 = decide_av_sync_stateful(-0.015, t, mode);
        check(d3.action == AVSyncAction::None, "hysteresis: ahead correction exits below exit");
    }

    {
        AVSyncCorrectionMode mode = AVSyncCorrectionMode::Locked;
        const auto d1 = decide_av_sync_stateful(-0.031, t, mode);
        const auto d2 = decide_av_sync_stateful(-0.035, t, mode);
        check(d1.action == AVSyncAction::None, "hysteresis: locked -31ms does not trigger hold");
        check(d2.action == AVSyncAction::None, "hysteresis: locked -35ms does not trigger hold");
        check(mode == AVSyncCorrectionMode::Locked, "hysteresis: near-threshold negatives keep locked mode");
    }

    {
        check(sync_gate_active(10, 20), "gate helper: active before expiry");
        check(!sync_gate_active(20, 20), "gate helper: inactive at expiry");

        AVSyncCorrectionMode mode = AVSyncCorrectionMode::Locked;
        const auto base = decide_av_sync_stateful(0.100, t, mode);
        const auto gated = apply_sync_gate(base, true);
        check(base.action == AVSyncAction::Skip, "gate helper baseline decision is corrective");
        check(gated.action == AVSyncAction::None, "gate helper suppresses corrective action");

        const auto ungated = apply_sync_gate(base, false);
        check(ungated.action == base.action, "gate helper preserves action when gate off");
    }

    {
        AVSyncCorrectionMode mode = AVSyncCorrectionMode::CorrectingAhead;
        const auto gated = decide_av_sync_stateful_gated(-0.100, t, mode, true);
        check(gated.action == AVSyncAction::None, "stateful gated helper suppresses correction");
        check(mode == AVSyncCorrectionMode::Locked, "stateful gated helper resets mode to locked");
    }

    return g_fail == 0 ? 0 : 1;
}
