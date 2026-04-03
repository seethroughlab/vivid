#pragma once

#include <string>

namespace vivid {

struct PackageScaffoldOptions {
    std::string name;                 // package directory + manifest name
    std::string variant = "single";   // single|multi
    std::string output_dir;           // parent directory for generated package
    std::string template_root;        // optional explicit template repo root
    std::string source_dir;           // optional vivid source dir (for sibling lookup)
    bool force = false;               // overwrite destination if it exists
};

struct PackageScaffoldResult {
    bool success = false;
    std::string error;
    std::string template_dir;
    std::string package_dir;
};

class PackageScaffolder {
public:
    static std::string validate_package_name(const std::string& name);
    static PackageScaffoldResult scaffold(const PackageScaffoldOptions& opts);

private:
    static std::string resolve_template_root(const PackageScaffoldOptions& opts);
};

} // namespace vivid
