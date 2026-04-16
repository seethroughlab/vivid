#include "runtime/packages/project_lockfile.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
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

}  // namespace vivid
