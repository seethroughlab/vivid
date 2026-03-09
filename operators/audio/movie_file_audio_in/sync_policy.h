#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class AVSyncAction {
    None,
    Skip,
    Silence,
    Resync,
};

enum class AVSyncCorrectionMode {
    Locked,
    CorrectingBehind,
    CorrectingAhead,
};

struct AVSyncThresholds {
    double tolerance_s = 0.030;
    double enter_correction_s = 0.036;
    double exit_correction_s = 0.020;
    double warn_s = 0.120;
    double critical_s = 0.300;
    double hard_resync_s = 1.000;
};

struct AVSyncDecision {
    AVSyncAction action = AVSyncAction::None;
    double error_s = 0.0;
    double severity = 0.0;      // 0..1 for skip aggressiveness diagnostics
    double skip_media_s = 0.0;  // media-time skip recommendation
};

inline bool sync_gate_active(uint64_t callback_index, uint64_t gate_until_callback) {
    return callback_index < gate_until_callback;
}

inline AVSyncDecision apply_sync_gate(const AVSyncDecision& decision, bool gate_on) {
    if (!gate_on) return decision;
    AVSyncDecision gated{};
    gated.error_s = decision.error_s;
    return gated;
}

inline AVSyncDecision decide_av_sync(double error_s, const AVSyncThresholds& t) {
    AVSyncDecision d{};
    d.error_s = error_s;
    const double abs_err = std::abs(error_s);
    if (abs_err <= t.tolerance_s) {
        d.action = AVSyncAction::None;
        return d;
    }
    if (abs_err >= t.hard_resync_s) {
        d.action = AVSyncAction::Resync;
        d.severity = 1.0;
        return d;
    }
    if (error_s < -t.tolerance_s) {
        d.action = AVSyncAction::Silence;
        d.severity = std::clamp((abs_err - t.tolerance_s) /
                                std::max(1e-6, t.critical_s - t.tolerance_s), 0.0, 1.0);
        return d;
    }

    // Positive error means audio is behind video; skip ahead.
    d.action = AVSyncAction::Skip;
    const double base = std::max(0.0, error_s - t.tolerance_s);
    double factor = 0.40;
    if (error_s >= t.critical_s) factor = 1.00;
    else if (error_s >= t.warn_s) factor = 0.75;
    d.skip_media_s = base * factor;
    d.severity = factor;
    return d;
}

inline AVSyncDecision decide_av_sync_stateful(double error_s,
                                              const AVSyncThresholds& t,
                                              AVSyncCorrectionMode& mode) {
    AVSyncDecision d{};
    d.error_s = error_s;
    const double abs_err = std::abs(error_s);
    const double enter = std::max(t.tolerance_s, t.enter_correction_s);
    const double exit = std::max(0.0, std::min(enter, t.exit_correction_s));

    if (abs_err <= exit) {
        mode = AVSyncCorrectionMode::Locked;
        d.action = AVSyncAction::None;
        return d;
    }

    if (mode == AVSyncCorrectionMode::Locked && abs_err < enter) {
        d.action = AVSyncAction::None;
        return d;
    }

    if (abs_err >= t.hard_resync_s) {
        d.action = AVSyncAction::Resync;
        d.severity = 1.0;
        return d;
    }

    if (mode == AVSyncCorrectionMode::Locked) {
        mode = (error_s >= 0.0) ? AVSyncCorrectionMode::CorrectingBehind
                                : AVSyncCorrectionMode::CorrectingAhead;
    } else if (mode == AVSyncCorrectionMode::CorrectingBehind && error_s <= -enter) {
        mode = AVSyncCorrectionMode::CorrectingAhead;
    } else if (mode == AVSyncCorrectionMode::CorrectingAhead && error_s >= enter) {
        mode = AVSyncCorrectionMode::CorrectingBehind;
    }

    if (mode == AVSyncCorrectionMode::CorrectingAhead || error_s <= -enter) {
        d.action = AVSyncAction::Silence;
        d.severity = std::clamp((abs_err - t.tolerance_s) /
                                std::max(1e-6, t.critical_s - t.tolerance_s), 0.0, 1.0);
        return d;
    }

    d.action = AVSyncAction::Skip;
    const double base = std::max(0.0, error_s - t.tolerance_s);
    double factor = 0.40;
    if (error_s >= t.critical_s) factor = 1.00;
    else if (error_s >= t.warn_s) factor = 0.75;
    d.skip_media_s = base * factor;
    d.severity = factor;
    return d;
}

inline AVSyncDecision decide_av_sync_stateful_gated(double error_s,
                                                    const AVSyncThresholds& t,
                                                    AVSyncCorrectionMode& mode,
                                                    bool gate_on) {
    if (gate_on) {
        mode = AVSyncCorrectionMode::Locked;
        AVSyncDecision d{};
        d.error_s = error_s;
        return d;
    }
    return decide_av_sync_stateful(error_s, t, mode);
}
