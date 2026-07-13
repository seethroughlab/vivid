// What a plugin IS, from its factory metadata (audio/plugin_class.h). Pure string logic — no SDK,
// no dylib, no plugins installed. Getting this backwards silently routes a synth into an FX slot,
// and the strings come from third-party plugins, so the edge cases are the point.
#include "audio/plugin_class.h"
#include "test_helpers.h"

using namespace vivid::session;

namespace {

void test_clap_features() {
    const char* inst[]    = { "instrument", "synthesizer", "stereo", nullptr };
    const char* fx[]      = { "audio-effect", "distortion", nullptr };
    const char* note[]    = { "note-effect", nullptr };
    const char* both[]    = { "audio-effect", "instrument", nullptr };   // a synth with an FX mode
    const char* neither[] = { "stereo", "utility", nullptr };
    const char* empty[]   = { nullptr };

    CHECK(class_from_clap_features(inst) == kClassInstrument);
    CHECK(class_from_clap_features(fx) == kClassEffect);
    CHECK(class_from_clap_features(note) == kClassNoteEffect);
    CHECK(class_from_clap_features(both) == kClassInstrument);   // instrument wins: you ADD it as a source
    CHECK(class_from_clap_features(neither) == kClassUnknown);
    CHECK(class_from_clap_features(empty) == kClassUnknown);
    CHECK(class_from_clap_features(nullptr) == kClassUnknown);
}

void test_vst3_subcategories() {
    CHECK(class_from_vst3_subcategories("Instrument|Synth") == kClassInstrument);
    CHECK(class_from_vst3_subcategories("Instrument") == kClassInstrument);
    CHECK(class_from_vst3_subcategories("Fx|Delay") == kClassEffect);
    CHECK(class_from_vst3_subcategories("Fx") == kClassEffect);
    CHECK(class_from_vst3_subcategories("Fx|Instrument") == kClassInstrument);   // instrument wins
    CHECK(class_from_vst3_subcategories("Spatial|Fx") == kClassEffect);
    CHECK(class_from_vst3_subcategories("") == kClassUnknown);
    CHECK(class_from_vst3_subcategories(nullptr) == kClassUnknown);
    CHECK(class_from_vst3_subcategories("Analyzer") == kClassUnknown);
}

// Per-TOKEN matching: a substring test would call "Instrumental" an instrument, and would match
// "Fx" inside "FxSomething". These are the bugs this function exists to not have.
void test_token_matching_not_substring() {
    CHECK(class_from_vst3_subcategories("Instrumental") == kClassUnknown);
    CHECK(class_from_vst3_subcategories("Fxx") == kClassUnknown);
    CHECK(class_from_vst3_subcategories("SuperFx") == kClassUnknown);
    // ...but a real token anywhere in the list still matches.
    CHECK(class_from_vst3_subcategories("Mono|Fx|Filter") == kClassEffect);
    CHECK(vst3_subcategory_has("Fx|Delay", "Delay"));
    CHECK(!vst3_subcategory_has("Fx|Delay", "Del"));
}

// The SDK's constants are a convention, not a guarantee — plugins ship odd casing.
void test_case_insensitive() {
    CHECK(class_from_vst3_subcategories("instrument|synth") == kClassInstrument);
    CHECK(class_from_vst3_subcategories("FX|REVERB") == kClassEffect);
}

}  // namespace

int main() {
    test_clap_features();
    test_vst3_subcategories();
    test_token_matching_not_substring();
    test_case_insensitive();
    return vivid::test::summary("test_plugin_class");
}
