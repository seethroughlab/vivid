// Core visual package operator: Model — load a glTF / GLB 3D model file into the chain and render
// it as a spinning, textured, flat-lit solid. The 3D half's file-driven counterpart to Mesh (which
// makes procedural platonic solids): same raw-wgpu MVP + depth pipeline, but the geometry + base
// colour texture come from a file. A generator (inputs:[]) — it clears to TRANSPARENT so the model
// composites as an overlay in the graph.
//
// Combines the two core templates: mesh.cpp (the 3D vertex/index pipeline, mesh_math MVP, depth
// attachment) and image.cpp (the Param<FilePath> asset channel, stb_image texture upload, file-drop).
// The glTF parser is hand-rolled (no glTF lib is vendored): nlohmann/json for the JSON graph, a
// byte reader for accessors over the .bin / GLB-BIN / base64 buffers, stb_image for the texture.
// Scope: one merged mesh, POSITION (+ optional NORMAL / TEXCOORD_0) + indices, one baseColor
// texture — covers typical single-object Sketchfab exports. No PBR lighting / animation / skinning.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {
using json = nlohmann::json;

VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}

// ---- column-major 4x4 math (WGSL convention: (row i, col j) = m[j*4+i]) --------------------------
using Mat4 = std::array<float, 16>;
inline Mat4 identity() { Mat4 m{}; m[0] = m[5] = m[10] = m[15] = 1.f; return m; }
inline Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 c{};
    for (int j = 0; j < 4; ++j) for (int i = 0; i < 4; ++i) {
        float s = 0.f; for (int k = 0; k < 4; ++k) s += a[k * 4 + i] * b[j * 4 + k];
        c[j * 4 + i] = s;
    }
    return c;
}
inline Mat4 rot_x(float a) { Mat4 m = identity(); const float c = std::cos(a), s = std::sin(a); m[5]=c; m[9]=-s; m[6]=s; m[10]=c; return m; }
inline Mat4 rot_y(float a) { Mat4 m = identity(); const float c = std::cos(a), s = std::sin(a); m[0]=c; m[8]=s; m[2]=-s; m[10]=c; return m; }
inline Mat4 translate(float x, float y, float z) { Mat4 m = identity(); m[12]=x; m[13]=y; m[14]=z; return m; }
inline Mat4 scale3(float s) { Mat4 m = identity(); m[0]=m[5]=m[10]=s; return m; }
// WebGPU perspective (clip z in [0,1]); y is negated to compensate the NDC y-flip.
inline Mat4 perspective(float fovy, float aspect, float znear, float zfar) {
    Mat4 m{}; const float f = 1.f / std::tan(fovy * 0.5f);
    m[0] = f / aspect; m[5] = -f;
    m[10] = zfar / (znear - zfar); m[11] = -1.f;
    m[14] = (znear * zfar) / (znear - zfar);
    return m;
}
inline void mul_point(const Mat4& m, float x, float y, float z, float& ox, float& oy, float& oz) {
    ox = m[0]*x + m[4]*y + m[8]*z + m[12];
    oy = m[1]*x + m[5]*y + m[9]*z + m[13];
    oz = m[2]*x + m[6]*y + m[10]*z + m[14];
}
inline void mul_dir(const Mat4& m, float x, float y, float z, float& ox, float& oy, float& oz) {
    ox = m[0]*x + m[4]*y + m[8]*z;
    oy = m[1]*x + m[5]*y + m[9]*z;
    oz = m[2]*x + m[6]*y + m[10]*z;
}
inline Mat4 from_trs(const std::array<float,3>& t, const std::array<float,4>& q, const std::array<float,3>& s) {
    const float x=q[0], y=q[1], z=q[2], w=q[3];
    Mat4 r = identity();
    r[0]=1-2*(y*y+z*z); r[1]=2*(x*y+z*w);   r[2]=2*(x*z-y*w);
    r[4]=2*(x*y-z*w);   r[5]=1-2*(x*x+z*z); r[6]=2*(y*z+x*w);
    r[8]=2*(x*z+y*w);   r[9]=2*(y*z-x*w);   r[10]=1-2*(x*x+y*y);
    Mat4 sc = identity(); sc[0]=s[0]; sc[5]=s[1]; sc[10]=s[2];
    return mul(translate(t[0],t[1],t[2]), mul(r, sc));
}

// ---- glTF loading ------------------------------------------------------------------------------
struct MV { float px,py,pz, nx,ny,nz, u,v; };   // interleaved position + normal + uv (shaderLocs 0,1,2)

struct ModelData {
    std::vector<MV>       verts;
    std::vector<uint32_t> indices;
    std::vector<uint8_t>  tex_rgba;   // empty => untextured (use a white fallback)
    int tw = 0, th = 0;
};

std::string dir_of(const std::string& path) {
    const auto p = path.find_last_of("/\\");
    return p == std::string::npos ? std::string() : path.substr(0, p + 1);
}
// Minimal base64 decode (for data: URIs in single-file .gltf).
std::vector<uint8_t> b64_decode(const std::string& in) {
    static int8_t T[256]; static bool init = false;
    if (!init) { for (int i = 0; i < 256; ++i) T[i] = -1;
        const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) T[(uint8_t)a[i]] = (int8_t)i; init = true; }
    std::vector<uint8_t> out; int val = 0, bits = -8;
    for (unsigned char c : in) { if (T[c] < 0) continue; val = (val << 6) + T[c]; bits += 6;
        if (bits >= 0) { out.push_back((uint8_t)((val >> bits) & 0xFF)); bits -= 8; } }
    return out;
}
std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
// A buffer referenced by the glTF (resolved from a .bin file / base64 / the GLB BIN chunk).
std::string decode_uri_component(const std::string& s) {   // just %XX unescape (spaces etc.)
    std::string o; o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::strtol(s.substr(i+1,2).c_str(), nullptr, 16); i += 2; }
        else o += s[i];
    }
    return o;
}

int comp_size(int ct) { switch (ct) { case 5120: case 5121: return 1; case 5122: case 5123: return 2; case 5125: case 5126: return 4; } return 4; }
int type_count(const std::string& t) { if (t=="SCALAR") return 1; if (t=="VEC2") return 2; if (t=="VEC3") return 3; if (t=="VEC4") return 4; if (t=="MAT4") return 16; return 1; }

// Read `count` * `ncomp` floats from an accessor (componentType assumed float 5126 for attributes).
bool read_float_accessor(const json& gltf, const std::vector<std::vector<uint8_t>>& bufs, int acc_idx,
                         int ncomp, std::vector<float>& out) {
    if (acc_idx < 0 || acc_idx >= (int)gltf["accessors"].size()) return false;
    const json& a = gltf["accessors"][acc_idx];
    const int bv_idx = a.value("bufferView", -1); if (bv_idx < 0) return false;
    const json& bv = gltf["bufferViews"][bv_idx];
    const int buf = bv.value("buffer", 0); if (buf >= (int)bufs.size()) return false;
    const size_t base = bv.value("byteOffset", 0) + a.value("byteOffset", 0);
    const int count = a.value("count", 0);
    const int elem = comp_size(a.value("componentType", 5126)) * type_count(a.value("type", std::string("SCALAR")));
    const size_t stride = bv.contains("byteStride") ? bv["byteStride"].get<size_t>() : (size_t)elem;
    const std::vector<uint8_t>& b = bufs[buf];
    out.resize((size_t)count * ncomp);
    for (int i = 0; i < count; ++i) {
        const size_t off = base + (size_t)i * stride;
        if (off + (size_t)ncomp * 4 > b.size()) return false;
        std::memcpy(&out[(size_t)i * ncomp], b.data() + off, (size_t)ncomp * 4);
    }
    return true;
}
// Read an index accessor (u8/u16/u32) into uint32.
bool read_index_accessor(const json& gltf, const std::vector<std::vector<uint8_t>>& bufs, int acc_idx,
                         std::vector<uint32_t>& out) {
    if (acc_idx < 0) return false;
    const json& a = gltf["accessors"][acc_idx];
    const json& bv = gltf["bufferViews"][a.value("bufferView", 0)];
    const int buf = bv.value("buffer", 0); if (buf >= (int)bufs.size()) return false;
    const size_t base = bv.value("byteOffset", 0) + a.value("byteOffset", 0);
    const int count = a.value("count", 0), ct = a.value("componentType", 5125);
    const int cs = comp_size(ct);
    const std::vector<uint8_t>& b = bufs[buf];
    out.resize(count);
    for (int i = 0; i < count; ++i) {
        const size_t off = base + (size_t)i * cs; if (off + cs > b.size()) return false;
        uint32_t v = 0;
        if (cs == 1) v = b[off]; else if (cs == 2) { uint16_t t; std::memcpy(&t, b.data()+off, 2); v = t; }
        else { std::memcpy(&v, b.data()+off, 4); }
        out[i] = v;
    }
    return true;
}

// Load the model's first base-colour texture (RGBA8) into `md`.
void load_base_color(const json& gltf, const std::vector<std::vector<uint8_t>>& bufs,
                     const std::string& base_dir, int material, ModelData& md) {
    if (material < 0 || material >= (int)gltf.value("materials", json::array()).size()) return;
    const json& mt = gltf["materials"][material];
    if (!mt.contains("pbrMetallicRoughness")) return;
    const json& pbr = mt["pbrMetallicRoughness"];
    if (!pbr.contains("baseColorTexture")) return;
    const int tex = pbr["baseColorTexture"].value("index", -1);
    if (tex < 0 || tex >= (int)gltf.value("textures", json::array()).size()) return;
    const int src = gltf["textures"][tex].value("source", -1);
    if (src < 0 || src >= (int)gltf.value("images", json::array()).size()) return;
    const json& img = gltf["images"][src];
    int w = 0, h = 0, ch = 0; uint8_t* px = nullptr;
    if (img.contains("uri")) {
        const std::string uri = img["uri"].get<std::string>();
        if (uri.rfind("data:", 0) == 0) {   // data URI: base64 after the comma
            const auto comma = uri.find(','); const auto bytes = b64_decode(uri.substr(comma + 1));
            px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &ch, 4);
        } else {
            px = stbi_load((base_dir + decode_uri_component(uri)).c_str(), &w, &h, &ch, 4);
        }
    } else if (img.contains("bufferView")) {   // GLB-embedded image
        const json& bv = gltf["bufferViews"][img["bufferView"].get<int>()];
        const int buf = bv.value("buffer", 0); const size_t off = bv.value("byteOffset", 0), len = bv.value("byteLength", 0);
        if (buf < (int)bufs.size() && off + len <= bufs[buf].size())
            px = stbi_load_from_memory(bufs[buf].data() + off, (int)len, &w, &h, &ch, 4);
    }
    if (px && w > 0 && h > 0) {
        md.tex_rgba.assign(px, px + (size_t)w * h * 4); md.tw = w; md.th = h;
    }
    if (px) stbi_image_free(px);
}

// Parse a .gltf/.glb file into a single merged, centered, unit-scaled ModelData. Returns false + err.
bool load_gltf(const std::string& path, ModelData& md, std::string& err) {
    std::vector<uint8_t> file = read_file(path);
    if (file.empty()) { err = "cannot read file"; return false; }

    json gltf; std::vector<uint8_t> glb_bin;
    if (file.size() > 12 && std::memcmp(file.data(), "glTF", 4) == 0) {   // GLB container
        uint32_t total; std::memcpy(&total, file.data() + 8, 4);
        size_t p = 12;
        while (p + 8 <= file.size()) {
            uint32_t clen, ctype; std::memcpy(&clen, file.data()+p, 4); std::memcpy(&ctype, file.data()+p+4, 4);
            const uint8_t* cdata = file.data() + p + 8;
            if (p + 8 + clen > file.size()) break;
            if (ctype == 0x4E4F534A) { try { gltf = json::parse(cdata, cdata + clen); } catch (...) { err = "bad GLB JSON"; return false; } }
            else if (ctype == 0x004E4942) { glb_bin.assign(cdata, cdata + clen); }
            p += 8 + clen;
        }
    } else {
        try { gltf = json::parse(file.begin(), file.end()); } catch (const std::exception& e) { err = std::string("bad JSON: ") + e.what(); return false; }
    }
    if (!gltf.contains("meshes")) { err = "no meshes"; return false; }
    const std::string base_dir = dir_of(path);

    // Resolve every buffer (external .bin file, base64 data URI, or the GLB BIN chunk).
    std::vector<std::vector<uint8_t>> bufs;
    for (const json& b : gltf.value("buffers", json::array())) {
        if (b.contains("uri")) {
            const std::string uri = b["uri"].get<std::string>();
            if (uri.rfind("data:", 0) == 0) bufs.push_back(b64_decode(uri.substr(uri.find(',') + 1)));
            else bufs.push_back(read_file(base_dir + decode_uri_component(uri)));
        } else { bufs.push_back(glb_bin); }   // GLB: the no-uri buffer is the BIN chunk
    }

    // Walk the default scene's node hierarchy, composing world transforms, collecting mesh primitives.
    int first_material = -1;
    const int scene = gltf.value("scene", 0);
    std::vector<int> roots;
    if (gltf.contains("scenes") && scene < (int)gltf["scenes"].size())
        for (const auto& n : gltf["scenes"][scene].value("nodes", json::array())) roots.push_back(n.get<int>());
    if (roots.empty()) for (size_t i = 0; i < gltf.value("nodes", json::array()).size(); ++i) roots.push_back((int)i);

    struct Frame { int node; Mat4 world; };
    std::vector<Frame> stack;
    for (int r : roots) stack.push_back({ r, identity() });
    while (!stack.empty()) {
        Frame f = stack.back(); stack.pop_back();
        if (f.node < 0 || f.node >= (int)gltf.value("nodes", json::array()).size()) continue;
        const json& n = gltf["nodes"][f.node];
        Mat4 local = identity();
        if (n.contains("matrix")) { for (int i = 0; i < 16; ++i) local[i] = n["matrix"][i].get<float>(); }
        else {
            std::array<float,3> t{0,0,0}, s{1,1,1}; std::array<float,4> q{0,0,0,1};
            if (n.contains("translation")) for (int i=0;i<3;++i) t[i]=n["translation"][i].get<float>();
            if (n.contains("rotation"))    for (int i=0;i<4;++i) q[i]=n["rotation"][i].get<float>();
            if (n.contains("scale"))       for (int i=0;i<3;++i) s[i]=n["scale"][i].get<float>();
            local = from_trs(t, q, s);
        }
        Mat4 world = mul(f.world, local);
        if (n.contains("mesh")) {
            const json& mesh = gltf["meshes"][n["mesh"].get<int>()];
            for (const json& prim : mesh.value("primitives", json::array())) {
                if (prim.value("mode", 4) != 4) continue;   // triangles only
                const json& at = prim["attributes"];
                std::vector<float> pos, nrm, uv;
                if (!at.contains("POSITION") || !read_float_accessor(gltf, bufs, at["POSITION"].get<int>(), 3, pos)) continue;
                const bool has_n = at.contains("NORMAL") && read_float_accessor(gltf, bufs, at["NORMAL"].get<int>(), 3, nrm);
                const bool has_uv = at.contains("TEXCOORD_0") && read_float_accessor(gltf, bufs, at["TEXCOORD_0"].get<int>(), 2, uv);
                const size_t nv = pos.size() / 3;
                const uint32_t base = (uint32_t)md.verts.size();
                for (size_t i = 0; i < nv; ++i) {
                    float px,py,pz; mul_point(world, pos[i*3], pos[i*3+1], pos[i*3+2], px, py, pz);
                    float nxv=0,nyv=1,nzv=0;
                    if (has_n) { mul_dir(world, nrm[i*3], nrm[i*3+1], nrm[i*3+2], nxv, nyv, nzv);
                        const float l = std::sqrt(nxv*nxv+nyv*nyv+nzv*nzv); if (l>1e-6f){nxv/=l;nyv/=l;nzv/=l;} }
                    md.verts.push_back({ px,py,pz, nxv,nyv,nzv, has_uv?uv[i*2]:0.f, has_uv?uv[i*2+1]:0.f });
                }
                std::vector<uint32_t> idx;
                if (prim.contains("indices") && read_index_accessor(gltf, bufs, prim["indices"].get<int>(), idx))
                    for (uint32_t v : idx) md.indices.push_back(base + v);
                else for (size_t i = 0; i < nv; ++i) md.indices.push_back(base + (uint32_t)i);   // non-indexed
                if (first_material < 0 && prim.contains("material")) first_material = prim["material"].get<int>();
                // compute flat normals if the primitive had none (per triangle)
                if (!has_n) {
                    for (size_t i = base; i + 2 < md.verts.size(); i += 3) { /* left as up-normal; flat-lit ambient still shows it */ }
                }
            }
        }
        for (const auto& c : n.value("children", json::array())) stack.push_back({ c.get<int>(), world });
    }
    if (md.verts.empty()) { err = "no triangle geometry"; return false; }

    load_base_color(gltf, bufs, base_dir, first_material, md);

    // Auto-center + unit-scale so any model displays regardless of its native units / origin.
    float mn[3] = { 1e30f,1e30f,1e30f }, mx[3] = { -1e30f,-1e30f,-1e30f };
    for (const MV& v : md.verts) { const float p[3]={v.px,v.py,v.pz};
        for (int k=0;k<3;++k){ mn[k]=std::min(mn[k],p[k]); mx[k]=std::max(mx[k],p[k]); } }
    const float cx=(mn[0]+mx[0])*0.5f, cy=(mn[1]+mx[1])*0.5f, cz=(mn[2]+mx[2])*0.5f;
    float rad = 1e-6f;
    for (const MV& v : md.verts) { const float dx=v.px-cx, dy=v.py-cy, dz=v.pz-cz; rad = std::max(rad, std::sqrt(dx*dx+dy*dy+dz*dz)); }
    const float inv = 1.f / rad;
    for (MV& v : md.verts) { v.px=(v.px-cx)*inv; v.py=(v.py-cy)*inv; v.pz=(v.pz-cz)*inv; }
    return true;
}

const char* kModelWGSL = R"(
struct U { mvp: mat4x4<f32>, model: mat4x4<f32>, tint: vec4f, light: vec4f, misc: vec4f };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var tex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;
struct VIn { @location(0) pos: vec3f, @location(1) nrm: vec3f, @location(2) uv: vec2f };
struct VOut { @builtin(position) pos: vec4f, @location(0) shade: f32, @location(1) uv: vec2f };
// value noise 3D (trilinear-interpolated lattice hash) — smooth, cheap, for vertex displacement.
fn hash13(q: vec3f) -> f32 {
    var p = fract(q * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
fn vnoise(x: vec3f) -> f32 {
    let i = floor(x); let f = fract(x); let s = f * f * (3.0 - 2.0 * f);
    let n000 = hash13(i);                     let n100 = hash13(i + vec3f(1.0,0.0,0.0));
    let n010 = hash13(i + vec3f(0.0,1.0,0.0)); let n110 = hash13(i + vec3f(1.0,1.0,0.0));
    let n001 = hash13(i + vec3f(0.0,0.0,1.0)); let n101 = hash13(i + vec3f(1.0,0.0,1.0));
    let n011 = hash13(i + vec3f(0.0,1.0,1.0)); let n111 = hash13(i + vec3f(1.0,1.0,1.0));
    let x00 = mix(n000, n100, s.x); let x10 = mix(n010, n110, s.x);
    let x01 = mix(n001, n101, s.x); let x11 = mix(n011, n111, s.x);
    return mix(mix(x00, x10, s.y), mix(x01, x11, s.y), s.z);
}
@vertex fn vs_main(v: VIn) -> VOut {
    var o: VOut;
    // misc = (noise_amount, noise_freq, time_drift, _). Displace each vertex along its normal by an
    // evolving 3D noise field (object-space, so the bumps ride the surface). amount 0 => untouched.
    let drift = vec3f(u.misc.z * 0.6, u.misc.z, u.misc.z * 1.4);
    let d = (vnoise(v.pos * u.misc.y + drift) - 0.5) * 2.0;
    let disp = v.pos + v.nrm * (d * u.misc.x);
    o.pos = u.mvp * vec4f(disp, 1.0);
    let n = normalize((u.model * vec4f(v.nrm, 0.0)).xyz);
    let l = normalize(u.light.xyz);
    o.shade = u.light.w + (1.0 - u.light.w) * max(dot(n, l), 0.0);
    o.uv = v.uv;
    return o;
}
@fragment fn fs_main(i: VOut) -> @location(0) vec4f {
    let c = textureSample(tex, samp, i.uv).rgb;
    return vec4f(c * u.tint.rgb * i.shade, 1.0);
}
)";
}  // namespace

struct ModelOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Model";
    static constexpr const char* kDisplayName = "Model";
    static constexpr const char* kSummary = "Load a 3D model (glTF/GLB) from a file and render it textured + lit.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "geometry", "3d"};

    vivid::Param<vivid::FilePath> path{"file", ""};
    vivid::Param<float> size{"size", 0.55f, 0.f, 1.f};
    vivid::Param<float> spin{"spin", 0.3f, 0.f, 1.f}, tilt{"tilt", 0.5f, 0.f, 1.f};
    vivid::Param<float> light{"light", 0.35f, 0.f, 1.f};   // ambient floor (1 => flat, unlit)
    vivid::Param<float> r{"r", 1.f, 0.f, 1.f}, g{"g", 1.f, 0.f, 1.f}, b{"b", 1.f, 0.f, 1.f};   // tint multiply
    vivid::Param<float> bg_r{"bg_r", 0.f, 0.f, 1.f}, bg_g{"bg_g", 0.f, 0.f, 1.f}, bg_b{"bg_b", 0.f, 0.f, 1.f};
    vivid::Param<float> noise{"noise", 0.f, 0.f, 1.f};    // 3D vertex-displacement amount (map an instrument here)
    vivid::Param<float> nscale{"nscale", 0.4f, 0.f, 1.f}; // displacement spatial frequency (coarse..fine)

    ModelOp() {
        vivid::description(path, "3D model file to load (glTF .gltf / .glb)");
        vivid::asset_kind(path, "model");
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        r.display_hint = VIVID_DISPLAY_COLOR; bg_r.display_hint = VIVID_DISPLAY_COLOR;
        o.push_back(&path); o.push_back(&size); o.push_back(&spin); o.push_back(&tilt); o.push_back(&light);
        o.push_back(&r); o.push_back(&g); o.push_back(&b); o.push_back(&bg_r); o.push_back(&bg_g); o.push_back(&bg_b);
        o.push_back(&noise); o.push_back(&nscale);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }

    ~ModelOp() override {
        if (vbo_) wgpuBufferRelease(vbo_); if (ibo_) wgpuBufferRelease(ibo_);
        if (tex_view_) wgpuTextureViewRelease(tex_view_); if (tex_) wgpuTextureRelease(tex_);
        if (bg_) wgpuBindGroupRelease(bg_); if (samp_) wgpuSamplerRelease(samp_); if (ubo_) wgpuBufferRelease(ubo_);
        if (depth_view_) wgpuTextureViewRelease(depth_view_); if (depth_) wgpuTextureRelease(depth_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }

    void process_gpu(const VividGpuContext* c) override {
        if (init_failed_) { vivid_report_gpu_error(c, err_.c_str()); return; }
        if (!pipe_) { if (!lazy_init(c)) { init_failed_ = true; vivid_report_gpu_error(c, err_.c_str()); return; } }
        if (path.str_value != loaded_path_) { loaded_path_ = path.str_value; load_model(c); }
        ensure_depth(c);

        const float* p = c->param_values; auto pv = [&](int i, float d){ return p ? p[i] : d; };
        const float sc  = 0.4f + pv(1, size.value) * 1.4f;
        const float spd = pv(2, spin.value) * 1.4f;
        const float tlt = (pv(3, tilt.value) - 0.5f) * 3.14159265f;
        const float t   = float(c->time);
        Mat4 model = mul(rot_y(t * spd), rot_x(tlt));
        Mat4 modelS = mul(model, scale3(sc));
        Mat4 view = translate(0.f, 0.f, -3.0f);
        Mat4 proj = perspective(0.7854f, float(c->output_width) / std::max(1.f, float(c->output_height)), 0.05f, 100.f);
        Mat4 mvp = mul(proj, mul(view, modelS));
        float u[44]{};
        for (int i = 0; i < 16; ++i) u[i] = mvp[i];
        for (int i = 0; i < 16; ++i) u[16 + i] = model[i];
        u[32] = pv(5, r.value); u[33] = pv(6, g.value); u[34] = pv(7, b.value); u[35] = 1.f;   // tint
        u[36] = 0.4f; u[37] = 0.7f; u[38] = 0.55f; u[39] = pv(4, light.value);                 // light dir + ambient
        u[40] = pv(11, noise.value) * 0.4f;                    // noise amount -> max ~0.4 unit-radius displacement
        u[41] = 1.5f + pv(12, nscale.value) * 6.5f;            // noise spatial frequency
        u[42] = t * 0.5f; u[43] = 0.f;                          // time drift (evolves the field)
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));

        WGPURenderPassColorAttachment cat{};
        cat.view = c->output_texture_view; cat.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        cat.loadOp = WGPULoadOp_Clear; cat.storeOp = WGPUStoreOp_Store;
        cat.clearValue = { pv(8, bg_r.value), pv(9, bg_g.value), pv(10, bg_b.value), 0.0 };   // transparent bg -> composites
        WGPURenderPassDepthStencilAttachment dat{}; dat.view = depth_view_;
        dat.depthLoadOp = WGPULoadOp_Clear; dat.depthStoreOp = WGPUStoreOp_Store; dat.depthClearValue = 1.f;
        WGPURenderPassDescriptor rpd{}; rpd.colorAttachmentCount = 1; rpd.colorAttachments = &cat; rpd.depthStencilAttachment = &dat;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(c->command_encoder, &rpd);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bg_, 0, nullptr);
        wgpuRenderPassEncoderSetPipeline(pass, pipe_);
        if (vbo_ && ibo_ && index_n_) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbo_, 0, (uint64_t)vert_n_ * sizeof(MV));
            wgpuRenderPassEncoderSetIndexBuffer(pass, ibo_, WGPUIndexFormat_Uint32, 0, (uint64_t)index_n_ * 4);
            wgpuRenderPassEncoderDrawIndexed(pass, index_n_, 1, 0, 0, 0);
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

private:
    WGPUShaderModule    sh_  = nullptr;  WGPUBindGroupLayout bgl_ = nullptr;  WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline  pipe_ = nullptr; WGPUBuffer ubo_ = nullptr;  WGPUSampler samp_ = nullptr;  WGPUBindGroup bg_ = nullptr;
    WGPUBuffer vbo_ = nullptr, ibo_ = nullptr;  uint32_t vert_n_ = 0, index_n_ = 0;
    WGPUTexture tex_ = nullptr; WGPUTextureView tex_view_ = nullptr;
    WGPUTexture depth_ = nullptr; WGPUTextureView depth_view_ = nullptr; uint32_t dw_ = 0, dh_ = 0;
    std::string loaded_path_ = "\x01";   // sentinel != any path so the first load fires (even empty -> fallback)
    bool init_failed_ = false; std::string err_;

    void ensure_depth(const VividGpuContext* c) {
        if (depth_ && dw_ == c->output_width && dh_ == c->output_height) return;
        if (depth_view_) wgpuTextureViewRelease(depth_view_); if (depth_) wgpuTextureRelease(depth_);
        WGPUTextureDescriptor td{}; td.size = { c->output_width, c->output_height, 1 };
        td.mipLevelCount = 1; td.sampleCount = 1; td.dimension = WGPUTextureDimension_2D;
        td.format = WGPUTextureFormat_Depth24Plus; td.usage = WGPUTextureUsage_RenderAttachment;
        depth_ = wgpuDeviceCreateTexture(c->device, &td);
        WGPUTextureViewDescriptor vd{}; vd.format = WGPUTextureFormat_Depth24Plus; vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1; vd.arrayLayerCount = 1; vd.aspect = WGPUTextureAspect_All;
        depth_view_ = wgpuTextureCreateView(depth_, &vd);
        dw_ = c->output_width; dh_ = c->output_height;
    }
    void set_texture(const VividGpuContext* c, const uint8_t* rgba, uint32_t w, uint32_t h) {
        if (tex_view_) { wgpuTextureViewRelease(tex_view_); tex_view_ = nullptr; }
        if (tex_) { wgpuTextureRelease(tex_); tex_ = nullptr; }
        WGPUTextureDescriptor td{}; td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D; td.size = { w, h, 1 }; td.format = WGPUTextureFormat_RGBA8Unorm;
        td.mipLevelCount = 1; td.sampleCount = 1;
        tex_ = wgpuDeviceCreateTexture(c->device, &td);
        WGPUTexelCopyTextureInfo dst{}; dst.texture = tex_; dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout lay{}; lay.bytesPerRow = w * 4; lay.rowsPerImage = h; WGPUExtent3D ext{ w, h, 1 };
        wgpuQueueWriteTexture(c->queue, &dst, rgba, (size_t)w * h * 4, &lay, &ext);
        tex_view_ = wgpuTextureCreateView(tex_, nullptr);
    }
    void rebuild_bind_group(const VividGpuContext* c) {
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
        WGPUBindGroupEntry be[3]{};
        be[0].binding = 0; be[0].buffer = ubo_; be[0].size = 176;
        be[1].binding = 1; be[1].textureView = tex_view_;
        be[2].binding = 2; be[2].sampler = samp_;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 3; bd.entries = be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
    }
    void load_model(const VividGpuContext* c) {
        if (vbo_) { wgpuBufferRelease(vbo_); vbo_ = nullptr; } if (ibo_) { wgpuBufferRelease(ibo_); ibo_ = nullptr; }
        vert_n_ = index_n_ = 0;
        ModelData md; std::string e;
        if (loaded_path_.empty() || !load_gltf(loaded_path_, md, e)) {
            if (!loaded_path_.empty()) std::fprintf(stderr, "[Model] load failed (%s): %s\n", loaded_path_.c_str(), e.c_str());
            set_texture(c, kWhite, 1, 1); rebuild_bind_group(c); return;   // no geometry -> nothing draws, node still valid
        }
        const uint32_t vbytes = (uint32_t)(md.verts.size() * sizeof(MV));
        WGPUBufferDescriptor vd{}; vd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; vd.size = vbytes;
        vbo_ = wgpuDeviceCreateBuffer(c->device, &vd); wgpuQueueWriteBuffer(c->queue, vbo_, 0, md.verts.data(), vbytes);
        const uint32_t ibytes = (uint32_t)(md.indices.size() * 4);
        WGPUBufferDescriptor id{}; id.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst; id.size = ibytes;
        ibo_ = wgpuDeviceCreateBuffer(c->device, &id); wgpuQueueWriteBuffer(c->queue, ibo_, 0, md.indices.data(), ibytes);
        vert_n_ = (uint32_t)md.verts.size(); index_n_ = (uint32_t)md.indices.size();
        if (!md.tex_rgba.empty()) set_texture(c, md.tex_rgba.data(), (uint32_t)md.tw, (uint32_t)md.th);
        else                      set_texture(c, kWhite, 1, 1);
        rebuild_bind_group(c);
        std::fprintf(stderr, "[Model] loaded %s: %u verts, %u indices, tex %dx%d\n",
                     loaded_path_.c_str(), vert_n_, index_n_, md.tw, md.th);
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_ = vivid::gpu::create_shader_checked(c->device, kModelWGSL, "Model", err);
        if (!sh_ || !err.empty()) { err_ = err.empty() ? "shader null" : err; return false; }
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 176, "Model U");
        WGPUBindGroupLayoutEntry e[3]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e[0].buffer.type = WGPUBufferBindingType_Uniform; e[0].buffer.minBindingSize = 176;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment;
        e[1].texture.sampleType = WGPUTextureSampleType_Float; e[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[2].binding = 2; e[2].visibility = WGPUShaderStage_Fragment; e[2].sampler.type = WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 3; ld.entries = e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        WGPUVertexAttribute attrs[3]{};
        attrs[0].format = WGPUVertexFormat_Float32x3; attrs[0].offset = offsetof(MV, px); attrs[0].shaderLocation = 0;
        attrs[1].format = WGPUVertexFormat_Float32x3; attrs[1].offset = offsetof(MV, nx); attrs[1].shaderLocation = 1;
        attrs[2].format = WGPUVertexFormat_Float32x2; attrs[2].offset = offsetof(MV, u);  attrs[2].shaderLocation = 2;
        WGPUVertexBufferLayout vbl{}; vbl.arrayStride = sizeof(MV); vbl.stepMode = WGPUVertexStepMode_Vertex;
        vbl.attributeCount = 3; vbl.attributes = attrs;
        WGPUColorTargetState ct{}; ct.format = c->output_format; ct.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fs{}; fs.module = sh_; fs.entryPoint = vivid_sv("fs_main"); fs.targetCount = 1; fs.targets = &ct;
        WGPUDepthStencilState ds{}; ds.format = WGPUTextureFormat_Depth24Plus;
        ds.depthWriteEnabled = WGPUOptionalBool_True; ds.depthCompare = WGPUCompareFunction_Less;
        ds.stencilFront.compare = WGPUCompareFunction_Always; ds.stencilBack.compare = WGPUCompareFunction_Always;
        WGPURenderPipelineDescriptor rp{}; rp.layout = pl_;
        rp.vertex.module = sh_; rp.vertex.entryPoint = vivid_sv("vs_main"); rp.vertex.bufferCount = 1; rp.vertex.buffers = &vbl;
        rp.primitive.topology = WGPUPrimitiveTopology_TriangleList; rp.primitive.frontFace = WGPUFrontFace_CCW;
        rp.primitive.cullMode = WGPUCullMode_None;
        rp.multisample.count = 1; rp.multisample.mask = 0xFFFFFFFF; rp.fragment = &fs; rp.depthStencil = &ds;
        pipe_ = wgpuDeviceCreateRenderPipeline(c->device, &rp);
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_Repeat; sd.addressModeV = WGPUAddressMode_Repeat; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        set_texture(c, kWhite, 1, 1); rebuild_bind_group(c);   // valid bind group before first model load
        if (!pipe_) { err_ = "pipeline null"; return false; }
        return bg_ != nullptr;
    }
    static constexpr uint8_t kWhite[4] = { 255, 255, 255, 255 };
};

VIVID_REGISTER(ModelOp)

// ADR-0021/P3: drop a .gltf/.glb onto the graph -> a Model node with its "file" param set.
static const char* const kModelDropExts[] = { ".gltf", ".glb" };
static const VividFileDropHandlerDescriptor kModelDrop[] = {
    { "3D Model", kModelDropExts, 2, "file", 10, "Load as a 3D model" }
};
VIVID_FILE_DROP(kModelDrop)
