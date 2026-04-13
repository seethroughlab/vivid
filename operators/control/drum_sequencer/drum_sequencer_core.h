#pragma once
#include "operator_api/metronome_sync.h"
#include "operator_api/operator.h"
#include "operator_api/draw_ui_helpers.h"
#include "operator_api/midi_types.h"
#include "operator_api/type_id.h"
#include "drum_sequencer_layout.h"
#include "midi_helpers.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include "operator_api/thumbnail.h"

/**
 * @brief Six-track drum pattern sequencer with probability and swing.
 *
 * Programs up to 16-step patterns across 6 drum tracks with per-step
 * probability. Outputs trigger/gate/velocity as both spreads and
 * individual signals, plus MIDI output.
 *
 * @see DrumKit, Euclidean, StepSeq
 */
struct DrumSequencerCore : vivid::OperatorBase {
    static constexpr bool kTimeDependent = false;

    // Param index layout:
    // [0]=steps  [1]=swing  [2]=clock_source  [3]=midi_channel
    // [4..9]=kick_note, snare_note, hat_note, oh_note, clap_note, tom_note
    // [10..25]=kick_0..15  [26..41]=snare_0..15  [42..57]=hat_0..15
    // [58..73]=oh_0..15    [74..89]=clap_0..15   [90..105]=tom_0..15
    // Mod A: [106..121]=kick_ma_0..15  [122..137]=snare_ma_0..15  [138..153]=hat_ma_0..15
    //        [154..169]=oh_ma_0..15    [170..185]=clap_ma_0..15   [186..201]=tom_ma_0..15
    // Mod B: [202..217]=kick_mb_0..15  [218..233]=snare_mb_0..15  [234..249]=hat_mb_0..15
    //        [250..265]=oh_mb_0..15    [266..281]=clap_mb_0..15   [282..297]=tom_mb_0..15

    vivid::Param<int>   steps {"steps",  16, 1, 16};
    vivid::Param<float> swing {"swing",  0.0f, 0.0f, 0.5f};
    vivid::Param<int>   clock_source{"clock_source", vivid::kClockSourceExternal, vivid::clock_source_labels()};

    // MIDI note number per drum track (indices 2..7)
    vivid::Param<int> kick_note  {"kick_note",  36, 0, 127};
    vivid::Param<int> snare_note {"snare_note", 38, 0, 127};
    vivid::Param<int> hat_note   {"hat_note",   42, 0, 127};
    vivid::Param<int> oh_note    {"oh_note",    46, 0, 127};
    vivid::Param<int> clap_note  {"clap_note",  39, 0, 127};
    vivid::Param<int> tom_note   {"tom_note",   45, 0, 127};

    // 6 drums x 16 steps = 96 bool params
    vivid::Param<float> kick_0 {"kick_0", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> kick_1 {"kick_1", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> kick_2 {"kick_2", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> kick_3 {"kick_3", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> kick_4 {"kick_4", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> kick_5 {"kick_5", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> kick_6 {"kick_6", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> kick_7 {"kick_7", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> kick_8 {"kick_8", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> kick_9 {"kick_9", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> kick_10{"kick_10",0.0f, 0.0f, 1.0f};
    vivid::Param<float> kick_11{"kick_11",0.0f, 0.0f, 1.0f};
    vivid::Param<float> kick_12{"kick_12",0.0f, 0.0f, 1.0f};
    vivid::Param<float> kick_13{"kick_13",0.0f, 0.0f, 1.0f};
    vivid::Param<float> kick_14{"kick_14",0.0f, 0.0f, 1.0f};
    vivid::Param<float> kick_15{"kick_15",0.0f, 0.0f, 1.0f};

    vivid::Param<float> snare_0 {"snare_0", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> snare_1 {"snare_1", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> snare_2 {"snare_2", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> snare_3 {"snare_3", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> snare_4 {"snare_4", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> snare_5 {"snare_5", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> snare_6 {"snare_6", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> snare_7 {"snare_7", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> snare_8 {"snare_8", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> snare_9 {"snare_9", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> snare_10{"snare_10",0.0f, 0.0f, 1.0f};
    vivid::Param<float> snare_11{"snare_11",0.0f, 0.0f, 1.0f};
    vivid::Param<float> snare_12{"snare_12",0.0f, 0.0f, 1.0f};
    vivid::Param<float> snare_13{"snare_13",0.0f, 0.0f, 1.0f};
    vivid::Param<float> snare_14{"snare_14",0.0f, 0.0f, 1.0f};
    vivid::Param<float> snare_15{"snare_15",0.0f, 0.0f, 1.0f};

    vivid::Param<float> hat_0 {"hat_0", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> hat_1 {"hat_1", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> hat_2 {"hat_2", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> hat_3 {"hat_3", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> hat_4 {"hat_4", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> hat_5 {"hat_5", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> hat_6 {"hat_6", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> hat_7 {"hat_7", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> hat_8 {"hat_8", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> hat_9 {"hat_9", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> hat_10{"hat_10",0.0f, 0.0f, 1.0f};
    vivid::Param<float> hat_11{"hat_11",0.0f, 0.0f, 1.0f};
    vivid::Param<float> hat_12{"hat_12",0.0f, 0.0f, 1.0f};
    vivid::Param<float> hat_13{"hat_13",0.0f, 0.0f, 1.0f};
    vivid::Param<float> hat_14{"hat_14",0.0f, 0.0f, 1.0f};
    vivid::Param<float> hat_15{"hat_15",0.0f, 0.0f, 1.0f};

    vivid::Param<float> oh_0 {"oh_0", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> oh_1 {"oh_1", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> oh_2 {"oh_2", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> oh_3 {"oh_3", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> oh_4 {"oh_4", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> oh_5 {"oh_5", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> oh_6 {"oh_6", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> oh_7 {"oh_7", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> oh_8 {"oh_8", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> oh_9 {"oh_9", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> oh_10{"oh_10",0.0f, 0.0f, 1.0f};
    vivid::Param<float> oh_11{"oh_11",0.0f, 0.0f, 1.0f};
    vivid::Param<float> oh_12{"oh_12",0.0f, 0.0f, 1.0f};
    vivid::Param<float> oh_13{"oh_13",0.0f, 0.0f, 1.0f};
    vivid::Param<float> oh_14{"oh_14",0.0f, 0.0f, 1.0f};
    vivid::Param<float> oh_15{"oh_15",0.0f, 0.0f, 1.0f};

    vivid::Param<float> clap_0 {"clap_0", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> clap_1 {"clap_1", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> clap_2 {"clap_2", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> clap_3 {"clap_3", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> clap_4 {"clap_4", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> clap_5 {"clap_5", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> clap_6 {"clap_6", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> clap_7 {"clap_7", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> clap_8 {"clap_8", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> clap_9 {"clap_9", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> clap_10{"clap_10",0.0f, 0.0f, 1.0f};
    vivid::Param<float> clap_11{"clap_11",0.0f, 0.0f, 1.0f};
    vivid::Param<float> clap_12{"clap_12",0.0f, 0.0f, 1.0f};
    vivid::Param<float> clap_13{"clap_13",0.0f, 0.0f, 1.0f};
    vivid::Param<float> clap_14{"clap_14",0.0f, 0.0f, 1.0f};
    vivid::Param<float> clap_15{"clap_15",0.0f, 0.0f, 1.0f};

    vivid::Param<float> tom_0 {"tom_0", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> tom_1 {"tom_1", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> tom_2 {"tom_2", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> tom_3 {"tom_3", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> tom_4 {"tom_4", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> tom_5 {"tom_5", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> tom_6 {"tom_6", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> tom_7 {"tom_7", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> tom_8 {"tom_8", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> tom_9 {"tom_9", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> tom_10{"tom_10",0.0f, 0.0f, 1.0f};
    vivid::Param<float> tom_11{"tom_11",0.0f, 0.0f, 1.0f};
    vivid::Param<float> tom_12{"tom_12",0.0f, 0.0f, 1.0f};
    vivid::Param<float> tom_13{"tom_13",0.0f, 0.0f, 1.0f};
    vivid::Param<float> tom_14{"tom_14",0.0f, 0.0f, 1.0f};
    vivid::Param<float> tom_15{"tom_15",0.0f, 0.0f, 1.0f};

    // Mod A: 6 drums x 16 steps = 96 params (indices 98..193), default 0.5
    vivid::Param<float> kick_ma_0 {"kick_ma_0", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_ma_1 {"kick_ma_1", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_ma_2 {"kick_ma_2", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_ma_3 {"kick_ma_3", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_ma_4 {"kick_ma_4", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_ma_5 {"kick_ma_5", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_ma_6 {"kick_ma_6", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_ma_7 {"kick_ma_7", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_ma_8 {"kick_ma_8", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_ma_9 {"kick_ma_9", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_ma_10{"kick_ma_10",0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_ma_11{"kick_ma_11",0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_ma_12{"kick_ma_12",0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_ma_13{"kick_ma_13",0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_ma_14{"kick_ma_14",0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_ma_15{"kick_ma_15",0.5f, 0.0f, 1.0f};

    vivid::Param<float> snare_ma_0 {"snare_ma_0", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_ma_1 {"snare_ma_1", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_ma_2 {"snare_ma_2", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_ma_3 {"snare_ma_3", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_ma_4 {"snare_ma_4", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_ma_5 {"snare_ma_5", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_ma_6 {"snare_ma_6", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_ma_7 {"snare_ma_7", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_ma_8 {"snare_ma_8", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_ma_9 {"snare_ma_9", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_ma_10{"snare_ma_10",0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_ma_11{"snare_ma_11",0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_ma_12{"snare_ma_12",0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_ma_13{"snare_ma_13",0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_ma_14{"snare_ma_14",0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_ma_15{"snare_ma_15",0.5f, 0.0f, 1.0f};

    vivid::Param<float> hat_ma_0 {"hat_ma_0", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_ma_1 {"hat_ma_1", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_ma_2 {"hat_ma_2", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_ma_3 {"hat_ma_3", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_ma_4 {"hat_ma_4", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_ma_5 {"hat_ma_5", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_ma_6 {"hat_ma_6", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_ma_7 {"hat_ma_7", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_ma_8 {"hat_ma_8", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_ma_9 {"hat_ma_9", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_ma_10{"hat_ma_10",0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_ma_11{"hat_ma_11",0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_ma_12{"hat_ma_12",0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_ma_13{"hat_ma_13",0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_ma_14{"hat_ma_14",0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_ma_15{"hat_ma_15",0.5f, 0.0f, 1.0f};

    vivid::Param<float> oh_ma_0 {"oh_ma_0", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_ma_1 {"oh_ma_1", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_ma_2 {"oh_ma_2", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_ma_3 {"oh_ma_3", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_ma_4 {"oh_ma_4", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_ma_5 {"oh_ma_5", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_ma_6 {"oh_ma_6", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_ma_7 {"oh_ma_7", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_ma_8 {"oh_ma_8", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_ma_9 {"oh_ma_9", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_ma_10{"oh_ma_10",0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_ma_11{"oh_ma_11",0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_ma_12{"oh_ma_12",0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_ma_13{"oh_ma_13",0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_ma_14{"oh_ma_14",0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_ma_15{"oh_ma_15",0.5f, 0.0f, 1.0f};

    vivid::Param<float> clap_ma_0 {"clap_ma_0", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_ma_1 {"clap_ma_1", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_ma_2 {"clap_ma_2", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_ma_3 {"clap_ma_3", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_ma_4 {"clap_ma_4", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_ma_5 {"clap_ma_5", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_ma_6 {"clap_ma_6", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_ma_7 {"clap_ma_7", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_ma_8 {"clap_ma_8", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_ma_9 {"clap_ma_9", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_ma_10{"clap_ma_10",0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_ma_11{"clap_ma_11",0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_ma_12{"clap_ma_12",0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_ma_13{"clap_ma_13",0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_ma_14{"clap_ma_14",0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_ma_15{"clap_ma_15",0.5f, 0.0f, 1.0f};

    vivid::Param<float> tom_ma_0 {"tom_ma_0", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_ma_1 {"tom_ma_1", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_ma_2 {"tom_ma_2", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_ma_3 {"tom_ma_3", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_ma_4 {"tom_ma_4", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_ma_5 {"tom_ma_5", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_ma_6 {"tom_ma_6", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_ma_7 {"tom_ma_7", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_ma_8 {"tom_ma_8", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_ma_9 {"tom_ma_9", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_ma_10{"tom_ma_10",0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_ma_11{"tom_ma_11",0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_ma_12{"tom_ma_12",0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_ma_13{"tom_ma_13",0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_ma_14{"tom_ma_14",0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_ma_15{"tom_ma_15",0.5f, 0.0f, 1.0f};

    // Mod B: 6 drums x 16 steps = 96 params (indices 194..289), default 0.5
    vivid::Param<float> kick_mb_0 {"kick_mb_0", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_mb_1 {"kick_mb_1", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_mb_2 {"kick_mb_2", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_mb_3 {"kick_mb_3", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_mb_4 {"kick_mb_4", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_mb_5 {"kick_mb_5", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_mb_6 {"kick_mb_6", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_mb_7 {"kick_mb_7", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_mb_8 {"kick_mb_8", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_mb_9 {"kick_mb_9", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_mb_10{"kick_mb_10",0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_mb_11{"kick_mb_11",0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_mb_12{"kick_mb_12",0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_mb_13{"kick_mb_13",0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_mb_14{"kick_mb_14",0.5f, 0.0f, 1.0f};
    vivid::Param<float> kick_mb_15{"kick_mb_15",0.5f, 0.0f, 1.0f};

    vivid::Param<float> snare_mb_0 {"snare_mb_0", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_mb_1 {"snare_mb_1", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_mb_2 {"snare_mb_2", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_mb_3 {"snare_mb_3", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_mb_4 {"snare_mb_4", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_mb_5 {"snare_mb_5", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_mb_6 {"snare_mb_6", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_mb_7 {"snare_mb_7", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_mb_8 {"snare_mb_8", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_mb_9 {"snare_mb_9", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_mb_10{"snare_mb_10",0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_mb_11{"snare_mb_11",0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_mb_12{"snare_mb_12",0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_mb_13{"snare_mb_13",0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_mb_14{"snare_mb_14",0.5f, 0.0f, 1.0f};
    vivid::Param<float> snare_mb_15{"snare_mb_15",0.5f, 0.0f, 1.0f};

    vivid::Param<float> hat_mb_0 {"hat_mb_0", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_mb_1 {"hat_mb_1", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_mb_2 {"hat_mb_2", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_mb_3 {"hat_mb_3", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_mb_4 {"hat_mb_4", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_mb_5 {"hat_mb_5", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_mb_6 {"hat_mb_6", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_mb_7 {"hat_mb_7", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_mb_8 {"hat_mb_8", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_mb_9 {"hat_mb_9", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_mb_10{"hat_mb_10",0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_mb_11{"hat_mb_11",0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_mb_12{"hat_mb_12",0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_mb_13{"hat_mb_13",0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_mb_14{"hat_mb_14",0.5f, 0.0f, 1.0f};
    vivid::Param<float> hat_mb_15{"hat_mb_15",0.5f, 0.0f, 1.0f};

    vivid::Param<float> oh_mb_0 {"oh_mb_0", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_mb_1 {"oh_mb_1", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_mb_2 {"oh_mb_2", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_mb_3 {"oh_mb_3", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_mb_4 {"oh_mb_4", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_mb_5 {"oh_mb_5", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_mb_6 {"oh_mb_6", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_mb_7 {"oh_mb_7", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_mb_8 {"oh_mb_8", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_mb_9 {"oh_mb_9", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_mb_10{"oh_mb_10",0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_mb_11{"oh_mb_11",0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_mb_12{"oh_mb_12",0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_mb_13{"oh_mb_13",0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_mb_14{"oh_mb_14",0.5f, 0.0f, 1.0f};
    vivid::Param<float> oh_mb_15{"oh_mb_15",0.5f, 0.0f, 1.0f};

    vivid::Param<float> clap_mb_0 {"clap_mb_0", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_mb_1 {"clap_mb_1", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_mb_2 {"clap_mb_2", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_mb_3 {"clap_mb_3", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_mb_4 {"clap_mb_4", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_mb_5 {"clap_mb_5", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_mb_6 {"clap_mb_6", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_mb_7 {"clap_mb_7", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_mb_8 {"clap_mb_8", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_mb_9 {"clap_mb_9", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_mb_10{"clap_mb_10",0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_mb_11{"clap_mb_11",0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_mb_12{"clap_mb_12",0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_mb_13{"clap_mb_13",0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_mb_14{"clap_mb_14",0.5f, 0.0f, 1.0f};
    vivid::Param<float> clap_mb_15{"clap_mb_15",0.5f, 0.0f, 1.0f};

    vivid::Param<float> tom_mb_0 {"tom_mb_0", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_mb_1 {"tom_mb_1", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_mb_2 {"tom_mb_2", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_mb_3 {"tom_mb_3", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_mb_4 {"tom_mb_4", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_mb_5 {"tom_mb_5", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_mb_6 {"tom_mb_6", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_mb_7 {"tom_mb_7", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_mb_8 {"tom_mb_8", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_mb_9 {"tom_mb_9", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_mb_10{"tom_mb_10",0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_mb_11{"tom_mb_11",0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_mb_12{"tom_mb_12",0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_mb_13{"tom_mb_13",0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_mb_14{"tom_mb_14",0.5f, 0.0f, 1.0f};
    vivid::Param<float> tom_mb_15{"tom_mb_15",0.5f, 0.0f, 1.0f};

    vivid::Param<int> midi_channel {"midi_channel", 1, 1, 16};

    DrumSequencerCore();

    void collect_params(std::vector<vivid::ParamBase*>& out) override;
    void collect_ports(std::vector<VividPortDescriptor>& out) override;
    void compute(float phase, float reset_in, const float* params,
                 float* output_values, VividLaneOutput* out_spreads,
                 void** custom_outputs, uint32_t custom_output_count);
    void draw_inspector(VividInspectorContext* ctx) override;
    void draw_thumbnail(const VividThumbnailContext* ctx) override;

    // Inspector state
    int insp_tab_ = 0;
    bool insp_dragging_ = false;
    int insp_drag_drum_ = -1;
    int insp_drag_step_ = -1;

protected:
    int prev_step_ = -1;
    float phase_offset_ = 0.0f;
    bool prev_reset_ = false;
    int prev_clock_source_ = -1;
    VividMidiBuffer midi_buf_ = {};
};
