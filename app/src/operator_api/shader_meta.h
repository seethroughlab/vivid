#pragma once

// ADR-0016 — a shader FILE is an operator.
//
// This is the one place that understands the shader-file format: a `.wgsl`/`.glsl`
// carrying a JSON header in a leading `/*{ ... }*/` comment that declares the
// operator's name, its texture inputs and its params.
//
//   /*{
//     "version": 1,
//     "name": "Plasma",
//     "summary": "Animated plasma colour-field generator.",
//     "keywords": ["generator", "plasma"],
//     "inputs": [],
//     "params": [
//       {"name": "warp", "type": "float", "default": 0.5, "min": 0, "max": 1, "display": "knob"}
//     ]
//   }*/
//   @fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f { ... }
//
// The header DECLARES; the host GENERATES. `generate_prelude()` emits the uniform
// struct, the bindings, the sampler and the vertex stage from the declaration, and
// `uniform_layout()` says where each param's bytes go. Because both come from the
// same declaration, the hand-packed-uniform corruption trap is impossible by
// construction rather than merely unlikely.
//
// Pure and headless: no wgpu, no filesystem. It lives in operator_api/ because both
// the host (scanning the library) and an operator dylib may need it.

#include "operator_api/types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vivid {

// Which shader language the body is written in — taken from the file extension.
enum class ShaderDialect { Wgsl, Glsl };

// The declared type of a param. Color and Point2 are vector params: they occupy
// several consecutive HOST params (which is how the inspector's compound widgets
// find them) but a single vector field in the generated uniform struct.
enum class ShaderParamType { Float, Int, Bool, Color, Point2 };

// Number of host params (and uniform components) a declared param expands into.
int shader_param_components(ShaderParamType t);

struct ShaderParam {
    std::string name;                   // identifier; also the uniform field name
    std::string label;                  // display label (empty => use name)
    std::string description;            // inspector tooltip
    std::string group;                  // inspector group (empty => ungrouped)
    ShaderParamType type = ShaderParamType::Float;
    float def[3] = {0.f, 0.f, 0.f};     // per-component default (only [0] unless vector)
    float min = 0.f;
    float max = 1.f;
    std::vector<std::string> choices;   // non-empty => enum (implies Int)
    VividDisplayHint display = VIVID_DISPLAY_DEFAULT;  // derived from type unless declared
};

struct ShaderMeta {
    int version = 1;
    std::string name;                   // operator type name, e.g. "Plasma"
    std::string summary;
    std::vector<std::string> keywords;
    std::vector<std::string> inputs;    // 0..2 texture input port names
    std::vector<ShaderParam> params;
    std::string body;                   // source with the header comment stripped
    ShaderDialect dialect = ShaderDialect::Wgsl;
    std::string error;                  // non-empty => malformed; the catalog row still exists
};

// One host-visible param — what the operator ABI (and therefore the inspector, the
// graph, mappings and persistence) actually sees. A Color declared as `tint` expands
// into tint_r / tint_g / tint_b, the first carrying VIVID_DISPLAY_COLOR.
struct ShaderHostParam {
    std::string name;                   // "tint_r"
    std::string label;
    std::string description;
    std::string group;
    VividParamType type = VIVID_PARAM_FLOAT;
    float def = 0.f, min = 0.f, max = 1.f;
    VividDisplayHint display = VIVID_DISPLAY_DEFAULT;
    std::vector<std::string> choices;
    size_t param_index = 0;             // index into ShaderMeta::params
    int component = 0;                  // 0..2 within that param
};

// Where each declared param's bytes live in the uniform buffer. Offsets follow the
// WGSL uniform address-space rules (== GLSL std140 for the types we emit), so they
// are valid for both dialects.
struct UniformLayout {
    struct Entry {
        uint32_t offset = 0;            // byte offset of the param's field
        int components = 1;             // 1..3
        bool is_int = false;            // pack as i32 rather than f32
    };
    std::vector<Entry> entries;         // parallel to ShaderMeta::params
    uint32_t res_offset = 0;            // vec2f — output resolution in pixels
    uint32_t time_offset = 8;           // f32   — seconds
    uint32_t size = 16;                 // total buffer size, a multiple of 16
};

// Parse a shader file. On failure the returned meta carries a non-empty `error` and
// whatever was parsed before the failure — a malformed shader must yield a catalog
// row WITH an error, never a vanished row.
ShaderMeta parse_shader(const std::string& source, ShaderDialect dialect);

// Expand the declared params into the host params the operator ABI exposes.
std::vector<ShaderHostParam> host_params(const ShaderMeta& meta);

// Byte offsets of every field of the generated uniform struct.
UniformLayout uniform_layout(const ShaderMeta& meta);

// The generated source prepended to the body before compilation: the uniform struct,
// the bind-group entries, the sampler (only when there is at least one input) and
// the fullscreen vertex stage.
std::string generate_prelude(const ShaderMeta& meta);

// Pack host param VALUES (one float per ShaderHostParam, in host_params() order) into
// `out`, which must be at least uniform_layout(meta).size bytes. Ints and bools are
// converted to i32; `out` is zeroed first, so the padding is deterministic.
void pack_uniforms(const ShaderMeta& meta, const UniformLayout& layout,
                   const float* values, size_t value_count,
                   float res_w, float res_h, float time,
                   void* out, size_t out_size);

}  // namespace vivid
