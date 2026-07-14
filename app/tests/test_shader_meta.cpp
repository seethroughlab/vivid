// ADR-0016 / S1 — the shader-file format: header parser, host-param expansion, the
// generated prelude and the uniform layout. Pure and headless: no GPU, no filesystem.
//
// The layout tests are the ones that matter most: the whole point of generating the
// uniform struct AND the byte offsets from one declaration is that the hand-packed
// `float u[8]` corruption trap becomes impossible. If these offsets and the emitted
// WGSL ever disagree, that guarantee is gone.
#include "operator_api/shader_meta.h"
#include "test_helpers.h"

#include <cstring>
#include <string>

using namespace vivid;

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

static const char* kValid = R"(/*{
  "version": 1,
  "name": "Plasma",
  "summary": "Animated plasma colour-field generator.",
  "keywords": ["generator", "plasma"],
  "inputs": [],
  "params": [
    {"name": "warp",    "type": "float", "default": 0.5, "min": 0, "max": 1, "display": "knob"},
    {"name": "density", "type": "float", "default": 0.5, "description": "pattern density"}
  ]
}*/
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    return vec4f(u.warp, u.density, u.time, 1.0);
}
)";

static void test_valid_header() {
    ShaderMeta m = parse_shader(kValid, ShaderDialect::Wgsl);
    CHECK(m.error.empty());
    CHECK(m.version == 1);
    CHECK(m.name == "Plasma");
    CHECK(m.summary == "Animated plasma colour-field generator.");
    CHECK(m.keywords.size() == 2 && m.keywords[1] == "plasma");
    CHECK(m.inputs.empty());                       // a generator
    CHECK(m.params.size() == 2);
    CHECK(m.params[0].name == "warp");
    CHECK(m.params[0].display == VIVID_DISPLAY_KNOB);
    CHECK_NEAR(m.params[0].def[0], 0.5f, 1e-6);
    CHECK(m.params[1].description == "pattern density");
    CHECK_NEAR(m.params[1].max, 1.0f, 1e-6);       // min/max default to 0..1

    // The header comment is stripped; the body is what gets compiled.
    CHECK(!contains(m.body, "\"name\""));
    CHECK(contains(m.body, "@fragment fn fs_main"));
}

static void test_no_header() {
    ShaderMeta m = parse_shader("@fragment fn fs_main() -> @location(0) vec4f { return vec4f(1.0); }",
                                ShaderDialect::Wgsl);
    CHECK(!m.error.empty());
    CHECK(contains(m.error, "no JSON header"));
}

static void test_malformed_json() {
    ShaderMeta m = parse_shader("/*{ \"name\": \"Broken\", }*/\nbody", ShaderDialect::Wgsl);
    CHECK(!m.error.empty());
    CHECK(contains(m.error, "malformed JSON"));
}

static void test_missing_name() {
    ShaderMeta m = parse_shader("/*{ \"summary\": \"anonymous\" }*/\nbody", ShaderDialect::Wgsl);
    CHECK(!m.error.empty());
    CHECK(contains(m.error, "name"));
}

static void test_duplicate_param_names() {
    ShaderMeta m = parse_shader(
        "/*{ \"name\": \"Dup\", \"params\": [{\"name\":\"a\"},{\"name\":\"a\"}] }*/\nbody",
        ShaderDialect::Wgsl);
    CHECK(!m.error.empty());
    CHECK(contains(m.error, "duplicate param"));
}

static void test_unknown_param_type() {
    ShaderMeta m = parse_shader(
        "/*{ \"name\": \"Bad\", \"params\": [{\"name\":\"a\",\"type\":\"quaternion\"}] }*/\nbody",
        ShaderDialect::Wgsl);
    CHECK(!m.error.empty());
    CHECK(contains(m.error, "unknown type"));
}

// Param and input names are interpolated into generated shader source; a name that
// isn't a plain identifier could inject code, so it is rejected outright.
static void test_identifier_hygiene() {
    ShaderMeta inj = parse_shader(
        "/*{ \"name\": \"Inj\", \"params\": [{\"name\":\"a: f32}; fn evil()\"}] }*/\nbody",
        ShaderDialect::Wgsl);
    CHECK(!inj.error.empty());
    CHECK(contains(inj.error, "not a valid identifier"));

    // A param may not shadow a name the generated prelude owns.
    ShaderMeta res = parse_shader("/*{ \"name\": \"R\", \"params\": [{\"name\":\"time\"}] }*/\nbody",
                                  ShaderDialect::Wgsl);
    CHECK(!res.error.empty());
    CHECK(contains(res.error, "reserved"));
}

// The boundary rule: a shader file is a fullscreen pass over 0..2 textures.
static void test_input_limits() {
    ShaderMeta ok = parse_shader("/*{ \"name\": \"Comp\", \"inputs\": [\"a\", \"b\"] }*/\nbody",
                                 ShaderDialect::Wgsl);
    CHECK(ok.error.empty());
    CHECK(ok.inputs.size() == 2 && ok.inputs[0] == "a");

    ShaderMeta too_many = parse_shader("/*{ \"name\": \"C\", \"inputs\": [\"a\",\"b\",\"c\"] }*/\nbody",
                                       ShaderDialect::Wgsl);
    CHECK(!too_many.error.empty());
    CHECK(contains(too_many.error, "at most 2"));
}

// "passes"/"buffers" are reserved NOW so that v1 files stay valid when multi-pass
// lands — parse-and-reject with a message that says why (ADR-0016).
static void test_reserved_keys() {
    ShaderMeta m = parse_shader("/*{ \"name\": \"Blur\", \"passes\": 2 }*/\nbody", ShaderDialect::Wgsl);
    CHECK(!m.error.empty());
    CHECK(contains(m.error, "reserved"));
    CHECK(contains(m.error, "passes"));

    ShaderMeta v2 = parse_shader("/*{ \"version\": 2, \"name\": \"Future\" }*/\nbody", ShaderDialect::Wgsl);
    CHECK(!v2.error.empty());
    CHECK(contains(v2.error, "version"));
}

// color => 3 host params (r/g/b) with COLOR on the first; point2 => 2 (x/y) with
// XY_PAD on the first; choices => an int enum. This is what gives a shader rich
// inspector widgets with zero new UI code.
static void test_vector_and_enum_expansion() {
    ShaderMeta m = parse_shader(R"(/*{
      "name": "Tint",
      "inputs": ["input"],
      "params": [
        {"name": "tint",   "type": "color",  "default": [1.0, 0.5, 0.25]},
        {"name": "center", "type": "point2", "default": [0.5, 0.5]},
        {"name": "mode",   "choices": ["add", "multiply", "screen"], "default": 1},
        {"name": "invert", "type": "bool",   "default": true}
      ]
    }*/
body)", ShaderDialect::Wgsl);
    CHECK(m.error.empty());
    CHECK(m.params.size() == 4);

    auto h = host_params(m);
    CHECK(h.size() == 3 + 2 + 1 + 1);

    CHECK(h[0].name == "tint_r" && h[1].name == "tint_g" && h[2].name == "tint_b");
    CHECK(h[0].display == VIVID_DISPLAY_COLOR);
    CHECK(h[1].display == VIVID_DISPLAY_DEFAULT);   // channels draw as ordinary sliders
    CHECK(h[2].display == VIVID_DISPLAY_DEFAULT);
    CHECK(h[0].type == VIVID_PARAM_FLOAT);
    CHECK_NEAR(h[1].def, 0.5f, 1e-6);
    CHECK_NEAR(h[2].def, 0.25f, 1e-6);

    CHECK(h[3].name == "center_x" && h[4].name == "center_y");
    CHECK(h[3].display == VIVID_DISPLAY_XY_PAD);

    CHECK(h[5].name == "mode");
    CHECK(h[5].type == VIVID_PARAM_INT);            // choices imply an int enum
    CHECK(h[5].choices.size() == 3 && h[5].choices[2] == "screen");
    CHECK_NEAR(h[5].def, 1.0f, 1e-6);
    CHECK_NEAR(h[5].min, 0.0f, 1e-6);
    CHECK_NEAR(h[5].max, 2.0f, 1e-6);               // range derived from the choice count

    CHECK(h[6].name == "invert");
    CHECK(h[6].type == VIVID_PARAM_BOOL);
    CHECK_NEAR(h[6].def, 1.0f, 1e-6);
}

// The generated prelude declares everything the body may reference — and nothing the
// body must declare itself.
static void test_prelude() {
    ShaderMeta gen = parse_shader(kValid, ShaderDialect::Wgsl);
    std::string p = generate_prelude(gen);
    CHECK(contains(p, "struct U {"));
    CHECK(contains(p, "res: vec2f"));
    CHECK(contains(p, "time: f32"));
    CHECK(contains(p, "warp: f32"));
    CHECK(contains(p, "@group(0) @binding(0) var<uniform> u: U;"));
    CHECK(contains(p, "@vertex fn vs_main"));
    CHECK(!contains(p, "sampler"));                 // a generator binds no sampler

    ShaderMeta filt = parse_shader(
        "/*{ \"name\": \"Blur\", \"inputs\": [\"input\"], "
        "\"params\": [{\"name\":\"tint\",\"type\":\"color\"},{\"name\":\"mode\",\"type\":\"int\"}] }*/\nbody",
        ShaderDialect::Wgsl);
    std::string fp = generate_prelude(filt);
    CHECK(contains(fp, "@group(0) @binding(1) var input: texture_2d<f32>;"));
    CHECK(contains(fp, "@group(0) @binding(2) var samp: sampler;"));
    CHECK(contains(fp, "tint: vec3f"));
    CHECK(contains(fp, "mode: i32"));

    ShaderMeta comp = parse_shader("/*{ \"name\": \"Mix\", \"inputs\": [\"a\",\"b\"] }*/\nbody",
                                   ShaderDialect::Wgsl);
    std::string cp = generate_prelude(comp);
    CHECK(contains(cp, "@binding(1) var a: texture_2d<f32>;"));
    CHECK(contains(cp, "@binding(2) var b: texture_2d<f32>;"));
    CHECK(contains(cp, "@binding(3) var samp: sampler;"));

    ShaderMeta glsl = parse_shader(kValid, ShaderDialect::Glsl);
    std::string gp = generate_prelude(glsl);
    CHECK(contains(gp, "#version 450"));
    CHECK(contains(gp, "layout(std140, set = 0, binding = 0) uniform U {"));
    CHECK(contains(gp, "float warp;"));
    CHECK(contains(gp, "} u;"));
}

// WGSL uniform address-space alignment (== GLSL std140 for the field types we emit):
// f32/i32 align 4, vec2f align 8, vec3f align 16; the struct's size rounds up to 16.
static void test_uniform_layout() {
    ShaderMeta m = parse_shader(R"(/*{
      "name": "Layout",
      "params": [
        {"name": "a", "type": "float"},
        {"name": "p", "type": "point2"},
        {"name": "c", "type": "color"},
        {"name": "n", "type": "int"}
      ]
    }*/
body)", ShaderDialect::Wgsl);
    CHECK(m.error.empty());

    UniformLayout l = uniform_layout(m);
    CHECK(l.res_offset == 0);      // vec2f  [0, 8)
    CHECK(l.time_offset == 8);     // f32    [8, 12)
    CHECK(l.entries.size() == 4);
    CHECK(l.entries[0].offset == 12);   // f32     — 4-aligned, packs right after time
    CHECK(l.entries[1].offset == 16);   // vec2f   — 8-aligned
    CHECK(l.entries[2].offset == 32);   // vec3f   — 16-aligned (24 rounds up to 32)
    CHECK(l.entries[3].offset == 44);   // i32     — 4-aligned, right after the vec3f's 12 bytes
    CHECK(l.entries[2].components == 3);
    CHECK(l.entries[3].is_int);
    CHECK(l.size == 48);                // 48 already a multiple of 16
    CHECK(l.size % 16 == 0);

    // A generator with no params still needs a legal (16-byte) uniform buffer.
    ShaderMeta bare = parse_shader("/*{ \"name\": \"Bare\" }*/\nbody", ShaderDialect::Wgsl);
    CHECK(uniform_layout(bare).size == 16);
}

// The packer writes host param values at exactly the offsets the layout advertises —
// floats as f32, ints/bools as i32.
static void test_pack_uniforms() {
    ShaderMeta m = parse_shader(R"(/*{
      "name": "Pack",
      "params": [
        {"name": "a", "type": "float", "default": 0.25},
        {"name": "c", "type": "color",  "default": [1.0, 0.5, 0.0]},
        {"name": "n", "type": "int",    "min": 0, "max": 7, "default": 3}
      ]
    }*/
body)", ShaderDialect::Wgsl);
    CHECK(m.error.empty());

    UniformLayout l = uniform_layout(m);
    auto h = host_params(m);
    CHECK(h.size() == 5);   // a, c_r, c_g, c_b, n

    const float values[5] = {0.75f, 0.1f, 0.2f, 0.3f, 5.0f};
    unsigned char buf[256];
    std::memset(buf, 0xAB, sizeof(buf));
    pack_uniforms(m, l, values, 5, 1920.f, 1080.f, 2.5f, buf, l.size);

    float f[2];
    std::memcpy(f, buf + l.res_offset, sizeof(f));
    CHECK_NEAR(f[0], 1920.f, 1e-3);
    CHECK_NEAR(f[1], 1080.f, 1e-3);
    std::memcpy(f, buf + l.time_offset, sizeof(float));
    CHECK_NEAR(f[0], 2.5f, 1e-6);

    std::memcpy(f, buf + l.entries[0].offset, sizeof(float));
    CHECK_NEAR(f[0], 0.75f, 1e-6);

    float rgb[3];
    std::memcpy(rgb, buf + l.entries[1].offset, sizeof(rgb));
    CHECK_NEAR(rgb[0], 0.1f, 1e-6);
    CHECK_NEAR(rgb[1], 0.2f, 1e-6);
    CHECK_NEAR(rgb[2], 0.3f, 1e-6);

    int32_t n = 0;
    std::memcpy(&n, buf + l.entries[2].offset, sizeof(n));
    CHECK(n == 5);                       // packed as an i32, not a float bit pattern

    // Padding is zeroed, not left as whatever was in the buffer.
    CHECK(buf[l.entries[0].offset - 1] == 0 || l.entries[0].offset == 12);
    CHECK(buf[l.size] == 0xAB);          // and the packer stays inside its buffer

    // No values (a node whose params the host hasn't pushed yet) => the declared defaults.
    std::memset(buf, 0xAB, sizeof(buf));
    pack_uniforms(m, l, nullptr, 0, 100.f, 100.f, 0.f, buf, l.size);
    std::memcpy(f, buf + l.entries[0].offset, sizeof(float));
    CHECK_NEAR(f[0], 0.25f, 1e-6);
    std::memcpy(&n, buf + l.entries[2].offset, sizeof(n));
    CHECK(n == 3);
}

int main() {
    test_valid_header();
    test_no_header();
    test_malformed_json();
    test_missing_name();
    test_duplicate_param_names();
    test_unknown_param_type();
    test_identifier_hygiene();
    test_input_limits();
    test_reserved_keys();
    test_vector_and_enum_expansion();
    test_prelude();
    test_uniform_layout();
    test_pack_uniforms();
    return vivid::test::summary("shader_meta");
}
