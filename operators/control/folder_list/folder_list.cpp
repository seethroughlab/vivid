#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"
#include "operator_api/draw_plot_helpers.h"
#include "operator_api/draw_ui_helpers.h"

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
/**
 * @brief Scans a directory and outputs matching filenames as string lanes.
 *
 * Lists files in a folder filtered by comma-separated extensions, with
 * optional recursive scanning and sorting.
 *
 * @param extensions Comma-separated file extensions to include (e.g. "wav,mp3").
 * @see StringSelect, Basename, TextureLoader
 */
struct FolderList : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "FolderList";
    static constexpr bool kTimeDependent = false;

    vivid::Param<vivid::FilePath> folder{"folder", ""};
    vivid::Param<vivid::TextValue> extensions{"extensions", "mp4,mov,m4v,avi,mkv,webm"};
    vivid::Param<bool> recursive{"recursive", false};
    vivid::Param<int> sort_mode{"sort_mode", 0, {"NameAsc", "NameDesc"}};

    FolderList() {
        vivid::description(folder, "Directory path to scan for files");
        vivid::description(extensions, "Comma-separated file extensions to include (e.g. \"wav,mp3\")");
        vivid::description(recursive, "Scan subdirectories when enabled");
        vivid::description(sort_mode, "Sort order for the output file list");

        vivid::semantic_tag(recursive, "enabled");
        vivid::semantic_shape(recursive, "bool");
    }

    std::string last_folder_;
    std::string last_exts_;
    bool last_recursive_ = false;
    int last_sort_mode_ = 0;
    std::vector<std::string> files_;
    std::vector<const char*> file_ptrs_;

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx || !ctx->draw.opaque) return;
        auto& d = const_cast<VividDrawAPI&>(ctx->draw);
        void* o = d.opaque;
        float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
        float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

        vivid::draw_plot::draw_thumb_background(d, o, w, h);
        vivid::draw_plot::draw_thumb_label(d, o, 6.0f, 4.0f, "DIR");

        int count = (ctx->output_count > 1) ? static_cast<int>(ctx->output_values[1]) : 0;
        char count_str[16];
        std::snprintf(count_str, sizeof(count_str), "%d", count);

        if (count > 0) {
            // Large centered count
            vivid::draw_ui::draw_text_aligned(d, o, 0.0f, h * 0.28f, w,
                                              count_str, {0.75f, 0.85f, 0.95f, 0.95f}, 1.6f, 0.5f);
            vivid::draw_ui::draw_text_aligned(d, o, 0.0f, h * 0.28f + 24.0f, w,
                                              "files", {0.40f, 0.48f, 0.56f, 0.7f}, 0.8f, 0.5f);
        } else {
            vivid::draw_ui::draw_text_aligned(d, o, 0.0f, h * 0.38f, w,
                                              "empty", {0.40f, 0.42f, 0.46f, 0.6f}, 0.9f, 0.5f);
        }
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&folder);
        out.push_back(&extensions);
        out.push_back(&recursive);
        out.push_back(&sort_mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"files", VIVID_PORT_STRING_LANES, VIVID_PORT_OUTPUT});
        out.push_back({"count", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        const bool should_refresh =
            folder.str_value != last_folder_ ||
            extensions.str_value != last_exts_ ||
            recursive.bool_value() != last_recursive_ ||
            sort_mode.int_value() != last_sort_mode_;

        if (should_refresh) refresh();

        if (ctx->output_string_lanes) {
            auto& out = ctx->output_string_lanes[0];
            uint32_t n = static_cast<uint32_t>(file_ptrs_.size());
            if (out.resize(out.handle, n)) {
                for (uint32_t i = 0; i < n; ++i)
                    out.set(out.handle, i, file_ptrs_[i]);
                out.commit(out.handle, n);
            }
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

VIVID_DEFINE_OP(FolderList) {
}

VIVID_REGISTER(FolderList)
VIVID_THUMBNAIL(FolderList)
