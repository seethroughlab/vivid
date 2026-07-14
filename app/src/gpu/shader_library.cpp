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

int ShaderLibrary::scan_dir(const std::string& dir, const std::string& tier, OpRegistry& reg) {
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return 0;

    // Sorted, so the catalog's order is the same on every machine and every launch.
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        ShaderDialect d;
        if (e.is_regular_file(ec) && is_shader_file(e.path(), d)) files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());

    int registered = 0;
    for (const fs::path& p : files) {
        ShaderDialect dialect = ShaderDialect::Wgsl;
        is_shader_file(p, dialect);

        ShaderLibraryEntry entry;
        entry.path = p.string();
        entry.tier = tier;

        const std::string src = read_file(p);
        if (src.empty()) {
            entry.error = "could not read the file (or it is empty)";
            entries_.push_back(std::move(entry));
            continue;
        }

        auto def = std::make_shared<ShaderDef>();
        def->meta = parse_shader(src, dialect);
        def->path = entry.path;
        def->tier = tier;

        entry.name = def->meta.name;
        entry.summary = def->meta.summary;

        // A malformed shader yields a row WITH an error, never a vanished row: the catalog
        // must not lie about what is in the folder. It is simply not registered as a type —
        // we do not even reliably know what to call it.
        if (!def->meta.error.empty()) {
            entry.error = def->meta.error;
            std::fprintf(stderr, "[vivid] shader '%s': %s\n",
                         p.filename().string().c_str(), entry.error.c_str());
            entries_.push_back(std::move(entry));
            continue;
        }

        if (reg.has(def->meta.name)) {
            // Shadowed — by a higher-precedence tier, or by a compiled operator of the same
            // name. Say so rather than dropping it silently.
            entry.error = "shadowed by an already-registered operator named '" + def->meta.name + "'";
            entries_.push_back(std::move(entry));
            continue;
        }

        def->finalize();

        OpMeta meta;
        meta.display_name = def->meta.name;
        meta.summary = def->meta.summary;
        meta.keywords = def->meta.keywords;
        meta.keywords.push_back("shader");   // so the Tab chooser can find every shader by one word
        meta.has = true;

        // The factory captures the shared def: every instance of this type shares one parsed
        // header, one uniform layout, and one set of stable param-name strings.
        std::shared_ptr<const ShaderDef> shared = def;
        reg.register_type(def->meta.name,
                          [shared] { return std::unique_ptr<OperatorBase>(new ShaderFileOp(shared)); },
                          std::move(meta));

        entry.registered = true;
        entries_.push_back(std::move(entry));
        defs_.push_back(std::move(def));
        ++registered;
    }
    return registered;
}

int ShaderLibrary::scan(OpRegistry& reg, const std::string& project_dir) {
    entries_.clear();
    int n = 0;
    for (const auto& [dir, tier] : shader_search_path(project_dir))
        n += scan_dir(dir, tier, reg);
    return n;
}

}  // namespace vivid
