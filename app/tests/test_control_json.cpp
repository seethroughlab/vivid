// Headless test for the shared discovery serializer (cli/control_json.h): a param/operator
// descriptor -> the rich JSON schema, including semantic metadata + display hints, with optional
// fields emitted only when set. Pure (types.h + nlohmann/json), no app.
#include "cli/control_json.h"
#include "test_helpers.h"

#include <string>

using namespace vivid;
namespace cj = vivid::control_json;

int main() {
    // A bare param: required fields present, optional (semantic/description/hint) absent.
    {
        VividParamDescriptor p{};
        p.name = "amount"; p.type = VIVID_PARAM_FLOAT;
        p.default_value = 0.5f; p.min_value = 0.f; p.max_value = 1.f;
        auto j = cj::param_to_json(p);
        CHECK(j["name"] == "amount");
        CHECK(j["type"] == "float");
        CHECK(j["default"] == 0.5f);
        CHECK(j["min"] == 0.f);
        CHECK(j["max"] == 1.f);
        CHECK(!j.contains("semantic_tag"));
        CHECK(!j.contains("display_hint"));   // DEFAULT hint is omitted
    }

    // A semantically-rich param: every optional field surfaces.
    {
        VividParamDescriptor p{};
        p.name = "cutoff"; p.type = VIVID_PARAM_FLOAT;
        p.default_value = 1000.f; p.min_value = 20.f; p.max_value = 20000.f;
        p.description = "filter cutoff";
        p.semantic_tag = "frequency_hz";
        p.semantic_unit = "Hz";
        p.semantic_intent = "brightness";
        p.display_hint = VIVID_DISPLAY_KNOB;
        auto j = cj::param_to_json(p);
        CHECK(j["type"] == "float");
        CHECK(j["description"] == "filter cutoff");
        CHECK(j["semantic_tag"] == "frequency_hz");
        CHECK(j["semantic_unit"] == "Hz");
        CHECK(j["semantic_intent"] == "brightness");
        CHECK(j["display_hint"] == "knob");
    }

    // Enum param choices surface; int type name.
    {
        const char* labels[] = { "sine", "saw", "square" };
        VividParamDescriptor p{};
        p.name = "wave"; p.type = VIVID_PARAM_INT;
        p.choice_labels = labels; p.choice_count = 3;
        auto j = cj::param_to_json(p);
        CHECK(j["type"] == "int");
        CHECK(j["choices"].size() == 3);
        CHECK(j["choices"][1] == "saw");
    }

    // operator_to_json: identity + kind + params + ports.
    {
        VividParamDescriptor p{}; p.name = "gain"; p.type = VIVID_PARAM_FLOAT; p.semantic_tag = "gain_db";
        VividPortDescriptor  in{};  in.name = "input";  in.type = VIVID_PORT_AUDIO_BUFFER; in.direction = VIVID_PORT_INPUT;
        VividPortDescriptor  out{}; out.name = "output"; out.type = VIVID_PORT_AUDIO_BUFFER; out.direction = VIVID_PORT_OUTPUT;
        VividPortDescriptor  ports[2] = { in, out };
        VividOperatorDescriptor d{};
        d.name = "Drive"; d.display_name = "Drive"; d.summary = "overdrive";
        d.param_count = 1; d.params = &p; d.port_count = 2; d.ports = ports;
        auto j = cj::operator_to_json(d, "audio_effect");
        CHECK(j["name"] == "Drive");
        CHECK(j["kind"] == "audio_effect");
        CHECK(j["params"].size() == 1);
        CHECK(j["params"][0]["semantic_tag"] == "gain_db");
        CHECK(j["ports"].size() == 2);
        CHECK(j["ports"][0]["dir"] == "in");
        CHECK(j["ports"][1]["dir"] == "out");
        CHECK(!j.contains("role"));   // ADR-0046: DEFAULT role is implicit (omitted)
    }

    // ADR-0046: a declared operator role surfaces as a lowercase string; DEFAULT stays omitted.
    {
        VividOperatorDescriptor d{};
        d.name = "Instancer"; d.role = VIVID_OP_ROLE_RECIPE;
        auto j = cj::operator_to_json(d, "gpu_visual");
        CHECK(j["role"] == "recipe");

        d.role = VIVID_OP_ROLE_SOURCE;
        CHECK(cj::operator_to_json(d, "gpu_visual")["role"] == "source");

        d.role = VIVID_OP_ROLE_DEFAULT;
        CHECK(!cj::operator_to_json(d, "gpu_visual").contains("role"));
    }

    return vivid::test::summary("test_control_json");
}
