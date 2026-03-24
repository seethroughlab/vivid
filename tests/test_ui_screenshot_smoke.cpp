#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

enum class FailureKind {
    Harness,
    ProcessExit,
    Semantic,
    Baseline,
};

struct CaseArtifacts {
    std::filesystem::path lane_root;
    std::filesystem::path screenshot_path;
    std::filesystem::path log_path;
    std::filesystem::path dump_path;
    std::filesystem::path script_path;
    std::filesystem::path runtime_home;
    std::filesystem::path runtime_tmp;
};

struct CaseReport {
    std::string case_name;
    CaseArtifacts artifacts;
    int harness_failures = 0;
    int process_failures = 0;
    int semantic_failures = 0;
    int baseline_failures = 0;
};

static const char* failure_kind_label(FailureKind kind) {
    switch (kind) {
    case FailureKind::Harness: return "harness";
    case FailureKind::ProcessExit: return "process";
    case FailureKind::Semantic: return "semantic";
    case FailureKind::Baseline: return "baseline";
    }
    return "unknown";
}

static void bump_failure_bucket(CaseReport& report, FailureKind kind) {
    switch (kind) {
    case FailureKind::Harness: ++report.harness_failures; break;
    case FailureKind::ProcessExit: ++report.process_failures; break;
    case FailureKind::Semantic: ++report.semantic_failures; break;
    case FailureKind::Baseline: ++report.baseline_failures; break;
    }
}

static void report_check(CaseReport& report,
                         FailureKind kind,
                         bool cond,
                         const std::string& msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL [%s] %s\n",
                     failure_kind_label(kind), msg.c_str());
        std::fprintf(stderr,
                     "    artifacts: lane=%s screenshot=%s dump=%s log=%s\n",
                     report.artifacts.lane_root.string().c_str(),
                     report.artifacts.screenshot_path.string().c_str(),
                     report.artifacts.dump_path.string().c_str(),
                     report.artifacts.log_path.string().c_str());
        ++failures;
        bump_failure_bucket(report, kind);
    } else {
        std::fprintf(stderr, "  PASS [%s] %s\n",
                     failure_kind_label(kind), msg.c_str());
    }
}

static bool env_enabled(const char* name) {
    const char* value = std::getenv(name);
    if (!value || !*value)
        return false;
    return std::string(value) != "0" && std::string(value) != "false" &&
           std::string(value) != "FALSE" && std::string(value) != "no" &&
           std::string(value) != "NO";
}

static std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
}

struct ConnectionExpectation {
    std::string from_node;
    std::string from_port;
    std::string to_node;
    std::string to_port;
};

struct FileParamExpectation {
    std::string node_id;
    std::string param_name;
    std::string expected_value;
};

struct LayoutShiftExpectation {
    std::string node_id;
    float initial_x = 0.0f;
    float initial_y = 0.0f;
    float min_distance = 0.0f;
};

struct OutputExpectation {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.62f;
    float y1 = 0.72f;
    double min_luma_stddev = 10.0;
    double min_non_background_fraction = 0.06;
};

struct CheckpointExpectation {
    std::string label;
    std::vector<std::string> required_nodes;
    std::vector<std::string> forbidden_nodes;
    std::vector<ConnectionExpectation> required_connections;
    std::vector<ConnectionExpectation> forbidden_connections;
};

struct ScreenshotCase {
    std::string name;
    std::filesystem::path graph;
    std::string node_id;
    std::string output_name;
    std::filesystem::path test_drop_path;
    int test_drop_frame = 5;
    std::vector<int> test_drop_screen_pos;
    int screenshot_delay = 20;
    std::vector<std::string> required_log_substrings;
    std::vector<std::string> forbidden_log_substrings;
    std::string ui_script_json;
    std::string baseline_key;
    bool forbid_missing_operators = true;
    std::vector<std::string> required_nodes;
    std::vector<std::string> forbidden_nodes;
    std::vector<std::string> required_selected_nodes;
    std::vector<ConnectionExpectation> required_connections;
    std::vector<ConnectionExpectation> forbidden_connections;
    std::vector<FileParamExpectation> required_file_params;
    int expected_file_drop_chooser_open = -1;
    int max_native_file_dialog_count = -1;
    int max_overlap_pairs = -1;
    std::vector<LayoutShiftExpectation> required_layout_shifts;
    bool require_nontrivial_output = false;
    OutputExpectation output_expectation;
    std::vector<CheckpointExpectation> checkpoint_expectations;
};

struct ScreenshotBaseline {
    int width = 0;
    int height = 0;
    std::vector<int> blocks;
};

struct DumpNode {
    std::string node_id;
    std::string type_name;
    bool missing_operator = false;
    bool has_layout = false;
    float layout_x = 0.0f;
    float layout_y = 0.0f;
    std::unordered_map<std::string, std::string> file_params;
};

struct DumpConnection {
    std::string from_node;
    std::string from_port;
    std::string to_node;
    std::string to_port;
    bool invalid = false;
};

struct DumpState {
    std::vector<DumpNode> nodes;
    std::vector<DumpConnection> connections;
    std::vector<std::string> selected_node_ids;
    bool has_nodes_field = false;
    bool has_connections_field = false;
    bool has_selected_node_ids_field = false;
    bool has_node_count_field = false;
    bool has_connection_count_field = false;
    bool has_chooser_open_field = false;
    bool has_file_drop_chooser_open_field = false;
    bool has_role_chooser_open_field = false;
    bool has_native_file_dialog_count_field = false;
    bool has_file_dialog_stats_field = false;
    int declared_node_count = 0;
    int declared_connection_count = 0;
    bool chooser_open = false;
    bool file_drop_chooser_open = false;
    bool role_chooser_open = false;
    int native_file_dialog_count = 0;
};

struct DumpDocument {
    bool parse_ok = false;
    bool has_final_state = false;
    bool has_checkpoints_field = false;
    DumpState final_state;
    std::unordered_map<std::string, DumpState> checkpoints;
};

struct OutputStats {
    double luma_stddev = 0.0;
    double non_background_fraction = 0.0;
};

static std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

static void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path);
    out << text;
}

static std::string replace_all(std::string text, const std::string& needle, const std::string& replacement) {
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        text.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
    return text;
}

static std::vector<int> split_ints(const std::string& text) {
    std::vector<int> out;
    std::stringstream ss(text);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (!tok.empty())
            out.push_back(std::atoi(tok.c_str()));
    }
    return out;
}

static std::unordered_map<std::string, ScreenshotBaseline> load_baselines(
    const std::filesystem::path& path) {
    std::unordered_map<std::string, ScreenshotBaseline> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::stringstream ss(line);
        std::string key, width, height, blocks;
        if (!std::getline(ss, key, '|') ||
            !std::getline(ss, width, '|') ||
            !std::getline(ss, height, '|') ||
            !std::getline(ss, blocks)) {
            continue;
        }
        out[key] = ScreenshotBaseline{
            std::atoi(width.c_str()),
            std::atoi(height.c_str()),
            split_ints(blocks),
        };
    }
    return out;
}

static ScreenshotBaseline fingerprint_png(const std::filesystem::path& path) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (!data)
        return {};

    constexpr int kCols = 12;
    constexpr int kRows = 8;
    ScreenshotBaseline baseline;
    baseline.width = width;
    baseline.height = height;
    baseline.blocks.reserve(kCols * kRows * 3);

    for (int row = 0; row < kRows; ++row) {
        int y0 = row * height / kRows;
        int y1 = (row + 1) * height / kRows;
        for (int col = 0; col < kCols; ++col) {
            int x0 = col * width / kCols;
            int x1 = (col + 1) * width / kCols;
            long long sums[3] = {0, 0, 0};
            long long count = 0;
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const unsigned char* px = data + (y * width + x) * 4;
                    sums[0] += px[0];
                    sums[1] += px[1];
                    sums[2] += px[2];
                    ++count;
                }
            }
            for (int c = 0; c < 3; ++c)
                baseline.blocks.push_back(count > 0 ? static_cast<int>(sums[c] / count) : 0);
        }
    }
    stbi_image_free(data);
    return baseline;
}

static void check_baseline(const ScreenshotBaseline& actual,
                           const ScreenshotBaseline& expected,
                           const std::string& name) {
    check(actual.width == expected.width && actual.height == expected.height,
          ("baseline dimensions match for " + name).c_str());
    check(actual.blocks.size() == expected.blocks.size(),
          ("baseline fingerprint size matches for " + name).c_str());
    if (actual.blocks.size() != expected.blocks.size())
        return;

    double total_diff = 0.0;
    int max_diff = 0;
    for (size_t i = 0; i < actual.blocks.size(); ++i) {
        int diff = std::abs(actual.blocks[i] - expected.blocks[i]);
        total_diff += diff;
        max_diff = std::max(max_diff, diff);
    }
    double mean_diff = total_diff / static_cast<double>(actual.blocks.size());
    check(mean_diff <= 8.5, ("baseline mean diff stays within threshold for " + name).c_str());
    check(max_diff <= 28, ("baseline max diff stays within threshold for " + name).c_str());
}

static std::string baseline_line(const std::string& key, const ScreenshotBaseline& baseline) {
    std::ostringstream out;
    out << key << "|" << baseline.width << "|" << baseline.height << "|";
    for (size_t i = 0; i < baseline.blocks.size(); ++i) {
        if (i) out << ",";
        out << baseline.blocks[i];
    }
    return out.str();
}

using json = nlohmann::json;

static bool json_bool(const json& j, const char* key, bool fallback = false) {
    auto it = j.find(key);
    return (it != j.end() && it->is_boolean()) ? it->get<bool>() : fallback;
}

static int json_int(const json& j, const char* key, int fallback = 0) {
    auto it = j.find(key);
    return (it != j.end() && it->is_number_integer()) ? it->get<int>() : fallback;
}

static float json_float(const json& j, const char* key, float fallback = 0.0f) {
    auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? it->get<float>() : fallback;
}

static std::string json_string(const json& j, const char* key) {
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string{};
}

static std::string sanitize_lane_name(std::string lane) {
    if (lane.empty())
        return "gui_smoke";
    for (char& c : lane) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '_' || c == '-')
            continue;
        c = '_';
    }
    return lane;
}

static std::string resolve_lane_name() {
    const char* env_lane = std::getenv("VIVID_UI_SMOKE_LANE");
    if (env_lane && *env_lane)
        return sanitize_lane_name(env_lane);
    if (env_enabled("VIVID_ENABLE_GUI_ENV_SMOKE"))
        return "gui_env";
    return "gui_smoke";
}

static bool should_check_visual_baselines() {
    // GUI_ENV is the scheduled package/environment lane. It reuses the semantic
    // harness and screenshots for triage, but it should not fail on the same
    // coarse visual baselines that gate the tightly-controlled per-push lane.
    return !env_enabled("VIVID_ENABLE_GUI_ENV_SMOKE");
}

static DumpState parse_dump_state(const json& value) {
    DumpState state;
    if (!value.is_object())
        return state;

    state.has_chooser_open_field = value.contains("chooser_open");
    state.has_file_drop_chooser_open_field = value.contains("file_drop_chooser_open");
    state.has_role_chooser_open_field = value.contains("role_chooser_open");
    state.has_native_file_dialog_count_field = value.contains("native_file_dialog_count");
    state.has_file_dialog_stats_field =
        value.contains("file_dialog_stats") && value["file_dialog_stats"].is_object();
    state.has_node_count_field = value.contains("node_count");
    state.has_connection_count_field = value.contains("connection_count");

    state.chooser_open = json_bool(value, "chooser_open");
    state.file_drop_chooser_open = json_bool(value, "file_drop_chooser_open");
    state.role_chooser_open = json_bool(value, "role_chooser_open");
    state.native_file_dialog_count = json_int(value, "native_file_dialog_count");
    state.declared_node_count =
        json_int(value, "node_count", static_cast<int>(state.nodes.size()));
    state.declared_connection_count =
        json_int(value, "connection_count", static_cast<int>(state.connections.size()));

    auto sel_it = value.find("selected_node_ids");
    state.has_selected_node_ids_field = sel_it != value.end() && sel_it->is_array();
    if (state.has_selected_node_ids_field) {
        for (const auto& item : *sel_it) {
            if (item.is_string())
                state.selected_node_ids.emplace_back(item.get<std::string>());
        }
    }

    auto nodes_it = value.find("nodes");
    state.has_nodes_field = nodes_it != value.end() && nodes_it->is_array();
    if (state.has_nodes_field) {
        for (const auto& item : *nodes_it) {
            if (!item.is_object())
                continue;
            DumpNode node;
            node.node_id = json_string(item, "node_id");
            node.type_name = json_string(item, "type_name");
            node.missing_operator = json_bool(item, "missing_operator");
            node.has_layout = json_bool(item, "has_layout");
            node.layout_x = json_float(item, "layout_x");
            node.layout_y = json_float(item, "layout_y");

            auto fp_it = item.find("file_params");
            if (fp_it != item.end() && fp_it->is_object()) {
                for (auto& [key, val] : fp_it->items()) {
                    if (val.is_string())
                        node.file_params[key] = val.get<std::string>();
                }
            }

            state.nodes.push_back(std::move(node));
        }
    }

    auto conn_it = value.find("connections");
    state.has_connections_field = conn_it != value.end() && conn_it->is_array();
    if (state.has_connections_field) {
        for (const auto& item : *conn_it) {
            if (!item.is_object())
                continue;
            DumpConnection conn;
            conn.from_node = json_string(item, "from_node");
            conn.from_port = json_string(item, "from_port");
            conn.to_node = json_string(item, "to_node");
            conn.to_port = json_string(item, "to_port");
            conn.invalid = json_bool(item, "invalid");
            state.connections.push_back(std::move(conn));
        }
    }

    return state;
}

static DumpDocument load_dump_document(const std::filesystem::path& path) {
    DumpDocument doc_out;
    std::string text = read_text(path);
    if (text.empty())
        return doc_out;

    json root;
    try {
        root = json::parse(text);
    } catch (const json::parse_error&) {
        return doc_out;
    }
    doc_out.parse_ok = true;

    if (root.is_object()) {
        doc_out.has_final_state = json_bool(root, "has_final_state");
        auto fs_it = root.find("final_state");
        if (fs_it != root.end())
            doc_out.final_state = parse_dump_state(*fs_it);

        auto cp_it = root.find("checkpoints");
        doc_out.has_checkpoints_field = cp_it != root.end() && cp_it->is_array();
        if (doc_out.has_checkpoints_field) {
            for (const auto& item : *cp_it) {
                if (!item.is_object())
                    continue;
                std::string label = json_string(item, "label");
                auto st_it = item.find("state");
                if (st_it != item.end())
                    doc_out.checkpoints[label] = parse_dump_state(*st_it);
            }
        }
    }

    return doc_out;
}

static const DumpNode* find_node(const DumpState& state, const std::string& node_id) {
    for (const auto& node : state.nodes) {
        if (node.node_id == node_id)
            return &node;
    }
    return nullptr;
}

static bool has_connection(const DumpState& state, const ConnectionExpectation& expected) {
    for (const auto& conn : state.connections) {
        if (conn.from_node == expected.from_node &&
            conn.from_port == expected.from_port &&
            conn.to_node == expected.to_node &&
            conn.to_port == expected.to_port) {
            return true;
        }
    }
    return false;
}

static int count_missing_operators(const DumpState& state) {
    int count = 0;
    for (const auto& node : state.nodes) {
        if (node.missing_operator)
            ++count;
    }
    return count;
}

static int count_overlap_pairs(const DumpState& state) {
    constexpr float kOverlapDx = 170.0f;
    constexpr float kOverlapDy = 110.0f;
    int count = 0;
    for (size_t i = 0; i < state.nodes.size(); ++i) {
        if (!state.nodes[i].has_layout)
            continue;
        for (size_t j = i + 1; j < state.nodes.size(); ++j) {
            if (!state.nodes[j].has_layout)
                continue;
            if (std::fabs(state.nodes[i].layout_x - state.nodes[j].layout_x) < kOverlapDx &&
                std::fabs(state.nodes[i].layout_y - state.nodes[j].layout_y) < kOverlapDy) {
                ++count;
            }
        }
    }
    return count;
}

static OutputStats analyze_output_region(const std::filesystem::path& path,
                                         const OutputExpectation& expectation) {
    OutputStats stats;
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (!data)
        return stats;

    int x0 = std::max(0, std::min(width - 1, static_cast<int>(expectation.x0 * width)));
    int y0 = std::max(0, std::min(height - 1, static_cast<int>(expectation.y0 * height)));
    int x1 = std::max(x0 + 1, std::min(width, static_cast<int>(expectation.x1 * width)));
    int y1 = std::max(y0 + 1, std::min(height, static_cast<int>(expectation.y1 * height)));

    constexpr int kBgR = 22;
    constexpr int kBgG = 25;
    constexpr int kBgB = 29;
    constexpr int kNonBgThreshold = 24;

    double sum = 0.0;
    double sum_sq = 0.0;
    long long count = 0;
    long long non_bg = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const unsigned char* px = data + (y * width + x) * 4;
            double luma = 0.2126 * px[0] + 0.7152 * px[1] + 0.0722 * px[2];
            sum += luma;
            sum_sq += luma * luma;
            ++count;

            int diff = std::abs(static_cast<int>(px[0]) - kBgR) +
                       std::abs(static_cast<int>(px[1]) - kBgG) +
                       std::abs(static_cast<int>(px[2]) - kBgB);
            if (diff >= kNonBgThreshold)
                ++non_bg;
        }
    }
    stbi_image_free(data);
    if (count <= 0)
        return stats;

    double mean = sum / static_cast<double>(count);
    double variance = std::max(0.0, (sum_sq / static_cast<double>(count)) - mean * mean);
    stats.luma_stddev = std::sqrt(variance);
    stats.non_background_fraction =
        static_cast<double>(non_bg) / static_cast<double>(count);
    return stats;
}

static void check_state_expectations(const DumpState& state,
                                     const ScreenshotCase& c,
                                     const std::string& phase_name,
                                     CaseReport& report) {
    for (const auto& node_id : c.required_nodes) {
        report_check(report, FailureKind::Semantic, find_node(state, node_id) != nullptr,
                     phase_name + " contains node " + node_id + " for " + c.name);
    }
    for (const auto& node_id : c.forbidden_nodes) {
        report_check(report, FailureKind::Semantic, find_node(state, node_id) == nullptr,
                     phase_name + " omits node " + node_id + " for " + c.name);
    }
    if (c.forbid_missing_operators) {
        report_check(report, FailureKind::Semantic, count_missing_operators(state) == 0,
                     phase_name + " has no missing operators for " + c.name);
    }
    for (const auto& node_id : c.required_selected_nodes) {
        bool found = false;
        for (const auto& selected : state.selected_node_ids) {
            if (selected == node_id) {
                found = true;
                break;
            }
        }
        report_check(report, FailureKind::Semantic, found,
                     phase_name + " selects node " + node_id + " for " + c.name);
    }
    for (const auto& conn : c.required_connections) {
        report_check(report, FailureKind::Semantic, has_connection(state, conn),
                     phase_name + " contains connection " + conn.from_node + "/" + conn.from_port +
                         " -> " + conn.to_node + "/" + conn.to_port + " for " + c.name);
    }
    for (const auto& conn : c.forbidden_connections) {
        report_check(report, FailureKind::Semantic, !has_connection(state, conn),
                     phase_name + " omits connection " + conn.from_node + "/" + conn.from_port +
                         " -> " + conn.to_node + "/" + conn.to_port + " for " + c.name);
    }
    for (const auto& param : c.required_file_params) {
        const DumpNode* node = find_node(state, param.node_id);
        bool matches = false;
        if (node) {
            auto it = node->file_params.find(param.param_name);
            matches = it != node->file_params.end() && it->second == param.expected_value;
        }
        report_check(report, FailureKind::Semantic, matches,
                     phase_name + " sets file param " + param.node_id + "/" + param.param_name +
                         " for " + c.name);
    }
    if (c.expected_file_drop_chooser_open >= 0) {
        report_check(report, FailureKind::Semantic,
                     state.file_drop_chooser_open == (c.expected_file_drop_chooser_open != 0),
                     phase_name + " file-drop chooser state matches for " + c.name);
    }
    if (c.max_native_file_dialog_count >= 0) {
        report_check(report, FailureKind::Semantic,
                     state.native_file_dialog_count <= c.max_native_file_dialog_count,
                     phase_name + " native file dialog count stays within threshold for " + c.name);
    }
    if (c.max_overlap_pairs >= 0) {
        report_check(report, FailureKind::Semantic,
                     count_overlap_pairs(state) <= c.max_overlap_pairs,
                     phase_name + " node overlap pairs stay within threshold for " + c.name);
    }
    for (const auto& layout : c.required_layout_shifts) {
        const DumpNode* node = find_node(state, layout.node_id);
        bool shifted = false;
        if (node && node->has_layout) {
            float dx = node->layout_x - layout.initial_x;
            float dy = node->layout_y - layout.initial_y;
            shifted = std::sqrt(dx * dx + dy * dy) >= layout.min_distance;
        }
        report_check(report, FailureKind::Semantic, shifted,
                     phase_name + " layout shift threshold met for " + layout.node_id +
                         " in " + c.name);
    }
}

static void check_dump_state_health(const DumpState& state,
                                    const std::string& phase_name,
                                    const ScreenshotCase& c,
                                    CaseReport& report) {
    report_check(report, FailureKind::Harness, state.has_nodes_field,
                 phase_name + " includes nodes[] in UI dump for " + c.name);
    report_check(report, FailureKind::Harness, state.has_connections_field,
                 phase_name + " includes connections[] in UI dump for " + c.name);
    report_check(report, FailureKind::Harness, state.has_selected_node_ids_field,
                 phase_name + " includes selected_node_ids[] in UI dump for " + c.name);
    report_check(report, FailureKind::Harness, state.has_node_count_field,
                 phase_name + " includes node_count in UI dump for " + c.name);
    report_check(report, FailureKind::Harness, state.has_connection_count_field,
                 phase_name + " includes connection_count in UI dump for " + c.name);
    report_check(report, FailureKind::Harness, state.has_chooser_open_field,
                 phase_name + " includes chooser_open in UI dump for " + c.name);
    report_check(report, FailureKind::Harness, state.has_file_drop_chooser_open_field,
                 phase_name + " includes file_drop_chooser_open in UI dump for " + c.name);
    report_check(report, FailureKind::Harness, state.has_role_chooser_open_field,
                 phase_name + " includes role_chooser_open in UI dump for " + c.name);
    report_check(report, FailureKind::Harness, state.has_native_file_dialog_count_field,
                 phase_name + " includes native_file_dialog_count in UI dump for " + c.name);
    report_check(report, FailureKind::Harness, state.has_file_dialog_stats_field,
                 phase_name + " includes file_dialog_stats in UI dump for " + c.name);
    if (state.has_node_count_field) {
        report_check(report, FailureKind::Harness,
                     state.declared_node_count == static_cast<int>(state.nodes.size()),
                     phase_name + " node_count matches nodes[] size for " + c.name);
    }
    if (state.has_connection_count_field) {
        report_check(report, FailureKind::Harness,
                     state.declared_connection_count == static_cast<int>(state.connections.size()),
                     phase_name + " connection_count matches connections[] size for " + c.name);
    }
}

static int run_harness_selftest(const std::filesystem::path& build_dir) {
    const std::string gui_smoke_lane = sanitize_lane_name("gui_smoke");
    const std::string gui_env_lane = sanitize_lane_name("gui_env");
    const std::filesystem::path root = build_dir / ".test_ui_screenshot_smoke";
    const std::filesystem::path gui_smoke_root = root / gui_smoke_lane / "artifacts";
    const std::filesystem::path gui_env_root = root / gui_env_lane / "artifacts";
    check(gui_smoke_root != gui_env_root,
          "lane artifact roots differ between GUI_SMOKE and GUI_ENV");

    ScreenshotCase case_spec;
    case_spec.name = "harness selftest";
    case_spec.checkpoint_expectations.push_back({"after_missing_checkpoint", {"node_a"}, {}});

    CaseReport report;
    report.case_name = case_spec.name;
    report.artifacts.lane_root = root / "harness_selftest";
    report.artifacts.dump_path = report.artifacts.lane_root / "missing.state.json";
    report.artifacts.log_path = report.artifacts.lane_root / "case.log";
    report.artifacts.screenshot_path = report.artifacts.lane_root / "case.png";

    report_check(report, FailureKind::Harness,
                 !std::filesystem::exists(report.artifacts.dump_path),
                 "selftest missing dump path starts absent");
    DumpDocument missing_dump;
    report_check(report, FailureKind::Harness, !missing_dump.parse_ok,
                 "selftest treats unreadable dump as harness failure");

    DumpDocument doc;
    doc.parse_ok = true;
    doc.has_final_state = true;
    doc.has_checkpoints_field = true;
    doc.final_state.has_nodes_field = true;
    doc.final_state.has_connections_field = true;
    doc.final_state.has_selected_node_ids_field = true;
    doc.final_state.has_node_count_field = true;
    doc.final_state.has_connection_count_field = true;
    doc.final_state.has_chooser_open_field = true;
    doc.final_state.has_file_drop_chooser_open_field = true;
    doc.final_state.has_role_chooser_open_field = true;
    doc.final_state.has_native_file_dialog_count_field = true;
    doc.final_state.has_file_dialog_stats_field = true;
    doc.final_state.declared_node_count = 1;
    doc.final_state.declared_connection_count = 0;
    doc.final_state.nodes.push_back({"node_a", "Shape", false, true, 10.0f, 20.0f, {}});
    check_dump_state_health(doc.final_state, "final state", case_spec, report);
    auto it = doc.checkpoints.find("after_missing_checkpoint");
    report_check(report, FailureKind::Harness, it == doc.checkpoints.end(),
                 "selftest detects missing expected checkpoint");

    check(report.harness_failures == 0,
          "harness selftest setup checks stay internally consistent");
    return failures == 0 ? 0 : 1;
}

int main(int argc, char* argv[]) {
    if (!env_enabled("VIVID_ENABLE_UI_SCREENSHOT_SMOKE")) {
        std::fprintf(stderr,
                     "[test_ui_screenshot_smoke] SKIP: set VIVID_ENABLE_UI_SCREENSHOT_SMOKE=1 to run GUI screenshot smoke\n");
        return 0;
    }

    const std::filesystem::path build_dir =
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path();
    const std::filesystem::path vivid_bin = build_dir / "vivid";
    const std::filesystem::path graphs_dir = build_dir / "graphs";
    const std::string lane_name = resolve_lane_name();
    const bool check_visual_baselines = should_check_visual_baselines();
    const std::filesystem::path lane_root =
        build_dir / ".test_ui_screenshot_smoke" / lane_name;
    const std::filesystem::path artifacts_dir = lane_root / "artifacts";
    const std::filesystem::path fixtures_dir = artifacts_dir / "fixtures";
    const std::filesystem::path scripts_dir = artifacts_dir / "scripts";
    const std::filesystem::path runtime_home = lane_root / "home";
    const std::filesystem::path runtime_tmp = lane_root / "tmp";
    const std::filesystem::path runtime_config_dir =
        runtime_home / "Library" / "Application Support" / "Vivid";

    if (env_enabled("VIVID_UI_SMOKE_HARNESS_SELFTEST"))
        return run_harness_selftest(build_dir);

    std::error_code fs_ec;
    std::filesystem::remove_all(artifacts_dir, fs_ec);
    check(!fs_ec, ("cleared artifacts directory for lane " + lane_name).c_str());
    fs_ec.clear();
    std::filesystem::remove_all(runtime_tmp, fs_ec);
    check(!fs_ec, ("cleared runtime TMPDIR for lane " + lane_name).c_str());
    fs_ec.clear();
    std::filesystem::create_directories(fixtures_dir, fs_ec);
    check(!fs_ec, ("created fixtures directory for lane " + lane_name).c_str());
    fs_ec.clear();
    std::filesystem::create_directories(scripts_dir, fs_ec);
    check(!fs_ec, ("created scripts directory for lane " + lane_name).c_str());
    fs_ec.clear();
    std::filesystem::create_directories(runtime_home, fs_ec);
    check(!fs_ec, ("created runtime HOME directory for lane " + lane_name).c_str());
    fs_ec.clear();
    std::filesystem::create_directories(runtime_tmp, fs_ec);
    check(!fs_ec, ("created runtime TMPDIR directory for lane " + lane_name).c_str());
    fs_ec.clear();
    std::filesystem::create_directories(runtime_config_dir, fs_ec);
    check(!fs_ec, ("created runtime config directory for lane " + lane_name).c_str());
    fs_ec.clear();
    std::filesystem::remove(runtime_config_dir / "settings.json", fs_ec);
    check(!fs_ec, ("reset runtime settings for lane " + lane_name).c_str());

    const std::filesystem::path repo_root = build_dir.parent_path();
    const auto baselines = load_baselines(repo_root / "tests" / "ui_screenshot_baselines.txt");
    const std::filesystem::path midi_fixture =
        std::filesystem::exists(build_dir / "assets" / "sweelinck.mid")
            ? build_dir / "assets" / "sweelinck.mid"
            : repo_root / "assets" / "sweelinck.mid";
    const std::filesystem::path plugins_dir = build_dir / "vivid.app" / "Contents" / "PlugIns";
    std::filesystem::create_directories(plugins_dir);
    for (const auto& plugin_name : {"file_drop_test_op.dylib", "file_drop_test_op_alt.dylib"}) {
        const std::filesystem::path src = build_dir / plugin_name;
        const std::filesystem::path dst = plugins_dir / plugin_name;
        check(std::filesystem::exists(src),
              ("file-drop fixture plugin exists: " + src.string()).c_str());
        if (std::filesystem::exists(src)) {
            std::filesystem::copy_file(
                src, dst,
                std::filesystem::copy_options::overwrite_existing);
        }
    }
    const std::filesystem::path multi_drop_fixture = fixtures_dir / "example.dropx";
    {
        std::ofstream out(multi_drop_fixture);
        out << "drop fixture\n";
    }

    std::string spawned_package_paths =
        std::getenv("VIVID_PACKAGE_PATHS") ? std::getenv("VIVID_PACKAGE_PATHS") : "";

    std::vector<ScreenshotCase> cases = {
        {
            "instanced-shapes inspector",
            graphs_dir / "gpu" / "instanced_shapes_demo.json",
            "shapes",
            "instanced_shapes.png",
            {},
            5,
            {},
            20,
            {},
            {},
            {},
            {},
            true,
            {"shapes", "vout"},
            {},
            {"shapes"},
        },
        {
            "env inspector with visible output",
            repo_root / "tests" / "fixtures" / "ui_semantic_env_graph.json",
            "env",
            "env.png",
            {},
            5,
            {},
            20,
            {},
            {},
            {},
            {},
            true,
            {"clock1", "env", "shape1", "vout"},
            {},
            {"env"},
            {
                {"clock1", "beat_phase", "env", "beat_phase"},
                {"env", "value", "shape1", "radius"},
                {"shape1", "texture", "vout", "input"},
            },
            {},
            {},
            -1,
            0,
            -1,
            {},
            true,
        },
        {
            "graph drop reload",
            repo_root / "tests" / "fixtures" / "ui_semantic_env_graph.json",
            "shapes",
            "graph_drop_instanced_shapes.png",
            graphs_dir / "gpu" / "instanced_shapes_demo.json",
            5,
            {},
            30,
            {"Test drop injected"},
            {
                "Drop: failed to add",
                "Drop: no operator registered",
            },
            {},
            {},
            true,
            {"shapes", "bloom", "vout"},
            {},
            {"shapes"},
            {
                {"shapes", "texture", "bloom", "input"},
                {"bloom", "texture", "vout", "input"},
            },
            {},
            {},
            -1,
            0,
            -1,
            {},
            true,
        },
        {
            "single-match file drop creates configured node",
            repo_root / "tests" / "fixtures" / "ui_semantic_env_graph.json",
            "MidiFilePlayer1",
            "single_drop_midi_file_player.png",
            midi_fixture,
            5,
            {},
            30,
            {"Test drop injected"},
            {
                "Drop: failed to add",
                "Drop: no operator registered",
            },
            {},
            {},
            true,
            {"MidiFilePlayer1"},
            {},
            {"MidiFilePlayer1"},
            {},
            {},
            {
                {"MidiFilePlayer1", "file", midi_fixture.string()},
            },
            -1,
            0,
        },
        {
            "multi-match file drop chooser",
            graphs_dir / "gpu" / "instanced_shapes_demo.json",
            "",
            "multi_drop_chooser.png",
            multi_drop_fixture,
            5,
            {820, 420},
            30,
            {"Test drop injected"},
            {
                "Drop: failed to add",
                "Drop: no operator registered",
            },
            {},
            {},
            true,
            {"shapes", "vout"},
            {},
            {},
            {},
            {},
            {},
            1,
            0,
        },
        {
            "scripted editor node drag",
            repo_root / "tests" / "fixtures" / "ui_script_editor_graph.json",
            "",
            "scripted_node_drag.png",
            {},
            5,
            {},
            20,
            {"UI script mouse_move"},
            {},
            R"([
  {"type":"wait","frames":2},
  {"type":"mouse_move","x":165,"y":160},
  {"type":"mouse_button","button":"left","action":"press"},
  {"type":"wait","frames":1},
  {"type":"mouse_move","x":360,"y":250},
  {"type":"wait","frames":1},
  {"type":"mouse_button","button":"left","action":"release"},
  {"type":"wait","frames":2},
  {"type":"screenshot","path":"{{SCREENSHOT_PATH}}","delay_frames":0}
])",
            {},
            true,
            {"lfo1", "lfo2", "math1", "shape1", "vout"},
            {},
            {},
            {
                {"lfo1", "value", "math1", "a"},
                {"lfo2", "value", "math1", "b"},
                {"math1", "result", "shape1", "radius"},
                {"shape1", "texture", "vout", "input"},
            },
            {},
            {},
            -1,
            0,
            -1,
            {
                {"lfo1", 120.0f, 120.0f, 120.0f},
            },
            true,
        },
        {
            "scripted copy-paste redo",
            repo_root / "tests" / "fixtures" / "ui_script_editor_graph.json",
            "",
            "scripted_copy_paste_redo.png",
            {},
            5,
            {},
            20,
            {"UI script key"},
            {},
            R"([
  {"type":"wait","frames":2},
  {"type":"key","key":"A","action":"press","mods":["super"]},
  {"type":"wait","frames":1},
  {"type":"key","key":"C","action":"press","mods":["super"]},
  {"type":"mouse_move","x":860,"y":340},
  {"type":"key","key":"V","action":"press","mods":["super"]},
  {"type":"wait","frames":1},
  {"type":"checkpoint","label":"after_paste"},
  {"type":"key","key":"Z","action":"press","mods":["super"]},
  {"type":"key","key":"Z","action":"press","mods":["super"]},
  {"type":"key","key":"Z","action":"press","mods":["super"]},
  {"type":"key","key":"Z","action":"press","mods":["super"]},
  {"type":"key","key":"Z","action":"press","mods":["super"]},
  {"type":"key","key":"Z","action":"press","mods":["super"]},
  {"type":"key","key":"Z","action":"press","mods":["super"]},
  {"type":"key","key":"Z","action":"press","mods":["super"]},
  {"type":"key","key":"Z","action":"press","mods":["super"]},
  {"type":"key","key":"Z","action":"press","mods":["super"]},
  {"type":"key","key":"Z","action":"press","mods":["super"]},
  {"type":"key","key":"Z","action":"press","mods":["super"]},
  {"type":"key","key":"Z","action":"press","mods":["super"]},
  {"type":"key","key":"Z","action":"press","mods":["super"]},
  {"type":"wait","frames":1},
  {"type":"checkpoint","label":"after_undo"},
  {"type":"wait","frames":2},
  {"type":"key","key":"Z","action":"press","mods":["super","shift"]},
  {"type":"wait","frames":1},
  {"type":"key","key":"Z","action":"press","mods":["super","shift"]},
  {"type":"wait","frames":1},
  {"type":"key","key":"Z","action":"press","mods":["super","shift"]},
  {"type":"wait","frames":1},
  {"type":"key","key":"Z","action":"press","mods":["super","shift"]},
  {"type":"wait","frames":1},
  {"type":"key","key":"Z","action":"press","mods":["super","shift"]},
  {"type":"wait","frames":1},
  {"type":"key","key":"Z","action":"press","mods":["super","shift"]},
  {"type":"wait","frames":1},
  {"type":"key","key":"Z","action":"press","mods":["super","shift"]},
  {"type":"wait","frames":1},
  {"type":"key","key":"Z","action":"press","mods":["super","shift"]},
  {"type":"wait","frames":1},
  {"type":"key","key":"Z","action":"press","mods":["super","shift"]},
  {"type":"wait","frames":1},
  {"type":"key","key":"Z","action":"press","mods":["super","shift"]},
  {"type":"wait","frames":1},
  {"type":"key","key":"Z","action":"press","mods":["super","shift"]},
  {"type":"wait","frames":1},
  {"type":"key","key":"Z","action":"press","mods":["super","shift"]},
  {"type":"wait","frames":1},
  {"type":"key","key":"Z","action":"press","mods":["super","shift"]},
  {"type":"wait","frames":1},
  {"type":"key","key":"Z","action":"press","mods":["super","shift"]},
  {"type":"wait","frames":1},
  {"type":"checkpoint","label":"after_redo"},
  {"type":"wait","frames":1},
  {"type":"screenshot","path":"{{SCREENSHOT_PATH}}","delay_frames":0}
])",
            {},
            true,
            {"lfo1", "lfo2", "math1", "shape1", "vout",
             "lfo1_copy", "lfo2_copy", "math1_copy", "shape1_copy", "vout_copy"},
            {},
            {},
            {
                {"lfo1_copy", "value", "math1_copy", "a"},
                {"lfo2_copy", "value", "math1_copy", "b"},
                {"math1_copy", "result", "shape1_copy", "radius"},
                {"shape1_copy", "texture", "vout_copy", "input"},
            },
            {},
            {},
            -1,
            0,
            0,
            {},
            true,
            {},
            {
                {
                    "after_undo",
                    {},
                    {"lfo1_copy", "lfo2_copy", "math1_copy", "shape1_copy", "vout_copy"},
                },
                {
                    "after_redo",
                    {"lfo1_copy", "lfo2_copy", "math1_copy", "shape1_copy", "vout_copy"},
                    {},
                    {
                        {"lfo1_copy", "value", "math1_copy", "a"},
                        {"lfo2_copy", "value", "math1_copy", "b"},
                        {"math1_copy", "result", "shape1_copy", "radius"},
                        {"shape1_copy", "texture", "vout_copy", "input"},
                    },
                },
            },
        },
        {
            "scripted wire reconnect",
            repo_root / "tests" / "fixtures" / "ui_script_editor_graph.json",
            "",
            "scripted_wire_reconnect.png",
            {},
            5,
            {},
            20,
            {"UI script mouse_button"},
            {},
            R"([
  {"type":"wait","frames":2},
  {"type":"mouse_move","x":360,"y":314},
  {"type":"mouse_button","button":"left","action":"press"},
  {"type":"wait","frames":1},
  {"type":"mouse_button","button":"left","action":"release"},
  {"type":"wait","frames":1},
  {"type":"mouse_move","x":260,"y":232},
  {"type":"mouse_button","button":"left","action":"press"},
  {"type":"wait","frames":1},
  {"type":"mouse_move","x":360,"y":314},
  {"type":"wait","frames":1},
  {"type":"mouse_button","button":"left","action":"release"},
  {"type":"wait","frames":2},
  {"type":"screenshot","path":"{{SCREENSHOT_PATH}}","delay_frames":0}
])",
            {},
            true,
            {"lfo1", "lfo2", "math1", "shape1", "vout"},
            {},
            {},
            {
                {"lfo1", "value", "math1", "a"},
                {"lfo1", "value", "math1", "b"},
                {"math1", "result", "shape1", "radius"},
                {"shape1", "texture", "vout", "input"},
            },
            {
                {"lfo2", "value", "math1", "b"},
            },
            {},
            -1,
            0,
            -1,
            {},
            true,
        },
    };

    if (env_enabled("VIVID_ENABLE_GUI_ENV_SMOKE")) {
        std::filesystem::path package_root =
            std::getenv("VIVID_GUI_ENV_PACKAGE_ROOT")
                ? std::filesystem::path(std::getenv("VIVID_GUI_ENV_PACKAGE_ROOT"))
                : (repo_root.parent_path() / "vivid-wavetable");
        if (spawned_package_paths.empty()) {
            spawned_package_paths = package_root.parent_path().string();
        }
        const std::filesystem::path wavetable_graph =
            package_root / "graphs" / "extended" / "wavetable_dream_keys_demo.json";
        cases.push_back({
            "wavetable cp1 inspector",
            wavetable_graph,
            "cp1",
            "wavetable_dream_keys_cp1.png",
            {},
            5,
            {},
            20,
            {},
            {
                "reserved keyword",
                "ChordProg Thumb Pipeline",
                "Error in wgpuQueueSubmit",
            },
            {},
            {},
            true,
            {"cp1"},
            {},
            {"cp1"},
            {},
            {},
            {},
            -1,
            0,
        });
    }

    check(std::filesystem::exists(vivid_bin), "vivid binary exists for screenshot smoke");

    for (const auto& c : cases) {
        const auto graph_path = c.graph;
        const auto screenshot_path = artifacts_dir / c.output_name;
        const auto log_path = artifacts_dir / (std::filesystem::path(c.output_name).stem().string() + ".log");
        const auto script_path = scripts_dir / (std::filesystem::path(c.output_name).stem().string() + ".json");
        const auto dump_path = artifacts_dir / (std::filesystem::path(c.output_name).stem().string() + ".state.json");
        CaseReport report{
            c.name,
            {
                lane_root,
                screenshot_path,
                log_path,
                dump_path,
                script_path,
                runtime_home,
                runtime_tmp,
            },
        };

        report_check(report, FailureKind::Harness, std::filesystem::exists(graph_path),
                     "graph fixture exists: " + graph_path.string());
        if (!c.test_drop_path.empty()) {
            report_check(report, FailureKind::Harness, std::filesystem::exists(c.test_drop_path),
                         "drop fixture exists: " + c.test_drop_path.string());
        }
        std::filesystem::remove(screenshot_path);
        std::filesystem::remove(log_path);
        std::filesystem::remove(script_path);
        std::filesystem::remove(dump_path);

        if (!c.ui_script_json.empty()) {
            write_text(script_path,
                       replace_all(c.ui_script_json, "{{SCREENSHOT_PATH}}", screenshot_path.string()));
        }

        std::string cmd = shell_quote(vivid_bin.string()) + " " +
                          shell_quote(graph_path.string()) +
                          (c.node_id.empty()
                               ? ""
                               : " --select-node " + shell_quote(c.node_id)) +
                          (c.test_drop_path.empty()
                               ? ""
                               : " --test-drop-path " + shell_quote(c.test_drop_path.string()) +
                                     " --test-drop-frame " + std::to_string(c.test_drop_frame)) +
                          (c.test_drop_screen_pos.size() == 2
                               ? " --test-drop-screen-pos " + std::to_string(c.test_drop_screen_pos[0]) +
                                     " " + std::to_string(c.test_drop_screen_pos[1])
                               : "") +
                          " --test-dump-ui-state " + shell_quote(dump_path.string()) +
                          (c.ui_script_json.empty()
                               ? " --screenshot " + shell_quote(screenshot_path.string()) +
                                     " --screenshot-delay " + std::to_string(c.screenshot_delay)
                               : " --test-ui-script " + shell_quote(script_path.string())) +
                          " < /dev/null" +
                          " > " + shell_quote(log_path.string()) + " 2>&1";
        cmd = "env HOME=" + shell_quote(runtime_home.string()) +
              " TMPDIR=" + shell_quote(runtime_tmp.string()) +
              " VIVID_UI_SMOKE_LANE=" + shell_quote(lane_name) +
              (spawned_package_paths.empty()
                   ? ""
                   : " VIVID_PACKAGE_PATHS=" + shell_quote(spawned_package_paths)) +
              " " + cmd;
        std::fprintf(stderr, "[test_ui_screenshot_smoke] %s\n", cmd.c_str());
        int rc = std::system(cmd.c_str());
        report_check(report, FailureKind::ProcessExit, rc == 0,
                     "screenshot command exited cleanly for " + c.name +
                         " (rc=" + std::to_string(rc) + ")");

        report_check(report, FailureKind::Harness, std::filesystem::exists(screenshot_path),
                     "screenshot written for " + c.name);
        if (std::filesystem::exists(screenshot_path)) {
            report_check(report, FailureKind::Harness,
                         std::filesystem::file_size(screenshot_path) > 0,
                         "screenshot non-empty for " + c.name);
            if (c.require_nontrivial_output) {
                OutputStats stats = analyze_output_region(screenshot_path, c.output_expectation);
                report_check(report, FailureKind::Semantic,
                             stats.luma_stddev >= c.output_expectation.min_luma_stddev,
                             "output luminance variance clears threshold for " + c.name);
                report_check(report, FailureKind::Semantic,
                             stats.non_background_fraction >=
                                 c.output_expectation.min_non_background_fraction,
                             "output non-background fraction clears threshold for " + c.name);
            }
            if (check_visual_baselines && !c.baseline_key.empty()) {
                auto it = baselines.find(c.baseline_key);
                report_check(report, FailureKind::Baseline, it != baselines.end(),
                             "baseline entry exists for " + c.baseline_key);
                if (it != baselines.end()) {
                    const auto actual = fingerprint_png(screenshot_path);
                    report_check(report, FailureKind::Baseline,
                                 actual.width == it->second.width &&
                                     actual.height == it->second.height,
                                 "baseline dimensions match for " + c.name);
                    report_check(report, FailureKind::Baseline,
                                 actual.blocks.size() == it->second.blocks.size(),
                                 "baseline fingerprint size matches for " + c.name);
                    if (actual.blocks.size() == it->second.blocks.size()) {
                        double total_diff = 0.0;
                        int max_diff = 0;
                        for (size_t i = 0; i < actual.blocks.size(); ++i) {
                            int diff = std::abs(actual.blocks[i] - it->second.blocks[i]);
                            total_diff += diff;
                            max_diff = std::max(max_diff, diff);
                        }
                        double mean_diff = total_diff / static_cast<double>(actual.blocks.size());
                        report_check(report, FailureKind::Baseline, mean_diff <= 8.5,
                                     "baseline mean diff stays within threshold for " + c.name);
                        report_check(report, FailureKind::Baseline, max_diff <= 28,
                                     "baseline max diff stays within threshold for " + c.name);
                    }
                } else {
                    std::fprintf(stderr, "BASELINE %s\n",
                                 baseline_line(c.baseline_key, fingerprint_png(screenshot_path)).c_str());
                }
            } else if (!c.baseline_key.empty()) {
                std::fprintf(stderr,
                             "[test_ui_screenshot_smoke] baseline checks skipped for '%s' in lane '%s'\n",
                             c.name.c_str(), lane_name.c_str());
            }
        }

        report_check(report, FailureKind::Harness, std::filesystem::exists(dump_path),
                     "semantic UI dump written for " + c.name);
        if (std::filesystem::exists(dump_path)) {
            DumpDocument dump = load_dump_document(dump_path);
            report_check(report, FailureKind::Harness, dump.parse_ok,
                         "semantic UI dump parses cleanly for " + c.name);
            report_check(report, FailureKind::Harness, dump.has_checkpoints_field,
                         "semantic UI dump includes checkpoints[] for " + c.name);
            report_check(report, FailureKind::Harness, dump.has_final_state,
                         "semantic UI dump has final state for " + c.name);
            if (dump.has_final_state) {
                check_dump_state_health(dump.final_state, "final state", c, report);
                check_state_expectations(dump.final_state, c, "final state", report);
            }
            for (const auto& checkpoint : c.checkpoint_expectations) {
                auto it = dump.checkpoints.find(checkpoint.label);
                report_check(report, FailureKind::Harness, it != dump.checkpoints.end(),
                             "checkpoint '" + checkpoint.label + "' exists for " + c.name);
                if (it == dump.checkpoints.end())
                    continue;

                ScreenshotCase checkpoint_case = c;
                checkpoint_case.required_nodes = checkpoint.required_nodes;
                checkpoint_case.forbidden_nodes = checkpoint.forbidden_nodes;
                checkpoint_case.required_selected_nodes.clear();
                checkpoint_case.required_connections = checkpoint.required_connections;
                checkpoint_case.forbidden_connections = checkpoint.forbidden_connections;
                checkpoint_case.required_file_params.clear();
                checkpoint_case.expected_file_drop_chooser_open = -1;
                checkpoint_case.max_native_file_dialog_count = -1;
                checkpoint_case.max_overlap_pairs = -1;
                checkpoint_case.required_layout_shifts.clear();
                checkpoint_case.require_nontrivial_output = false;
                check_dump_state_health(it->second, "checkpoint " + checkpoint.label,
                                        checkpoint_case, report);
                check_state_expectations(it->second, checkpoint_case,
                                         "checkpoint " + checkpoint.label, report);
            }
        }

        report_check(report, FailureKind::Harness, std::filesystem::exists(log_path),
                     "log written for " + c.name);
        if (std::filesystem::exists(log_path)) {
            const std::string log = read_text(log_path);
            static const std::vector<std::string> common_forbidden = {
                "Scissor Rect",
                "set_scissor_rect",
                "Error in wgpuQueueSubmit",
            };
            for (const auto& forbidden : common_forbidden) {
                report_check(report, FailureKind::Semantic,
                             log.find(forbidden) == std::string::npos,
                             "log does not contain '" + forbidden + "' for " + c.name);
            }
            for (const auto& required : c.required_log_substrings) {
                report_check(report, FailureKind::Semantic,
                             log.find(required) != std::string::npos,
                             "log contains '" + required + "' for " + c.name);
            }
            for (const auto& forbidden : c.forbidden_log_substrings) {
                report_check(report, FailureKind::Semantic,
                             log.find(forbidden) == std::string::npos,
                             "log does not contain '" + forbidden + "' for " + c.name);
            }
        }

        std::fprintf(stderr,
                     "[test_ui_screenshot_smoke] case '%s' summary: harness=%d process=%d semantic=%d baseline=%d\n",
                     c.name.c_str(),
                     report.harness_failures,
                     report.process_failures,
                     report.semantic_failures,
                     report.baseline_failures);
        if (report.harness_failures == 0 &&
            report.process_failures == 0 &&
            report.semantic_failures == 0 &&
            report.baseline_failures > 0) {
            std::fprintf(stderr,
                         "[test_ui_screenshot_smoke] case '%s': semantic checks passed; only baseline drift failed\n",
                         c.name.c_str());
        }
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
