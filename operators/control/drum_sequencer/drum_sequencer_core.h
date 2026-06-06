#pragma once
#include "operator_api/metronome_sync.h"
#include "operator_api/operator.h"
#include "operator_api/draw_ui_helpers.h"
#include "operator_api/editor_ui.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "drum_sequencer_layout.h"
#include "drum_sequencer_editor_shared.h"
#include "note_helpers.h"
#include "note_id_counter.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <random>
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
    // [0]=steps  [1]=swing  [2]=clock_mode  [3]=midi_channel
    // [4..9]=kick_note, snare_note, hat_note, oh_note, clap_note, tom_note
    // [10..25]=kick_0..15  [26..41]=snare_0..15  [42..57]=hat_0..15
    // [58..73]=oh_0..15    [74..89]=clap_0..15   [90..105]=tom_0..15
    // Mod A: [106..121]=kick_ma_0..15  [122..137]=snare_ma_0..15  [138..153]=hat_ma_0..15
    //        [154..169]=oh_ma_0..15    [170..185]=clap_ma_0..15   [186..201]=tom_ma_0..15
    // Mod B: [202..217]=kick_mb_0..15  [218..233]=snare_mb_0..15  [234..249]=hat_mb_0..15
    //        [250..265]=oh_mb_0..15    [266..281]=clap_mb_0..15   [282..297]=tom_mb_0..15

    vivid::Param<int>   steps {"steps",  16, 1, 16};
    vivid::Param<float> swing {"swing",  0.0f, 0.0f, 0.5f};
    vivid::Param<int>   clock_mode{"clock_mode", vivid::kClockModeSyncedMetronome, vivid::clock_mode_synced_labels()};
    vivid::Param<int>   bar_sync    {"bar_sync",    0, {"off","1 bar","2 bar","4 bar","8 bar"}};

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

    // --- Follow-up: pattern-B triggers, per-step probability, per-step roll ---
    // These sit AFTER bar_sync in collect_params so older saved graphs keep
    // every pre-existing param at its original descriptor-order index.
    //
    // active_pattern was originally 0..1 (A/B). It now exposes A/B/C/D.
    // Old saves with int value 0 or 1 still load — the labels constructor
    // sets min/max to 0..(labels.size()-1) so 0 and 1 fall safely inside.
    vivid::Param<int> active_pattern {"active_pattern", 0, {"A","B","C","D"}};

// Helper macros — 16 brace-initialized params per drum row.
#define VIVID_DS_ROW_F(prefix, def) \
    {prefix "0",  def, 0.0f, 1.0f}, {prefix "1",  def, 0.0f, 1.0f}, \
    {prefix "2",  def, 0.0f, 1.0f}, {prefix "3",  def, 0.0f, 1.0f}, \
    {prefix "4",  def, 0.0f, 1.0f}, {prefix "5",  def, 0.0f, 1.0f}, \
    {prefix "6",  def, 0.0f, 1.0f}, {prefix "7",  def, 0.0f, 1.0f}, \
    {prefix "8",  def, 0.0f, 1.0f}, {prefix "9",  def, 0.0f, 1.0f}, \
    {prefix "10", def, 0.0f, 1.0f}, {prefix "11", def, 0.0f, 1.0f}, \
    {prefix "12", def, 0.0f, 1.0f}, {prefix "13", def, 0.0f, 1.0f}, \
    {prefix "14", def, 0.0f, 1.0f}, {prefix "15", def, 0.0f, 1.0f}

#define VIVID_DS_ROW_I(prefix, def) \
    {prefix "0",  def, 1, 4}, {prefix "1",  def, 1, 4}, \
    {prefix "2",  def, 1, 4}, {prefix "3",  def, 1, 4}, \
    {prefix "4",  def, 1, 4}, {prefix "5",  def, 1, 4}, \
    {prefix "6",  def, 1, 4}, {prefix "7",  def, 1, 4}, \
    {prefix "8",  def, 1, 4}, {prefix "9",  def, 1, 4}, \
    {prefix "10", def, 1, 4}, {prefix "11", def, 1, 4}, \
    {prefix "12", def, 1, 4}, {prefix "13", def, 1, 4}, \
    {prefix "14", def, 1, 4}, {prefix "15", def, 1, 4}

    // Pattern-B triggers (96). Default 0 — pattern B is empty until filled.
    std::array<vivid::Param<float>,
               vivid_sequencers::drum_layout::kDrumCount *
               vivid_sequencers::drum_layout::kStepCount> trig_b_ = {{
        VIVID_DS_ROW_F("kick_b_",  0.0f),
        VIVID_DS_ROW_F("snare_b_", 0.0f),
        VIVID_DS_ROW_F("hat_b_",   0.0f),
        VIVID_DS_ROW_F("oh_b_",    0.0f),
        VIVID_DS_ROW_F("clap_b_",  0.0f),
        VIVID_DS_ROW_F("tom_b_",   0.0f),
    }};

    // Pattern-C triggers (96). Default 0 — pattern C is empty until filled.
    std::array<vivid::Param<float>,
               vivid_sequencers::drum_layout::kDrumCount *
               vivid_sequencers::drum_layout::kStepCount> trig_c_ = {{
        VIVID_DS_ROW_F("kick_c_",  0.0f),
        VIVID_DS_ROW_F("snare_c_", 0.0f),
        VIVID_DS_ROW_F("hat_c_",   0.0f),
        VIVID_DS_ROW_F("oh_c_",    0.0f),
        VIVID_DS_ROW_F("clap_c_",  0.0f),
        VIVID_DS_ROW_F("tom_c_",   0.0f),
    }};

    // Pattern-D triggers (96). Default 0 — pattern D is empty until filled.
    std::array<vivid::Param<float>,
               vivid_sequencers::drum_layout::kDrumCount *
               vivid_sequencers::drum_layout::kStepCount> trig_d_ = {{
        VIVID_DS_ROW_F("kick_d_",  0.0f),
        VIVID_DS_ROW_F("snare_d_", 0.0f),
        VIVID_DS_ROW_F("hat_d_",   0.0f),
        VIVID_DS_ROW_F("oh_d_",    0.0f),
        VIVID_DS_ROW_F("clap_d_",  0.0f),
        VIVID_DS_ROW_F("tom_d_",   0.0f),
    }};

    // Song mode: when "song", the playing pattern auto-advances every time
    // the pattern wraps (step n_active-1 → 0). active_pattern then becomes
    // the edit cursor (which pattern the editor grid shows) and playback
    // ignores it. Default "manual" — behaviour identical to before.
    vivid::Param<int> song_mode {"song_mode", 0, {"manual","song"}};

    // How many pattern wraps each song-mode section holds for before
    // advancing. 1 = bar-by-bar (default), 4 = "4-bar section" — a 4×4
    // arrangement plays for 16 bars total before the song cycles.
    vivid::Param<int> bars_per_pattern {"bars_per_pattern", 1, 1, 8};

    // Per-step probability (96). Default 1.0 — "always fires" preserves
    // existing graph behaviour until the user actively reduces a value.
    std::array<vivid::Param<float>,
               vivid_sequencers::drum_layout::kDrumCount *
               vivid_sequencers::drum_layout::kStepCount> prob_ = {{
        VIVID_DS_ROW_F("kick_prob_",  1.0f),
        VIVID_DS_ROW_F("snare_prob_", 1.0f),
        VIVID_DS_ROW_F("hat_prob_",   1.0f),
        VIVID_DS_ROW_F("oh_prob_",    1.0f),
        VIVID_DS_ROW_F("clap_prob_",  1.0f),
        VIVID_DS_ROW_F("tom_prob_",   1.0f),
    }};

    // Per-step roll count (96). Default 1 — one hit, no ratchet. Range
    // 1..4 covers triplet + quad ratchets while staying legible in the UI.
    std::array<vivid::Param<int>,
               vivid_sequencers::drum_layout::kDrumCount *
               vivid_sequencers::drum_layout::kStepCount> roll_ = {{
        VIVID_DS_ROW_I("kick_roll_",  1),
        VIVID_DS_ROW_I("snare_roll_", 1),
        VIVID_DS_ROW_I("hat_roll_",   1),
        VIVID_DS_ROW_I("oh_roll_",    1),
        VIVID_DS_ROW_I("clap_roll_",  1),
        VIVID_DS_ROW_I("tom_roll_",   1),
    }};

#undef VIVID_DS_ROW_F
#undef VIVID_DS_ROW_I

    DrumSequencerCore();

    void collect_params(std::vector<vivid::ParamBase*>& out) override;
    void collect_ports(std::vector<VividPortDescriptor>& out) override;
    void compute(float phase, float reset_in,
                 double beats_elapsed, int beats_per_bar,
                 const float* params,
                 float* output_values, VividValueOutput* out_spreads,
                 void** custom_outputs, uint32_t custom_output_count);
    void draw_thumbnail(const VividThumbnailContext* ctx) override;

    // Editor (Phase 4) — dedicated VIVID_EDITOR window.
    static VividEditorMetadata editor_metadata();
    void draw_editor(VividEditorContext* ctx);

    // Editor cursor persists across frames.
    int  editor_cursor_drum_ = 0;
    int  editor_cursor_step_ = 0;

    // Per-instance step clipboard for Cmd+C / Cmd+V in the editor.
    // Survives editor close/reopen; distinct per DrumSequencer node.
    vivid_sequencers::drum_editor::StepClipboard step_clipboard_;

    // Grid widget state (Phase B of the editor-UI platform plan). Owns
    // the selection anchor + in-progress drag state (paint or extend).
    // Rebuilt into `editor_selection_` every frame from anchor + cursor.
    vivid::ui::GridState                              grid_state_{};
    vivid_sequencers::drum_editor::Selection          editor_selection_{};
    vivid_sequencers::drum_editor::SelectionClipboard selection_clipboard_{};

    // Two-key "P <digit>" probability shortcut. When `P` is pressed we enter
    // prefix mode; the next digit 0-9 commits probability = digit / 10 across
    // the current selection and exits. Any non-digit also exits.
    bool editor_prob_prefix_mode_ = false;

    // Side-panel slider drag state (Phase A of the editor-UI platform plan).
    // Each vivid::ui::SliderState owns one slider's cross-frame drag context;
    // the side-panel render + mouse path uses ui_slider_h calls which read
    // and write these structs in place.
    vivid::ui::SliderState sp_vel_drag_{};
    vivid::ui::SliderState sp_modb_drag_{};
    vivid::ui::SliderState sp_prob_drag_{};

protected:
    int prev_step_ = -1;
    float phase_offset_ = 0.0f;
    bool prev_reset_ = false;
    int prev_clock_mode_ = -1;
    int64_t prev_phrase_idx_ = 0;
    bool phrase_initialized_ = false;

    // Song mode: which pattern (0..3) is currently playing. Reset to 0 on
    // every reset path (clock-source change, reset port edge, phrase sync,
    // and manual→song toggle). Advances when the pattern wraps in compute().
    int song_pos_       = 0;
    int prev_song_mode_ = 0;
    // Wraps elapsed inside the current song-mode section. Resets when the
    // section advances or when any reset path fires.
    int wraps_in_section_ = 0;
    VividNoteBuffer notes_buf_ = {};
    std::array<VividNoteBuffer,
               vivid_sequencers::drum_layout::kDrumCount> per_drum_bufs_ = {};

    // Probability PRNG + per-drum roll/ratchet scheduling. The PRNG is
    // sampled exactly once per step edge (never mid-step) so audio-rate
    // ticks stay deterministic given a fixed seed. `fire_this_step_`
    // records the gating decision so sub-step emissions (for rolls) all
    // share the same outcome, and `prev_sub_step_` tracks how many
    // ratchet hits have already been emitted within the current step.
    std::mt19937 rng_ {
        static_cast<std::mt19937::result_type>(
            reinterpret_cast<std::uintptr_t>(this))
    };
    std::array<bool, vivid_sequencers::drum_layout::kDrumCount> fire_this_step_{};
    std::array<int,  vivid_sequencers::drum_layout::kDrumCount> roll_count_this_step_{};
    std::array<int,  vivid_sequencers::drum_layout::kDrumCount> prev_sub_step_{};
};
