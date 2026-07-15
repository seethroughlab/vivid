#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ADR-0021/P3 — the host consumer of the operator file-drop ABI. Operators declare, via the
// VIVID_FILE_DROP macro, which file extensions they handle and which FILE param the dropped path
// fills; this registry indexes those declarations by extension so a drop on the graph can offer
// the ops that accept the file. The ABI (VividFileDropHandlerDescriptor) already existed with no
// consumer — this is it.
namespace vivid {

class OperatorLoader;

struct FileDropMatch {
    std::string op_type;     // the operator to create
    std::string file_param;  // the FILE param on it to fill with the dropped path
    std::string label;       // human label (for the chooser when several ops match)
    int         priority = 0;
};

class FileDropRegistry {
public:
    // Rebuild the extension index from every loaded operator's declared drop handlers. Each loader
    // knows its own descriptor name, so no external op-name→loader map is needed. Idempotent.
    void rebuild(const std::vector<std::unique_ptr<OperatorLoader>>& loaders);

    // The ops that handle `path`'s extension, highest priority first (ties keep insertion order).
    // Empty when nothing handles it.
    std::vector<FileDropMatch> matches_for_path(const std::string& path) const;

    // The extensions (lowercased, no dot) that operator `op_type` declares it can open, for
    // filtering the native file dialog. Empty when the op declares no drop handlers.
    std::vector<std::string> extensions_for_op(const std::string& op_type) const;

    bool empty() const { return by_ext_.empty(); }

private:
    // key: lowercased extension WITHOUT the leading dot ("png"); value: matches, priority-desc.
    std::unordered_map<std::string, std::vector<FileDropMatch>> by_ext_;
    // reverse index: op type -> the extensions it handles (for the file-dialog filter).
    std::unordered_map<std::string, std::vector<std::string>> exts_by_op_;
};

// Lowercased file extension without the dot ("/a/b.PNG" -> "png"; "" when none). Free + pure so
// the drop callback and the registry agree, and it is unit-testable.
std::string file_ext_lower(const std::string& path);

}  // namespace vivid
