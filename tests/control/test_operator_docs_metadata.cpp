// Tests that build_operator_docs_response emits the v3 metadata fields
// (display_name, keywords, summary) on the JSON wire.
//
// build_operator_docs_response is the shared helper used by both
// handle_operator_docs and handle_list_types' richer entries; if the field
// emission breaks here, MCP clients lose their human-facing labels.

#include "runtime/control/control_server_internal.h"
#include "operator_api/types.h"
#include "test_helpers.h"

#include <nlohmann/json.hpp>
#include <string>

int main() {
    std::fprintf(stderr, "--- test_operator_docs_metadata ---\n");

    // Descriptor with explicit display_name + keywords + summary.
    {
        std::fprintf(stderr, "\n=== Explicit metadata ===\n");
        const char* kw[] = {"harmony", "chords", "diatonic"};
        VividOperatorDescriptor desc{};
        desc.name           = "ChordProgression";
        desc.display_name   = "Chord Progression";
        desc.keywords       = kw;
        desc.keyword_count  = 3;
        desc.summary        = "Diatonic chord changes from a key + Roman-numeral pattern.";
        desc.has_process_frame = 1;

        nlohmann::json op = vivid::build_operator_docs_response(desc, nullptr);
        check(op["name"] == "ChordProgression", "name preserved");
        check(op["display_name"] == "Chord Progression", "display_name on the wire");
        check(op.contains("keywords") && op["keywords"].is_array() &&
                  op["keywords"].size() == 3 &&
                  op["keywords"][0] == "harmony",
              "keywords array on the wire");
        check(op["summary"] ==
                  "Diatonic chord changes from a key + Roman-numeral pattern.",
              "summary on the wire");
    }

    // Descriptor with no display_name -> auto-derived.
    {
        std::fprintf(stderr, "\n=== Auto-derived display_name ===\n");
        VividOperatorDescriptor desc{};
        desc.name = "audio_out";
        desc.has_process_frame = 1;
        nlohmann::json op = vivid::build_operator_docs_response(desc, nullptr);
        check(op["display_name"] == "Audio Out",
              "auto-derived display_name on the wire when descriptor's is null");
        check(!op.contains("keywords"), "no keywords key when count is 0");
        check(!op.contains("summary"), "no summary key when descriptor's is null");
    }

    // Descriptor with empty display_name string -> auto-derived (treated as
    // unset). Avoids accidentally locking in an empty label.
    {
        std::fprintf(stderr, "\n=== Empty display_name treated as unset ===\n");
        VividOperatorDescriptor desc{};
        desc.name         = "ToneGen";
        desc.display_name = "";  // empty string
        desc.has_process_frame = 1;
        nlohmann::json op = vivid::build_operator_docs_response(desc, nullptr);
        check(op["display_name"] == "Tone Gen",
              "empty display_name string falls back to auto-derive");
    }

    // Value-model fields (lane-value clean-break, v6): operator multiplicity_behavior
    // + per-port value_type/multiplicity derived from the port type.
    {
        std::fprintf(stderr, "\n=== Value-model fields ===\n");
        VividPortDescriptor ports[1]{};
        ports[0].name      = "voices";
        ports[0].type      = VIVID_PORT_LANE_ARRAY;   // -> Float + Many
        ports[0].direction = VIVID_PORT_OUTPUT;
        VividOperatorDescriptor desc{};
        desc.name                  = "VoiceGen";
        desc.has_process_frame     = 1;
        desc.multiplicity_behavior = VIVID_MULTIPLICITY_REDUCE;
        desc.ports                 = ports;
        desc.port_count            = 1;

        nlohmann::json op = vivid::build_operator_docs_response(desc, nullptr);
        check(op["multiplicity_behavior"] == "reduce", "operator multiplicity_behavior on the wire");
        check(op.contains("outputs") && op["outputs"].is_array() && op["outputs"].size() == 1,
              "output port present");
        const auto& port = op["outputs"][0];
        check(port["value_type"] == "float", "lane-array output value_type derived = float");
        check(port["multiplicity"] == "many", "lane-array output multiplicity derived = many");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
