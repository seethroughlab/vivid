// Focused test for the operator authoring helpers in operator_api/ports.h — the port-descriptor
// builders and param readers that replace the copy-pasted local tex_port()/aud_port()/pv-lambda.
// Header-only, headless: asserts the descriptors are byte-identical to what the hand-written copies
// produced, and that the param readers honor the null-check + fallback contract.
#include "operator_api/ports.h"

#include <cassert>
#include <cstring>

// A minimal stand-in for any process context: the param readers are templated and touch only
// `param_values`, so this is all they need.
struct FakeCtx { const float* param_values; };

int main() {
    using namespace vivid;

    // --- texture ports: match the old tex_port() exactly ---
    const VividPortDescriptor tout = texture_output();
    assert(std::strcmp(tout.name, "texture") == 0);
    assert(tout.type == VIVID_PORT_TEXTURE);
    assert(tout.direction == VIVID_PORT_OUTPUT);
    assert(tout.value_type == VIVID_VALUE_TEXTURE);
    assert(tout.multiplicity == VIVID_MULTIPLICITY_SCALAR);

    const VividPortDescriptor tin = texture_input("map");   // effects name their input freely
    assert(std::strcmp(tin.name, "map") == 0);
    assert(tin.type == VIVID_PORT_TEXTURE);
    assert(tin.direction == VIVID_PORT_INPUT);
    assert(std::strcmp(texture_input().name, "input") == 0);  // default input name

    // --- stereo audio ports: match the old aud_port() exactly ---
    const VividPortDescriptor aout = audio_output_stereo();
    assert(std::strcmp(aout.name, "output") == 0);
    assert(aout.type == VIVID_PORT_AUDIO_BUFFER);
    assert(aout.direction == VIVID_PORT_OUTPUT);
    assert(aout.value_type == VIVID_VALUE_AUDIO);
    assert(aout.multiplicity == VIVID_MULTIPLICITY_SCALAR);
    assert(aout.channels == 2);

    const VividPortDescriptor ain = audio_input_stereo();
    assert(std::strcmp(ain.name, "input") == 0);
    assert(ain.direction == VIVID_PORT_INPUT);
    assert(ain.channels == 2);

    // --- param readers: live value when present, fallback otherwise ---
    const float vals[3] = { 0.25f, 2.6f, 1.0f };
    const FakeCtx ctx{ vals };
    assert(param_value(&ctx, 0, -1.0f) == 0.25f);
    assert(param_int(&ctx, 1, -1) == 3);        // lround(2.6) == 3, not truncation to 2
    assert(param_bool(&ctx, 2, false) == true); // 1.0 > 0.5

    // null param_values array -> fallback
    const FakeCtx no_params{ nullptr };
    assert(param_value(&no_params, 0, 9.0f) == 9.0f);
    assert(param_int(&no_params, 0, 7) == 7);
    assert(param_bool(&no_params, 0, true) == true);

    // null context -> fallback (never dereferenced)
    const FakeCtx* null_ctx = nullptr;
    assert(param_value(null_ctx, 0, 5.0f) == 5.0f);
    assert(param_int(null_ctx, 0, 4) == 4);
    assert(param_bool(null_ctx, 0, false) == false);

    return 0;
}
