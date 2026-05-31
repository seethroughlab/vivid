#pragma once
// vst3_presets.h — preset discovery + loading for VST3 instruments.
//
// VST3 plugins expose preset *parameters* through the controller, but the
// preset *library* (the sounds) lives in one of three places, only the first
// two of which the VST3 API can reach generically:
//
//   1. `.vstpreset` files in the standard preset tree
//        <root>/<Vendor>/<PlugInName>/**/<name>.vstpreset
//      where <root> is /Library/Audio/Presets or ~/Library/Audio/Presets.
//      The file is a documented container wrapping the exact component-state
//      chunk that IComponent::setState consumes — so loading reuses the
//      existing apply path in vst3_host_common.h.
//
//   2. VST3 program lists (IUnitInfo / IProgramListData): factory programs
//      selected by driving the parameter flagged kIsProgramChange.
//
//   3. The plugin's own internal browser, in a native format the host cannot
//      reach through the VST3 API (Serum's .SerumPreset, Vital's .vital, …).
//      These need a per-plugin *adapter* (see Vst3PresetAdapter) that knows the
//      plugin's preset directory + file format. Adapters always provide rich
//      metadata (name/category/author/tags) for discovery; whether they can
//      *load* a preset is plugin-specific and reported via Vst3PresetMeta.loadable.
//
// Enumeration (building the catalog) is filesystem-only and therefore safe to
// run on a worker thread. Program-list enumeration needs the live controller
// and must run on the main thread; the operator merges the two.
//
// AU/CLAP parity: this same shape maps onto AudioUnit (.aupreset files +
// kAudioUnitProperty_FactoryPresets) and CLAP (clap.preset-discovery factory).
// When au_instrument/clap_instrument gain presets, mirror this header: a
// universal file source + a program/factory source + an adapter registry.

#include "shared/vst3_host/vst3_host_common.h"   // Vst3Handle, MemIBStream, b64, setState path
#include "pluginterfaces/vst/ivstunits.h"        // IUnitInfo, IProgramListData

#include <nlohmann/json.hpp>

#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Catalog data model
// ---------------------------------------------------------------------------

struct Vst3PresetMeta {
    std::string              id;          // opaque load token (path, or "program:<list>:<idx>")
    std::string              name;
    std::string              category;    // bank/folder or program-list name
    std::string              author;
    std::string              description;
    std::vector<std::string> tags;
    std::string              source;      // "vstpreset" | "program" | adapter id (e.g. "serum2")
    bool                     loadable = false;
};

static nlohmann::json vst3_preset_meta_to_json(const Vst3PresetMeta& m) {
    nlohmann::json j;
    j["id"]       = m.id;
    j["name"]     = m.name;
    if (!m.category.empty())    j["category"]    = m.category;
    if (!m.author.empty())      j["author"]      = m.author;
    if (!m.description.empty())  j["description"] = m.description;
    if (!m.tags.empty())        j["tags"]        = m.tags;
    j["source"]   = m.source;
    j["loadable"] = m.loadable;
    return j;
}

static std::string vst3_catalog_to_json(const std::vector<Vst3PresetMeta>& metas) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& m : metas) arr.push_back(vst3_preset_meta_to_json(m));
    return arr.dump();
}

// Component (+ optional controller) state bytes ready for setState.
struct Vst3PresetState {
    std::vector<uint8_t> component;
    std::vector<uint8_t> controller;   // empty if the source carried no controller chunk
    bool                 ok = false;
};

// The resolved action for a load request: apply state bytes, or select a program.
struct Vst3PresetLoad {
    enum Kind { kNone, kState, kProgram } kind = kNone;
    Vst3PresetState                state;               // kState
    Steinberg::Vst::ParamID        program_param = 0;   // kProgram
    double                         program_normalized = 0.0;  // kProgram
};

// ---------------------------------------------------------------------------
// Filesystem helpers
// ---------------------------------------------------------------------------

static std::string vst3p_expand_home(const std::string& p) {
    if (p.empty() || p[0] != '~') return p;
    const char* home = getenv("HOME");
    if (!home) return p;
    return std::string(home) + p.substr(1);
}

static bool vst3p_is_dir(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Recursively collect files whose name ends with `ext` (case-sensitive). For
// each, `out` receives {full_path, parent_folder_name}.
static void vst3p_collect_files(const std::string& dir, const std::string& ext,
                                std::vector<std::pair<std::string, std::string>>& out,
                                int depth = 0) {
    if (depth > 6) return;
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string name = ent->d_name;
        std::string full = dir + "/" + name;
        if (name.size() > ext.size() &&
            name.compare(name.size() - ext.size(), ext.size(), ext) == 0) {
            size_t slash = dir.rfind('/');
            std::string parent = (slash == std::string::npos) ? dir : dir.substr(slash + 1);
            out.emplace_back(full, parent);
        } else if (vst3p_is_dir(full)) {
            vst3p_collect_files(full, ext, out, depth + 1);
        }
    }
    closedir(d);
}

// Recursively collect every regular file, skipping dotfiles and any whose name
// ends with one of `skip_exts`. For each, `out` receives {full_path,
// parent_folder_name}. Used for preset formats with no file extension (Pigments).
static void vst3p_collect_files_filtered(const std::string& dir,
                                         const std::vector<std::string>& skip_exts,
                                         std::vector<std::pair<std::string, std::string>>& out,
                                         int depth = 0) {
    if (depth > 6) return;
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string name = ent->d_name;
        std::string full = dir + "/" + name;
        if (vst3p_is_dir(full)) {
            vst3p_collect_files_filtered(full, skip_exts, out, depth + 1);
            continue;
        }
        bool skip = false;
        for (const auto& ext : skip_exts) {
            if (name.size() > ext.size() &&
                name.compare(name.size() - ext.size(), ext.size(), ext) == 0) { skip = true; break; }
        }
        if (skip) continue;
        size_t slash = dir.rfind('/');
        std::string parent = (slash == std::string::npos) ? dir : dir.substr(slash + 1);
        out.emplace_back(full, parent);
    }
    closedir(d);
}

static std::string vst3p_stem(const std::string& path, const std::string& ext) {
    size_t slash = path.rfind('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    if (name.size() > ext.size() &&
        name.compare(name.size() - ext.size(), ext.size(), ext) == 0)
        name.resize(name.size() - ext.size());
    return name;
}

static std::vector<uint8_t> vst3p_read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
}

// ---------------------------------------------------------------------------
// .vstpreset container parser
//
// Layout (little-endian):
//   char[4]  "VST3"
//   int32    version
//   char[32] classID (ASCII hex)
//   int64    listOffset           — offset from file start to the chunk list
//   ... chunk data ...
//   at listOffset:
//     char[4] "List"
//     int32   entryCount
//     repeated { char[4] chunkID; int64 offset; int64 size; }
//        chunkID is "Comp" (component state) or "Cont" (controller state).
// ---------------------------------------------------------------------------

static uint32_t vst3p_rd_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t vst3p_rd_u64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

static Vst3PresetState vst3_parse_vstpreset(const std::string& path) {
    Vst3PresetState st;
    std::vector<uint8_t> data = vst3p_read_file(path);
    if (data.size() < 48) return st;
    if (std::memcmp(data.data(), "VST3", 4) != 0) return st;

    uint64_t list_off = vst3p_rd_u64(data.data() + 40);
    if (list_off + 8 > data.size()) return st;
    const uint8_t* lp = data.data() + list_off;
    if (std::memcmp(lp, "List", 4) != 0) return st;
    uint32_t entries = vst3p_rd_u32(lp + 4);
    const uint8_t* e = lp + 8;
    for (uint32_t i = 0; i < entries; ++i) {
        if (e + 20 > data.data() + data.size()) break;
        char id[5] = {};
        std::memcpy(id, e, 4);
        uint64_t off = vst3p_rd_u64(e + 4);
        uint64_t sz  = vst3p_rd_u64(e + 12);
        e += 20;
        if (off + sz > data.size()) continue;
        const uint8_t* chunk = data.data() + off;
        if (std::strcmp(id, "Comp") == 0)
            st.component.assign(chunk, chunk + sz);
        else if (std::strcmp(id, "Cont") == 0)
            st.controller.assign(chunk, chunk + sz);
    }
    st.ok = !st.component.empty();
    return st;
}

// ---------------------------------------------------------------------------
// Apply a resolved state to a live handle (proper VST3 preset apply sequence).
// Mirrors vst3_load_state() but takes raw bytes and an optional controller chunk.
// ---------------------------------------------------------------------------

// Current component state as raw bytes (the baseline some adapters splice into).
static std::vector<uint8_t> vst3_current_component_state(Vst3Handle* h) {
    if (!h || !h->component) return {};
    MemIBStream s;
    if (h->component->getState(&s) != Steinberg::kResultOk) return {};
    return s.buf;
}

static void vst3_apply_preset_state(Vst3Handle* h, const Vst3PresetState& st) {
    if (!h || !h->component || st.component.empty()) return;

    MemIBStream comp;
    comp.buf = st.component;
    if (h->component->setState(&comp) != Steinberg::kResultOk)
        fprintf(stderr, "[Vst3] preset setState(component) failed\n");

    if (h->controller) {
        comp.pos = 0;
        h->controller->setComponentState(&comp);
        if (!st.controller.empty()) {
            MemIBStream cont;
            cont.buf = st.controller;
            h->controller->setState(&cont);
        }
    }
}

// ---------------------------------------------------------------------------
// VST3 program lists (universal factory presets via IUnitInfo)
// ---------------------------------------------------------------------------

// Find the parameter flagged kIsProgramChange, if any. Programs are selected by
// driving this parameter (normalized = index / (count-1)).
static bool vst3_find_program_param(Vst3Handle* h, Steinberg::Vst::ParamID& out_id,
                                    int32_t& out_step_count) {
    using namespace Steinberg::Vst;
    if (!h || !h->controller) return false;
    int32_t n = h->controller->getParameterCount();
    for (int32_t i = 0; i < n; ++i) {
        ParameterInfo info{};
        if (h->controller->getParameterInfo(i, info) != Steinberg::kResultOk) continue;
        if (info.flags & ParameterInfo::kIsProgramChange) {
            out_id = info.id;
            out_step_count = info.stepCount;
            return true;
        }
    }
    return false;
}

// Enumerate factory programs via IUnitInfo/IProgramListData. Main-thread only
// (touches the live controller). Returns empty for the majority of synths that
// keep their library in an internal browser instead.
static std::vector<Vst3PresetMeta> vst3_enumerate_programs(Vst3Handle* h) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;
    std::vector<Vst3PresetMeta> out;
    if (!h || !h->controller) return out;

    IUnitInfo* units = nullptr;
    if (h->controller->queryInterface(IUnitInfo::iid, (void**)&units) != kResultOk || !units)
        return out;

    int32 list_count = units->getProgramListCount();
    for (int32 li = 0; li < list_count; ++li) {
        ProgramListInfo pli{};
        if (units->getProgramListInfo(li, pli) != kResultOk) continue;
        std::string list_name = vst3_tchar_to_utf8(pli.name);
        for (int32 pi = 0; pi < pli.programCount; ++pi) {
            String128 pname{};
            if (units->getProgramName(pli.id, pi, pname) != kResultOk) continue;
            Vst3PresetMeta m;
            m.name     = vst3_tchar_to_utf8(pname);
            m.category = list_name;
            m.source   = "program";
            m.id       = "program:" + std::to_string(pli.id) + ":" + std::to_string(pi);
            m.loadable = (pli.programCount > 0);
            out.push_back(std::move(m));
        }
    }
    units->release();
    return out;
}

// Resolve "program:<listId>:<idx>" → a program-change parameter selection.
static bool vst3_resolve_program_load(Vst3Handle* h, const std::string& id, Vst3PresetLoad& out) {
    if (id.rfind("program:", 0) != 0) return false;
    size_t c1 = id.find(':');
    size_t c2 = id.find(':', c1 + 1);
    if (c2 == std::string::npos) return false;
    int idx = std::atoi(id.c_str() + c2 + 1);

    Steinberg::Vst::ParamID pid = 0;
    int32_t step_count = 0;
    if (!vst3_find_program_param(h, pid, step_count)) return false;

    out.kind = Vst3PresetLoad::kProgram;
    out.program_param = pid;
    // Program-change params are stepped; normalized = idx / stepCount.
    out.program_normalized = (step_count > 0) ? (double)idx / (double)step_count : 0.0;
    return true;
}

// ---------------------------------------------------------------------------
// Per-plugin adapter layer (native preset formats)
// ---------------------------------------------------------------------------

struct Vst3PresetAdapter {
    virtual ~Vst3PresetAdapter() = default;
    virtual const char* source_id() const = 0;
    // Does this adapter handle the plugin identified by (vendor, plugin_name)?
    virtual bool matches(const std::string& vendor, const std::string& plugin) const = 0;
    // Filesystem-only enumeration (worker-thread safe).
    virtual void enumerate(std::vector<Vst3PresetMeta>& out) const = 0;
    // Resolve a preset id (one of this adapter's enumerated ids) to state bytes.
    // `baseline` is the plugin's current component state (from getState), which
    // some adapters need to splice a native patch into. Returns false if the
    // adapter can enumerate but not (yet) load this format.
    virtual bool resolve_state(const std::string& id,
                               const std::vector<uint8_t>& baseline,
                               Vst3PresetState& out) const = 0;
};

// ---- Serum 2 (Xfer Records) -----------------------------------------------
//
// Serum 2 keeps its library as `*.SerumPreset` files under a NON-standard dir
// (`Xfer Records/Serum 2 Presets/Presets`, not `Xfer Records/Serum2`), so the
// universal .vstpreset scanner misses it entirely — hence this adapter.
//
// File layout: 8-byte "XferJson" magic, a small binary header, then a plaintext
// JSON metadata object (presetName/presetAuthor/presetDescription/tags), then a
// zstd-compressed native patch payload. Metadata needs only the JSON header.
//
// Loading (.SerumPreset → live instance) requires knowing how Serum frames a
// patch inside its IComponent::getState blob — determined by the load spike.
// Until that lands, resolve_state() returns false and presets are marked
// loadable=false (discovery works; loading is reported as unavailable).
struct SerumPresetAdapter : Vst3PresetAdapter {
    const char* source_id() const override { return "serum2"; }

    bool matches(const std::string& vendor, const std::string& plugin) const override {
        const bool xfer = vendor.find("Xfer") != std::string::npos;
        const bool serum = plugin.find("Serum") != std::string::npos;
        return serum || (xfer && serum);
    }

    static std::vector<std::string> preset_roots() {
        return {
            "/Library/Audio/Presets/Xfer Records/Serum 2 Presets/Presets",
            vst3p_expand_home("~/Library/Audio/Presets/Xfer Records/Serum 2 Presets/Presets"),
            vst3p_expand_home("~/Documents/Xfer/Serum 2 Presets/Presets"),
        };
    }

    // Extract the embedded JSON object that follows the "XferJson" magic.
    static bool read_xferjson_header(const std::string& path, nlohmann::json& out) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        char magic[8];
        f.read(magic, 8);
        if (f.gcount() != 8 || std::memcmp(magic, "XferJson", 8) != 0) return false;
        // The JSON object begins at the first '{' within the first few KB.
        std::vector<char> head(8192);
        f.read(head.data(), (std::streamsize)head.size());
        std::streamsize got = f.gcount();
        std::string s(head.data(), (size_t)got);
        size_t start = s.find('{');
        if (start == std::string::npos) return false;
        // Brace-match, respecting string literals/escapes, to find the object end.
        int depth = 0; bool in_str = false, esc = false; size_t end = std::string::npos;
        for (size_t i = start; i < s.size(); ++i) {
            char c = s[i];
            if (esc) { esc = false; continue; }
            if (in_str) {
                if (c == '\\') esc = true;
                else if (c == '"') in_str = false;
            } else {
                if (c == '"') in_str = true;
                else if (c == '{') depth++;
                else if (c == '}') { if (--depth == 0) { end = i + 1; break; } }
            }
        }
        if (end == std::string::npos) return false;
        out = nlohmann::json::parse(s.substr(start, end - start), nullptr, false);
        return !out.is_discarded();
    }

    void enumerate(std::vector<Vst3PresetMeta>& out) const override {
        const std::string ext = ".SerumPreset";
        for (const auto& root : preset_roots()) {
            if (!vst3p_is_dir(root)) continue;
            std::vector<std::pair<std::string, std::string>> files;
            vst3p_collect_files(root, ext, files);
            for (const auto& [path, parent] : files) {
                Vst3PresetMeta m;
                m.id       = path;
                m.source   = source_id();
                m.category = parent;        // e.g. "Pad", "Bass" (immediate folder)
                m.name     = vst3p_stem(path, ext);
                m.loadable = false;         // discovery-only (see resolve_state)
                nlohmann::json hdr;
                if (read_xferjson_header(path, hdr) && hdr.is_object()) {
                    if (hdr.contains("presetName") && hdr["presetName"].is_string())
                        m.name = hdr["presetName"].get<std::string>();
                    if (hdr.contains("presetAuthor") && hdr["presetAuthor"].is_string())
                        m.author = hdr["presetAuthor"].get<std::string>();
                    if (hdr.contains("presetDescription") && hdr["presetDescription"].is_string())
                        m.description = hdr["presetDescription"].get<std::string>();
                    if (hdr.contains("tags") && hdr["tags"].is_array())
                        for (const auto& t : hdr["tags"])
                            if (t.is_string()) m.tags.push_back(t.get<std::string>());
                }
                out.push_back(std::move(m));
            }
            break;  // first existing root wins
        }
    }

    // Loading a .SerumPreset into a live instance is NOT implemented — Serum
    // exposes no host-reachable load path, and synthesizing a processor state
    // from a preset file is not robust. Findings from the load spike (kept here
    // so a future attempt doesn't re-derive them):
    //
    //   * Serum's IComponent::getState and a .SerumPreset are both "XferJson"
    //     containers: magic + JSON metadata + payload block [u32 rawSize, u32
    //     fmt=2, zstd-stream]. The JSON "hash" == MD5(zstd-stream).
    //   * They are NOT interchangeable. setState rejects a raw .SerumPreset
    //     (its JSON is {"fileType":"SerumPreset"}, not {"component":"processor"}).
    //   * Splicing the preset's payload under the processor header — even with a
    //     recomputed MD5 hash — is also rejected: the decompressed payloads are
    //     CBOR maps with DIFFERENT shapes (processor state ~162 keys incl. runtime
    //     fields like activeClip; preset ~176 keys). Serum's setState validates
    //     the CBOR structure, so a block swap yields an invalid processor state.
    //   * A correct loader would CBOR-decode the preset patch, merge its sound
    //     fields into a processor-shaped map, re-encode, zstd-compress (note: the
    //     operator links zlib, not zstd), recompute the hash, and rewrap — a
    //     fragile, Serum-version-specific transform not worth shipping today.
    //
    // So Serum presets are discovery-only (loadable=false). The supported way to
    // pin a Serum sound is: recommend a preset by its metadata, have the user load
    // it in Serum's own UI, then capture the full state with save_preset.
    bool resolve_state(const std::string& /*id*/,
                       const std::vector<uint8_t>& /*baseline*/,
                       Vst3PresetState& /*out*/) const override {
        return false;
    }
};

// ---- Arturia Pigments ------------------------------------------------------
//
// Pigments factory presets are EXTENSIONLESS files under
// /Library/Arturia/Presets/Pigments/{Factory,User}/<Pack>/<Name>, stored as
// Boost text archives: a whitespace-separated stream of length-prefixed strings
// "<N> <N chars>". Metadata is a run of key→value string pairs we anchor on:
// OriginalPresetName, OriginalPackName, Type, Subtype, and a single
// "Characteristics" value holding the taxonomy
// "Characteristics,a|b;Genres,c|d;Styles,e|f;". Richest metadata of any plugin
// here. The file is itself a Boost state archive, so resolve_state hands the raw
// bytes to setState (load spike decides whether Pigments accepts it).
struct PigmentsPresetAdapter : Vst3PresetAdapter {
    const char* source_id() const override { return "pigments"; }

    bool matches(const std::string& vendor, const std::string& plugin) const override {
        return plugin.find("Pigments") != std::string::npos ||
               vendor.find("Arturia") != std::string::npos;
    }

    static std::vector<std::string> preset_roots() {
        return {
            "/Library/Arturia/Presets/Pigments/Factory",
            "/Library/Arturia/Presets/Pigments/User",
            vst3p_expand_home("~/Library/Arturia/Presets/Pigments/Factory"),
            vst3p_expand_home("~/Library/Arturia/Presets/Pigments/User"),
        };
    }

    // Read the value string that follows a length-prefixed key token
    // ("<klen> <key>") in a Boost text archive. Returns "" if not found.
    static std::string value_after_key(const std::string& s, const std::string& key) {
        // The key is itself length-prefixed: e.g. " 4 Type ". Build that anchor.
        std::string anchor = " " + std::to_string(key.size()) + " " + key + " ";
        size_t k = s.find(anchor);
        if (k == std::string::npos) return {};
        size_t p = k + anchor.size();
        // Following token: "<vlen> <vlen chars>".
        size_t sp = s.find(' ', p);
        if (sp == std::string::npos) return {};
        long vlen = std::atol(s.substr(p, sp - p).c_str());
        if (vlen <= 0 || sp + 1 + (size_t)vlen > s.size()) return {};
        return s.substr(sp + 1, (size_t)vlen);
    }

    // Split the "Characteristics,a|b;Genres,c|d;Styles,e|f;" taxonomy into tags.
    static void parse_taxonomy(const std::string& tax, std::vector<std::string>& tags) {
        size_t i = 0;
        while (i < tax.size()) {
            size_t semi = tax.find(';', i);
            if (semi == std::string::npos) semi = tax.size();
            std::string group = tax.substr(i, semi - i);   // "Genres,Ambient|Hip Hop"
            size_t comma = group.find(',');
            if (comma != std::string::npos) {
                std::string vals = group.substr(comma + 1);
                size_t j = 0;
                while (j < vals.size()) {
                    size_t bar = vals.find('|', j);
                    if (bar == std::string::npos) bar = vals.size();
                    std::string v = vals.substr(j, bar - j);
                    if (!v.empty()) tags.push_back(v);
                    j = bar + 1;
                }
            }
            i = semi + 1;
        }
    }

    void enumerate(std::vector<Vst3PresetMeta>& out) const override {
        for (const auto& root : preset_roots()) {
            if (!vst3p_is_dir(root)) continue;
            std::vector<std::pair<std::string, std::string>> files;
            vst3p_collect_files_filtered(root, {".xml", ".meta"}, files);
            for (const auto& [path, parent] : files) {
                std::ifstream f(path, std::ios::binary);
                if (!f) continue;
                std::vector<char> head(4096);
                f.read(head.data(), (std::streamsize)head.size());
                std::string s(head.data(), (size_t)f.gcount());
                // Only treat Boost-archive files as presets; skip anything else.
                if (s.find("serialization::archive") == std::string::npos) continue;

                Vst3PresetMeta m;
                m.id     = path;
                m.source = source_id();
                std::string oname = value_after_key(s, "OriginalPresetName");
                m.name = !oname.empty() ? oname : vst3p_stem(path, "");
                m.category = value_after_key(s, "Type");       // e.g. "Synth Lead"
                if (m.category.empty()) m.category = parent;    // fallback: pack folder
                std::string sub = value_after_key(s, "Subtype");
                if (!sub.empty()) m.tags.push_back(sub);
                std::string pack = value_after_key(s, "OriginalPackName");
                if (!pack.empty()) m.tags.push_back(pack);
                parse_taxonomy(value_after_key(s, "Characteristics"), m.tags);
                // Verified: Pigments' preset file IS its component-state archive,
                // and setState accepts it (deterministic getState round-trip).
                m.loadable = true;
                out.push_back(std::move(m));
            }
        }
    }

    bool resolve_state(const std::string& id,
                       const std::vector<uint8_t>& /*baseline*/,
                       Vst3PresetState& out) const override {
        // The preset file is a Boost state archive — hand it straight to setState.
        out.component = vst3p_read_file(id);
        out.ok = !out.component.empty();
        return out.ok;
    }
};

// ---- Xfer Records Cthulhu --------------------------------------------------
//
// Cthulhu presets are VST2 .fxp files (CcnK / FPCh chunk) under
// /Library/Audio/Presets/Xfer Records/Cthulhu Presets/<Category>/. The embedded
// 28-byte program name is often generic, so the filename stem is the better
// name; the folder is the category. A VST2 FXP chunk is not VST3 setState
// material, so loading is unsupported (discovery-only).
struct CthulhuPresetAdapter : Vst3PresetAdapter {
    const char* source_id() const override { return "cthulhu"; }

    bool matches(const std::string& vendor, const std::string& plugin) const override {
        // Distinct from Serum (also Xfer Records): require "Cthulhu" in the name.
        (void)vendor;
        return plugin.find("Cthulhu") != std::string::npos;
    }

    static std::vector<std::string> preset_roots() {
        return {
            "/Library/Audio/Presets/Xfer Records/Cthulhu Presets",
            vst3p_expand_home("~/Library/Audio/Presets/Xfer Records/Cthulhu Presets"),
        };
    }

    void enumerate(std::vector<Vst3PresetMeta>& out) const override {
        const std::string ext = ".fxp";
        for (const auto& root : preset_roots()) {
            if (!vst3p_is_dir(root)) continue;
            std::vector<std::pair<std::string, std::string>> files;
            vst3p_collect_files(root, ext, files);
            for (const auto& [path, parent] : files) {
                Vst3PresetMeta m;
                m.id       = path;
                m.source   = source_id();
                m.category = parent;                 // Arp / Chord / Bass / ...
                m.name     = vst3p_stem(path, ext);  // filename is the meaningful name
                m.loadable = false;                  // FXP chunk ≠ VST3 state (discovery-only)
                out.push_back(std::move(m));
            }
            break;  // first existing root wins
        }
    }

    bool resolve_state(const std::string& /*id*/,
                       const std::vector<uint8_t>& /*baseline*/,
                       Vst3PresetState& /*out*/) const override {
        return false;
    }
};

// Adapter registry. Add new native-format adapters here.
static const std::vector<std::unique_ptr<Vst3PresetAdapter>>& vst3_preset_adapters() {
    static std::vector<std::unique_ptr<Vst3PresetAdapter>> reg = [] {
        std::vector<std::unique_ptr<Vst3PresetAdapter>> v;
        v.push_back(std::make_unique<SerumPresetAdapter>());
        v.push_back(std::make_unique<PigmentsPresetAdapter>());
        v.push_back(std::make_unique<CthulhuPresetAdapter>());
        return v;
    }();
    return reg;
}

static const Vst3PresetAdapter* vst3_find_adapter(const std::string& vendor,
                                                  const std::string& plugin) {
    for (const auto& a : vst3_preset_adapters())
        if (a->matches(vendor, plugin)) return a.get();
    return nullptr;
}

// ---------------------------------------------------------------------------
// Universal .vstpreset file source
// ---------------------------------------------------------------------------

static void vst3_enumerate_vstpresets(const std::string& vendor, const std::string& plugin,
                                      std::vector<Vst3PresetMeta>& out) {
    const std::string ext = ".vstpreset";
    std::vector<std::string> roots = {
        "/Library/Audio/Presets",
        vst3p_expand_home("~/Library/Audio/Presets"),
    };
    for (const auto& root : roots) {
        std::string dir = root + "/" + vendor + "/" + plugin;
        if (!vst3p_is_dir(dir)) continue;
        std::vector<std::pair<std::string, std::string>> files;
        vst3p_collect_files(dir, ext, files);
        for (const auto& [path, parent] : files) {
            Vst3PresetMeta m;
            m.id       = path;
            m.name     = vst3p_stem(path, ext);
            m.category = (parent == plugin) ? "" : parent;
            m.source   = "vstpreset";
            m.loadable = true;
            out.push_back(std::move(m));
        }
    }
}

// ---------------------------------------------------------------------------
// Catalog assembly
// ---------------------------------------------------------------------------

// Filesystem-only sources (worker-thread safe): .vstpreset files + adapters.
static std::vector<Vst3PresetMeta> vst3_enumerate_file_presets(const std::string& vendor,
                                                               const std::string& plugin) {
    std::vector<Vst3PresetMeta> out;
    vst3_enumerate_vstpresets(vendor, plugin, out);
    if (const Vst3PresetAdapter* a = vst3_find_adapter(vendor, plugin))
        a->enumerate(out);
    std::sort(out.begin(), out.end(), [](const Vst3PresetMeta& a, const Vst3PresetMeta& b) {
        if (a.category != b.category) return a.category < b.category;
        return a.name < b.name;
    });
    return out;
}

// Resolve a load request to a concrete action. Main-thread only (may touch the
// controller for program selection). `baseline` is the plugin's current
// component state (from getState), used by adapters that splice native patches.
static Vst3PresetLoad vst3_resolve_preset_load(Vst3Handle* h,
                                               const std::string& vendor,
                                               const std::string& plugin,
                                               const std::string& id,
                                               const std::vector<uint8_t>& baseline) {
    Vst3PresetLoad load;
    if (id.empty()) return load;

    // Program selection
    if (id.rfind("program:", 0) == 0) {
        vst3_resolve_program_load(h, id, load);
        return load;
    }
    // .vstpreset file
    if (id.size() > 10 && id.compare(id.size() - 10, 10, ".vstpreset") == 0) {
        Vst3PresetState st = vst3_parse_vstpreset(id);
        if (st.ok) { load.kind = Vst3PresetLoad::kState; load.state = std::move(st); }
        return load;
    }
    // Adapter-owned native format
    if (const Vst3PresetAdapter* a = vst3_find_adapter(vendor, plugin)) {
        Vst3PresetState st;
        if (a->resolve_state(id, baseline, st) && st.ok) {
            load.kind = Vst3PresetLoad::kState;
            load.state = std::move(st);
        }
    }
    return load;
}

} // namespace
