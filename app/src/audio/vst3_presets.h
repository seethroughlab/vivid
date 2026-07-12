#pragma once
// vst3_presets.h — VST3 instrument preset discovery + loading (app/ trunk).
//
// A VST3 plugin's preset *library* (the sounds) lives in one of a few places, only some
// of which the VST3 API can reach generically:
//
//   1. `.vstpreset` files in the standard preset tree
//        <root>/<Vendor>/<PlugInName>/**/<name>.vstpreset   (root = /Library/Audio/Presets
//      or ~/Library/Audio/Presets, plus factory copies in the bundle Resources). A documented
//      container wrapping the exact component-state chunk IComponent::setState consumes, so
//      loading reuses MemIBStream + the setState path. LOADABLE.
//
//   2. The plugin's own internal browser, in a NATIVE format the VST3 API can't reach
//      generically (Serum's `.SerumPreset`, Pigments' Boost archives, …). These need a
//      per-plugin *adapter* (Vst3PresetAdapter) that knows the plugin's preset dir + file
//      format. Adapters always give rich metadata (name/category/tags) for discovery; whether
//      a preset can be *loaded* is plugin-specific (PresetEntry.loadable).
//
// Enumeration is filesystem-only. It runs on the main thread here (from the control handler),
// so it's bounded (a cap + a name filter) to keep the browse responsive. Program-list /
// IUnitInfo factory presets are a separate mechanism, not covered yet.
//
// Included by vst3_host.cpp AFTER vst3_host_common.h — both live in the TU's single anonymous
// namespace, so Vst3Handle / MemIBStream / the Steinberg using-directives are already in scope.

#include "vst3_host_common.h"     // Vst3Handle, MemIBStream, kResultOk (via using namespace Steinberg)

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

// A preset for the generic browse/load flow: the {name,id} the flow needs, plus discovery
// metadata (category/tags) and whether it can actually be loaded on this host.
struct PresetEntry {
    std::string              name;
    std::string              id;         // load token: a file path (`.vstpreset` or native)
    std::string              category;   // bank / folder / type
    std::string              source;     // "vstpreset" | adapter id ("serum2"/"pigments")
    std::vector<std::string> tags;
    bool                     loadable = true;
};

namespace vst3p {

inline bool ci_contains(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto low = [](std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
    return low(hay).find(low(needle)) != std::string::npos;
}
inline bool ends_with(const std::string& s, const char* suf) {
    const size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}
inline uint32_t rd_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline uint64_t rd_u64(const uint8_t* p) {
    uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i); return v;
}
inline std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace vst3p

// --- Apply resolved state bytes to a live handle (mirrors vst3_load_state) -------------------
static bool vst3_apply_state(Vst3Handle* h, const std::vector<uint8_t>& comp,
                             const std::vector<uint8_t>& cont) {
    if (!h || !h->component || comp.empty()) return false;
    MemIBStream cs; cs.buf = comp;
    const bool ok = (h->component->setState(&cs) == kResultOk);        // component (DSP) state
    if (h->controller_is_owned && h->controller) { cs.pos = 0; h->controller->setComponentState(&cs); }
    if (ok && !cont.empty() && h->controller_is_owned && h->controller) {   // controller (UI) state
        MemIBStream ct; ct.buf = cont; h->controller->setState(&ct);
    }
    return ok;
}

// --- Universal `.vstpreset` container --------------------------------------------------------
// Layout (LE): "VST3" | int32 version | char[32] classID | int64 listOffset (= 48-byte header).
// At listOffset: "List" | int32 entryCount | entries { char[4] id | int64 off | int64 size }.
static bool vst3_parse_vstpreset(const std::string& path, std::vector<uint8_t>& comp,
                                 std::vector<uint8_t>& cont) {
    std::vector<uint8_t> data = vst3p::read_file(path);
    if (data.size() < 48 || std::memcmp(data.data(), "VST3", 4) != 0) return false;
    const uint64_t listOff = vst3p::rd_u64(data.data() + 40);
    if (listOff + 8 > data.size() || std::memcmp(data.data() + listOff, "List", 4) != 0) return false;
    const uint32_t count = vst3p::rd_u32(data.data() + listOff + 4);
    size_t ep = static_cast<size_t>(listOff) + 8;
    for (uint32_t i = 0; i < count; ++i) {
        if (ep + 20 > data.size()) break;
        char id[4]; std::memcpy(id, data.data() + ep, 4);
        const uint64_t off = vst3p::rd_u64(data.data() + ep + 4);
        const uint64_t sz  = vst3p::rd_u64(data.data() + ep + 12);
        ep += 20;
        if (off + sz > data.size()) continue;
        if      (std::memcmp(id, "Comp", 4) == 0) comp.assign(data.begin() + off, data.begin() + off + sz);
        else if (std::memcmp(id, "Cont", 4) == 0) cont.assign(data.begin() + off, data.begin() + off + sz);
    }
    return !comp.empty();
}

static void vst3_enumerate_vstpresets(Vst3Handle* h, std::vector<PresetEntry>& out, size_t cap) {
    namespace fs = std::filesystem;
    if (h->plugin_name.empty()) return;
    const std::string sub = (h->vendor.empty() ? std::string() : h->vendor + "/") + h->plugin_name;
    std::vector<fs::path> roots;
    if (const char* home = std::getenv("HOME"))
        roots.push_back(fs::path(home) / "Library" / "Audio" / "Presets" / sub);
    roots.push_back(fs::path("/Library/Audio/Presets") / sub);
    if (!h->bundle_path_.empty())
        roots.push_back(fs::path(h->bundle_path_) / "Contents" / "Resources");   // factory presets sometimes ship here
    for (const auto& root : roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             it != end && out.size() < cap; it.increment(ec)) {
            if (ec) break;
            const fs::path& p = it->path();
            if (p.extension() != ".vstpreset") continue;
            PresetEntry e;
            e.name = p.stem().string();
            e.id = p.string();
            const std::string parent = p.parent_path().filename().string();
            e.category = (parent == h->plugin_name) ? "" : parent;
            e.source = "vstpreset";
            e.loadable = true;
            out.push_back(std::move(e));
        }
    }
}

// --- Per-plugin native-format adapters -------------------------------------------------------
struct Vst3PresetAdapter {
    virtual ~Vst3PresetAdapter() = default;
    virtual const char* id() const = 0;
    virtual bool matches(const std::string& vendor, const std::string& plugin) const = 0;
    virtual void enumerate(std::vector<PresetEntry>& out, size_t cap) const = 0;
    // Resolve one of this adapter's ids to component-state bytes. false => enumerable but not
    // loadable on this host (discovery-only).
    virtual bool resolve_state(const std::string& id, std::vector<uint8_t>& comp) const = 0;
};

// Recurse `root` collecting regular files; `keep(path, entry)` fills an entry and returns whether
// to keep it. Bounded by `cap` and a depth-limited walk. Shared by the adapters.
template <class KeepFn>
static void vst3p_walk(const std::string& root, std::vector<PresetEntry>& out, size_t cap, KeepFn keep) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         it != end && out.size() < cap; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const fs::path& p = it->path();
        if (p.filename().string().rfind('.', 0) == 0) continue;   // skip dotfiles
        PresetEntry e;
        if (keep(p, e)) out.push_back(std::move(e));
    }
}

// ---- Serum 2 (Xfer Records): `.SerumPreset` files, DISCOVERY-ONLY --------------------------
// Non-standard dir the universal .vstpreset scan misses. File = 8-byte "XferJson" magic, then a
// plaintext JSON metadata object (presetName/presetAuthor/tags), then a zstd patch payload.
// Metadata reads from the JSON header alone (no plugin, no decompression) — great for discovery.
// LOAD is not feasible: setState rejects a raw `.SerumPreset` (its JSON is {"fileType":
// "SerumPreset"}, not a processor state) and the zstd/CBOR patch shape differs from Serum's own
// getState — a correct loader would be a fragile, version-specific CBOR transform. So Serum
// presets are browse-only; pin a sound by loading it in Serum's UI, then save_project captures it.
struct SerumPresetAdapter : Vst3PresetAdapter {
    const char* id() const override { return "serum2"; }
    bool matches(const std::string& vendor, const std::string& plugin) const override {
        return plugin.find("Serum") != std::string::npos ||
               (vendor.find("Xfer") != std::string::npos && plugin.find("Serum") != std::string::npos);
    }
    static std::vector<std::string> roots() {
        std::vector<std::string> r{ "/Library/Audio/Presets/Xfer Records/Serum 2 Presets/Presets" };
        if (const char* home = std::getenv("HOME")) {
            r.push_back(std::string(home) + "/Library/Audio/Presets/Xfer Records/Serum 2 Presets/Presets");
            r.push_back(std::string(home) + "/Documents/Xfer/Serum 2 Presets/Presets");
        }
        return r;
    }
    // Parse the JSON object that follows the "XferJson" magic (brace-matched, string-aware).
    static bool read_header(const std::string& path, nlohmann::json& out) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        char magic[8];
        f.read(magic, 8);
        if (f.gcount() != 8 || std::memcmp(magic, "XferJson", 8) != 0) return false;
        std::vector<char> head(8192);
        f.read(head.data(), static_cast<std::streamsize>(head.size()));
        std::string s(head.data(), static_cast<size_t>(f.gcount()));
        size_t start = s.find('{');
        if (start == std::string::npos) return false;
        int depth = 0; bool in_str = false, esc = false; size_t end = std::string::npos;
        for (size_t i = start; i < s.size(); ++i) {
            char c = s[i];
            if (esc) { esc = false; continue; }
            if (in_str) { if (c == '\\') esc = true; else if (c == '"') in_str = false; }
            else if (c == '"') in_str = true;
            else if (c == '{') depth++;
            else if (c == '}') { if (--depth == 0) { end = i + 1; break; } }
        }
        if (end == std::string::npos) return false;
        out = nlohmann::json::parse(s.substr(start, end - start), nullptr, false);
        return !out.is_discarded();
    }
    void enumerate(std::vector<PresetEntry>& out, size_t cap) const override {
        for (const auto& root : roots()) {
            std::filesystem::path rp(root);
            const size_t before = out.size();
            vst3p_walk(root, out, cap, [&](const std::filesystem::path& p, PresetEntry& e) {
                if (p.extension() != ".SerumPreset") return false;
                e.id = p.string();
                e.source = id();
                e.category = p.parent_path().filename().string();   // e.g. "Bass", "Pad"
                e.name = p.stem().string();
                e.loadable = false;                                 // discovery-only (see class comment)
                nlohmann::json hdr;
                if (read_header(p.string(), hdr) && hdr.is_object()) {
                    if (auto n = hdr.find("presetName"); n != hdr.end() && n->is_string() && !n->get<std::string>().empty())
                        e.name = n->get<std::string>();
                    if (auto a = hdr.find("presetAuthor"); a != hdr.end() && a->is_string() && !a->get<std::string>().empty())
                        e.tags.push_back(a->get<std::string>());
                    if (auto t = hdr.find("tags"); t != hdr.end() && t->is_array())
                        for (const auto& tag : *t) if (tag.is_string()) e.tags.push_back(tag.get<std::string>());
                }
                return true;
            });
            if (out.size() > before) break;   // first existing root wins
        }
    }
    bool resolve_state(const std::string&, std::vector<uint8_t>&) const override { return false; }
};

// ---- Arturia Pigments: extensionless Boost-archive files, LOADABLE --------------------------
// Under /Library/Arturia/Presets/Pigments/{Factory,User}/<Pack>/<Name>. Each file is a Boost
// text archive that IS Pigments' component-state blob (verified: setState round-trips), so it
// loads by handing the raw bytes to setState. Rich metadata is a run of length-prefixed
// key→value string pairs ("<len> <key> <len> <value>") we anchor on.
struct PigmentsPresetAdapter : Vst3PresetAdapter {
    const char* id() const override { return "pigments"; }
    bool matches(const std::string& vendor, const std::string& plugin) const override {
        return plugin.find("Pigments") != std::string::npos || vendor.find("Arturia") != std::string::npos;
    }
    static std::vector<std::string> roots() {
        std::vector<std::string> r{ "/Library/Arturia/Presets/Pigments/Factory",
                                    "/Library/Arturia/Presets/Pigments/User" };
        if (const char* home = std::getenv("HOME")) {
            r.push_back(std::string(home) + "/Library/Arturia/Presets/Pigments/Factory");
            r.push_back(std::string(home) + "/Library/Arturia/Presets/Pigments/User");
        }
        return r;
    }
    // Value string that follows a length-prefixed key token (" <klen> <key> ") in the archive.
    static std::string value_after_key(const std::string& s, const std::string& key) {
        std::string anchor = " " + std::to_string(key.size()) + " " + key + " ";
        size_t k = s.find(anchor);
        if (k == std::string::npos) return {};
        size_t p = k + anchor.size();
        size_t sp = s.find(' ', p);
        if (sp == std::string::npos) return {};
        long vlen = std::atol(s.substr(p, sp - p).c_str());
        if (vlen <= 0 || sp + 1 + static_cast<size_t>(vlen) > s.size()) return {};
        return s.substr(sp + 1, static_cast<size_t>(vlen));
    }
    // "Characteristics,a|b;Genres,c|d;Styles,e|f;" -> individual tags.
    static void parse_taxonomy(const std::string& tax, std::vector<std::string>& tags) {
        size_t i = 0;
        while (i < tax.size()) {
            size_t semi = tax.find(';', i); if (semi == std::string::npos) semi = tax.size();
            std::string group = tax.substr(i, semi - i);
            size_t comma = group.find(',');
            if (comma != std::string::npos) {
                std::string vals = group.substr(comma + 1);
                size_t j = 0;
                while (j < vals.size()) {
                    size_t bar = vals.find('|', j); if (bar == std::string::npos) bar = vals.size();
                    std::string v = vals.substr(j, bar - j);
                    if (!v.empty()) tags.push_back(v);
                    j = bar + 1;
                }
            }
            i = semi + 1;
        }
    }
    void enumerate(std::vector<PresetEntry>& out, size_t cap) const override {
        for (const auto& root : roots()) {
            vst3p_walk(root, out, cap, [&](const std::filesystem::path& p, PresetEntry& e) {
                const std::string ext = p.extension().string();
                if (ext == ".xml" || ext == ".meta") return false;
                std::ifstream f(p, std::ios::binary);
                if (!f) return false;
                std::vector<char> head(4096);
                f.read(head.data(), static_cast<std::streamsize>(head.size()));
                std::string s(head.data(), static_cast<size_t>(f.gcount()));
                if (s.find("serialization::archive") == std::string::npos) return false;   // only Boost archives
                e.id = p.string();
                e.source = id();
                std::string oname = value_after_key(s, "OriginalPresetName");
                e.name = !oname.empty() ? oname : p.stem().string();
                e.category = value_after_key(s, "Type");
                if (e.category.empty()) e.category = p.parent_path().filename().string();
                std::string sub = value_after_key(s, "Subtype"); if (!sub.empty()) e.tags.push_back(sub);
                parse_taxonomy(value_after_key(s, "Characteristics"), e.tags);
                e.loadable = true;    // the preset file IS the component-state archive
                return true;
            });
        }
    }
    bool resolve_state(const std::string& id, std::vector<uint8_t>& comp) const override {
        comp = vst3p::read_file(id);   // Boost state archive -> straight to setState
        return !comp.empty();
    }
};

static const std::vector<std::unique_ptr<Vst3PresetAdapter>>& vst3_preset_adapters() {
    static const std::vector<std::unique_ptr<Vst3PresetAdapter>> reg = [] {
        std::vector<std::unique_ptr<Vst3PresetAdapter>> v;
        v.push_back(std::make_unique<SerumPresetAdapter>());
        v.push_back(std::make_unique<PigmentsPresetAdapter>());
        return v;
    }();
    return reg;
}
static const Vst3PresetAdapter* vst3_find_adapter(const std::string& vendor, const std::string& plugin) {
    for (const auto& a : vst3_preset_adapters())
        if (a->matches(vendor, plugin)) return a.get();
    return nullptr;
}

// --- Public entry points (called by vst3_host.cpp) -------------------------------------------

// Enumerate this plugin's presets (`.vstpreset` files + a native adapter if one matches) into
// `out`, filtered by a case-insensitive name substring, sorted by (category, name), capped.
static void vst3_scan_presets(Vst3Handle* h, const char* filter, std::vector<PresetEntry>& out) {
    if (!h) return;
    const std::string flt = filter ? filter : "";
    const size_t cap = 6000;                       // bound the (main-thread) filesystem walk
    vst3_enumerate_vstpresets(h, out, cap);
    if (const Vst3PresetAdapter* a = vst3_find_adapter(h->vendor, h->plugin_name))
        a->enumerate(out, cap);
    if (!flt.empty())
        out.erase(std::remove_if(out.begin(), out.end(),
                  [&](const PresetEntry& e) { return !vst3p::ci_contains(e.name, flt); }), out.end());
    std::sort(out.begin(), out.end(), [](const PresetEntry& a, const PresetEntry& b) {
        if (a.category != b.category) return a.category < b.category;
        return a.name < b.name;
    });
    fprintf(stderr, "[Vst3] preset scan '%s/%s': %zu presets%s\n", h->vendor.c_str(),
            h->plugin_name.c_str(), out.size(), flt.empty() ? "" : " (filtered)");
}

// Apply a preset by id: a `.vstpreset` file path, or an adapter-owned native path. Returns true
// only if state was actually applied (a browse-only preset returns false).
static bool vst3_load_preset(Vst3Handle* h, const std::string& id) {
    if (!h || !h->component || id.empty()) return false;
    if (vst3p::ends_with(id, ".vstpreset")) {
        std::vector<uint8_t> comp, cont;
        if (!vst3_parse_vstpreset(id, comp, cont)) return false;
        return vst3_apply_state(h, comp, cont);
    }
    if (const Vst3PresetAdapter* a = vst3_find_adapter(h->vendor, h->plugin_name)) {
        std::vector<uint8_t> comp;
        if (a->resolve_state(id, comp)) return vst3_apply_state(h, comp, {});
    }
    return false;
}

}  // namespace
