#pragma once
// ParametricEQ core — 4-band cascaded biquad EQ. Struct lives in this
// header so parametric_eq_editor.cpp can implement draw_editor against
// the same class definition that parametric_eq.cpp registers.

#include "operator_api/operator.h"

#include <array>

struct BiquadState {
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;
};

/**
 * @brief Four-band parametric equalizer with cascaded biquad filters.
 *
 * Up to 4 filter bands in series, each configurable as peak, low shelf,
 * high shelf, low pass, or high pass. Band 1 frequency supports CV modulation.
 *
 * Dedicated editor window replaces the flat knob grid with a
 * frequency-response curve on a log-freq / linear-dB plane.
 *
 * @see Filter, Vocoder
 */
struct ParametricEQ : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "ParametricEQ";
    static constexpr bool kTimeDependent = false;

    static constexpr int kMaxBands = 4;

    vivid::Param<int> band_count{"band_count", 4, 1, 4};

    vivid::Param<float> freq_1{"freq_1", 100.0f, 20.0f, 20000.0f};
    vivid::Param<float> gain_1{"gain_1", 0.0f, -24.0f, 24.0f};
    vivid::Param<float> q_1   {"q_1",    1.0f, 0.1f, 20.0f};
    vivid::Param<int>   type_1{"type_1", 0, {"Peak", "Low Shelf", "High Shelf", "Low Pass", "High Pass"}};

    vivid::Param<float> freq_2{"freq_2", 500.0f, 20.0f, 20000.0f};
    vivid::Param<float> gain_2{"gain_2", 0.0f, -24.0f, 24.0f};
    vivid::Param<float> q_2   {"q_2",    1.0f, 0.1f, 20.0f};
    vivid::Param<int>   type_2{"type_2", 0, {"Peak", "Low Shelf", "High Shelf", "Low Pass", "High Pass"}};

    vivid::Param<float> freq_3{"freq_3", 2000.0f, 20.0f, 20000.0f};
    vivid::Param<float> gain_3{"gain_3", 0.0f, -24.0f, 24.0f};
    vivid::Param<float> q_3   {"q_3",    1.0f, 0.1f, 20.0f};
    vivid::Param<int>   type_3{"type_3", 0, {"Peak", "Low Shelf", "High Shelf", "Low Pass", "High Pass"}};

    vivid::Param<float> freq_4{"freq_4", 8000.0f, 20.0f, 20000.0f};
    vivid::Param<float> gain_4{"gain_4", 0.0f, -24.0f, 24.0f};
    vivid::Param<float> q_4   {"q_4",    1.0f, 0.1f, 20.0f};
    vivid::Param<int>   type_4{"type_4", 0, {"Peak", "Low Shelf", "High Shelf", "Low Pass", "High Pass"}};

    BiquadState bands_[kMaxBands];

    // Editor UI state. Public so tests can arrange; matches the pattern
    // used by the other Tier-3 adopters.
    int  editor_selected_band_ = 0;   // 0..3, -1 = none selected
    int  editor_drag_band_     = -1;  // which band is being dragged
    bool editor_drag_is_q_     = false;  // shift-drag: adjust Q instead of freq/gain

    ParametricEQ();

    void collect_params(std::vector<vivid::ParamBase*>& out) override;
    void collect_ports(std::vector<VividPortDescriptor>& out) override;
    void process_audio(const VividAudioContext* ctx) override;

    // Editor (see parametric_eq_editor.cpp).
    static VividEditorMetadata editor_metadata();
    void draw_editor(VividEditorContext* ctx);
};
