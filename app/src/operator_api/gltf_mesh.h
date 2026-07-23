#pragma once
// CPU-side geometry: column-major 4x4 math + a hand-rolled glTF/GLB parser that yields ONE merged,
// centered, unit-scaled mesh (interleaved position+normal+uv, indices, one baseColor RGBA texture).
// Header-only so every geometry operator (Model, MeshLoad, …) shares ONE parser instead of copying
// it — the whole point of the geometry-operator upgrade. Independent of wgpu: produces a CpuMesh the
// GPU side (mesh_render.h) uploads. Scope matches the original Model op: POSITION (+ optional
// NORMAL / TEXCOORD_0) + indices, first baseColor texture; no PBR/animation/skinning.
//
// The includer that wants the baseColor texture must define STB_IMAGE_IMPLEMENTATION in exactly one
// translation unit before including this header (each operator is its own single-source dylib, so
// that is just the op's .cpp). nlohmann/json must be on the include path (link nlohmann_json).
// Geometry types + Mat4 math live in mesh_types.h (dependency-free), which this header extends.
#include "operator_api/mesh_types.h"
#include <nlohmann/json.hpp>
#include "stb_image.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace vivid::geom {

using json = nlohmann::json;

// ---- glTF loading ------------------------------------------------------------------------------
namespace detail {

inline std::string dir_of(const std::string& path) {
    const auto p = path.find_last_of("/\\");
    return p == std::string::npos ? std::string() : path.substr(0, p + 1);
}
// Minimal base64 decode (for data: URIs in single-file .gltf).
inline std::vector<uint8_t> b64_decode(const std::string& in) {
    static int8_t T[256]; static bool init = false;
    if (!init) { for (int i = 0; i < 256; ++i) T[i] = -1;
        const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) T[(uint8_t)a[i]] = (int8_t)i; init = true; }
    std::vector<uint8_t> out; int val = 0, bits = -8;
    for (unsigned char c : in) { if (T[c] < 0) continue; val = (val << 6) + T[c]; bits += 6;
        if (bits >= 0) { out.push_back((uint8_t)((val >> bits) & 0xFF)); bits -= 8; } }
    return out;
}
inline std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
inline std::string decode_uri_component(const std::string& s) {   // just %XX unescape (spaces etc.)
    std::string o; o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::strtol(s.substr(i+1,2).c_str(), nullptr, 16); i += 2; }
        else o += s[i];
    }
    return o;
}
inline int comp_size(int ct) { switch (ct) { case 5120: case 5121: return 1; case 5122: case 5123: return 2; case 5125: case 5126: return 4; } return 4; }
inline int type_count(const std::string& t) { if (t=="SCALAR") return 1; if (t=="VEC2") return 2; if (t=="VEC3") return 3; if (t=="VEC4") return 4; if (t=="MAT4") return 16; return 1; }

// Read `count` * `ncomp` floats from an accessor (componentType assumed float 5126 for attributes).
inline bool read_float_accessor(const json& gltf, const std::vector<std::vector<uint8_t>>& bufs, int acc_idx,
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
inline bool read_index_accessor(const json& gltf, const std::vector<std::vector<uint8_t>>& bufs, int acc_idx,
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
inline void load_base_color(const json& gltf, const std::vector<std::vector<uint8_t>>& bufs,
                            const std::string& base_dir, int material, CpuMesh& md) {
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

}  // namespace detail

// Parse a .gltf/.glb file into a single merged, centered, unit-scaled CpuMesh. Returns false + err.
inline bool load_gltf(const std::string& path, CpuMesh& md, std::string& err) {
    using namespace detail;
    std::vector<uint8_t> file = read_file(path);
    if (file.empty()) { err = "cannot read file"; return false; }

    json gltf; std::vector<uint8_t> glb_bin;
    if (file.size() > 12 && std::memcmp(file.data(), "glTF", 4) == 0) {   // GLB container
        uint32_t total; std::memcpy(&total, file.data() + 8, 4); (void)total;
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
            }
        }
        for (const auto& c : n.value("children", json::array())) stack.push_back({ c.get<int>(), world });
    }
    if (md.verts.empty()) { err = "no triangle geometry"; return false; }

    load_base_color(gltf, bufs, base_dir, first_material, md);

    // Auto-center + unit-scale so any model displays regardless of its native units / origin.
    float mn[3] = { 1e30f,1e30f,1e30f }, mx[3] = { -1e30f,-1e30f,-1e30f };
    for (const MeshVertex& v : md.verts) { const float p[3]={v.px,v.py,v.pz};
        for (int k=0;k<3;++k){ mn[k]=std::min(mn[k],p[k]); mx[k]=std::max(mx[k],p[k]); } }
    const float cx=(mn[0]+mx[0])*0.5f, cy=(mn[1]+mx[1])*0.5f, cz=(mn[2]+mx[2])*0.5f;
    float rad = 1e-6f;
    for (const MeshVertex& v : md.verts) { const float dx=v.px-cx, dy=v.py-cy, dz=v.pz-cz; rad = std::max(rad, std::sqrt(dx*dx+dy*dy+dz*dz)); }
    const float inv = 1.f / rad;
    for (MeshVertex& v : md.verts) { v.px=(v.px-cx)*inv; v.py=(v.py-cy)*inv; v.pz=(v.pz-cz)*inv; }
    return true;
}

}  // namespace vivid::geom
