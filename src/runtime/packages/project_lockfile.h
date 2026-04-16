#pragma once

#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

namespace vivid {

inline constexpr int LOCKFILE_VERSION = 1;

struct LockfileGraphRef {
    std::string path;
    int schema_version = 0;
    std::string content_hash;
};

inline bool operator==(const LockfileGraphRef& a, const LockfileGraphRef& b) {
    return std::tie(a.path, a.schema_version, a.content_hash)
        == std::tie(b.path, b.schema_version, b.content_hash);
}

struct LockfileCore {
    std::string version;
    std::string commit;
    int operator_abi = 0;
};

inline bool operator==(const LockfileCore& a, const LockfileCore& b) {
    return std::tie(a.version, a.commit, a.operator_abi)
        == std::tie(b.version, b.commit, b.operator_abi);
}

struct LockfilePackageSource {
    std::string kind;    // "git" | "local" | "registry"
    std::string url;
    std::string commit;
};

inline bool operator==(const LockfilePackageSource& a, const LockfilePackageSource& b) {
    return std::tie(a.kind, a.url, a.commit)
        == std::tie(b.kind, b.url, b.commit);
}

struct LockfilePackage {
    std::string name;
    std::string version;
    std::string vivid_core;
    LockfilePackageSource source;
    bool linked = false;
    std::string linked_path;
};

inline bool operator==(const LockfilePackage& a, const LockfilePackage& b) {
    return std::tie(a.name, a.version, a.vivid_core, a.source, a.linked, a.linked_path)
        == std::tie(b.name, b.version, b.vivid_core, b.source, b.linked, b.linked_path);
}

struct LockfileOperator {
    std::string type;
    std::string package;
    std::string package_version;
    std::string descriptor_hash;
    int operator_abi = 0;
};

inline bool operator==(const LockfileOperator& a, const LockfileOperator& b) {
    return std::tie(a.type, a.package, a.package_version, a.descriptor_hash, a.operator_abi)
        == std::tie(b.type, b.package, b.package_version, b.descriptor_hash, b.operator_abi);
}

struct LockfileAsset {
    std::string asset_id;
    std::string kind;
    std::string path;
    std::string content_hash;
};

inline bool operator==(const LockfileAsset& a, const LockfileAsset& b) {
    return std::tie(a.asset_id, a.kind, a.path, a.content_hash)
        == std::tie(b.asset_id, b.kind, b.path, b.content_hash);
}

struct ProjectLockfile {
    int lockfile_version = LOCKFILE_VERSION;
    std::string generated_at;  // RFC3339 UTC
    LockfileGraphRef graph;
    LockfileCore vivid_core;
    std::vector<LockfilePackage> packages;
    std::vector<LockfileOperator> operators;
    std::vector<LockfileAsset> assets;
};

inline bool operator==(const ProjectLockfile& a, const ProjectLockfile& b) {
    return std::tie(a.lockfile_version, a.generated_at, a.graph, a.vivid_core,
                    a.packages, a.operators, a.assets)
        == std::tie(b.lockfile_version, b.generated_at, b.graph, b.vivid_core,
                    b.packages, b.operators, b.assets);
}

struct LockfileError {
    enum class Kind {
        Ok,
        IoError,
        ParseError,
        UnsupportedVersion,
        InvalidShape,
    };
    Kind kind = Kind::Ok;
    std::string message;

    bool ok() const { return kind == Kind::Ok; }
};

struct LockfileLoadResult {
    ProjectLockfile lockfile;
    LockfileError error;

    bool ok() const { return error.ok(); }
};

LockfileLoadResult load_lockfile(const std::filesystem::path& path);
LockfileError      save_lockfile(const std::filesystem::path& path,
                                 const ProjectLockfile& lockfile);

}  // namespace vivid
