#include "runtime/control/control_server_checks.h"

#include "runtime/graph/compiled_graph.h"
#include "runtime/graph/graph.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/core/runtime_core.h"
#include "runtime/audio/audio_engine.h"
#include "operator_api/types.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vivid::control_server_checks {
namespace {

std::string json_err(const std::string& msg) {
    return nlohmann::json{{"ok", false}, {"error", msg}}.dump();
}

int severity_rank(const std::string& severity) {
    if (severity == "critical") return 0;
    if (severity == "warning") return 1;
    return 2;
}

const char* kind_str_local(VividOperatorKind k) {
    switch (k) {
        case VIVID_OP_CONTROL: return "control";
        case VIVID_OP_AUDIO:   return "audio";
        case VIVID_OP_GPU:     return "gpu";
        default: return "unknown";
    }
}

struct DiagnosticFinding {
    std::string id;
    std::string severity;
    std::string node_id;
    std::string message;
    std::string suggestion;
};

std::vector<DiagnosticFinding> collect_diagnostics(
        Graph& graph, RuntimeCore& core, OperatorRegistry& registry) {
    std::unordered_map<std::string, int> incoming_wires;
    std::unordered_map<std::string, int> outgoing_wires;
    for (const auto& conn : graph.connections()) {
        incoming_wires[conn.to_node]++;
        outgoing_wires[conn.from_node]++;
    }

    std::vector<DiagnosticFinding> findings;
    findings.reserve(32);

    const auto* cg = core.compiled_graph();
    if (!cg) return findings;
    const auto& nodes = cg->nodes;
    for (const auto& ns : nodes) {
        const VividOperatorDescriptor* desc = ns.loader ? ns.loader->descriptor() : nullptr;
        std::string type_name = ns.type_name;
        if (type_name.empty() && desc && desc->name) type_name = desc->name;

        if (ns.missing_operator) {
            std::string description = ns.missing_operator_detail.empty()
                ? "Operator type is unresolved; missing-operator placeholder is active."
                : ns.missing_operator_detail;
            std::string suggestion;
            if (ns.missing_operator_reason == "not_built")
                suggestion = "Run 'vivid rebuild <package>' to compile this package.";
            else if (ns.missing_operator_reason == "abi_mismatch")
                suggestion = "Rebuild vivid and rerun 'vivid rebuild <package>'.";
            else if (ns.missing_operator_reason == "load_failed")
                suggestion = "Check library dependencies; run 'vivid run-diagnostics' for details.";
            else
                suggestion = "Install or link the package providing this operator type, then reload.";
            findings.push_back({
                "missing_operator_type",
                "critical",
                ns.node_id,
                description,
                suggestion
            });
        }

        if (ns.errored) {
            findings.push_back({
                "node_runtime_error",
                "critical",
                ns.node_id,
                std::string("Node is in errored state: ") + ns.error_message,
                "Fix compile/runtime error in this operator; graph currently uses stale/broken output."
            });
        }

        if (ns.active_cadence == vivid::Cadence::Audio && type_name == "audio_out" && incoming_wires[ns.node_id] == 0) {
            findings.push_back({
                "audio_sink_disconnected",
                "critical",
                ns.node_id,
                "Audio sink node has no incoming connections.",
                "Connect an audio-producing node to audio_out inputs."
            });
        }

        if (!ns.missing_operator && incoming_wires[ns.node_id] == 0 && outgoing_wires[ns.node_id] == 0) {
            findings.push_back({
                "isolated_node",
                "warning",
                ns.node_id,
                "Node is fully disconnected from the graph.",
                "Connect it to upstream/downstream nodes or remove it if unused."
            });
        }

        bool found_non_finite = false;
        for (float v : ns.param_values) {
            if (!std::isfinite(v)) { found_non_finite = true; break; }
        }
        if (!found_non_finite) {
            for (float v : ns.output_values) {
                if (!std::isfinite(v)) { found_non_finite = true; break; }
            }
        }
        if (!found_non_finite) {
            for (const auto& ref : ns.output_lane_refs) {
                if (!ref) continue;
                for (uint32_t j = 0; j < ref.length(); ++j) {
                    if (!std::isfinite(ref.data()[j])) { found_non_finite = true; break; }
                }
                if (found_non_finite) break;
            }
        }
        if (found_non_finite) {
            findings.push_back({
                "non_finite_values",
                "warning",
                ns.node_id,
                "NaN or Inf value detected in node runtime state.",
                "Clamp or sanitize values; check divisions, logs, and numeric domain assumptions."
            });
        }

        if (ns.active_cadence == vivid::Cadence::Audio) {
            auto peak_it = ns.output_port_indices.find("peak");
            if (peak_it != ns.output_port_indices.end() && peak_it->second < ns.output_values.size()) {
                float peak = ns.output_values[peak_it->second];
                if (std::isfinite(peak) && peak > 1.05f) {
                    findings.push_back({
                        "audio_peak_clipping_risk",
                        "warning",
                        ns.node_id,
                        "Audio peak exceeds 1.05; clipping risk likely.",
                        "Reduce gain, add limiting, or remap modulation depth."
                    });
                }
            }
        }

        // --- Movie playback checks ---

        if (type_name == "MovieFile") {
            auto fit = ns.file_param_indices.find("file");
            if (fit != ns.file_param_indices.end() &&
                fit->second < ns.file_param_storage.size() &&
                ns.file_param_storage[fit->second].empty()) {
                findings.push_back({
                    "movie_file_path_empty",
                    "warning",
                    ns.node_id,
                    "No file path set; no media will play.",
                    "Set the 'file' parameter to a valid media file path."
                });
            }
        }

        if (type_name == "MovieFile") {
            auto nil_it = ns.output_port_indices.find("nil_frames");
            auto new_it = ns.output_port_indices.find("new_frames");
            if (nil_it != ns.output_port_indices.end() && new_it != ns.output_port_indices.end() &&
                nil_it->second < ns.output_values.size() && new_it->second < ns.output_values.size()) {
                float nil_frames = ns.output_values[nil_it->second];
                float new_frames = ns.output_values[new_it->second];
                float total = new_frames + nil_frames;
                if (total > 60.0f && nil_frames / total > 0.5f) {
                    findings.push_back({
                        "movie_sustained_nil_frames",
                        "warning",
                        ns.node_id,
                        "Video decode is stalling \u2014 more than 50% nil frames detected.",
                        "Check codec compatibility and system load; consider HAP format for guaranteed performance."
                    });
                }
            }

            auto drift_it = ns.output_port_indices.find("drift_ms");
            if (drift_it != ns.output_port_indices.end() && drift_it->second < ns.output_values.size()) {
                float drift = ns.output_values[drift_it->second];
                if (std::isfinite(drift) && drift > 200.0f) {
                    findings.push_back({
                        "movie_sustained_large_drift",
                        "warning",
                        ns.node_id,
                        "AV sync drift exceeds 200ms; audio and video may appear out of sync.",
                        "Check system load; sustained drift suggests decode cannot keep up with playback rate."
                    });
                }
            }

            auto seek_it = ns.output_port_indices.find("seek_corrections");
            if (seek_it != ns.output_port_indices.end() &&
                drift_it != ns.output_port_indices.end() &&
                seek_it->second < ns.output_values.size() &&
                drift_it->second < ns.output_values.size()) {
                float seeks = ns.output_values[seek_it->second];
                float drift = ns.output_values[drift_it->second];
                if (std::isfinite(seeks) && std::isfinite(drift) &&
                    seeks > 5.0f && drift > 66.0f) {
                    findings.push_back({
                        "movie_seek_churn",
                        "warning",
                        ns.node_id,
                        "Video is issuing repeated AV correction seeks during steady playback.",
                        "MovieFile should use drop/repeat for steady-state correction; repeated seeks indicate decode or loop recovery is unstable."
                    });
                }
            }
        }
    }

    for (const auto& dc : cg->dropped_connections) {
        findings.push_back({
            "dropped_connection",
            "warning",
            dc.to_node,
            "Connection " + dc.from_node + "/" + dc.from_port + " → " +
                dc.to_node + "/" + dc.to_port + " was dropped: " + dc.reason,
            "Check port names match the operator's declared ports."
        });
    }

    std::sort(findings.begin(), findings.end(), [](const DiagnosticFinding& a, const DiagnosticFinding& b) {
        int ar = severity_rank(a.severity);
        int br = severity_rank(b.severity);
        if (ar != br) return ar < br;
        if (a.id != b.id) return a.id < b.id;
        if (a.node_id != b.node_id) return a.node_id < b.node_id;
        if (a.message != b.message) return a.message < b.message;
        return a.suggestion < b.suggestion;
    });
    return findings;
}

struct CheckValue {
    enum class Kind { Missing, Number, Bool, String };
    Kind kind = Kind::Missing;
    double number = 0.0;
    bool boolean = false;
    std::string string;
};

CheckValue cv_number(double n) { CheckValue v; v.kind = CheckValue::Kind::Number; v.number = n; return v; }
CheckValue cv_bool(bool b) { CheckValue v; v.kind = CheckValue::Kind::Bool; v.boolean = b; return v; }
CheckValue cv_string(const std::string& s) { CheckValue v; v.kind = CheckValue::Kind::String; v.string = s; return v; }

bool parse_check_value(const nlohmann::json& v, CheckValue& out) {
    if (v.is_null()) return false;
    if (v.is_number()) {
        out = cv_number(v.get<double>());
        return true;
    }
    if (v.is_boolean()) {
        out = cv_bool(v.get<bool>());
        return true;
    }
    if (v.is_string()) {
        out = cv_string(v.get<std::string>());
        return true;
    }
    return false;
}

void add_json_check_value(nlohmann::json& obj,
                          const char* key, const CheckValue& v) {
    if (v.kind == CheckValue::Kind::Number)
        obj[key] = v.number;
    else if (v.kind == CheckValue::Kind::Bool)
        obj[key] = v.boolean;
    else if (v.kind == CheckValue::Kind::String)
        obj[key] = v.string;
}

bool eval_compare(const CheckValue& actual, const std::string& op,
                  const CheckValue& expected, double tolerance,
                  const CheckValue* between_max = nullptr) {
    if (op == "exists") return actual.kind != CheckValue::Kind::Missing;
    if (op == "not_exists") return actual.kind == CheckValue::Kind::Missing;
    if (actual.kind == CheckValue::Kind::Missing) return false;

    if (op == "between") {
        if (!between_max) return false;
        if (actual.kind != CheckValue::Kind::Number ||
            expected.kind != CheckValue::Kind::Number ||
            between_max->kind != CheckValue::Kind::Number) return false;
        double lo = expected.number;
        double hi = between_max->number;
        if (lo > hi) std::swap(lo, hi);
        return actual.number >= (lo - tolerance) && actual.number <= (hi + tolerance);
    }

    if (actual.kind == CheckValue::Kind::Number && expected.kind == CheckValue::Kind::Number) {
        double a = actual.number;
        double b = expected.number;
        if (op == "==") return std::fabs(a - b) <= tolerance;
        if (op == "!=") return std::fabs(a - b) > tolerance;
        if (op == ">") return a > b;
        if (op == ">=") return a >= b;
        if (op == "<") return a < b;
        if (op == "<=") return a <= b;
        return false;
    }
    if (actual.kind == CheckValue::Kind::Bool && expected.kind == CheckValue::Kind::Bool) {
        if (op == "==") return actual.boolean == expected.boolean;
        if (op == "!=") return actual.boolean != expected.boolean;
        return false;
    }
    if (actual.kind == CheckValue::Kind::String && expected.kind == CheckValue::Kind::String) {
        if (op == "==") return actual.string == expected.string;
        if (op == "!=") return actual.string != expected.string;
        return false;
    }
    return false;
}

bool resolve_state_path(Graph& graph, RuntimeCore& core,
                        const std::unordered_map<std::string, int>& incoming_wires,
                        const std::unordered_map<std::string, int>& outgoing_wires,
                        const std::string& path, CheckValue& out) {
    if (path == "graph.node_count") {
        out = cv_number(static_cast<double>(graph.nodes().size()));
        return true;
    }
    const std::string prefix = "nodes.";
    if (path.rfind(prefix, 0) != 0) return false;

    size_t node_end = path.find('.', prefix.size());
    if (node_end == std::string::npos) return false;
    std::string node_id = path.substr(prefix.size(), node_end - prefix.size());
    std::string rest = path.substr(node_end + 1);

    const auto* cg = core.compiled_graph();
    if (!cg) return false;
    const CompiledNode* node = cg->find_node(node_id);
    if (!node) return false;

    if (rest == "kind") {
        out = cv_string(kind_str_local(node->operator_kind));
        return true;
    }
    if (rest == "incoming_wires") {
        auto it = incoming_wires.find(node_id);
        out = cv_number(static_cast<double>(it == incoming_wires.end() ? 0 : it->second));
        return true;
    }
    if (rest == "outgoing_wires") {
        auto it = outgoing_wires.find(node_id);
        out = cv_number(static_cast<double>(it == outgoing_wires.end() ? 0 : it->second));
        return true;
    }
    if (rest == "health.errored") {
        out = cv_bool(node->errored || node->missing_operator);
        return true;
    }
    if (rest == "health.missing_operator") {
        out = cv_bool(node->missing_operator);
        return true;
    }
    if (rest == "health.message") {
        out = cv_string(node->error_message);
        return true;
    }
    if (rest == "env_metrics.audio.rms") {
        auto it = node->output_port_indices.find("rms");
        if (node->active_cadence != vivid::Cadence::Audio || it == node->output_port_indices.end() || it->second >= node->output_values.size())
            return false;
        out = cv_number(node->output_values[it->second]);
        return true;
    }
    if (rest == "env_metrics.audio.peak") {
        auto it = node->output_port_indices.find("peak");
        if (node->active_cadence != vivid::Cadence::Audio || it == node->output_port_indices.end() || it->second >= node->output_values.size())
            return false;
        out = cv_number(node->output_values[it->second]);
        return true;
    }
    if (rest == "env_metrics.audio.waveform_length") {
        auto it = node->output_port_indices.find("waveform");
        if (node->active_cadence != vivid::Cadence::Audio || it == node->output_port_indices.end() || it->second >= node->output_lane_refs.size())
            return false;
        out = cv_number(static_cast<double>(node->output_lane_refs[it->second].length()));
        return true;
    }

    const std::string param_prefix = "params.";
    if (rest.rfind(param_prefix, 0) == 0) {
        std::string pname = rest.substr(param_prefix.size());
        auto it = node->param_indices.find(pname);
        if (it != node->param_indices.end() && it->second < node->param_values.size()) {
            out = cv_number(node->param_values[it->second]);
            return true;
        }
        auto fit = node->file_param_indices.find(pname);
        if (fit != node->file_param_indices.end() && fit->second < node->file_param_storage.size()) {
            out = cv_string(node->file_param_storage[fit->second]);
            return true;
        }
        return false;
    }

    const std::string output_prefix = "outputs.";
    if (rest.rfind(output_prefix, 0) == 0) {
        size_t sep = rest.find('.', output_prefix.size());
        if (sep == std::string::npos) return false;
        std::string pname = rest.substr(output_prefix.size(), sep - output_prefix.size());
        std::string tail = rest.substr(sep + 1);
        auto it = node->output_port_indices.find(pname);
        if (it == node->output_port_indices.end()) return false;
        uint32_t pi = it->second;
        if (tail == "scalar" && pi < node->output_values.size()) {
            out = cv_number(node->output_values[pi]);
            return true;
        }
        if (tail == "lane_array.length" && pi < node->output_lane_refs.size()) {
            out = cv_number(static_cast<double>(node->output_lane_refs[pi].length()));
            return true;
        }
        return false;
    }
    return false;
}

struct ParsedCheck {
    std::string id;
    std::string type;
    std::string op;
    std::string severity = "warning";
    std::string message;
    std::string path;
    double tolerance = 0.0;
    int64_t for_frames = 1;
    int64_t after_frame = 0;

    bool has_value = false;
    CheckValue value;
    bool has_between_max = false;
    CheckValue between_max;

    bool has_when = false;
    std::string when_path;
    std::string when_op;
    bool has_when_value = false;
    CheckValue when_value;

    std::string finding_id;
    std::string check_diag_severity;
};

bool parse_check_def(const nlohmann::json& obj, ParsedCheck& out, std::string& err) {
    if (!obj.is_object()) { err = "check must be an object"; return false; }
    if (!obj.contains("id") || !obj["id"].is_string()) { err = "check missing 'id'"; return false; }
    if (!obj.contains("type") || !obj["type"].is_string()) { err = "check missing 'type'"; return false; }
    if (!obj.contains("op") || !obj["op"].is_string()) { err = "check missing 'op'"; return false; }
    out.id = obj["id"].get<std::string>();
    out.type = obj["type"].get<std::string>();
    out.op = obj["op"].get<std::string>();
    if (obj.contains("severity") && obj["severity"].is_string()) out.severity = obj["severity"].get<std::string>();
    if (obj.contains("message") && obj["message"].is_string()) out.message = obj["message"].get<std::string>();
    if (obj.contains("tolerance") && obj["tolerance"].is_number()) out.tolerance = obj["tolerance"].get<double>();
    if (obj.contains("for_frames") && obj["for_frames"].is_number_integer()) out.for_frames = obj["for_frames"].get<int64_t>();
    if (obj.contains("after_frame") && obj["after_frame"].is_number_integer()) out.after_frame = obj["after_frame"].get<int64_t>();

    if (out.type == "state_check") {
        if (!obj.contains("path") || !obj["path"].is_string()) { err = "state_check missing 'path'"; return false; }
        out.path = obj["path"].get<std::string>();
        if (out.op != "exists" && out.op != "not_exists") {
            if (out.op == "between") {
                if (obj.contains("value") && obj["value"].is_array() && obj["value"].size() == 2) {
                    out.has_value = parse_check_value(obj["value"][0], out.value);
                    out.has_between_max = parse_check_value(obj["value"][1], out.between_max);
                } else {
                    if (obj.contains("min")) out.has_value = parse_check_value(obj["min"], out.value);
                    if (obj.contains("max")) out.has_between_max = parse_check_value(obj["max"], out.between_max);
                }
                if (!out.has_value || !out.has_between_max) {
                    err = "state_check 'between' requires numeric min/max (or value[2])";
                    return false;
                }
            } else {
                if (obj.contains("value")) out.has_value = parse_check_value(obj["value"], out.value);
                if (!out.has_value) { err = "state_check missing scalar 'value'"; return false; }
            }
        }
    } else if (out.type == "diagnostic_check") {
        if (obj.contains("check_severity") && obj["check_severity"].is_string())
            out.check_diag_severity = obj["check_severity"].get<std::string>();
        else if (obj.contains("severity") && obj["severity"].is_string())
            out.check_diag_severity = obj["severity"].get<std::string>();
        if (obj.contains("finding_id") && obj["finding_id"].is_string())
            out.finding_id = obj["finding_id"].get<std::string>();
        if (out.finding_id.empty() && obj.contains("check_diagnostics_ids") &&
            obj["check_diagnostics_ids"].is_array() && !obj["check_diagnostics_ids"].empty()) {
            const auto& first = obj["check_diagnostics_ids"][0];
            if (first.is_string()) out.finding_id = first.get<std::string>();
        }
        if (out.op == "count_by_severity_eq" ||
            out.op == "count_by_severity_lte" ||
            out.op == "count_by_severity_gte") {
            if (obj.contains("value")) out.has_value = parse_check_value(obj["value"], out.value);
            if (!out.has_value || out.value.kind != CheckValue::Kind::Number) {
                err = "diagnostic_check count op requires numeric 'value'";
                return false;
            }
            if (out.check_diag_severity.empty()) {
                err = "diagnostic_check count op requires 'check_severity'";
                return false;
            }
        } else if (out.op == "finding_present" || out.op == "finding_absent") {
            if (out.finding_id.empty()) {
                err = "diagnostic_check finding op requires 'finding_id'";
                return false;
            }
        } else {
            err = "unsupported diagnostic_check op";
            return false;
        }
    } else {
        err = "check 'type' must be 'state_check' or 'diagnostic_check'";
        return false;
    }

    if (obj.contains("when")) {
        const auto& when = obj["when"];
        if (!when.is_object()) { err = "'when' must be object"; return false; }
        if (!when.contains("path") || !when["path"].is_string() ||
            !when.contains("op") || !when["op"].is_string()) {
            err = "'when' requires 'path' and 'op'";
            return false;
        }
        out.has_when = true;
        out.when_path = when["path"].get<std::string>();
        out.when_op = when["op"].get<std::string>();
        if (when.contains("value")) out.has_when_value = parse_check_value(when["value"], out.when_value);
    }

    if (out.for_frames < 1) {
        err = "'for_frames' must be >= 1";
        return false;
    }
    if (out.after_frame < 0) {
        err = "'after_frame' must be >= 0";
        return false;
    }
    return true;
}

} // namespace

std::string handle_run_diagnostics(Graph& graph, RuntimeCore& core, OperatorRegistry& registry,
                                   AudioEngine* audio_engine) {
    std::vector<DiagnosticFinding> findings = collect_diagnostics(graph, core, registry);

    // Audio-engine-derived findings
    if (audio_engine && audio_engine->running()) {
        float load = audio_engine->audio_load();
        if (load > 0.95f) {
            findings.push_back({
                "audio_high_load", "critical", "",
                "Audio thread load is " + std::to_string(static_cast<int>(load * 100)) + "%; underruns are imminent.",
                "Reduce audio node count, increase buffer size, or simplify the graph."
            });
        } else if (load > 0.8f) {
            findings.push_back({
                "audio_high_load", "warning", "",
                "Audio thread load is " + std::to_string(static_cast<int>(load * 100)) + "%; headroom is low.",
                "Consider reducing audio complexity or increasing buffer size."
            });
        }

        uint32_t xruns = audio_engine->underrun_count();
        if (xruns > 0) {
            findings.push_back({
                "audio_xruns_detected", "warning", "",
                "Audio underruns detected: " + std::to_string(xruns) + " since session start.",
                "Increase buffer size, reduce audio graph complexity, or check system load."
            });
        }
    }

    // Re-sort with audio findings included
    std::sort(findings.begin(), findings.end(), [](const DiagnosticFinding& a, const DiagnosticFinding& b) {
        int ar = severity_rank(a.severity);
        int br = severity_rank(b.severity);
        if (ar != br) return ar < br;
        if (a.id != b.id) return a.id < b.id;
        if (a.node_id != b.node_id) return a.node_id < b.node_id;
        if (a.message != b.message) return a.message < b.message;
        return a.suggestion < b.suggestion;
    });

    nlohmann::json summary = nlohmann::json::object();
    int64_t critical_count = 0;
    int64_t warning_count = 0;
    int64_t info_count = 0;
    for (const auto& f : findings) {
        if (f.severity == "critical") critical_count++;
        else if (f.severity == "warning") warning_count++;
        else info_count++;
    }
    summary["critical"] = critical_count;
    summary["warning"] = warning_count;
    summary["info"] = info_count;

    nlohmann::json findings_arr = nlohmann::json::array();
    for (const auto& f : findings) {
        findings_arr.push_back({
            {"id", f.id}, {"severity", f.severity}, {"node_id", f.node_id},
            {"message", f.message}, {"suggestion", f.suggestion}
        });
    }

    nlohmann::json hints_arr = nlohmann::json::array();
    std::unordered_set<std::string> seen_hint_ids;
    for (const auto& f : findings) {
        if (seen_hint_ids.find(f.id) != seen_hint_ids.end()) continue;
        seen_hint_ids.insert(f.id);
        hints_arr.push_back({{"id", f.id}, {"severity", f.severity}, {"suggestion", f.suggestion}});
    }

    nlohmann::json result_obj = nlohmann::json::object();
    result_obj["summary"] = std::move(summary);
    result_obj["findings"] = std::move(findings_arr);
    result_obj["hints"] = std::move(hints_arr);

    // Build health telemetry
    nlohmann::json health = nlohmann::json::object();

    // Audio health
    nlohmann::json audio_health = nlohmann::json::object();
    audio_health["running"] = false;
    audio_health["sample_rate"] = 0;
    audio_health["buffer_size"] = 0;
    audio_health["node_count"] = 0;
    audio_health["xruns"] = 0;
    audio_health["last_buffer_underrun"] = false;
    audio_health["load"] = 0.0;
    if (audio_engine) {
        audio_health["running"] = audio_engine->running();
        audio_health["sample_rate"] = audio_engine->sample_rate();
        audio_health["buffer_size"] = audio_engine->buffer_size();
        audio_health["node_count"] = audio_engine->node_count();
        audio_health["xruns"] = audio_engine->underrun_count();
        audio_health["last_buffer_underrun"] = audio_engine->last_buffer_underrun();
        audio_health["load"] = audio_engine->audio_load();
    }

    nlohmann::json top_nodes = nlohmann::json::array();
    nlohmann::json top_lane_state_nodes = nlohmann::json::array();
    const auto* cg = core.compiled_graph();
    if (cg) {
        struct AudioNodeHealthRow {
            std::string node_id;
            std::string type_name;
            uint32_t last_block_total_us = 0;
            uint32_t last_process_us = 0;
            uint32_t ema_block_us = 0;
            float last_block_budget_pct = 0.0f;
            uint32_t last_lane_count = 0;
            uint32_t lane_state_entries = 0;
        };

        std::vector<AudioNodeHealthRow> rows;
        rows.reserve(cg->audio_order.size());
        for (uint32_t idx : cg->audio_order) {
            const auto& ns = cg->nodes[idx];
            if (!ns.audio) continue;
            auto snap = read_audio_node_debug(*ns.audio);
            if (!snap.valid) continue;
            rows.push_back({
                ns.node_id,
                ns.type_name,
                snap.last_block_total_us,
                snap.last_process_us,
                snap.ema_block_us,
                snap.last_block_budget_pct,
                snap.last_lane_count,
                snap.lane_state_entries
            });
        }

        auto emit_row = [](const AudioNodeHealthRow& row) {
            return nlohmann::json{
                {"node_id", row.node_id},
                {"type", row.type_name},
                {"last_block_total_us", static_cast<int64_t>(row.last_block_total_us)},
                {"last_process_us", static_cast<int64_t>(row.last_process_us)},
                {"ema_block_us", static_cast<int64_t>(row.ema_block_us)},
                {"last_block_budget_pct", static_cast<double>(row.last_block_budget_pct)},
                {"last_lane_count", static_cast<int64_t>(row.last_lane_count)},
                {"lane_state_entries", static_cast<int64_t>(row.lane_state_entries)},
            };
        };

        auto by_hotness = rows;
        std::sort(by_hotness.begin(), by_hotness.end(), [](const AudioNodeHealthRow& a,
                                                           const AudioNodeHealthRow& b) {
            if (a.ema_block_us != b.ema_block_us) return a.ema_block_us > b.ema_block_us;
            if (a.last_block_total_us != b.last_block_total_us) return a.last_block_total_us > b.last_block_total_us;
            return a.node_id < b.node_id;
        });
        for (size_t i = 0; i < by_hotness.size() && i < 5; ++i)
            top_nodes.push_back(emit_row(by_hotness[i]));

        auto by_lane_state = rows;
        std::sort(by_lane_state.begin(), by_lane_state.end(), [](const AudioNodeHealthRow& a,
                                                                 const AudioNodeHealthRow& b) {
            if (a.lane_state_entries != b.lane_state_entries) return a.lane_state_entries > b.lane_state_entries;
            if (a.last_lane_count != b.last_lane_count) return a.last_lane_count > b.last_lane_count;
            return a.node_id < b.node_id;
        });
        for (size_t i = 0; i < by_lane_state.size() && i < 5; ++i)
            top_lane_state_nodes.push_back(emit_row(by_lane_state[i]));
    }
    audio_health["top_nodes"] = std::move(top_nodes);
    audio_health["top_lane_state_nodes"] = std::move(top_lane_state_nodes);
    health["audio"] = std::move(audio_health);

    // Graph topology health
    nlohmann::json graph_health = nlohmann::json::object();
    graph_health["declared_nodes"] = static_cast<int64_t>(graph.nodes().size());
    graph_health["declared_connections"] = static_cast<int64_t>(graph.connections().size());
    if (cg) {
        graph_health["compiled_nodes"] = static_cast<int64_t>(cg->nodes.size());
        graph_health["frame_nodes"] = static_cast<int64_t>(cg->frame_order.size());
        graph_health["audio_nodes"] = static_cast<int64_t>(cg->audio_order.size());
        graph_health["total_edges"] = static_cast<int64_t>(cg->edges.size());
        graph_health["frame_edges"] = static_cast<int64_t>(cg->frame_direct_edges.size());
        graph_health["audio_edges"] = static_cast<int64_t>(cg->audio_direct_edges.size());
        graph_health["snapshot_edges"] = static_cast<int64_t>(
            cg->frame_to_audio_edges.size() + cg->audio_to_frame_edges.size());
        graph_health["dropped_connections"] = static_cast<int64_t>(cg->dropped_connections.size());

        int64_t errored = 0, missing = 0, shader_errors = 0, texture_nodes = 0;
        for (const auto& n : cg->nodes) {
            if (n.errored) errored++;
            if (n.missing_operator) missing++;
            if (n.gpu && n.gpu->shader_error) shader_errors++;
            if (n.gpu) texture_nodes++;
        }
        graph_health["errored_nodes"] = errored;
        graph_health["missing_operators"] = missing;

        nlohmann::json gpu_health = nlohmann::json::object();
        gpu_health["texture_nodes"] = texture_nodes;
        gpu_health["shader_errors"] = shader_errors;
        health["gpu"] = std::move(gpu_health);
    } else {
        graph_health["compiled_nodes"] = 0;
    }
    health["graph"] = std::move(graph_health);

    if (!health.contains("gpu")) {
        health["gpu"] = nlohmann::json{{"texture_nodes", 0}, {"shader_errors", 0}};
    }

    return nlohmann::json{
        {"ok", true}, {"schema_version", 2},
        {"health", std::move(health)},
        {"result", std::move(result_obj)}
    }.dump();
}

std::string handle_validate_checks(const nlohmann::json& root) {
    if (!root.contains("checks") || !root["checks"].is_array()) return json_err("missing 'checks' array");
    const auto& checks = root["checks"];

    nlohmann::json errs = nlohmann::json::array();
    int64_t error_count = 0;

    std::unordered_set<std::string> seen_ids;
    for (size_t idx = 0; idx < checks.size(); ++idx) {
        ParsedCheck pc;
        std::string err;
        if (!parse_check_def(checks[idx], pc, err)) {
            errs.push_back({{"index", static_cast<int64_t>(idx)}, {"message", err}});
            error_count++;
            continue;
        }
        if (!seen_ids.insert(pc.id).second) {
            errs.push_back({{"index", static_cast<int64_t>(idx)}, {"id", pc.id}, {"message", "duplicate check id"}});
            error_count++;
        }
    }

    nlohmann::json result_obj = nlohmann::json::object();
    result_obj["valid"] = (error_count == 0);
    result_obj["error_count"] = error_count;
    result_obj["errors"] = std::move(errs);

    return nlohmann::json{{"ok", true}, {"schema_version", 1}, {"result", std::move(result_obj)}}.dump();
}

std::string handle_run_checks(Graph& graph, RuntimeCore& core, OperatorRegistry& registry,
                              const nlohmann::json& root) {
    if (!root.contains("checks") || !root["checks"].is_array()) return json_err("missing 'checks' array");
    const auto& checks = root["checks"];

    std::vector<ParsedCheck> parsed;
    parsed.reserve(checks.size());
    std::unordered_set<std::string> seen_ids;
    for (size_t idx = 0; idx < checks.size(); ++idx) {
        ParsedCheck pc;
        std::string err;
        if (!parse_check_def(checks[idx], pc, err))
            return json_err("invalid check at index " + std::to_string(idx) + ": " + err);
        if (!seen_ids.insert(pc.id).second)
            return json_err("duplicate check id: " + pc.id);
        parsed.push_back(std::move(pc));
    }
    std::sort(parsed.begin(), parsed.end(), [](const ParsedCheck& a, const ParsedCheck& b) {
        return a.id < b.id;
    });

    std::unordered_map<std::string, int> incoming_wires;
    std::unordered_map<std::string, int> outgoing_wires;
    for (const auto& conn : graph.connections()) {
        incoming_wires[conn.to_node]++;
        outgoing_wires[conn.from_node]++;
    }
    std::vector<DiagnosticFinding> findings = collect_diagnostics(graph, core, registry);

    nlohmann::json results = nlohmann::json::array();

    int64_t passed = 0, failed = 0, skipped = 0;
    int64_t critical_failed = 0, warning_failed = 0, info_failed = 0;
    bool all_passed = true;
    bool all_critical_passed = true;

    for (const auto& c : parsed) {
        bool r_passed = false;
        bool r_skipped = false;
        CheckValue actual;
        CheckValue expected = c.value;
        std::string message = c.message;

        if (c.for_frames > 1) {
            r_skipped = true;
            message = message.empty() ? "for_frames > 1 not yet supported in single-snapshot run" : message;
        }

        if (!r_skipped && c.has_when) {
            CheckValue guard_actual;
            if (!resolve_state_path(graph, core, incoming_wires, outgoing_wires, c.when_path, guard_actual)) {
                r_skipped = true;
                message = message.empty() ? "guard path not found" : message;
            } else {
                CheckValue guard_expect = c.has_when_value ? c.when_value : cv_bool(true);
                if (!eval_compare(guard_actual, c.when_op, guard_expect, 0.0)) {
                    r_skipped = true;
                    message = message.empty() ? "guard condition not met" : message;
                }
            }
        }

        if (!r_skipped && c.type == "state_check") {
            bool has_actual = resolve_state_path(graph, core, incoming_wires, outgoing_wires, c.path, actual);
            if (!has_actual) actual.kind = CheckValue::Kind::Missing;
            if (c.op == "between")
                r_passed = eval_compare(actual, c.op, c.value, c.tolerance, &c.between_max);
            else
                r_passed = eval_compare(actual, c.op, c.value, c.tolerance, nullptr);
        } else if (!r_skipped && c.type == "diagnostic_check") {
            if (c.op == "count_by_severity_eq" ||
                c.op == "count_by_severity_lte" ||
                c.op == "count_by_severity_gte") {
                int64_t count = 0;
                for (const auto& f : findings) if (f.severity == c.check_diag_severity) count++;
                actual = cv_number(static_cast<double>(count));
                int64_t target = static_cast<int64_t>(c.value.number);
                if (c.op == "count_by_severity_eq") r_passed = (count == target);
                else if (c.op == "count_by_severity_lte") r_passed = (count <= target);
                else r_passed = (count >= target);
            } else {
                bool found = false;
                for (const auto& f : findings) {
                    if (f.id == c.finding_id) { found = true; break; }
                }
                actual = cv_bool(found);
                r_passed = (c.op == "finding_present") ? found : !found;
            }
        }

        nlohmann::json row = nlohmann::json::object();
        row["id"] = c.id;
        row["type"] = c.type;
        row["severity"] = c.severity;
        row["passed"] = r_skipped ? false : r_passed;
        row["skipped"] = r_skipped;
        row["op"] = c.op;
        if (!c.path.empty()) row["path"] = c.path;
        if (!message.empty()) row["message"] = message;
        if (c.has_value) add_json_check_value(row, "expected", expected);
        if (c.op == "between" && c.has_between_max) add_json_check_value(row, "expected_max", c.between_max);
        add_json_check_value(row, "actual", actual);
        results.push_back(std::move(row));

        if (r_skipped) {
            skipped++;
        } else if (r_passed) {
            passed++;
        } else {
            failed++;
            all_passed = false;
            if (c.severity == "critical") { critical_failed++; all_critical_passed = false; }
            else if (c.severity == "warning") warning_failed++;
            else info_failed++;
        }
    }

    nlohmann::json summary = {
        {"passed", passed}, {"failed", failed}, {"skipped", skipped},
        {"critical_failed", critical_failed}, {"warning_failed", warning_failed},
        {"info_failed", info_failed}
    };
    nlohmann::json result_obj = nlohmann::json::object();
    result_obj["all_passed"] = all_passed;
    result_obj["all_critical_passed"] = all_critical_passed;
    result_obj["summary"] = std::move(summary);
    result_obj["results"] = std::move(results);

    return nlohmann::json{{"ok", true}, {"schema_version", 1}, {"result", std::move(result_obj)}}.dump();
}

} // namespace vivid::control_server_checks
