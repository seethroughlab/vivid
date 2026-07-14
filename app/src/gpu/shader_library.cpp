#include "gpu/shader_library.h"

#include "gpu/op_runtime.h"
#include "gpu/shader_file_op.h"
#include "platform/platform.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace vivid {
namespace fs = std::filesystem;

namespace {

// The watcher's target for a DIRECTORY watch — a directory's mtime advances when a file is
// added or removed, which is how a shader dropped into the folder appears without a restart.
const char* kDirPrefix = "\x01dir:";

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool is_shader_file(const fs::path& p, ShaderDialect& d) {
    const std::string ext = p.extension().string();
    if (ext == ".wgsl") { d = ShaderDialect::Wgsl; return true; }
    if (ext == ".glsl" || ext == ".frag") { d = ShaderDialect::Glsl; return true; }
    return false;
}

std::shared_ptr<ShaderDef> parse_def(const std::string& path, const std::string& tier,
                                     std::string& error) {
    ShaderDialect dialect = ShaderDialect::Wgsl;
    is_shader_file(path, dialect);

    const std::string src = read_file(path);
    if (src.empty()) { error = "could not read the file (or it is empty)"; return nullptr; }

    auto def = std::make_shared<ShaderDef>();
    def->meta = parse_shader(src, dialect);
    def->path = path;
    def->tier = tier;
    if (!def->meta.error.empty()) { error = def->meta.error; return nullptr; }
    def->finalize();
    return def;
}

OpMeta op_meta_for(const ShaderDef& def) {
    OpMeta m;
    m.display_name = def.meta.name;
    m.summary = def.meta.summary;
    m.keywords = def.meta.keywords;
    m.keywords.push_back("shader");   // so the chooser finds every shader by one word
    m.has = true;
    return m;
}

}  // namespace

std::vector<std::pair<std::string, std::string>> shader_search_path(const std::string& project_dir) {
    std::vector<std::pair<std::string, std::string>> dirs;

    // user (highest precedence, so a fork shadows the shipped original)
    if (const char* env = std::getenv("VIVID_SHADERS_DIR")) {
        dirs.emplace_back(env, "user");
    } else {
        fs::path d = fs::path(platform::user_data_dir()) / "shaders";
        std::error_code ec;
        fs::create_directories(d, ec);   // exists from first launch, so it is a place to drop a file into
        dirs.emplace_back(d.string(), "user");
    }

    if (!project_dir.empty())
        dirs.emplace_back((fs::path(project_dir) / "shaders").string(), "project");

    const std::string exe = platform::executable_path();
    if (!exe.empty()) {
        const fs::path exe_dir = fs::path(exe).parent_path();
        dirs.emplace_back((exe_dir / ".." / "Resources" / "shaders").lexically_normal().string(), "bundled");
        dirs.emplace_back((exe_dir / "shaders").lexically_normal().string(), "bundled");
    }
    return dirs;
}

ShaderLibraryEntry* ShaderLibrary::find_by_path(const std::string& path) {
    for (auto& e : entries_) if (e.path == path) return &e;
    return nullptr;
}

const ShaderLibraryEntry* ShaderLibrary::find(const std::string& op_name) const {
    for (const auto& e : entries_)
        if (e.registered && e.name == op_name) return &e;
    return nullptr;
}

bool ShaderLibrary::add_file(const std::string& path, const std::string& tier, OpRegistry& reg) {
    ShaderLibraryEntry entry;
    entry.path = path;
    entry.tier = tier;

    // Every file is watched, registered or not: fixing a malformed header should just make the
    // shader appear, with no restart and no rescan command.
    watcher_.watch(path, path);

    std::string error;
    std::shared_ptr<ShaderDef> def = parse_def(path, tier, error);
    if (!def) {
        // A malformed shader yields a row WITH an error, never a vanished row: the catalog must
        // not lie about what is in the folder.
        entry.error = error;
        std::fprintf(stderr, "[vivid] shader '%s': %s\n",
                     fs::path(path).filename().string().c_str(), error.c_str());
        entries_.push_back(std::move(entry));
        return false;
    }

    entry.name = def->meta.name;
    entry.summary = def->meta.summary;

    if (reg.has(def->meta.name)) {
        // Loudly. A stale compiled operator left over from an older build silently winning the
        // name of a shipped shader is exactly how a migration ends up "not taking" — the user
        // keeps the old behaviour and nothing anywhere says why.
        entry.error = "shadowed by an already-registered operator named '" + def->meta.name + "'";
        std::fprintf(stderr, "[vivid] shader '%s' (%s) is SHADOWED by an already-registered "
                     "operator of the same name — the file is not in use\n",
                     def->meta.name.c_str(), fs::path(path).filename().string().c_str());
        entries_.push_back(std::move(entry));
        return false;
    }

    // The factory captures the SLOT, not the def — so a reload swaps the def underneath every
    // live node and every future instance, with no re-registration.
    auto slot = std::make_shared<ShaderSlot>();
    slot->def = def;
    reg.register_type(def->meta.name,
                      [slot] { return std::unique_ptr<OperatorBase>(new ShaderFileOp(slot)); },
                      op_meta_for(*def));

    entry.slot = slot;
    entry.registered = true;
    entries_.push_back(std::move(entry));
    defs_.push_back(std::move(def));   // never freed: raw const char* point into it (see defs_)
    return true;
}

int ShaderLibrary::scan(OpRegistry& reg, const std::string& project_dir) {
    entries_.clear();     // defs_ is deliberately NOT cleared — see its declaration
    watcher_.clear();
    project_dir_ = project_dir;
    return rescan(reg);
}

int ShaderLibrary::rescan(OpRegistry& reg) {
    int registered = 0;
    for (const auto& [dir, tier] : shader_search_path(project_dir_)) {
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec)) continue;
        watcher_.watch(dir, kDirPrefix + dir);   // a new file in the folder bumps the dir's mtime

        // Sorted, so the catalog reads the same on every machine and every launch.
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            ShaderDialect d;
            if (e.is_regular_file(ec) && is_shader_file(e.path(), d)) files.push_back(e.path());
        }
        std::sort(files.begin(), files.end());

        for (const fs::path& p : files) {
            if (find_by_path(p.string())) continue;      // already known (this is a rescan)
            if (add_file(p.string(), tier, reg)) ++registered;
        }
    }
    return registered;
}

std::vector<ShaderReload> ShaderLibrary::poll(OpRegistry& reg) {
    std::vector<ShaderReload> out;

    for (const std::string& target : watcher_.poll_changes()) {
        // A directory changed: a file was added (or removed). Pick up whatever is new.
        if (target.rfind(kDirPrefix, 0) == 0) {
            const size_t before = entries_.size();
            rescan(reg);
            for (size_t i = before; i < entries_.size(); ++i)
                if (entries_[i].registered)
                    out.push_back({entries_[i].name, entries_[i].path, ShaderChange::Added, {}});
            continue;
        }

        ShaderLibraryEntry* e = find_by_path(target);
        if (!e) continue;

        std::string error;
        std::shared_ptr<ShaderDef> next = parse_def(e->path, e->tier, error);
        if (!next) {
            // The edit does not parse. Say so, and leave the last good version running.
            e->error = error;
            std::fprintf(stderr, "[vivid] shader '%s': %s — keeping the last good version\n",
                         fs::path(e->path).filename().string().c_str(), error.c_str());
            out.push_back({e->name, e->path, ShaderChange::Failed, error});
            continue;
        }

        // It was not registered before (malformed, or its name was taken) — try again now.
        if (!e->registered) {
            const std::string path = e->path, tier = e->tier;
            entries_.erase(entries_.begin() + (e - entries_.data()));
            const bool ok = add_file(path, tier, reg);
            ShaderLibraryEntry* re = find_by_path(path);
            out.push_back({re ? re->name : std::string(), path,
                           ok ? ShaderChange::Added : ShaderChange::Failed,
                           re ? re->error : std::string()});
            continue;
        }

        // Renaming the type would orphan every node, wire and mapping that points at the old
        // name. Refuse it while running rather than doing that silently.
        if (next->meta.name != e->name) {
            const std::string msg = "renaming a live shader ('" + e->name + "' -> '" +
                                    next->meta.name + "') needs a restart; still running as '" +
                                    e->name + "'";
            e->error = msg;
            std::fprintf(stderr, "[vivid] shader: %s\n", msg.c_str());
            out.push_back({e->name, e->path, ShaderChange::Failed, msg});
            continue;
        }

        const bool body_only = next->same_interface(*e->slot->def);
        defs_.push_back(next);   // BEFORE the swap: the superseded def is still pointed into (see defs_)
        e->slot->def = next;
        ++e->slot->generation;   // every live node picks the new version up on its next frame
        e->error.clear();
        e->summary = next->meta.summary;

        if (body_only) {
            out.push_back({e->name, e->path, ShaderChange::Body, {}});
        } else {
            // Params/ports changed: the cached descriptor is stale, and the live nodes' ParamBase
            // storage no longer matches. The caller rebuilds those nodes (preserving values by name).
            reg.invalidate_descriptor(e->name);
            out.push_back({e->name, e->path, ShaderChange::Interface, {}});
        }
    }
    return out;
}

std::string ShaderLibrary::fork(const std::string& op_name, const std::string& new_name,
                                OpRegistry& reg, std::string& error) {
    const ShaderLibraryEntry* src = find(op_name);
    if (!src) { error = "'" + op_name + "' is not a shader in the library"; return {}; }
    if (new_name.empty()) { error = "a fork needs a new name"; return {}; }
    if (reg.has(new_name)) { error = "an operator named '" + new_name + "' already exists"; return {}; }

    const auto dirs = shader_search_path(project_dir_);
    if (dirs.empty()) { error = "no user shader directory"; return {}; }
    const fs::path user_dir = dirs.front().first;   // the user tier is first (highest precedence)

    const std::string source = read_file(src->path);
    std::string forked;
    if (source.empty() || !set_shader_name(source, new_name, forked)) {
        error = "could not rewrite the shader's header";
        return {};
    }

    std::error_code ec;
    fs::create_directories(user_dir, ec);
    fs::path out = user_dir / (new_name + fs::path(src->path).extension().string());
    if (fs::exists(out, ec)) { error = "a shader file already exists at " + out.string(); return {}; }

    std::ofstream o(out, std::ios::binary);
    if (!o) { error = "could not write " + out.string(); return {}; }
    o << forked;
    o.close();

    if (!add_file(out.string(), "user", reg)) {
        const ShaderLibraryEntry* e = find_by_path(out.string());
        error = e && !e->error.empty() ? e->error : "the fork did not register";
        return {};
    }
    return out.string();
}

}  // namespace vivid
