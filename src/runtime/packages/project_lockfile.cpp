#include "runtime/packages/project_lockfile.h"

#include "common/hash_util.h"
#include "operator_api/types.h"
#include "runtime/assets/asset_library.h"
#include "runtime/core/workspace_manager.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/graph/graph.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/packages/package_manager.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace vivid {

namespace {

LockfileError make_error(LockfileError::Kind kind, std::string message) {
    return LockfileError{kind, std::move(message)};
}

template <typename T>
T get_or(const nlohmann::json& obj, const char* key, T fallback) {
    if (!obj.is_object()) return fallback;
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return fallback;
    try {
        return it->get<T>();
    } catch (const nlohmann::json::exception&) {
        return fallback;
    }
}

LockfileGraphRef parse_graph_ref(const nlohmann::json& obj) {
    LockfileGraphRef g;
    if (!obj.is_object()) return g;
    g.path            = get_or<std::string>(obj, "path", {});
    g.schema_version  = get_or<int>(obj, "schema_version", 0);
    g.content_hash    = get_or<std::string>(obj, "content_hash", {});
    return g;
}

LockfileCore parse_core(const nlohmann::json& obj) {
    LockfileCore c;
    if (!obj.is_object()) return c;
    c.version       = get_or<std::string>(obj, "version", {});
    c.commit        = get_or<std::string>(obj, "commit", {});
    c.operator_abi  = get_or<int>(obj, "operator_abi", 0);
    return c;
}

LockfilePackageSource parse_source(const nlohmann::json& obj) {
    LockfilePackageSource s;
    if (!obj.is_object()) return s;
    s.kind   = get_or<std::string>(obj, "kind", {});
    s.url    = get_or<std::string>(obj, "url", {});
    s.commit = get_or<std::string>(obj, "commit", {});
    return s;
}

LockfilePackage parse_package(const nlohmann::json& obj) {
    LockfilePackage p;
    if (!obj.is_object()) return p;
    p.name        = get_or<std::string>(obj, "name", {});
    p.version     = get_or<std::string>(obj, "version", {});
    p.vivid_core  = get_or<std::string>(obj, "vivid_core", {});
    auto src_it   = obj.find("source");
    if (src_it != obj.end()) p.source = parse_source(*src_it);
    p.linked      = get_or<bool>(obj, "linked", false);
    p.linked_path = get_or<std::string>(obj, "linked_path", {});
    return p;
}

LockfileOperator parse_operator(const nlohmann::json& obj) {
    LockfileOperator o;
    if (!obj.is_object()) return o;
    o.type             = get_or<std::string>(obj, "type", {});
    o.package          = get_or<std::string>(obj, "package", {});
    o.package_version  = get_or<std::string>(obj, "package_version", {});
    o.descriptor_hash  = get_or<std::string>(obj, "descriptor_hash", {});
    o.operator_abi     = get_or<int>(obj, "operator_abi", 0);
    return o;
}

LockfileAsset parse_asset(const nlohmann::json& obj) {
    LockfileAsset a;
    if (!obj.is_object()) return a;
    a.asset_id     = get_or<std::string>(obj, "asset_id", {});
    a.kind         = get_or<std::string>(obj, "kind", {});
    a.path         = get_or<std::string>(obj, "path", {});
    a.content_hash = get_or<std::string>(obj, "content_hash", {});
    return a;
}

nlohmann::ordered_json serialize_graph_ref(const LockfileGraphRef& g) {
    nlohmann::ordered_json j = nlohmann::ordered_json::object();
    j["path"]           = g.path;
    j["schema_version"] = g.schema_version;
    j["content_hash"]   = g.content_hash;
    return j;
}

nlohmann::ordered_json serialize_core(const LockfileCore& c) {
    nlohmann::ordered_json j = nlohmann::ordered_json::object();
    j["version"]      = c.version;
    j["commit"]       = c.commit;
    j["operator_abi"] = c.operator_abi;
    return j;
}

nlohmann::ordered_json serialize_source(const LockfilePackageSource& s) {
    nlohmann::ordered_json j = nlohmann::ordered_json::object();
    j["kind"]   = s.kind;
    j["url"]    = s.url;
    j["commit"] = s.commit;
    return j;
}

nlohmann::ordered_json serialize_package(const LockfilePackage& p) {
    nlohmann::ordered_json j = nlohmann::ordered_json::object();
    j["name"]        = p.name;
    j["version"]     = p.version;
    j["vivid_core"]  = p.vivid_core;
    j["source"]      = serialize_source(p.source);
    j["linked"]      = p.linked;
    j["linked_path"] = p.linked_path;
    return j;
}

nlohmann::ordered_json serialize_operator(const LockfileOperator& o) {
    nlohmann::ordered_json j = nlohmann::ordered_json::object();
    j["type"]             = o.type;
    j["package"]          = o.package;
    j["package_version"]  = o.package_version;
    j["descriptor_hash"]  = o.descriptor_hash;
    j["operator_abi"]     = o.operator_abi;
    return j;
}

nlohmann::ordered_json serialize_asset(const LockfileAsset& a) {
    nlohmann::ordered_json j = nlohmann::ordered_json::object();
    j["asset_id"]     = a.asset_id;
    j["kind"]         = a.kind;
    j["path"]         = a.path;
    j["content_hash"] = a.content_hash;
    return j;
}

}  // namespace

LockfileLoadResult load_lockfile(const std::filesystem::path& path) {
    LockfileLoadResult result;

    std::ifstream in(path);
    if (!in) {
        std::ostringstream msg;
        msg << "failed to open lockfile: " << path.string();
        result.error = make_error(LockfileError::Kind::IoError, msg.str());
        return result;
    }

    nlohmann::json root;
    try {
        in >> root;
    } catch (const nlohmann::json::parse_error& e) {
        result.error = make_error(LockfileError::Kind::ParseError, e.what());
        return result;
    }

    if (!root.is_object()) {
        result.error = make_error(LockfileError::Kind::InvalidShape,
                                  "lockfile root is not a JSON object");
        return result;
    }

    auto version_it = root.find("lockfile_version");
    if (version_it == root.end() || !version_it->is_number_integer()) {
        result.error = make_error(LockfileError::Kind::InvalidShape,
                                  "lockfile_version missing or not an integer");
        return result;
    }
    int version = version_it->get<int>();
    if (version <= 0) {
        result.error = make_error(LockfileError::Kind::InvalidShape,
                                  "lockfile_version must be positive");
        return result;
    }
    if (version > LOCKFILE_VERSION) {
        std::ostringstream msg;
        msg << "unsupported lockfile_version " << version
            << " (this build understands up to " << LOCKFILE_VERSION << ")";
        result.error = make_error(LockfileError::Kind::UnsupportedVersion, msg.str());
        return result;
    }

    ProjectLockfile lf;
    lf.lockfile_version = version;
    lf.generated_at     = get_or<std::string>(root, "generated_at", {});

    if (auto it = root.find("graph"); it != root.end())
        lf.graph = parse_graph_ref(*it);
    if (auto it = root.find("vivid_core"); it != root.end())
        lf.vivid_core = parse_core(*it);

    if (auto it = root.find("packages"); it != root.end() && it->is_array()) {
        lf.packages.reserve(it->size());
        for (const auto& item : *it) lf.packages.push_back(parse_package(item));
    }
    if (auto it = root.find("operators"); it != root.end() && it->is_array()) {
        lf.operators.reserve(it->size());
        for (const auto& item : *it) lf.operators.push_back(parse_operator(item));
    }
    if (auto it = root.find("assets"); it != root.end() && it->is_array()) {
        lf.assets.reserve(it->size());
        for (const auto& item : *it) lf.assets.push_back(parse_asset(item));
    }

    result.lockfile = std::move(lf);
    return result;
}

LockfileError save_lockfile(const std::filesystem::path& path,
                            const ProjectLockfile& lockfile) {
    nlohmann::ordered_json root = nlohmann::ordered_json::object();
    root["lockfile_version"] = lockfile.lockfile_version;
    root["generated_at"]     = lockfile.generated_at;
    root["graph"]            = serialize_graph_ref(lockfile.graph);
    root["vivid_core"]       = serialize_core(lockfile.vivid_core);

    auto packages = nlohmann::ordered_json::array();
    for (const auto& p : lockfile.packages) packages.push_back(serialize_package(p));
    root["packages"] = std::move(packages);

    auto operators_j = nlohmann::ordered_json::array();
    for (const auto& o : lockfile.operators) operators_j.push_back(serialize_operator(o));
    root["operators"] = std::move(operators_j);

    auto assets = nlohmann::ordered_json::array();
    for (const auto& a : lockfile.assets) assets.push_back(serialize_asset(a));
    root["assets"] = std::move(assets);

    std::ofstream out(path);
    if (!out) {
        std::ostringstream msg;
        msg << "failed to open lockfile for write: " << path.string();
        return make_error(LockfileError::Kind::IoError, msg.str());
    }
    out << root.dump(2) << '\n';
    if (!out) {
        std::ostringstream msg;
        msg << "failed to write lockfile: " << path.string();
        return make_error(LockfileError::Kind::IoError, msg.str());
    }
    return LockfileError{};
}

// --- Phase 2 generation --------------------------------------------------

namespace {

std::string rfc3339_utc_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%FT%TZ", &tm);
    return buf;
}

}  // namespace

std::string canonicalize_graph_hash(const Graph& graph) {
    std::string json;
    graph.save_to_string(json);  // deterministic via nlohmann::ordered_json
    return "sha256:" + sha256_hex(json);
}

ProjectLockfile build_lockfile_for_graph(const Graph& graph,
                                         PackageManager& package_manager,
                                         const OperatorRegistry& operator_registry) {
    ProjectLockfile lf;
    lf.lockfile_version      = LOCKFILE_VERSION;
    lf.generated_at          = rfc3339_utc_now();
    lf.graph.path            = graph.source_path();
    lf.graph.schema_version  = GRAPH_SCHEMA_VERSION;
    lf.graph.content_hash    = canonicalize_graph_hash(graph);

    lf.vivid_core.version       = VIVID_CORE_VERSION;
#ifdef VIVID_CORE_COMMIT
    lf.vivid_core.commit        = VIVID_CORE_COMMIT;
#else
    lf.vivid_core.commit        = "";
#endif
    lf.vivid_core.operator_abi  = static_cast<int>(VIVID_OPERATOR_ABI_VERSION);

    // Index operator_map once for O(1) type_name lookup.
    auto op_entries = operator_registry.operator_map();
    std::unordered_map<std::string, const OperatorMapEntry*> op_by_type;
    op_by_type.reserve(op_entries.size());
    for (const auto& e : op_entries) op_by_type[e.type_name] = &e;

    // Walk nodes: collect unique operator types and their owning package names.
    std::set<std::string> seen_types;
    std::set<std::string> pkg_names;
    for (const auto& node : graph.nodes()) {
        seen_types.insert(node.type);
        if (const std::string* pkg = operator_registry.package_for_type(node.type)) {
            if (!pkg->empty()) pkg_names.insert(*pkg);
        } else if (!node.pkg_name.empty()) {
            // Fallback: on-disk provenance from a previously saved graph.
            pkg_names.insert(node.pkg_name);
        }
    }

    // Index the installed-package list by name.
    auto installed = package_manager.list();
    std::unordered_map<std::string, const PackageInfo*> pkg_by_name;
    pkg_by_name.reserve(installed.size());
    for (const auto& info : installed) pkg_by_name[info.name] = &info;

    // Populate packages[] — std::set iteration is sorted, giving deterministic order.
    for (const auto& name : pkg_names) {
        auto it = pkg_by_name.find(name);
        if (it == pkg_by_name.end()) continue;  // unresolved; Phase 3 verify() classifies
        const PackageInfo& info = *it->second;

        LockfilePackage p;
        p.name        = info.name;
        p.version     = info.version;
        p.vivid_core  = info.vivid_core;
        p.linked      = info.linked;
        p.linked_path = info.linked ? info.path : "";
        // source.kind: linked packages are "local" (regardless of whether the
        // linked tree has a git repo); non-linked installs are "git" if we
        // captured a URL, otherwise "local" (installed from a bare local copy).
        p.source.kind = info.linked
            ? "local"
            : (!info.source_url.empty() ? "git" : "local");
        p.source.url    = info.source_url;
        p.source.commit = info.git_commit;
        lf.packages.push_back(std::move(p));
    }

    // Populate operators[] — sorted by type name.
    for (const auto& type_name : seen_types) {
        LockfileOperator o;
        o.type = type_name;

        auto map_it = op_by_type.find(type_name);
        if (map_it != op_by_type.end()) {
            o.package      = map_it->second->package_name;
            o.operator_abi = static_cast<int>(map_it->second->abi_version);
            if (!o.package.empty()) {
                auto pi = pkg_by_name.find(o.package);
                if (pi != pkg_by_name.end()) o.package_version = pi->second->version;
            }
        }
        o.descriptor_hash = operator_registry.descriptor_hash(type_name);
        lf.operators.push_back(std::move(o));
    }

    // Phase 8: enumerate assets referenced by graph nodes via params whose
    // VividParamDescriptor.asset_kind is non-null. Skipped when
    // PackageManager has no AssetLibrary wired (backward compat).
    if (AssetLibrary* al = package_manager.asset_library()) {
        const std::filesystem::path graph_dir =
            std::filesystem::path(graph.source_path()).parent_path();

        std::set<std::string> seen_asset_paths;

        for (const auto& node : graph.nodes()) {
            const VividOperatorDescriptor* desc =
                operator_registry.probe_descriptor(node.type);
            if (!desc) continue;
            for (uint32_t i = 0; i < desc->param_count; ++i) {
                const VividParamDescriptor& p = desc->params[i];
                if (!p.asset_kind || !*p.asset_kind) continue;
                const std::string pname = p.name ? p.name : "";
                auto it = node.string_params.find(pname);
                if (it == node.string_params.end() || it->second.empty()) continue;
                const std::string& raw = it->second;

                std::filesystem::path path;
                std::string asset_id;

                // Try AssetLibrary resolution first — the raw value might be
                // an asset_id. Fall through to direct filesystem path.
                if (const AssetEntry* entry = al->find(raw)) {
                    path     = entry->canonical_path;
                    asset_id = entry->asset_id;
                } else {
                    std::filesystem::path candidate = raw;
                    if (candidate.is_relative() && !graph_dir.empty()) {
                        candidate = graph_dir / candidate;
                    }
                    path = candidate.lexically_normal();
                }
                if (path.empty()) continue;

                const std::string canonical = path.string();
                if (!seen_asset_paths.insert(canonical).second) continue;

                LockfileAsset la;
                la.asset_id = asset_id;
                la.kind     = p.asset_kind;
                la.path     = canonical;
                std::error_code ec;
                if (std::filesystem::exists(path, ec) && !ec) {
                    const std::string hex = sha256_file(path);
                    if (!hex.empty()) la.content_hash = "sha256:" + hex;
                }
                lf.assets.push_back(std::move(la));
            }
        }

        std::sort(lf.assets.begin(), lf.assets.end(),
                  [](const LockfileAsset& a, const LockfileAsset& b) {
                      return a.path < b.path;
                  });
    }

    return lf;
}

// --- Phase 3 verification helpers ----------------------------------------

namespace {

const char* to_string(LockfileOverall o) {
    switch (o) {
        case LockfileOverall::Match:           return "match";
        case LockfileOverall::CompatibleDrift: return "compatible_drift";
        case LockfileOverall::Mismatch:        return "mismatch";
        case LockfileOverall::NoLockfile:      return "no_lockfile";
    }
    return "match";
}

const char* to_string(LockfileSeverity s) {
    switch (s) {
        case LockfileSeverity::Info:     return "info";
        case LockfileSeverity::Warning:  return "warning";
        case LockfileSeverity::Critical: return "critical";
    }
    return "info";
}

}  // namespace

LockfileStatus verify_lockfile(const ProjectLockfile& lockfile,
                               const Graph& graph,
                               PackageManager& package_manager,
                               const OperatorRegistry& operator_registry) {
    using namespace lockfile_finding;
    LockfileStatus status;

    auto add = [&](const char* id, LockfileSeverity sev, std::string subject,
                   std::string message, std::string suggestion) {
        status.findings.push_back(LockfileFinding{
            std::string(id), sev, std::move(subject),
            std::move(message), std::move(suggestion)});
    };

    // --- Core version / ABI ---
    if (!lockfile.vivid_core.version.empty() &&
        lockfile.vivid_core.version != VIVID_CORE_VERSION) {
        add(kVividCoreVersionMismatch, LockfileSeverity::Warning, "vivid_core",
            "locked " + lockfile.vivid_core.version +
                " vs installed " + std::string(VIVID_CORE_VERSION),
            "re-lock against this core or install the locked core version");
    }
    if (lockfile.vivid_core.operator_abi != 0 &&
        lockfile.vivid_core.operator_abi !=
            static_cast<int>(VIVID_OPERATOR_ABI_VERSION)) {
        add(kAbiMismatch, LockfileSeverity::Critical, "vivid_core",
            "locked ABI " + std::to_string(lockfile.vivid_core.operator_abi) +
                " vs runtime ABI " +
                std::to_string(VIVID_OPERATOR_ABI_VERSION),
            "operator dylibs must be rebuilt against this core");
    }

    // --- Graph content ---
    if (!lockfile.graph.content_hash.empty()) {
        const std::string actual = canonicalize_graph_hash(graph);
        if (actual != lockfile.graph.content_hash) {
            add(kGraphContentDrift, LockfileSeverity::Info,
                lockfile.graph.path.empty() ? "graph" : lockfile.graph.path,
                "graph content changed since lockfile was written",
                "re-run write_project_lockfile to refresh graph.content_hash");
        }
    }

    // --- Packages ---
    auto installed = package_manager.list();
    std::unordered_map<std::string, const PackageInfo*> installed_by_name;
    installed_by_name.reserve(installed.size());
    for (const auto& info : installed) installed_by_name[info.name] = &info;

    for (const auto& p : lockfile.packages) {
        auto it = installed_by_name.find(p.name);
        if (it == installed_by_name.end()) {
            add(kMissingPackage, LockfileSeverity::Critical, p.name,
                "package not installed (locked " + p.version + ")",
                "install " + p.name + "@" + p.version);
            continue;
        }
        const PackageInfo& info = *it->second;
        if (info.version == p.version) continue;

        const PackageUpdateClass cls =
            PackageManager::classify_version_delta(p.version, info.version);
        std::string delta =
            "locked " + p.version + " vs installed " + info.version;
        switch (cls) {
            case PackageUpdateClass::CompatibleUpdate:
            case PackageUpdateClass::RemoteOlderOrEqual:
                add(kCompatibleUpdate, LockfileSeverity::Info, p.name,
                    std::move(delta), "");
                break;
            case PackageUpdateClass::IncompatibleUpdate:
                add(kIncompatibleUpdate, LockfileSeverity::Critical, p.name,
                    std::move(delta),
                    "reinstall " + p.name + "@" + p.version);
                break;
            case PackageUpdateClass::InvalidVersionData:
                add(kIncompatibleUpdate, LockfileSeverity::Warning, p.name,
                    "cannot compare versions (invalid semver): " + delta, "");
                break;
            case PackageUpdateClass::UpToDate:
                break;  // shouldn't happen given the != check above
        }
        // linked_unpinned: linked package with no commit (non-git) or dirty worktree.
        if (info.linked && (info.git_commit.empty() || info.dirty)) {
            add(kLinkedUnpinned, LockfileSeverity::Warning, p.name,
                info.git_commit.empty()
                    ? "linked from a non-git path (no commit to pin)"
                    : "linked worktree has uncommitted changes",
                "commit changes or install from a stable source");
        }
    }

    // --- Operators ---
    auto op_entries = operator_registry.operator_map();
    std::unordered_map<std::string, const OperatorMapEntry*> op_by_type;
    op_by_type.reserve(op_entries.size());
    for (const auto& e : op_entries) op_by_type[e.type_name] = &e;

    for (const auto& o : lockfile.operators) {
        auto it = op_by_type.find(o.type);
        if (it == op_by_type.end()) {
            add(kMissingOperator, LockfileSeverity::Critical, o.type,
                "operator type not available in registry",
                o.package.empty() ? "verify this is a core type"
                                  : "install or rebuild " + o.package);
            continue;
        }
        const OperatorMapEntry& entry = *it->second;
        if (o.operator_abi != 0 &&
            entry.abi_version != 0 &&
            entry.abi_version != static_cast<uint32_t>(o.operator_abi)) {
            add(kAbiMismatch, LockfileSeverity::Critical, o.type,
                "locked ABI " + std::to_string(o.operator_abi) +
                    " vs runtime ABI " + std::to_string(entry.abi_version),
                entry.package_name.empty() ? std::string("rebuild core")
                                           : "rebuild " + entry.package_name);
        }
        // descriptor_hash_mismatch: operator's descriptor fingerprint drifted
        // since the lockfile was written. Only emit when both sides have a
        // hash (old lockfiles without descriptor_hash skip this check).
        if (!o.descriptor_hash.empty()) {
            const std::string current =
                operator_registry.descriptor_hash(o.type);
            if (!current.empty() && current != o.descriptor_hash) {
                add(kDescriptorHashMismatch, LockfileSeverity::Critical, o.type,
                    "operator descriptor changed since lockfile was written",
                    entry.package_name.empty()
                        ? std::string("rebuild core")
                        : "rebuild " + entry.package_name);
            }
        }
    }

    // --- Assets (Phase 8) ---
    // Iterate every locked asset, resolve to a live file path (preferring
    // AssetLibrary when asset_id is set), then emit kAssetMissing or
    // kAssetChanged based on existence + content_hash comparison.
    AssetLibrary* asset_library_ptr = package_manager.asset_library();
    for (const auto& a : lockfile.assets) {
        std::filesystem::path path = a.path;
        if (!a.asset_id.empty() && asset_library_ptr) {
            if (const AssetEntry* entry = asset_library_ptr->find(a.asset_id)) {
                path = entry->canonical_path;
            }
        }

        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec) {
            add(kAssetMissing, LockfileSeverity::Critical, a.path,
                "asset file is no longer reachable",
                "restore " + a.path + " or re-lock");
            continue;
        }

        // Empty content_hash = pre-Phase-8 lockfile; skip the drift check so
        // old lockfiles verify cleanly (same backward-compat pattern used
        // for operator.descriptor_hash).
        if (a.content_hash.empty()) continue;

        const std::string current_hex = sha256_file(path);
        if (current_hex.empty()) continue;  // unreadable mid-verify; not our call
        const std::string current = "sha256:" + current_hex;
        if (current != a.content_hash) {
            add(kAssetChanged, LockfileSeverity::Warning, a.path,
                "asset content changed since lockfile was written",
                "re-lock or restore the original " + a.path);
        }
    }

    // --- Overall = worst severity seen ---
    for (const auto& f : status.findings) {
        if (f.severity == LockfileSeverity::Critical) {
            status.overall = LockfileOverall::Mismatch;
            break;
        }
    }
    if (status.overall != LockfileOverall::Mismatch) {
        for (const auto& f : status.findings) {
            if (f.severity == LockfileSeverity::Warning ||
                f.severity == LockfileSeverity::Info) {
                status.overall = LockfileOverall::CompatibleDrift;
                break;
            }
        }
    }
    return status;
}

LockfileLoadMode parse_lockfile_load_mode(const std::string& s) {
    if (s == "strict")   return LockfileLoadMode::Strict;
    if (s == "recovery") return LockfileLoadMode::Recovery;
    return LockfileLoadMode::Studio;
}

void apply_strict_mode_to_compiled_graph(const LockfileStatus& status,
                                         CompiledGraph& compiled,
                                         const OperatorRegistry& registry) {
    using namespace lockfile_finding;

    auto mark_node = [](CompiledNode& cn, const std::string& detail) {
        cn.missing_operator        = true;
        cn.missing_operator_reason = "locked_unavailable";
        if (cn.missing_operator_detail.empty()) cn.missing_operator_detail = detail;
        // This path runs post-compile (the compiler never saw a missing op), so
        // set the UI message here too (audit 01-R2-F3) — the snapshot builder now
        // copies it verbatim. Surfaces the lockfile reason instead of a generic
        // "not found" string.
        cn.missing_operator_ui_message = cn.missing_operator_detail;
    };

    auto disable_nodes_in_package = [&](const std::string& pkg_name,
                                        const std::string& detail) {
        if (pkg_name.empty()) return;
        for (auto& cn : compiled.nodes) {
            const std::string* pkg = registry.package_for_type(cn.type_name);
            if (pkg && *pkg == pkg_name) mark_node(cn, detail);
        }
    };
    auto disable_nodes_of_type = [&](const std::string& type_name,
                                     const std::string& detail) {
        if (type_name.empty()) return;
        for (auto& cn : compiled.nodes) {
            if (cn.type_name == type_name) mark_node(cn, detail);
        }
    };

    for (const auto& f : status.findings) {
        if (f.severity != LockfileSeverity::Critical) continue;

        if (f.id == kLockfileUnreadable) {
            // Environment is unverifiable. Lock down every node so strict
            // mode cannot be bypassed by a corrupt or malformed vivid.lock.
            for (auto& cn : compiled.nodes) mark_node(cn, f.message);
            return;
        }
        if (f.id == kMissingPackage || f.id == kIncompatibleUpdate) {
            disable_nodes_in_package(f.subject, f.message);
        } else if (f.id == kAbiMismatch || f.id == kDescriptorHashMismatch) {
            // "vivid_core" subject affects the whole runtime; leave that to
            // the caller (e.g. don't start audio/GPU). Per-type subjects are
            // disabled here.
            if (f.subject != "vivid_core") {
                disable_nodes_of_type(f.subject, f.message);
            }
        }
        // kMissingOperator: the compiler already emits "not_found" for
        // unresolved types; no additional action needed here.
    }
}

const char* to_string(LockfileLoadMode mode) {
    switch (mode) {
        case LockfileLoadMode::Studio:   return "studio";
        case LockfileLoadMode::Strict:   return "strict";
        case LockfileLoadMode::Recovery: return "recovery";
    }
    return "studio";
}

std::string lockfile_status_to_json(const LockfileStatus& status, int indent) {
    nlohmann::ordered_json root = nlohmann::ordered_json::object();
    root["overall"] = to_string(status.overall);

    auto findings = nlohmann::ordered_json::array();
    for (const auto& f : status.findings) {
        nlohmann::ordered_json j = nlohmann::ordered_json::object();
        j["id"]         = f.id;
        j["severity"]   = to_string(f.severity);
        j["subject"]    = f.subject;
        j["message"]    = f.message;
        j["suggestion"] = f.suggestion;
        findings.push_back(std::move(j));
    }
    root["findings"] = std::move(findings);
    return root.dump(indent);
}

}  // namespace vivid
