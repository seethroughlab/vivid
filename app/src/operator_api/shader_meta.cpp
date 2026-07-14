#include "operator_api/shader_meta.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

namespace vivid {
namespace {

using nlohmann::json;

constexpr int kFormatVersion = 1;
constexpr size_t kMaxInputs = 2;   // ADR-0016 boundary rule: 0..2 texture inputs

// Names the generated prelude itself owns; a shader may not shadow them.
bool is_reserved_identifier(const std::string& n) {
    static const char* kReserved[] = {"u", "samp", "res", "time", "U",
                                      "v_uv", "o_color", "vs_main", "fs_main",
                                      "FullscreenOutput", "fullscreenTriangle"};
    for (const char* r : kReserved)
        if (n == r) return true;
    return false;
}

// Param and input names are interpolated straight into generated shader source, so
// they must be plain identifiers — this is what keeps a header from injecting code.
bool is_identifier(const std::string& n) {
    if (n.empty() || n.size() > 64) return false;
    if (!(std::isalpha(static_cast<unsigned char>(n[0])) || n[0] == '_')) return false;
    for (char c : n)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Header block: /*{ ... }*/ at the top of the file
// ---------------------------------------------------------------------------

// Locates the JSON object of a leading `/*{ ... }*/` comment. json_start points at
// the '{', json_end one past the matching '}', block_end one past the closing '*/'.
bool find_header_block(const std::string& src, size_t& json_start, size_t& json_end,
                       size_t& block_end) {
    size_t i = 0;
    while (i < src.size() && std::isspace(static_cast<unsigned char>(src[i]))) i++;
    if (i + 1 >= src.size() || src[i] != '/' || src[i + 1] != '*') return false;

    size_t brace = i + 2;
    while (brace < src.size() && std::isspace(static_cast<unsigned char>(src[brace]))) brace++;
    if (brace >= src.size() || src[brace] != '{') return false;
    json_start = brace;

    // Scan for the `*/` that closes the comment, then walk back over whitespace to
    // find the '}' that ends the object. Braces inside strings can't confuse this:
    // we bound on the comment terminator, and JSON strings cannot contain `*/`
    // followed by nothing — but they *can*, so hand the substring to the JSON parser
    // and let a mismatch surface as a parse error rather than guessing.
    size_t pos = json_start + 1;
    while (pos < src.size()) {
        size_t close = src.find("*/", pos);
        if (close == std::string::npos) return false;
        size_t check = close;
        while (check > json_start && std::isspace(static_cast<unsigned char>(src[check - 1]))) check--;
        if (check > json_start && src[check - 1] == '}') {
            json_end = check;
            block_end = close + 2;
            return true;
        }
        pos = close + 2;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Field parsing
// ---------------------------------------------------------------------------

bool parse_param_type(const std::string& s, ShaderParamType& out) {
    if (s == "float")  { out = ShaderParamType::Float;  return true; }
    if (s == "int")    { out = ShaderParamType::Int;    return true; }
    if (s == "bool")   { out = ShaderParamType::Bool;   return true; }
    if (s == "color")  { out = ShaderParamType::Color;  return true; }
    if (s == "point2") { out = ShaderParamType::Point2; return true; }
    return false;
}

bool parse_display(const std::string& s, VividDisplayHint& out) {
    if (s == "default") { out = VIVID_DISPLAY_DEFAULT; return true; }
    if (s == "knob")    { out = VIVID_DISPLAY_KNOB;    return true; }
    if (s == "slider")  { out = VIVID_DISPLAY_DEFAULT; return true; }
    if (s == "xy_pad")  { out = VIVID_DISPLAY_XY_PAD;  return true; }
    if (s == "color")   { out = VIVID_DISPLAY_COLOR;   return true; }
    if (s == "hidden")  { out = VIVID_DISPLAY_HIDDEN;  return true; }
    return false;
}

VividDisplayHint default_display(ShaderParamType t) {
    switch (t) {
        case ShaderParamType::Color:  return VIVID_DISPLAY_COLOR;
        case ShaderParamType::Point2: return VIVID_DISPLAY_XY_PAD;
        default:                      return VIVID_DISPLAY_DEFAULT;
    }
}

VividParamType host_type(ShaderParamType t) {
    switch (t) {
        case ShaderParamType::Int:  return VIVID_PARAM_INT;
        case ShaderParamType::Bool: return VIVID_PARAM_BOOL;
        default:                    return VIVID_PARAM_FLOAT;   // Color/Point2 are float channels
    }
}

const char* kComponentSuffix[3][3] = {
    {"", "", ""},                 // 1 component: no suffix
    {"_x", "_y", ""},             // Point2
    {"_r", "_g", "_b"},           // Color
};

const char* component_suffix(ShaderParamType t, int c) {
    if (t == ShaderParamType::Point2) return kComponentSuffix[1][c];
    if (t == ShaderParamType::Color)  return kComponentSuffix[2][c];
    return "";
}

// WGSL/GLSL type names + std140-compatible alignment for the field types we emit.
struct FieldType { const char* wgsl; const char* glsl; uint32_t align; uint32_t size; };

FieldType field_type(ShaderParamType t) {
    switch (t) {
        case ShaderParamType::Int:
        case ShaderParamType::Bool:   return {"i32", "int", 4, 4};
        case ShaderParamType::Point2: return {"vec2f", "vec2", 8, 8};
        case ShaderParamType::Color:  return {"vec3f", "vec3", 16, 12};
        default:                      return {"f32", "float", 4, 4};
    }
}

uint32_t round_up(uint32_t v, uint32_t a) { return (v + a - 1) / a * a; }

// One param's parse; appends its error (if any) to `error` and returns false.
bool parse_one_param(const json& jp, ShaderParam& p, std::string& error) {
    if (!jp.is_object()) { error = "each param must be an object"; return false; }

    auto name = jp.find("name");
    if (name == jp.end() || !name->is_string()) { error = "each param needs a \"name\" string"; return false; }
    p.name = name->get<std::string>();
    if (!is_identifier(p.name)) { error = "param name \"" + p.name + "\" is not a valid identifier"; return false; }
    if (is_reserved_identifier(p.name)) { error = "param name \"" + p.name + "\" is reserved"; return false; }

    if (auto it = jp.find("type"); it != jp.end()) {
        if (!it->is_string() || !parse_param_type(it->get<std::string>(), p.type)) {
            error = "param \"" + p.name + "\": unknown type (expected float, int, bool, color or point2)";
            return false;
        }
    }

    if (auto it = jp.find("choices"); it != jp.end()) {
        if (!it->is_array()) { error = "param \"" + p.name + "\": \"choices\" must be an array"; return false; }
        for (const auto& c : *it) {
            if (!c.is_string()) { error = "param \"" + p.name + "\": each choice must be a string"; return false; }
            p.choices.push_back(c.get<std::string>());
        }
        if (!p.choices.empty()) {
            p.type = ShaderParamType::Int;   // choices imply an enum
            p.min = 0.f;
            p.max = static_cast<float>(p.choices.size() - 1);
        }
    }

    const int comps = shader_param_components(p.type);

    if (auto it = jp.find("default"); it != jp.end()) {
        if (it->is_array()) {
            if (static_cast<int>(it->size()) != comps) {
                error = "param \"" + p.name + "\": \"default\" needs " + std::to_string(comps) + " components";
                return false;
            }
            for (int i = 0; i < comps; ++i) {
                if (!(*it)[i].is_number()) { error = "param \"" + p.name + "\": default components must be numbers"; return false; }
                p.def[i] = (*it)[i].get<float>();
            }
        } else if (it->is_number()) {
            for (int i = 0; i < comps; ++i) p.def[i] = it->get<float>();
        } else if (it->is_boolean()) {
            p.def[0] = it->get<bool>() ? 1.f : 0.f;
        } else {
            error = "param \"" + p.name + "\": \"default\" must be a number, bool or array";
            return false;
        }
    }

    if (p.type == ShaderParamType::Bool) { p.min = 0.f; p.max = 1.f; }
    if (auto it = jp.find("min"); it != jp.end() && p.choices.empty()) {
        if (!it->is_number()) { error = "param \"" + p.name + "\": \"min\" must be a number"; return false; }
        p.min = it->get<float>();
    }
    if (auto it = jp.find("max"); it != jp.end() && p.choices.empty()) {
        if (!it->is_number()) { error = "param \"" + p.name + "\": \"max\" must be a number"; return false; }
        p.max = it->get<float>();
    }
    if (p.min > p.max) { error = "param \"" + p.name + "\": min is greater than max"; return false; }

    if (auto it = jp.find("label"); it != jp.end() && it->is_string()) p.label = it->get<std::string>();
    if (auto it = jp.find("description"); it != jp.end() && it->is_string()) p.description = it->get<std::string>();
    if (auto it = jp.find("group"); it != jp.end() && it->is_string()) p.group = it->get<std::string>();

    p.display = default_display(p.type);
    if (auto it = jp.find("display"); it != jp.end()) {
        if (!it->is_string() || !parse_display(it->get<std::string>(), p.display)) {
            error = "param \"" + p.name + "\": unknown display hint";
            return false;
        }
    }
    return true;
}

}  // namespace

int shader_param_components(ShaderParamType t) {
    switch (t) {
        case ShaderParamType::Color:  return 3;
        case ShaderParamType::Point2: return 2;
        default:                      return 1;
    }
}

// ---------------------------------------------------------------------------
// parse_shader
// ---------------------------------------------------------------------------

ShaderMeta parse_shader(const std::string& source, ShaderDialect dialect) {
    ShaderMeta m;
    m.dialect = dialect;
    m.body = source;

    size_t json_start = 0, json_end = 0, block_end = 0;
    if (!find_header_block(source, json_start, json_end, block_end)) {
        m.error = "no JSON header found (expected a /*{ ... }*/ comment at the top of the file)";
        return m;
    }

    // Body = everything after the header block, with the leading blank lines trimmed.
    size_t body_start = block_end;
    while (body_start < source.size() && (source[body_start] == '\n' || source[body_start] == '\r'))
        body_start++;
    m.body = source.substr(body_start);

    json root;
    try {
        root = json::parse(source.substr(json_start, json_end - json_start));
    } catch (const json::parse_error& e) {
        m.error = std::string("malformed JSON header: ") + e.what();
        return m;
    }
    if (!root.is_object()) { m.error = "the JSON header must be an object"; return m; }

    if (auto it = root.find("version"); it != root.end()) {
        if (!it->is_number_integer()) { m.error = "\"version\" must be an integer"; return m; }
        m.version = it->get<int>();
        if (m.version != kFormatVersion) {
            m.error = "unsupported shader format version " + std::to_string(m.version) +
                      " (this build understands version " + std::to_string(kFormatVersion) + ")";
            return m;
        }
    }

    // Reserved now so that v1 files stay valid when multi-pass lands (ADR-0016).
    for (const char* reserved : {"passes", "buffers"}) {
        if (root.contains(reserved)) {
            m.error = std::string("\"") + reserved + "\" is reserved for a future version; "
                      "a v1 shader is a single fullscreen pass";
            return m;
        }
    }

    auto name = root.find("name");
    if (name == root.end() || !name->is_string() || name->get<std::string>().empty()) {
        m.error = "missing required field \"name\"";
        return m;
    }
    m.name = name->get<std::string>();

    if (auto it = root.find("summary"); it != root.end() && it->is_string())
        m.summary = it->get<std::string>();

    if (auto it = root.find("keywords"); it != root.end()) {
        if (!it->is_array()) { m.error = "\"keywords\" must be an array of strings"; return m; }
        for (const auto& k : *it)
            if (k.is_string()) m.keywords.push_back(k.get<std::string>());
    }

    if (auto it = root.find("inputs"); it != root.end()) {
        if (!it->is_array()) { m.error = "\"inputs\" must be an array of port names"; return m; }
        if (it->size() > kMaxInputs) {
            m.error = "a shader file takes at most " + std::to_string(kMaxInputs) +
                      " texture inputs (an operator needing more stays compiled C++)";
            return m;
        }
        std::set<std::string> seen;
        for (const auto& in : *it) {
            if (!in.is_string()) { m.error = "each input must be a port-name string"; return m; }
            std::string n = in.get<std::string>();
            if (!is_identifier(n))        { m.error = "input name \"" + n + "\" is not a valid identifier"; return m; }
            if (is_reserved_identifier(n)) { m.error = "input name \"" + n + "\" is reserved"; return m; }
            if (!seen.insert(n).second)   { m.error = "duplicate input name \"" + n + "\""; return m; }
            m.inputs.push_back(std::move(n));
        }
    }

    if (auto it = root.find("params"); it != root.end()) {
        if (!it->is_array()) { m.error = "\"params\" must be an array"; return m; }
        std::set<std::string> seen;
        for (const auto& jp : *it) {
            ShaderParam p;
            std::string err;
            if (!parse_one_param(jp, p, err)) { m.error = err; return m; }
            if (std::find(m.inputs.begin(), m.inputs.end(), p.name) != m.inputs.end()) {
                m.error = "param \"" + p.name + "\" collides with an input port of the same name";
                return m;
            }
            if (!seen.insert(p.name).second) { m.error = "duplicate param name \"" + p.name + "\""; return m; }
            m.params.push_back(std::move(p));
        }
    }

    return m;
}

// ---------------------------------------------------------------------------
// host_params
// ---------------------------------------------------------------------------

std::vector<ShaderHostParam> host_params(const ShaderMeta& meta) {
    std::vector<ShaderHostParam> out;
    for (size_t i = 0; i < meta.params.size(); ++i) {
        const ShaderParam& p = meta.params[i];
        const int comps = shader_param_components(p.type);
        for (int c = 0; c < comps; ++c) {
            ShaderHostParam h;
            h.name = p.name + component_suffix(p.type, c);
            h.label = p.label.empty() ? h.name : (comps == 1 ? p.label
                                                             : p.label + component_suffix(p.type, c));
            h.description = c == 0 ? p.description : std::string();
            h.group = p.group;
            h.type = host_type(p.type);
            h.def = p.def[c];
            h.min = p.min;
            h.max = p.max;
            // A compound widget (color, xy pad) carries its hint on the FIRST param of the
            // group and claims the rest (ui/compound_widget.h); the channels themselves draw
            // as ordinary sliders, so they must NOT repeat the hint.
            h.display = c == 0 ? p.display : VIVID_DISPLAY_DEFAULT;
            if (c == 0) h.choices = p.choices;
            h.param_index = i;
            h.component = c;
            out.push_back(std::move(h));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// uniform_layout — WGSL uniform address-space rules (== GLSL std140 for these types)
// ---------------------------------------------------------------------------

UniformLayout uniform_layout(const ShaderMeta& meta) {
    UniformLayout l;
    l.res_offset = 0;    // vec2f
    l.time_offset = 8;   // f32
    uint32_t cursor = 12;
    uint32_t max_align = 8;   // vec2f res

    l.entries.reserve(meta.params.size());
    for (const ShaderParam& p : meta.params) {
        const FieldType ft = field_type(p.type);
        const uint32_t off = round_up(cursor, ft.align);
        UniformLayout::Entry e;
        e.offset = off;
        e.components = shader_param_components(p.type);
        e.is_int = (p.type == ShaderParamType::Int || p.type == ShaderParamType::Bool);
        l.entries.push_back(e);
        cursor = off + ft.size;
        max_align = std::max(max_align, ft.align);
    }

    // A uniform struct is 16-byte aligned, so its size rounds up to a multiple of 16.
    l.size = std::max<uint32_t>(16, round_up(cursor, std::max<uint32_t>(16, max_align)));
    return l;
}

// ---------------------------------------------------------------------------
// generate_prelude
// ---------------------------------------------------------------------------

static std::string wgsl_prelude(const ShaderMeta& m) {
    std::string s = "// generated by vivid from the shader header — do not hand-edit\n";
    s += "struct U {\n    res: vec2f,\n    time: f32,\n";
    for (const ShaderParam& p : m.params)
        s += "    " + p.name + ": " + field_type(p.type).wgsl + ",\n";
    s += "};\n@group(0) @binding(0) var<uniform> u: U;\n";

    uint32_t binding = 1;
    for (const std::string& in : m.inputs)
        s += "@group(0) @binding(" + std::to_string(binding++) + ") var " + in +
             ": texture_2d<f32>;\n";
    if (!m.inputs.empty())
        s += "@group(0) @binding(" + std::to_string(binding) + ") var samp: sampler;\n";

    // The FullscreenOutput struct and fullscreenTriangle() come from gpu_common.h's
    // FULLSCREEN_VERTEX_WGSL, which create_shader() prepends at compile time.
    s += "@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput {\n"
         "    return fullscreenTriangle(vi, false);\n}\n";
    return s;
}

static std::string glsl_prelude(const ShaderMeta& m) {
    std::string s = "#version 450\n"
                    "// generated by vivid from the shader header — do not hand-edit\n"
                    "layout(location = 0) in vec2 v_uv;\n"
                    "layout(location = 0) out vec4 o_color;\n"
                    "layout(std140, set = 0, binding = 0) uniform U {\n"
                    "    vec2 res;\n    float time;\n";
    for (const ShaderParam& p : m.params)
        s += "    " + std::string(field_type(p.type).glsl) + " " + p.name + ";\n";
    s += "} u;\n";

    uint32_t binding = 1;
    for (const std::string& in : m.inputs)
        s += "layout(set = 0, binding = " + std::to_string(binding++) + ") uniform texture2D " +
             in + ";\n";
    if (!m.inputs.empty())
        s += "layout(set = 0, binding = " + std::to_string(binding) + ") uniform sampler samp;\n";
    return s;
}

std::string generate_prelude(const ShaderMeta& meta) {
    return meta.dialect == ShaderDialect::Glsl ? glsl_prelude(meta) : wgsl_prelude(meta);
}

// ---------------------------------------------------------------------------
// pack_uniforms
// ---------------------------------------------------------------------------

void pack_uniforms(const ShaderMeta& meta, const UniformLayout& layout,
                   const float* values, size_t value_count,
                   float res_w, float res_h, float time,
                   void* out, size_t out_size) {
    if (!out || out_size < layout.size) return;
    auto* bytes = static_cast<uint8_t*>(out);
    std::memset(bytes, 0, out_size);

    const float res[2] = {res_w, res_h};
    std::memcpy(bytes + layout.res_offset, res, sizeof(res));
    std::memcpy(bytes + layout.time_offset, &time, sizeof(float));

    // `values` is one float per host param, in host_params() order — i.e. the order the
    // operator ABI hands us param_values. Walk the declared params, consuming components.
    size_t v = 0;
    for (size_t i = 0; i < meta.params.size() && i < layout.entries.size(); ++i) {
        const UniformLayout::Entry& e = layout.entries[i];
        for (int c = 0; c < e.components; ++c, ++v) {
            const float val = (values && v < value_count) ? values[v] : meta.params[i].def[c];
            const uint32_t off = e.offset + static_cast<uint32_t>(c) * 4u;
            if (off + 4u > out_size) return;
            if (e.is_int) {
                const int32_t iv = static_cast<int32_t>(val);
                std::memcpy(bytes + off, &iv, sizeof(iv));
            } else {
                std::memcpy(bytes + off, &val, sizeof(val));
            }
        }
    }
}

}  // namespace vivid
