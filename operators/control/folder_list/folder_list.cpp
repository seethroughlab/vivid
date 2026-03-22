#include "operator_api/operator.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace {
static std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::vector<std::string> parse_extensions(const std::string& csv) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : csv) {
        if (ch == ',' || ch == ';' || std::isspace(static_cast<unsigned char>(ch))) {
            if (!cur.empty()) {
                if (cur[0] != '.') cur = "." + cur;
                out.push_back(to_lower(cur));
                cur.clear();
            }
            continue;
        }
        cur.push_back(ch);
    }
    if (!cur.empty()) {
        if (cur[0] != '.') cur = "." + cur;
        out.push_back(to_lower(cur));
    }
    return out;
}
} // namespace

struct FolderList : vivid::ControlOperatorBase {
    static constexpr const char* kName = "FolderList";
    static constexpr bool kTimeDependent = false;

    vivid::Param<vivid::FilePath> folder{"folder", ""};
    vivid::Param<vivid::TextValue> extensions{"extensions", "mp4,mov,m4v,avi,mkv,webm"};
    vivid::Param<bool> recursive{"recursive", false};
    vivid::Param<int> sort_mode{"sort_mode", 0, {"NameAsc", "NameDesc"}};

    FolderList() {
        vivid::semantic_tag(recursive, "enabled");
        vivid::semantic_shape(recursive, "bool");
    }

    std::string last_folder_;
    std::string last_exts_;
    bool last_recursive_ = false;
    int last_sort_mode_ = 0;
    std::vector<std::string> files_;
    std::vector<const char*> file_ptrs_;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&folder);
        out.push_back(&extensions);
        out.push_back(&recursive);
        out.push_back(&sort_mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"files", VIVID_PORT_STRING_SPREAD, VIVID_PORT_OUTPUT});
        out.push_back({"count", VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        const bool should_refresh =
            folder.str_value != last_folder_ ||
            extensions.str_value != last_exts_ ||
            recursive.bool_value() != last_recursive_ ||
            sort_mode.int_value() != last_sort_mode_;

        if (should_refresh) refresh();

        if (ctx->output_string_spreads && ctx->output_string_spreads[0].data) {
            auto& out = ctx->output_string_spreads[0];
            uint32_t n = std::min(out.capacity, static_cast<uint32_t>(file_ptrs_.size()));
            out.length = n;
            for (uint32_t i = 0; i < n; ++i) out.data[i] = file_ptrs_[i];
        }
        if (ctx->output_values) ctx->output_values[1] = static_cast<float>(files_.size());
    }

    void refresh() {
        last_folder_ = folder.str_value;
        last_exts_ = extensions.str_value;
        last_recursive_ = recursive.bool_value();
        last_sort_mode_ = sort_mode.int_value();
        files_.clear();
        file_ptrs_.clear();

        if (last_folder_.empty()) return;
        std::filesystem::path base(last_folder_);
        if (!std::filesystem::exists(base) || !std::filesystem::is_directory(base)) return;

        const auto exts = parse_extensions(last_exts_);
        const auto ext_match = [&](const std::filesystem::path& p) {
            if (exts.empty()) return true;
            std::string e = to_lower(p.extension().string());
            return std::find(exts.begin(), exts.end(), e) != exts.end();
        };

        if (last_recursive_) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(base)) {
                if (!entry.is_regular_file()) continue;
                if (!ext_match(entry.path())) continue;
                files_.push_back(entry.path().string());
            }
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(base)) {
                if (!entry.is_regular_file()) continue;
                if (!ext_match(entry.path())) continue;
                files_.push_back(entry.path().string());
            }
        }

        std::sort(files_.begin(), files_.end());
        if (last_sort_mode_ == 1) std::reverse(files_.begin(), files_.end());
        file_ptrs_.reserve(files_.size());
        for (const auto& s : files_) file_ptrs_.push_back(s.c_str());
    }
};

VIVID_REGISTER(FolderList)
