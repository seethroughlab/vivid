#include "drum_sequencer_core.h"
#include <algorithm>
#include <cmath>
#include <cstring>

DrumSequencerCore::DrumSequencerCore() {
    vivid::description(steps, "Number of active steps in the pattern (1-16)");
    vivid::description(swing, "Swing amount, shifts even steps later (0 = straight, 0.5 = heavy triplet)");
    vivid::description(clock_source, "Choose whether beat timing comes from the external beat_phase input or the graph metronome");
    vivid::description(bar_sync,
        "Restart pattern at the top of every Nth bar (only when clock_source = metronome).");

    vivid::description(kick_note, "MIDI note number for the kick drum");
    vivid::description(snare_note, "MIDI note number for the snare drum");
    vivid::description(hat_note, "MIDI note number for the closed hi-hat");
    vivid::description(oh_note, "MIDI note number for the open hi-hat");
    vivid::description(clap_note, "MIDI note number for the clap");
    vivid::description(tom_note, "MIDI note number for the tom");

    vivid::description(midi_channel, "MIDI output channel (1-16)");

    // Per-step trigger params (6 drums x 16 steps)
    vivid::ParamBase* triggers[] = {
        &kick_0,  &kick_1,  &kick_2,  &kick_3,  &kick_4,  &kick_5,  &kick_6,  &kick_7,
        &kick_8,  &kick_9,  &kick_10, &kick_11, &kick_12, &kick_13, &kick_14, &kick_15,
        &snare_0, &snare_1, &snare_2, &snare_3, &snare_4, &snare_5, &snare_6, &snare_7,
        &snare_8, &snare_9, &snare_10,&snare_11,&snare_12,&snare_13,&snare_14,&snare_15,
        &hat_0,   &hat_1,   &hat_2,   &hat_3,   &hat_4,   &hat_5,   &hat_6,   &hat_7,
        &hat_8,   &hat_9,   &hat_10,  &hat_11,  &hat_12,  &hat_13,  &hat_14,  &hat_15,
        &oh_0,    &oh_1,    &oh_2,    &oh_3,    &oh_4,    &oh_5,    &oh_6,    &oh_7,
        &oh_8,    &oh_9,    &oh_10,   &oh_11,   &oh_12,   &oh_13,   &oh_14,   &oh_15,
        &clap_0,  &clap_1,  &clap_2,  &clap_3,  &clap_4,  &clap_5,  &clap_6,  &clap_7,
        &clap_8,  &clap_9,  &clap_10, &clap_11, &clap_12, &clap_13, &clap_14, &clap_15,
        &tom_0,   &tom_1,   &tom_2,   &tom_3,   &tom_4,   &tom_5,   &tom_6,   &tom_7,
        &tom_8,   &tom_9,   &tom_10,  &tom_11,  &tom_12,  &tom_13,  &tom_14,  &tom_15,
    };
    for (auto* p : triggers)
        p->description = "Trigger on/off for this step (>0.5 = active)";

    // Per-step Mod A params (6 drums x 16 steps) — controls velocity
    vivid::ParamBase* mod_a[] = {
        &kick_ma_0,  &kick_ma_1,  &kick_ma_2,  &kick_ma_3,  &kick_ma_4,  &kick_ma_5,  &kick_ma_6,  &kick_ma_7,
        &kick_ma_8,  &kick_ma_9,  &kick_ma_10, &kick_ma_11, &kick_ma_12, &kick_ma_13, &kick_ma_14, &kick_ma_15,
        &snare_ma_0, &snare_ma_1, &snare_ma_2, &snare_ma_3, &snare_ma_4, &snare_ma_5, &snare_ma_6, &snare_ma_7,
        &snare_ma_8, &snare_ma_9, &snare_ma_10,&snare_ma_11,&snare_ma_12,&snare_ma_13,&snare_ma_14,&snare_ma_15,
        &hat_ma_0,   &hat_ma_1,   &hat_ma_2,   &hat_ma_3,   &hat_ma_4,   &hat_ma_5,   &hat_ma_6,   &hat_ma_7,
        &hat_ma_8,   &hat_ma_9,   &hat_ma_10,  &hat_ma_11,  &hat_ma_12,  &hat_ma_13,  &hat_ma_14,  &hat_ma_15,
        &oh_ma_0,    &oh_ma_1,    &oh_ma_2,    &oh_ma_3,    &oh_ma_4,    &oh_ma_5,    &oh_ma_6,    &oh_ma_7,
        &oh_ma_8,    &oh_ma_9,    &oh_ma_10,   &oh_ma_11,   &oh_ma_12,   &oh_ma_13,   &oh_ma_14,   &oh_ma_15,
        &clap_ma_0,  &clap_ma_1,  &clap_ma_2,  &clap_ma_3,  &clap_ma_4,  &clap_ma_5,  &clap_ma_6,  &clap_ma_7,
        &clap_ma_8,  &clap_ma_9,  &clap_ma_10, &clap_ma_11, &clap_ma_12, &clap_ma_13, &clap_ma_14, &clap_ma_15,
        &tom_ma_0,   &tom_ma_1,   &tom_ma_2,   &tom_ma_3,   &tom_ma_4,   &tom_ma_5,   &tom_ma_6,   &tom_ma_7,
        &tom_ma_8,   &tom_ma_9,   &tom_ma_10,  &tom_ma_11,  &tom_ma_12,  &tom_ma_13,  &tom_ma_14,  &tom_ma_15,
    };
    for (auto* p : mod_a)
        p->description = "Per-step velocity for this hit (0-1, maps to MIDI velocity)";

    // Per-step Mod B params (6 drums x 16 steps) — general-purpose modulation
    vivid::ParamBase* mod_b[] = {
        &kick_mb_0,  &kick_mb_1,  &kick_mb_2,  &kick_mb_3,  &kick_mb_4,  &kick_mb_5,  &kick_mb_6,  &kick_mb_7,
        &kick_mb_8,  &kick_mb_9,  &kick_mb_10, &kick_mb_11, &kick_mb_12, &kick_mb_13, &kick_mb_14, &kick_mb_15,
        &snare_mb_0, &snare_mb_1, &snare_mb_2, &snare_mb_3, &snare_mb_4, &snare_mb_5, &snare_mb_6, &snare_mb_7,
        &snare_mb_8, &snare_mb_9, &snare_mb_10,&snare_mb_11,&snare_mb_12,&snare_mb_13,&snare_mb_14,&snare_mb_15,
        &hat_mb_0,   &hat_mb_1,   &hat_mb_2,   &hat_mb_3,   &hat_mb_4,   &hat_mb_5,   &hat_mb_6,   &hat_mb_7,
        &hat_mb_8,   &hat_mb_9,   &hat_mb_10,  &hat_mb_11,  &hat_mb_12,  &hat_mb_13,  &hat_mb_14,  &hat_mb_15,
        &oh_mb_0,    &oh_mb_1,    &oh_mb_2,    &oh_mb_3,    &oh_mb_4,    &oh_mb_5,    &oh_mb_6,    &oh_mb_7,
        &oh_mb_8,    &oh_mb_9,    &oh_mb_10,   &oh_mb_11,   &oh_mb_12,   &oh_mb_13,   &oh_mb_14,   &oh_mb_15,
        &clap_mb_0,  &clap_mb_1,  &clap_mb_2,  &clap_mb_3,  &clap_mb_4,  &clap_mb_5,  &clap_mb_6,  &clap_mb_7,
        &clap_mb_8,  &clap_mb_9,  &clap_mb_10, &clap_mb_11, &clap_mb_12, &clap_mb_13, &clap_mb_14, &clap_mb_15,
        &tom_mb_0,   &tom_mb_1,   &tom_mb_2,   &tom_mb_3,   &tom_mb_4,   &tom_mb_5,   &tom_mb_6,   &tom_mb_7,
        &tom_mb_8,   &tom_mb_9,   &tom_mb_10,  &tom_mb_11,  &tom_mb_12,  &tom_mb_13,  &tom_mb_14,  &tom_mb_15,
    };
    for (auto* p : mod_b)
        p->description = "Per-step modulation value for external routing (0-1)";

    vivid::description(active_pattern,
        "Active pattern bank (0=A, 1=B, 2=C, 3=D). Triggers switch with the "
        "bank; velocity / mod B / probability / roll are shared across patterns.");
    for (auto& p : trig_b_)
        p.description = "Pattern-B trigger on/off for this step (>0.5 = active)";
    for (auto& p : trig_c_)
        p.description = "Pattern-C trigger on/off for this step (>0.5 = active)";
    for (auto& p : trig_d_)
        p.description = "Pattern-D trigger on/off for this step (>0.5 = active)";
    for (auto& p : prob_)
        p.description = "Per-step probability (0 = never fires, 1 = always fires)";
    for (auto& p : roll_)
        p.description = "Per-step roll / ratchet count (1 = single hit, 2-4 = sub-step repeats)";

    vivid::description(song_mode,
        "Song mode: when 'song', the playing pattern auto-advances "
        "A→B→C→D every time the pattern wraps. active_pattern then "
        "selects which pattern the editor grid shows.");
    vivid::description(bars_per_pattern,
        "How many pattern wraps each song-mode section holds for "
        "before advancing (1 = bar-by-bar, 4 = 4-bar sections).");
}

void DrumSequencerCore::collect_params(std::vector<vivid::ParamBase*>& out) {
    out.push_back(&steps);        // 0
    out.push_back(&swing);        // 1
    out.push_back(&clock_source); // 2
    out.push_back(&midi_channel); // 3

    // Hide note/grid/mod params — rendered by custom inspector
    size_t hidden_start = out.size();

    // Note params: 4..9
    out.push_back(&kick_note);
    out.push_back(&snare_note);
    out.push_back(&hat_note);
    out.push_back(&oh_note);
    out.push_back(&clap_note);
    out.push_back(&tom_note);

    // kick: 8..23
    out.push_back(&kick_0);  out.push_back(&kick_1);
    out.push_back(&kick_2);  out.push_back(&kick_3);
    out.push_back(&kick_4);  out.push_back(&kick_5);
    out.push_back(&kick_6);  out.push_back(&kick_7);
    out.push_back(&kick_8);  out.push_back(&kick_9);
    out.push_back(&kick_10); out.push_back(&kick_11);
    out.push_back(&kick_12); out.push_back(&kick_13);
    out.push_back(&kick_14); out.push_back(&kick_15);

    // snare: 24..39
    out.push_back(&snare_0);  out.push_back(&snare_1);
    out.push_back(&snare_2);  out.push_back(&snare_3);
    out.push_back(&snare_4);  out.push_back(&snare_5);
    out.push_back(&snare_6);  out.push_back(&snare_7);
    out.push_back(&snare_8);  out.push_back(&snare_9);
    out.push_back(&snare_10); out.push_back(&snare_11);
    out.push_back(&snare_12); out.push_back(&snare_13);
    out.push_back(&snare_14); out.push_back(&snare_15);

    // hat: 40..55
    out.push_back(&hat_0);  out.push_back(&hat_1);
    out.push_back(&hat_2);  out.push_back(&hat_3);
    out.push_back(&hat_4);  out.push_back(&hat_5);
    out.push_back(&hat_6);  out.push_back(&hat_7);
    out.push_back(&hat_8);  out.push_back(&hat_9);
    out.push_back(&hat_10); out.push_back(&hat_11);
    out.push_back(&hat_12); out.push_back(&hat_13);
    out.push_back(&hat_14); out.push_back(&hat_15);

    // oh: 56..71
    out.push_back(&oh_0);  out.push_back(&oh_1);
    out.push_back(&oh_2);  out.push_back(&oh_3);
    out.push_back(&oh_4);  out.push_back(&oh_5);
    out.push_back(&oh_6);  out.push_back(&oh_7);
    out.push_back(&oh_8);  out.push_back(&oh_9);
    out.push_back(&oh_10); out.push_back(&oh_11);
    out.push_back(&oh_12); out.push_back(&oh_13);
    out.push_back(&oh_14); out.push_back(&oh_15);

    // clap: 72..87
    out.push_back(&clap_0);  out.push_back(&clap_1);
    out.push_back(&clap_2);  out.push_back(&clap_3);
    out.push_back(&clap_4);  out.push_back(&clap_5);
    out.push_back(&clap_6);  out.push_back(&clap_7);
    out.push_back(&clap_8);  out.push_back(&clap_9);
    out.push_back(&clap_10); out.push_back(&clap_11);
    out.push_back(&clap_12); out.push_back(&clap_13);
    out.push_back(&clap_14); out.push_back(&clap_15);

    // tom: 88..103
    out.push_back(&tom_0);  out.push_back(&tom_1);
    out.push_back(&tom_2);  out.push_back(&tom_3);
    out.push_back(&tom_4);  out.push_back(&tom_5);
    out.push_back(&tom_6);  out.push_back(&tom_7);
    out.push_back(&tom_8);  out.push_back(&tom_9);
    out.push_back(&tom_10); out.push_back(&tom_11);
    out.push_back(&tom_12); out.push_back(&tom_13);
    out.push_back(&tom_14); out.push_back(&tom_15);

    // Mod A kick: 104..119
    out.push_back(&kick_ma_0);  out.push_back(&kick_ma_1);
    out.push_back(&kick_ma_2);  out.push_back(&kick_ma_3);
    out.push_back(&kick_ma_4);  out.push_back(&kick_ma_5);
    out.push_back(&kick_ma_6);  out.push_back(&kick_ma_7);
    out.push_back(&kick_ma_8);  out.push_back(&kick_ma_9);
    out.push_back(&kick_ma_10); out.push_back(&kick_ma_11);
    out.push_back(&kick_ma_12); out.push_back(&kick_ma_13);
    out.push_back(&kick_ma_14); out.push_back(&kick_ma_15);

    // Mod A snare: 120..135
    out.push_back(&snare_ma_0);  out.push_back(&snare_ma_1);
    out.push_back(&snare_ma_2);  out.push_back(&snare_ma_3);
    out.push_back(&snare_ma_4);  out.push_back(&snare_ma_5);
    out.push_back(&snare_ma_6);  out.push_back(&snare_ma_7);
    out.push_back(&snare_ma_8);  out.push_back(&snare_ma_9);
    out.push_back(&snare_ma_10); out.push_back(&snare_ma_11);
    out.push_back(&snare_ma_12); out.push_back(&snare_ma_13);
    out.push_back(&snare_ma_14); out.push_back(&snare_ma_15);

    // Mod A hat: 136..151
    out.push_back(&hat_ma_0);  out.push_back(&hat_ma_1);
    out.push_back(&hat_ma_2);  out.push_back(&hat_ma_3);
    out.push_back(&hat_ma_4);  out.push_back(&hat_ma_5);
    out.push_back(&hat_ma_6);  out.push_back(&hat_ma_7);
    out.push_back(&hat_ma_8);  out.push_back(&hat_ma_9);
    out.push_back(&hat_ma_10); out.push_back(&hat_ma_11);
    out.push_back(&hat_ma_12); out.push_back(&hat_ma_13);
    out.push_back(&hat_ma_14); out.push_back(&hat_ma_15);

    // Mod A oh: 152..167
    out.push_back(&oh_ma_0);  out.push_back(&oh_ma_1);
    out.push_back(&oh_ma_2);  out.push_back(&oh_ma_3);
    out.push_back(&oh_ma_4);  out.push_back(&oh_ma_5);
    out.push_back(&oh_ma_6);  out.push_back(&oh_ma_7);
    out.push_back(&oh_ma_8);  out.push_back(&oh_ma_9);
    out.push_back(&oh_ma_10); out.push_back(&oh_ma_11);
    out.push_back(&oh_ma_12); out.push_back(&oh_ma_13);
    out.push_back(&oh_ma_14); out.push_back(&oh_ma_15);

    // Mod A clap: 168..183
    out.push_back(&clap_ma_0);  out.push_back(&clap_ma_1);
    out.push_back(&clap_ma_2);  out.push_back(&clap_ma_3);
    out.push_back(&clap_ma_4);  out.push_back(&clap_ma_5);
    out.push_back(&clap_ma_6);  out.push_back(&clap_ma_7);
    out.push_back(&clap_ma_8);  out.push_back(&clap_ma_9);
    out.push_back(&clap_ma_10); out.push_back(&clap_ma_11);
    out.push_back(&clap_ma_12); out.push_back(&clap_ma_13);
    out.push_back(&clap_ma_14); out.push_back(&clap_ma_15);

    // Mod A tom: 184..199
    out.push_back(&tom_ma_0);  out.push_back(&tom_ma_1);
    out.push_back(&tom_ma_2);  out.push_back(&tom_ma_3);
    out.push_back(&tom_ma_4);  out.push_back(&tom_ma_5);
    out.push_back(&tom_ma_6);  out.push_back(&tom_ma_7);
    out.push_back(&tom_ma_8);  out.push_back(&tom_ma_9);
    out.push_back(&tom_ma_10); out.push_back(&tom_ma_11);
    out.push_back(&tom_ma_12); out.push_back(&tom_ma_13);
    out.push_back(&tom_ma_14); out.push_back(&tom_ma_15);

    // Mod B kick: 200..215
    out.push_back(&kick_mb_0);  out.push_back(&kick_mb_1);
    out.push_back(&kick_mb_2);  out.push_back(&kick_mb_3);
    out.push_back(&kick_mb_4);  out.push_back(&kick_mb_5);
    out.push_back(&kick_mb_6);  out.push_back(&kick_mb_7);
    out.push_back(&kick_mb_8);  out.push_back(&kick_mb_9);
    out.push_back(&kick_mb_10); out.push_back(&kick_mb_11);
    out.push_back(&kick_mb_12); out.push_back(&kick_mb_13);
    out.push_back(&kick_mb_14); out.push_back(&kick_mb_15);

    // Mod B snare: 216..231
    out.push_back(&snare_mb_0);  out.push_back(&snare_mb_1);
    out.push_back(&snare_mb_2);  out.push_back(&snare_mb_3);
    out.push_back(&snare_mb_4);  out.push_back(&snare_mb_5);
    out.push_back(&snare_mb_6);  out.push_back(&snare_mb_7);
    out.push_back(&snare_mb_8);  out.push_back(&snare_mb_9);
    out.push_back(&snare_mb_10); out.push_back(&snare_mb_11);
    out.push_back(&snare_mb_12); out.push_back(&snare_mb_13);
    out.push_back(&snare_mb_14); out.push_back(&snare_mb_15);

    // Mod B hat: 232..247
    out.push_back(&hat_mb_0);  out.push_back(&hat_mb_1);
    out.push_back(&hat_mb_2);  out.push_back(&hat_mb_3);
    out.push_back(&hat_mb_4);  out.push_back(&hat_mb_5);
    out.push_back(&hat_mb_6);  out.push_back(&hat_mb_7);
    out.push_back(&hat_mb_8);  out.push_back(&hat_mb_9);
    out.push_back(&hat_mb_10); out.push_back(&hat_mb_11);
    out.push_back(&hat_mb_12); out.push_back(&hat_mb_13);
    out.push_back(&hat_mb_14); out.push_back(&hat_mb_15);

    // Mod B oh: 248..263
    out.push_back(&oh_mb_0);  out.push_back(&oh_mb_1);
    out.push_back(&oh_mb_2);  out.push_back(&oh_mb_3);
    out.push_back(&oh_mb_4);  out.push_back(&oh_mb_5);
    out.push_back(&oh_mb_6);  out.push_back(&oh_mb_7);
    out.push_back(&oh_mb_8);  out.push_back(&oh_mb_9);
    out.push_back(&oh_mb_10); out.push_back(&oh_mb_11);
    out.push_back(&oh_mb_12); out.push_back(&oh_mb_13);
    out.push_back(&oh_mb_14); out.push_back(&oh_mb_15);

    // Mod B clap: 264..279
    out.push_back(&clap_mb_0);  out.push_back(&clap_mb_1);
    out.push_back(&clap_mb_2);  out.push_back(&clap_mb_3);
    out.push_back(&clap_mb_4);  out.push_back(&clap_mb_5);
    out.push_back(&clap_mb_6);  out.push_back(&clap_mb_7);
    out.push_back(&clap_mb_8);  out.push_back(&clap_mb_9);
    out.push_back(&clap_mb_10); out.push_back(&clap_mb_11);
    out.push_back(&clap_mb_12); out.push_back(&clap_mb_13);
    out.push_back(&clap_mb_14); out.push_back(&clap_mb_15);

    // Mod B tom: 280..295
    out.push_back(&tom_mb_0);  out.push_back(&tom_mb_1);
    out.push_back(&tom_mb_2);  out.push_back(&tom_mb_3);
    out.push_back(&tom_mb_4);  out.push_back(&tom_mb_5);
    out.push_back(&tom_mb_6);  out.push_back(&tom_mb_7);
    out.push_back(&tom_mb_8);  out.push_back(&tom_mb_9);
    out.push_back(&tom_mb_10); out.push_back(&tom_mb_11);
    out.push_back(&tom_mb_12); out.push_back(&tom_mb_13);
    out.push_back(&tom_mb_14); out.push_back(&tom_mb_15);

    for (size_t i = hidden_start; i < out.size(); ++i)
        out[i]->display_hint = VIVID_DISPLAY_HIDDEN;

    // Phrase-sync param appended after the hidden block so existing
    // param indices in the layout helper stay stable.
    out.push_back(&bar_sync);

    // Follow-up params: pattern A/B + per-step probability + per-step roll,
    // plus patterns C and D appended after the roll block.
    // Appended AFTER bar_sync so every pre-existing index survives. Indices
    // here match drum_sequencer_layout.h (kActivePatternIndex = 299,
    // kTrigBParamBases = 300.., kProbParamBases = 396.., kRollParamBases = 492..,
    // kTrigCParamBases = 588.., kTrigDParamBases = 684..).
    out.push_back(&active_pattern);

    const size_t follow_hidden_start = out.size();
    for (auto& p : trig_b_) out.push_back(&p);
    for (auto& p : prob_)   out.push_back(&p);
    for (auto& p : roll_)   out.push_back(&p);
    for (auto& p : trig_c_) out.push_back(&p);
    for (auto& p : trig_d_) out.push_back(&p);
    for (size_t i = follow_hidden_start; i < out.size(); ++i)
        out[i]->display_hint = VIVID_DISPLAY_HIDDEN;

    // song_mode is visible in the inspector — it's a low-density toggle that
    // belongs alongside steps/swing/clock_source. Index = kSongModeIndex.
    out.push_back(&song_mode);
    // bars_per_pattern at kBarsPerPatternIndex. Visible.
    out.push_back(&bars_per_pattern);
}

void DrumSequencerCore::collect_ports(std::vector<VividPortDescriptor>& out) {
    out.push_back({"beat_phase",      VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
    out.push_back({"reset",           VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
    out.push_back({"step",            VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    // current_pattern: which pattern (0..3) is currently playing. Useful for
    // syncing visuals to the song's section transitions.
    out.push_back({"current_pattern", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    // `midi_out` carries all six drums merged — feed this into samplers
    // (SP404, etc.) that select voices by MIDI note. The per-drum ports
    // carry only their own drum, suitable for wiring straight into a
    // matching Drum* voice without a DrumKit hub in between. Order here
    // must match drum_layout::kTriggerPrefixes so `custom_outputs[d+1]`
    // lines up with index `d` in compute().
    out.push_back(VIVID_CUSTOM_REF_PORT("midi_out",  VIVID_PORT_OUTPUT, VividMidiBuffer));
    out.push_back(VIVID_CUSTOM_REF_PORT("kick_out",  VIVID_PORT_OUTPUT, VividMidiBuffer));
    out.push_back(VIVID_CUSTOM_REF_PORT("snare_out", VIVID_PORT_OUTPUT, VividMidiBuffer));
    out.push_back(VIVID_CUSTOM_REF_PORT("hat_out",   VIVID_PORT_OUTPUT, VividMidiBuffer));
    out.push_back(VIVID_CUSTOM_REF_PORT("oh_out",    VIVID_PORT_OUTPUT, VividMidiBuffer));
    out.push_back(VIVID_CUSTOM_REF_PORT("clap_out",  VIVID_PORT_OUTPUT, VividMidiBuffer));
    out.push_back(VIVID_CUSTOM_REF_PORT("tom_out",   VIVID_PORT_OUTPUT, VividMidiBuffer));
}


void DrumSequencerCore::compute(float phase, float reset_in,
             double beats_elapsed, int beats_per_bar,
             const float* params,
             float* output_values, VividLaneOutput* /*out_spreads*/,
             void** custom_outputs, uint32_t custom_output_count) {
    namespace layout = vivid_sequencers::drum_layout;

    // Reset phase when clock source changes so the pattern restarts immediately.
    // Also rewinds the song-mode position to A — a clock-source switch is
    // a clean "start over" boundary.
    int cs = clock_source.int_value();
    if (cs != prev_clock_source_) {
        phase_offset_ = phase;
        prev_clock_source_ = cs;
        song_pos_ = 0;
        wraps_in_section_ = 0;
    }

    bool reset = reset_in > 0.5f;

    // Phrase reset: when synced to the graph metronome, fold an internal
    // pulse into `reset` at the start of every N-bar phrase so the pattern
    // re-aligns with the top of the phrase. Phrase-sync resets fold into
    // `reset` below, so they also rewind song_pos_.
    int sync_idx = std::clamp(bar_sync.int_value(), 0, 4);
    if (sync_idx > 0 && cs == vivid::kClockSourceMetronome) {
        static constexpr int kSyncBars[] = {0, 1, 2, 4, 8};
        const int bpb = std::max(1, beats_per_bar);
        const double phrase_beats = static_cast<double>(bpb) * kSyncBars[sync_idx];
        const int64_t phrase_idx =
            static_cast<int64_t>(std::floor(beats_elapsed / phrase_beats));
        if (phrase_initialized_ && phrase_idx != prev_phrase_idx_)
            reset = true;
        prev_phrase_idx_ = phrase_idx;
        phrase_initialized_ = true;
    }

    // Rising-edge reset: capture current phase as offset and rewind song.
    if (reset && !prev_reset_) {
        phase_offset_ = phase;
        song_pos_ = 0;
        wraps_in_section_ = 0;
    }
    prev_reset_ = reset;

    // Manual → song edge: rewind to A so pressing "Song" gives a predictable
    // start. Edge-detect via prev_song_mode_ so toggling off/on always
    // resets, but staying in "song" doesn't clobber song_pos_ each frame.
    const int sm = song_mode.int_value();
    if (sm == 1 && prev_song_mode_ == 0) {
        song_pos_ = 0;
        wraps_in_section_ = 0;
    }
    prev_song_mode_ = sm;

    float adj_phase = std::fmod(phase - phase_offset_ + 1.0f, 1.0f);
    int n = std::max(steps.int_value(), 1);

    // Apply swing: stretch even steps, compress odd steps within each pair.
    // swing 0.0 = straight; 0.5 = heavy triplet feel (~83/17 split).
    float sw = swing.value;
    float scaled = adj_phase * n;                 // 0..n continuous
    int pair = static_cast<int>(scaled) / 2;      // which pair (0,1), (2,3), ...
    float pair_phase = scaled - pair * 2.0f;      // 0..2 within the pair
    float boundary = 1.0f + sw * 1.333f;          // even step stretches up to ~1.67 at max

    int step;
    if (pair_phase >= boundary) {
        step = pair * 2 + 1;
    } else {
        step = pair * 2;
    }
    step = std::clamp(step, 0, n - 1);

    bool step_changed = (step != prev_step_);
    const int old_prev_step = prev_step_;
    prev_step_ = step;

    // Song-mode advance on a clean pattern wrap (last active step → 0).
    // The `old_prev_step == n - 1` guard rejects the first-frame case
    // (prev_step_ starts at -1) and the case where `steps` shrinks
    // mid-pattern: only a genuine wrap from the bottom edge advances.
    // bars_per_pattern controls how many wraps each section holds for.
    if (sm == 1 && step_changed) {
        const bool wrapped = (old_prev_step == n - 1) && (step == 0);
        if (wrapped) {
            const int bpp = std::max(1, bars_per_pattern.int_value());
            ++wraps_in_section_;
            if (wraps_in_section_ >= bpp) {
                wraps_in_section_ = 0;
                song_pos_ = (song_pos_ + 1) & 0x3;
            }
        }
    }

    // Step output (index 0)
    output_values[0] = static_cast<float>(step);

    // Swing-aware fraction within the current step, 0..1. Used to place
    // ratchet / roll sub-hits evenly inside the audible duration of the
    // step regardless of the swing setting.
    float step_fraction;
    if (pair_phase >= boundary) {
        const float denom = std::max(1e-4f, 2.0f - boundary);
        step_fraction = (pair_phase - boundary) / denom;
    } else {
        const float denom = std::max(1e-4f, boundary);
        step_fraction = pair_phase / denom;
    }
    step_fraction = std::clamp(step_fraction, 0.0f, 0.999999f);

    // Populate MIDI output. Per-step Mod A maps to velocity. In manual mode
    // active_pattern selects the playing pattern; in song mode song_pos_
    // drives playback and active_pattern becomes the editor's edit cursor.
    // Velocity / mod B / probability / roll stay shared across patterns.
    // Probability is sampled once at the step edge; roll emits N hits spread
    // over the step's sub-fraction.
    const int edit_ptn    = std::clamp(active_pattern.int_value(), 0, 3);
    const int playing_ptn = (sm == 1) ? std::clamp(song_pos_, 0, 3) : edit_ptn;

    // current_pattern output (index 1) — emit which pattern is playing.
    output_values[layout::kCurrentPatternOutputIndex] =
        static_cast<float>(playing_ptn);

    const uint8_t ch = static_cast<uint8_t>(midi_channel.int_value() - 1);
    midi_buf_.count = 0;
    for (auto& buf : per_drum_bufs_) buf.count = 0;
    for (std::size_t d = 0; d < layout::kDrumCount; ++d) {
        if (step_changed) {
            const int trig_idx =
                layout::trigger_param_index_for_pattern(playing_ptn, d, step);
            const bool triggered = params[trig_idx] > 0.5f;
            if (triggered) {
                const float prob = std::clamp(
                    params[layout::prob_param_index(d, step)], 0.0f, 1.0f);
                std::uniform_real_distribution<float> dist(0.0f, 1.0f);
                const float sample = dist(rng_);
                if (sample <= prob) {
                    fire_this_step_[d] = true;
                    const int rc = static_cast<int>(
                        params[layout::roll_param_index(d, step)] + 0.5f);
                    roll_count_this_step_[d] = std::clamp(rc, 1, 4);
                } else {
                    fire_this_step_[d] = false;
                    roll_count_this_step_[d] = 0;
                }
            } else {
                fire_this_step_[d] = false;
                roll_count_this_step_[d] = 0;
            }
            prev_sub_step_[d] = -1;
        }

        if (fire_this_step_[d] && roll_count_this_step_[d] > 0) {
            const int rc = roll_count_this_step_[d];
            const int sub = std::min(
                static_cast<int>(step_fraction * static_cast<float>(rc)),
                rc - 1);
            if (sub != prev_sub_step_[d]) {
                const float mod_a = params[layout::mod_a_param_index(d, step)];
                const uint8_t vel = static_cast<uint8_t>(std::clamp(
                    static_cast<int>(mod_a * 127.0f + 0.5f), 1, 127));
                const uint8_t note = static_cast<uint8_t>(
                    params[layout::note_param_index(d)]);
                vivid_sequencers::midi_note_on(midi_buf_, note, vel, ch);
                vivid_sequencers::midi_note_on(per_drum_bufs_[d], note, vel, ch);
                prev_sub_step_[d] = sub;
            }
        }
    }
    if (custom_outputs) {
        if (custom_output_count > 0) custom_outputs[0] = &midi_buf_;
        const uint32_t per_drum_count = std::min<uint32_t>(
            static_cast<uint32_t>(layout::kDrumCount),
            custom_output_count > 0 ? custom_output_count - 1 : 0);
        for (uint32_t d = 0; d < per_drum_count; ++d)
            custom_outputs[1 + d] = &per_drum_bufs_[d];
    }
}

void DrumSequencerCore::draw_thumbnail(const VividThumbnailContext* ctx) {
    namespace layout = vivid_sequencers::drum_layout;
    if (!ctx || !ctx->draw.opaque) return;
    const auto& d = ctx->draw;
    void* o = d.opaque;

    float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
    float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

    int n = 16;
    if (ctx->param_count > 0)
        n = std::max(1, std::min(16, static_cast<int>(ctx->param_values[0])));
    int cur_step = -1;
    if (ctx->output_count > 0)
        cur_step = static_cast<int>(ctx->output_values[0]);

    // Dark background
    d.draw_rect(o, 0, 0, w, h, {0.07f, 0.08f, 0.09f, 0.9f});

    // Layout
    float label_w = 16.0f;
    float margin = 3.0f;
    float grid_x0 = label_w + margin;
    float grid_y0 = margin;
    float grid_w = w - grid_x0 - margin;
    float grid_h = h - 2 * margin;
    float row_h = grid_h / 6.0f;
    float col_w = grid_w / static_cast<float>(n);
    float gap = 1.0f;
    float cr = std::min(1.5f, col_w * 0.15f);

    static constexpr float kDrumColors[6][3] = {
        {0.86f, 0.31f, 0.31f}, {0.86f, 0.75f, 0.24f}, {0.24f, 0.78f, 0.71f},
        {0.31f, 0.51f, 0.86f}, {0.63f, 0.35f, 0.78f}, {0.31f, 0.78f, 0.39f},
    };
    static constexpr const char* kLabels[] = {"K", "S", "H", "O", "C", "T"};

    for (int drum = 0; drum < 6; ++drum) {
        float ry = grid_y0 + drum * row_h;

        // Drum label
        d.draw_text(o, margin, ry + row_h * 0.15f, kLabels[drum],
                    {kDrumColors[drum][0], kDrumColors[drum][1], kDrumColors[drum][2], 0.8f}, 0.85f);

        for (int step = 0; step < n; ++step) {
            float cx = grid_x0 + step * col_w;
            float cell_w = col_w - gap;
            float cell_h = row_h - gap;
            bool is_current = (step == cur_step);

            // Current step column highlight
            if (is_current) {
                d.draw_rect(o, cx, grid_y0, col_w, grid_h,
                            {0.15f, 0.17f, 0.2f, 0.4f});
            }

            int trig_idx = layout::trigger_param_index(drum, step);
            bool triggered = (ctx->param_count > static_cast<uint32_t>(trig_idx))
                             && (ctx->param_values[trig_idx] > 0.5f);

            if (triggered) {
                float alpha = is_current ? 1.0f : 0.75f;
                d.draw_rounded_rect(o, cx + gap * 0.5f, ry + gap * 0.5f, cell_w, cell_h, cr,
                                    {kDrumColors[drum][0], kDrumColors[drum][1], kDrumColors[drum][2], alpha});
            } else {
                d.draw_rounded_rect(o, cx + gap * 0.5f, ry + gap * 0.5f, cell_w, cell_h, cr,
                                    {0.12f, 0.13f, 0.15f, 0.2f});
            }
        }
    }
}
