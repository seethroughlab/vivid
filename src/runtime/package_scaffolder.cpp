#include "runtime/package_scaffolder.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace vivid {
namespace fs = std::filesystem;

static std::string replace_all(std::string s, const std::string& needle, const std::string& repl) {
    if (needle.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(needle, pos)) != std::string::npos) {
        s.replace(pos, needle.size(), repl);
        pos += repl.size();
    }
    return s;
}

static bool is_likely_text_file(const fs::path& p) {
    if (p.filename() == "CMakeLists.txt") return true;
    auto ext = p.extension().string();
    return ext == ".md" || ext == ".json" || ext == ".txt" ||
           ext == ".yml" || ext == ".yaml" || ext == ".cmake" ||
           ext == ".cpp" || ext == ".h";
}

static std::string package_name_to_project_name(const std::string& package_name) {
    std::string out = package_name;
    for (char& c : out) {
        if (c == '-' || c == '.') c = '_';
    }
    return out;
}

std::string PackageScaffolder::validate_package_name(const std::string& name) {
    if (name.empty()) return "package name cannot be empty";
    if (!std::islower(static_cast<unsigned char>(name[0]))) {
        return "package name must start with a lowercase letter";
    }
    if (name.front() == '-' || name.back() == '-') {
        return "package name cannot start or end with '-'";
    }
    if (name.find("--") != std::string::npos) {
        return "package name cannot contain consecutive '-'";
    }
    for (char c : name) {
        if (std::islower(static_cast<unsigned char>(c)) ||
            std::isdigit(static_cast<unsigned char>(c)) || c == '-') {
            continue;
        }
        return "package name must be lowercase letters, digits, and '-' only";
    }
    return {};
}

std::string PackageScaffolder::resolve_template_root(const PackageScaffoldOptions& opts) {
    if (!opts.template_root.empty() && fs::exists(opts.template_root)) {
        return fs::absolute(opts.template_root).string();
    }

    const char* env = std::getenv("VIVID_PACKAGE_TEMPLATE_DIR");
    if (env && *env && fs::exists(env)) {
        return fs::absolute(env).string();
    }

    if (!opts.source_dir.empty()) {
        fs::path sibling = fs::path(opts.source_dir).parent_path() / "vivid-package-template";
        if (fs::exists(sibling)) {
            return fs::absolute(sibling).string();
        }
    }

    fs::path cwd_sibling = fs::current_path() / ".." / "vivid-package-template";
    if (fs::exists(cwd_sibling)) {
        return fs::absolute(cwd_sibling).string();
    }

    return {};
}

PackageScaffoldResult PackageScaffolder::scaffold(const PackageScaffoldOptions& opts) {
    PackageScaffoldResult out;

    if (auto err = validate_package_name(opts.name); !err.empty()) {
        out.error = err;
        return out;
    }

    if (opts.variant != "single" && opts.variant != "multi") {
        out.error = "unknown template variant '" + opts.variant + "' (expected: single|multi)";
        return out;
    }

    std::string root = resolve_template_root(opts);
    if (root.empty()) {
        out.error = "cannot find package template root (set --template-root or VIVID_PACKAGE_TEMPLATE_DIR)";
        return out;
    }

    fs::path template_dir = fs::path(root) / (opts.variant == "single" ? "single-operator" : "multi-operator");
    if (!fs::exists(template_dir)) {
        out.error = "template variant directory not found: " + template_dir.string();
        return out;
    }

    fs::path output_parent = opts.output_dir.empty() ? fs::current_path() : fs::path(opts.output_dir);
    fs::path package_dir = fs::absolute(output_parent / opts.name);

    if (fs::exists(package_dir)) {
        if (!opts.force) {
            out.error = "destination already exists: " + package_dir.string() + " (use --force to overwrite)";
            return out;
        }
        std::error_code rm_ec;
        fs::remove_all(package_dir, rm_ec);
        if (rm_ec) {
            out.error = "failed to remove existing destination: " + rm_ec.message();
            return out;
        }
    }

    std::error_code cp_ec;
    fs::create_directories(package_dir.parent_path(), cp_ec);
    if (cp_ec) {
        out.error = "failed to create output directory: " + cp_ec.message();
        return out;
    }

    fs::copy(template_dir, package_dir,
             fs::copy_options::recursive | fs::copy_options::copy_symlinks,
             cp_ec);
    if (cp_ec) {
        out.error = "failed to copy template files: " + cp_ec.message();
        return out;
    }

    // Strip symlinks that point outside the package directory to prevent
    // template-based path traversal attacks.
    {
        std::error_code sym_ec;
        auto canonical_pkg = fs::weakly_canonical(package_dir, sym_ec);
        if (!sym_ec) {
            for (const auto& entry : fs::recursive_directory_iterator(package_dir, sym_ec)) {
                if (sym_ec) break;
                if (!entry.is_symlink()) continue;
                auto resolved = fs::weakly_canonical(entry.path(), sym_ec);
                if (sym_ec || resolved.string().find(canonical_pkg.string()) != 0) {
                    fs::remove(entry.path(), sym_ec);
                }
                sym_ec.clear();
            }
        }
    }

    const std::string project_name = package_name_to_project_name(opts.name);

    std::error_code walk_ec;
    for (const auto& entry : fs::recursive_directory_iterator(package_dir, walk_ec)) {
        if (walk_ec) {
            out.error = "failed to scan generated package: " + walk_ec.message();
            return out;
        }
        if (!entry.is_regular_file()) continue;
        if (!is_likely_text_file(entry.path())) continue;

        std::ifstream ifs(entry.path(), std::ios::binary);
        if (!ifs) continue;
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();

        if (content.find('\0') != std::string::npos) continue;

        std::string patched = content;
        patched = replace_all(patched, "your-package-name", opts.name);
        patched = replace_all(patched, "your_package_name", project_name);

        if (patched != content) {
            std::ofstream ofs(entry.path(), std::ios::binary | std::ios::trunc);
            if (!ofs) {
                out.error = "failed to write patched file: " + entry.path().string();
                return out;
            }
            ofs << patched;
        }
    }

    out.success = true;
    out.template_dir = template_dir.string();
    out.package_dir = package_dir.string();
    return out;
}

} // namespace vivid
