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
#include "test_helpers.h"

#include "test_ui_screenshot_smoke_support.inc"

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

#include "test_ui_screenshot_smoke_cases.inc"

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
