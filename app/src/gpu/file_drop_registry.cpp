#include "gpu/file_drop_registry.h"

#include "gpu/operator_loader.h"
#include "operator_api/types.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace vivid {

std::string file_ext_lower(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();  // includes the dot, or ""
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

void FileDropRegistry::rebuild(const std::vector<std::unique_ptr<OperatorLoader>>& loaders) {
    by_ext_.clear();
    exts_by_op_.clear();
    for (const auto& lp : loaders) {
        if (!lp) continue;
        const VividOperatorDescriptor* d = lp->descriptor();
        if (!d || !d->name) continue;
        const std::string op_type = d->name;

        uint32_t n = 0;
        const VividFileDropHandlerDescriptor* h = lp->file_drop_handlers(&n);
        for (uint32_t i = 0; i < n; ++i) {
            const auto& hd = h[i];
            FileDropMatch m;
            m.op_type    = op_type;
            m.file_param = hd.file_param ? hd.file_param : "";
            m.label      = hd.label ? hd.label : op_type;
            m.priority   = hd.priority;
            for (uint32_t e = 0; e < hd.extension_count; ++e) {
                if (!hd.extensions || !hd.extensions[e]) continue;
                std::string ext = hd.extensions[e];
                if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
                for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (!ext.empty()) { by_ext_[ext].push_back(m); exts_by_op_[op_type].push_back(ext); }
            }
        }
    }
    // Sort each extension's matches by priority, highest first; std::stable_sort keeps the load
    // order for ties so the result is deterministic across runs.
    for (auto& [ext, v] : by_ext_)
        std::stable_sort(v.begin(), v.end(),
                         [](const FileDropMatch& a, const FileDropMatch& b) { return a.priority > b.priority; });
}

std::vector<FileDropMatch> FileDropRegistry::matches_for_path(const std::string& path) const {
    const std::string ext = file_ext_lower(path);
    if (ext.empty()) return {};
    auto it = by_ext_.find(ext);
    return it == by_ext_.end() ? std::vector<FileDropMatch>{} : it->second;
}

std::vector<std::string> FileDropRegistry::extensions_for_op(const std::string& op_type) const {
    auto it = exts_by_op_.find(op_type);
    return it == exts_by_op_.end() ? std::vector<std::string>{} : it->second;
}

}  // namespace vivid
