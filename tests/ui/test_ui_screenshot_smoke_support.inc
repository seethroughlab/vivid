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
    // Editor-window screenshot support (follow-up: second-window automated
    // test coverage). When `editor_target_node` is set, the harness passes
    // --editor-screenshot to the spawned vivid, expects a PNG named
    // `editor_output_name`, and (if `editor_baseline_key` is non-empty) diffs
    // it against `ui_screenshot_baselines.txt` under that key. Main-window
    // capture is unaffected.
    std::string editor_target_node;
    std::string editor_output_name;
    std::string editor_baseline_key;
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
    doc.final_state.nodes.push_back({"node_a", "Shape2D", false, true, 10.0f, 20.0f, {}});
    check_dump_state_health(doc.final_state, "final state", case_spec, report);
    auto it = doc.checkpoints.find("after_missing_checkpoint");
    report_check(report, FailureKind::Harness, it == doc.checkpoints.end(),
                 "selftest detects missing expected checkpoint");

    check(report.harness_failures == 0,
          "harness selftest setup checks stay internally consistent");
    return failures == 0 ? 0 : 1;
}

