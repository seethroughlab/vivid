#include "operator_api/operator.h"
#include "operator_api/midi_types.h"
#include "operator_api/type_id.h"
#include "drum_sequencer_layout.h"
#include "midi_helpers.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include "operator_api/thumbnail.h"

namespace drum_insp {
static constexpr float kDrumColors[6][3] = {
    {0.86f, 0.31f, 0.31f}, {0.86f, 0.75f, 0.24f}, {0.24f, 0.78f, 0.71f},
    {0.31f, 0.51f, 0.86f}, {0.63f, 0.35f, 0.78f}, {0.31f, 0.78f, 0.39f},
};
static const char* kTabLabels[] = {"Pattern", "Mod A", "Mod B"};
static constexpr float kLabelW = 28.0f;
static constexpr float kCellH = 14.0f;
static constexpr float kCellPad = 2.0f;
static constexpr float kTabW = 80.0f;
static constexpr float kTabH = 18.0f;
} // namespace drum_insp

struct DrumSequencer : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "DrumSequencer";
    static constexpr bool kTimeDependent = false;

    // Param index layout:
    // [0]=steps  [1]=swing
    // [2..7]=kick_note, snare_note, hat_note, oh_note, clap_note, tom_note
    // [8..23]=kick_0..15  [24..39]=snare_0..15  [40..55]=hat_0..15
    // [56..71]=oh_0..15   [72..87]=clap_0..15   [88..103]=tom_0..15
    // Mod A: [104..119]=kick_ma_0..15  [120..135]=snare_ma_0..15  [136..151]=hat_ma_0..15
    //        [152..167]=oh_ma_0..15    [168..183]=clap_ma_0..15   [184..199]=tom_ma_0..15
    // Mod B: [200..215]=kick_mb_0..15  [216..231]=snare_mb_0..15  [232..247]=hat_mb_0..15
    //        [248..263]=oh_mb_0..15    [264..279]=clap_mb_0..15   [280..295]=tom_mb_0..15

    vivid::Param<int>   steps {"steps",  16, 1, 16};
    vivid::Param<float> swing {"swing",  0.0f, 0.0f, 0.5f};

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

    WGPURenderPipeline thumb_pipeline_ = nullptr;
    WGPUBindGroup thumb_bind_group_ = nullptr;
    WGPUBindGroupLayout thumb_bind_layout_ = nullptr;
    WGPUBuffer thumb_uniform_buf_ = nullptr;
    WGPUShaderModule thumb_shader_ = nullptr;
    WGPUPipelineLayout thumb_pipe_layout_ = nullptr;
    WGPUTextureFormat thumb_pipeline_format_ = WGPUTextureFormat_Undefined;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&steps);   // 0
        out.push_back(&swing);   // 1

        // Hide note/grid/mod params — rendered by custom inspector
        size_t hidden_start = out.size();

        // Note params: 2..7
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

        out.push_back(&midi_channel); // 296
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase",   VIVID_PORT_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"reset",   VIVID_PORT_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"kick",    VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"snare",   VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"hat",     VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"oh",      VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"clap",    VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"tom",     VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"step",    VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        // Mod A outputs (ports 7..12 after 2 inputs)
        out.push_back({"kick_mod_a",  VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"snare_mod_a", VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"hat_mod_a",   VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"oh_mod_a",    VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"clap_mod_a",  VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"tom_mod_a",   VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        // Mod B outputs (ports 13..18)
        out.push_back({"kick_mod_b",  VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"snare_mod_b", VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"hat_mod_b",   VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"oh_mod_b",    VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"clap_mod_b",  VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"tom_mod_b",   VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
        // Spread outputs for SP404 integration
        out.push_back({"gates",      VIVID_PORT_SPREAD, VIVID_PORT_OUTPUT});
        out.push_back({"notes",      VIVID_PORT_SPREAD, VIVID_PORT_OUTPUT});
        out.push_back({"velocities", VIVID_PORT_SPREAD, VIVID_PORT_OUTPUT});
        // MIDI output
        out.push_back(VIVID_CUSTOM_REF_PORT("midi_out", VIVID_PORT_OUTPUT, VividMidiBuffer));
    }

    void process_audio(const VividAudioContext* ctx) override {
        namespace layout = vivid_sequencers::drum_layout;
        float phase = ctx->input_float_values[0];
        bool reset = ctx->input_float_values[1] > 0.5f;

        // Rising-edge reset: capture current phase as offset
        if (reset && !prev_reset_)
            phase_offset_ = phase;
        prev_reset_ = reset;

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
        prev_step_ = step;

        for (std::size_t d = 0; d < layout::kDrumCount; ++d) {
            bool active = (step_changed &&
                ctx->param_values[layout::trigger_param_index(d, step)] > 0.5f);
            ctx->output_float_values[d] = active ? 1.0f : 0.0f;
        }
        ctx->output_float_values[layout::kStepOutputIndex] = static_cast<float>(step);

        for (std::size_t d = 0; d < layout::kDrumCount; ++d) {
            ctx->output_float_values[layout::mod_a_output_index(d)] =
                ctx->param_values[layout::mod_a_param_index(d, step)];
            ctx->output_float_values[layout::mod_b_output_index(d)] =
                ctx->param_values[layout::mod_b_param_index(d, step)];
        }

        if (ctx->output_spreads) {
            auto& gates_sp = ctx->output_spreads[layout::kGatesSpreadOutputIndex];
            auto& notes_sp = ctx->output_spreads[layout::kNotesSpreadOutputIndex];
            auto& vels_sp  = ctx->output_spreads[layout::kVelocitiesSpreadOutputIndex];

            if (gates_sp.capacity >= layout::kDrumCount) {
                gates_sp.length = static_cast<uint32_t>(layout::kDrumCount);
                notes_sp.length = static_cast<uint32_t>(layout::kDrumCount);
                vels_sp.length  = static_cast<uint32_t>(layout::kDrumCount);
                for (std::size_t d = 0; d < layout::kDrumCount; ++d) {
                    gates_sp.data[d] = ctx->output_float_values[d];
                    notes_sp.data[d] = static_cast<float>(ctx->param_values[layout::note_param_index(d)]);
                    vels_sp.data[d]  = ctx->param_values[layout::mod_a_param_index(d, step)];
                }
            }
        }

        // Populate MIDI output with note-on messages for active drums
        uint8_t ch = static_cast<uint8_t>(midi_channel.int_value() - 1);
        midi_buf_.count = 0;
        for (std::size_t d = 0; d < layout::kDrumCount; ++d) {
            bool active = (step_changed &&
                ctx->param_values[layout::trigger_param_index(d, step)] > 0.5f);
            if (active) {
                vivid_sequencers::midi_note_on(midi_buf_,
                    static_cast<uint8_t>(ctx->param_values[layout::note_param_index(d)]), 127, ch);
            }
        }
        if (ctx->custom_outputs && ctx->custom_output_count > 0) {
            ctx->custom_outputs[0] = &midi_buf_;
        }
    }

    // Inspector state
    int insp_tab_ = 0;
    bool insp_dragging_ = false;
    int insp_drag_drum_ = -1;
    int insp_drag_step_ = -1;

    void draw_inspector(VividInspectorContext* ctx) override {
        namespace di = drum_insp;
        namespace layout = vivid_sequencers::drum_layout;
        auto& d = ctx->draw;
        void* o = d.opaque;
        const auto& th = ctx->theme;
        const auto& mouse = ctx->mouse;

        float px = ctx->content_x;
        float base_y = ctx->content_y;
        float panel_w = ctx->content_width;

        int num_steps = (ctx->param_count > 0) ? std::clamp(static_cast<int>(ctx->param_values[0]), 1, 16) : 16;

        // Current step from output[6]
        int current_step = -1;
        if (ctx->output_count > layout::kStepOutputIndex)
            current_step = static_cast<int>(ctx->output_values[layout::kStepOutputIndex]);

        // Layout
        float grid_w = panel_w - di::kLabelW;
        float cell_w = grid_w / 16.0f;
        float grid_h = 6.0f * di::kCellH;

        float y = 4.0f; // relative y offset

        // --- Tab bar ---
        for (int t = 0; t < 3; ++t) {
            float tx = static_cast<float>(t) * di::kTabW;
            bool active = (insp_tab_ == t);

            if (active) {
                d.draw_rect(o, px + tx, base_y + y, di::kTabW, di::kTabH,
                            {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.9f});
                d.draw_rect(o, px + tx, base_y + y + di::kTabH - 2, di::kTabW, 2,
                            {th.accent.r, th.accent.g, th.accent.b, 1.0f});
            }

            float mult = active ? 1.5f : 1.0f;
            float text_alpha = active ? 1.0f : 0.5f;
            d.draw_text(o, px + tx + 8, base_y + y + 3, di::kTabLabels[t],
                        {th.dim_text.r * mult, th.dim_text.g * mult, th.dim_text.b * mult, text_alpha}, 1.0f);

            if (mouse.left_clicked &&
                mouse.x >= tx && mouse.x < tx + di::kTabW &&
                mouse.y >= y && mouse.y < y + di::kTabH) {
                insp_tab_ = t;
            }
        }

        y += di::kTabH + 2;

        float total_h = grid_h + 8.0f;

        // Dark background for grid area
        d.draw_rect(o, px, base_y + y, panel_w, total_h,
                    {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.9f});

        float grid_x = di::kLabelW;     // relative x
        float grid_y = y + 4.0f;        // relative y

        // Current step column highlight
        if (current_step >= 0 && current_step < num_steps) {
            d.draw_rect(o, px + grid_x + current_step * cell_w, base_y + grid_y,
                        cell_w, grid_h,
                        {th.accent.r, th.accent.g, th.accent.b, 0.15f});
        }

        // Beat group separators
        for (int b = 1; b < 4; ++b) {
            float sx = grid_x + b * 4 * cell_w;
            d.draw_rect(o, px + sx - 0.5f, base_y + grid_y, 1.0f, grid_h,
                        {th.separator.r, th.separator.g, th.separator.b, 0.6f});
        }

        for (std::size_t drum = 0; drum < layout::kDrumCount; ++drum) {
            float row_y = grid_y + drum * di::kCellH;

            // Row label
            d.draw_text(o, px + 2, base_y + row_y + 1, layout::kDrumLabels[drum],
                        {di::kDrumColors[drum][0], di::kDrumColors[drum][1], di::kDrumColors[drum][2], 0.8f}, 1.0f);

            for (std::size_t s = 0; s < layout::kStepCount; ++s) {
                float cx = grid_x + s * cell_w;
                float cy = row_y;
                bool beyond_steps = (s >= num_steps);

                bool trigger_active = false;
                int trig_idx = layout::trigger_param_index(drum, static_cast<int>(s));
                if (trig_idx < static_cast<int>(ctx->param_count))
                    trigger_active = ctx->param_values[trig_idx] > 0.5f;

                if (insp_tab_ == 0) {
                    // Pattern tab: boolean toggle
                    if (trigger_active) {
                        float alpha = beyond_steps ? 0.25f : 0.9f;
                        d.draw_rect(o, px + cx + di::kCellPad, base_y + cy + di::kCellPad,
                                    cell_w - 2 * di::kCellPad, di::kCellH - 2 * di::kCellPad,
                                    {di::kDrumColors[drum][0], di::kDrumColors[drum][1], di::kDrumColors[drum][2], alpha});
                    }

                    if (mouse.left_clicked &&
                        mouse.x >= cx && mouse.x < cx + cell_w &&
                        mouse.y >= cy && mouse.y < cy + di::kCellH) {
                        std::string name = std::string(layout::kTriggerPrefixes[drum]) + std::to_string(s);
                        ctx->commands.set_param(ctx->commands.opaque, name.c_str(),
                                                trigger_active ? 0.0f : 1.0f);
                    }
                } else {
                    // Mod A or Mod B tab
                    int mod_idx = (insp_tab_ == 1)
                        ? layout::mod_a_param_index(drum, static_cast<int>(s))
                        : layout::mod_b_param_index(drum, static_cast<int>(s));
                    float mod_val = 0.5f;
                    if (mod_idx < static_cast<int>(ctx->param_count))
                        mod_val = ctx->param_values[mod_idx];

                    float base_alpha = beyond_steps ? 0.25f : (trigger_active ? 0.8f : 0.3f);

                    // Dark track background
                    d.draw_rect(o, px + cx + di::kCellPad, base_y + cy + di::kCellPad,
                                cell_w - 2 * di::kCellPad, di::kCellH - 2 * di::kCellPad,
                                {0.1f, 0.1f, 0.12f, base_alpha});

                    // Fill bar from bottom
                    float inner_h = di::kCellH - 2 * di::kCellPad;
                    float fill_h = mod_val * inner_h;
                    d.draw_rect(o, px + cx + di::kCellPad,
                                base_y + cy + di::kCellPad + inner_h - fill_h,
                                cell_w - 2 * di::kCellPad, fill_h,
                                {di::kDrumColors[drum][0], di::kDrumColors[drum][1], di::kDrumColors[drum][2], base_alpha});

                    // Click to start drag
                    if (mouse.left_clicked &&
                        mouse.x >= cx && mouse.x < cx + cell_w &&
                        mouse.y >= cy && mouse.y < cy + di::kCellH) {
                        insp_dragging_ = true;
                        insp_drag_drum_ = drum;
                        insp_drag_step_ = s;
                    }
                }
            }
        }

        // Handle mod drag (continuous while held)
        if (insp_dragging_ && mouse.left_down) {
            const auto& mod_prefix = (insp_tab_ == 1)
                ? layout::kModAPrefixes
                : layout::kModBPrefixes;
            float cell_y = grid_y + insp_drag_drum_ * di::kCellH;
            float inner_y = cell_y + di::kCellPad;
            float inner_h = di::kCellH - 2 * di::kCellPad;
            float t = 1.0f - (mouse.y - inner_y) / inner_h;
            t = std::max(0.0f, std::min(1.0f, t));

            std::string name = std::string(mod_prefix[static_cast<std::size_t>(insp_drag_drum_)])
                + std::to_string(insp_drag_step_);
            ctx->commands.set_param(ctx->commands.opaque, name.c_str(), t);
        }

        if (mouse.left_released) {
            insp_dragging_ = false;
            insp_drag_drum_ = -1;
            insp_drag_step_ = -1;
        }

        y += total_h + 4;
        ctx->consumed_height = y;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        namespace layout = vivid_sequencers::drum_layout;
        if (!ctx) return;
        if (!thumb_pipeline_ || thumb_pipeline_format_ != ctx->thumbnail_format) {
            rebuild_thumb_pipeline(ctx);
        }
        if (!thumb_pipeline_ || !thumb_bind_group_ || !thumb_uniform_buf_) {
            vivid_report_thumbnail_error(ctx, "drum_sequencer thumbnail pipeline init failed");
            return;
        }

        // Pack triggers into bitmasks
        struct Uniforms {
            float meta[4];      // num_steps, current_step, pad, pad
            uint32_t triggers_lo[4]; // kick, snare, hat, oh bitmasks
            uint32_t triggers_hi[4]; // clap, tom, 0, 0
        } u{};

        int n = 16;
        if (ctx->param_count > 0)
            n = std::max(1, std::min(16, static_cast<int>(ctx->param_values[0])));
        int cur_step = -1;
        if (ctx->output_count > layout::kStepOutputIndex)
            cur_step = static_cast<int>(ctx->output_values[layout::kStepOutputIndex]);

        u.meta[0] = static_cast<float>(n);
        u.meta[1] = static_cast<float>(cur_step);

        for (std::size_t drum = 0; drum < layout::kDrumCount; ++drum) {
            uint32_t mask = 0;
            for (std::size_t s = 0; s < layout::kStepCount; ++s) {
                const int trig_idx = layout::trigger_param_index(drum, static_cast<int>(s));
                if (ctx->param_count > static_cast<uint32_t>(trig_idx)) {
                    if (ctx->param_values[trig_idx] > 0.5f) {
                        mask |= (1u << s);
                    }
                }
            }
            if (drum < 4) {
                u.triggers_lo[drum] = mask;
            } else {
                u.triggers_hi[drum - 4] = mask;
            }
        }

        wgpuQueueWriteBuffer(ctx->queue, thumb_uniform_buf_, 0, &u, sizeof(u));
        vivid::thumbnail::run_pass(ctx, thumb_pipeline_, thumb_bind_group_, "DrumSeq Thumb Pass");
    }

    ~DrumSequencer() override {
        vivid::gpu::release(thumb_pipeline_);
        vivid::gpu::release(thumb_bind_group_);
        vivid::gpu::release(thumb_bind_layout_);
        vivid::gpu::release(thumb_uniform_buf_);
        vivid::gpu::release(thumb_shader_);
        vivid::gpu::release(thumb_pipe_layout_);
    }

private:
    int prev_step_ = -1;
    float phase_offset_ = 0.0f;
    bool prev_reset_ = false;
    VividMidiBuffer midi_buf_ = {};

    void rebuild_thumb_pipeline(const VividThumbnailContext* ctx) {
        vivid::gpu::release(thumb_pipeline_);
        vivid::gpu::release(thumb_bind_group_);
        vivid::gpu::release(thumb_bind_layout_);
        vivid::gpu::release(thumb_uniform_buf_);
        vivid::gpu::release(thumb_shader_);
        vivid::gpu::release(thumb_pipe_layout_);

        static const char* kThumbFragment = R"(
struct Uniforms {
    info: vec4f,
    triggers_lo: vec4u,
    triggers_hi: vec4u,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

fn get_trigger(drum: i32, step: i32) -> bool {
    var mask: u32;
    if (drum < 4) {
        mask = uniforms.triggers_lo[drum];
    } else {
        mask = uniforms.triggers_hi[drum - 4];
    }
    return (mask & (1u << u32(step))) != 0u;
}

fn drum_color(drum: i32) -> vec3f {
    switch(drum) {
        case 0: { return vec3f(220.0, 80.0, 80.0); }    // kick — red
        case 1: { return vec3f(220.0, 190.0, 60.0); }   // snare — gold
        case 2: { return vec3f(60.0, 200.0, 180.0); }   // hat — teal
        case 3: { return vec3f(80.0, 130.0, 220.0); }   // oh — blue
        case 4: { return vec3f(160.0, 90.0, 200.0); }   // clap — purple
        case 5: { return vec3f(80.0, 200.0, 100.0); }   // tom — green
        default: { return vec3f(100.0, 100.0, 100.0); }
    }
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let bg = vec4f(18.0/255.0, 20.0/255.0, 23.0/255.0, 230.0/255.0);

    let num_steps = i32(uniforms.info.x);
    let cur_step = i32(uniforms.info.y);

    let pad = 2.0 / 64.0;
    let grid_x = (uv.x - pad) / (1.0 - 2.0 * pad);
    let grid_y = (uv.y - pad) / (1.0 - 2.0 * pad);

    if (grid_x < 0.0 || grid_x > 1.0 || grid_y < 0.0 || grid_y > 1.0) {
        return bg;
    }

    let col_f = grid_x * 16.0;
    let row_f = grid_y * 6.0;
    let col = i32(floor(col_f));
    let row = i32(floor(row_f));
    if (col >= 16 || row >= 6) { return bg; }

    var result = bg;

    // Current step column highlight
    if (col == cur_step && col < num_steps) {
        result = vec4f(
            min(1.0, bg.r + 25.0/255.0),
            min(1.0, bg.g + 30.0/255.0),
            min(1.0, bg.b + 35.0/255.0),
            bg.a
        );
    }

    // Active cell dot (with inset)
    let inset = 1.5 / 64.0 * 16.0;  // ~1.5px inset relative to cell
    let cell_x = fract(col_f);
    let cell_y = fract(row_f);
    let in_dot = cell_x > (inset / 16.0 * 16.0) && cell_x < (1.0 - inset / 16.0 * 16.0) &&
                 cell_y > (inset / 6.0 * 6.0) && cell_y < (1.0 - inset / 6.0 * 6.0);

    if (in_dot && get_trigger(row, col)) {
        let c = drum_color(row);
        var alpha = 230.0;
        if (col >= num_steps) { alpha = 60.0; }
        result = vec4f(c / 255.0, alpha / 255.0);
    }

    return result;
}
)";

        static constexpr uint64_t kUniformSize = sizeof(float) * 4 + sizeof(uint32_t) * 8;
        thumb_shader_ = vivid::thumbnail::create_shader(ctx->device, kThumbFragment, "DrumSeq Thumb Shader");
        thumb_uniform_buf_ =
            vivid::thumbnail::create_uniform_buffer(ctx->device, kUniformSize, "DrumSeq Thumb Uniforms");
        thumb_bind_layout_ =
            vivid::thumbnail::create_uniform_bind_layout(ctx->device, kUniformSize, "DrumSeq Thumb BGL");
        thumb_pipe_layout_ =
            vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_bind_layout_, "DrumSeq Thumb Layout");
        thumb_bind_group_ = vivid::thumbnail::create_uniform_bind_group(
            ctx->device, thumb_bind_layout_, thumb_uniform_buf_, kUniformSize, "DrumSeq Thumb BG");
        thumb_pipeline_ = vivid::thumbnail::create_pipeline(
            ctx->device, thumb_shader_, thumb_pipe_layout_, ctx->thumbnail_format, "DrumSeq Thumb Pipeline");
        if (!thumb_shader_ || !thumb_uniform_buf_ || !thumb_bind_layout_ || !thumb_pipe_layout_
            || !thumb_bind_group_ || !thumb_pipeline_) {
            vivid::gpu::release(thumb_pipeline_);
            vivid::gpu::release(thumb_bind_group_);
            vivid::gpu::release(thumb_bind_layout_);
            vivid::gpu::release(thumb_uniform_buf_);
            vivid::gpu::release(thumb_shader_);
            vivid::gpu::release(thumb_pipe_layout_);
            thumb_pipeline_format_ = WGPUTextureFormat_Undefined;
            return;
        }
        thumb_pipeline_format_ = ctx->thumbnail_format;
    }
};

VIVID_REGISTER(DrumSequencer)
VIVID_THUMBNAIL(DrumSequencer)
VIVID_INSPECTOR(DrumSequencer)
