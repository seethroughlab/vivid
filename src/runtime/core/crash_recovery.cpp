#include "runtime/core/crash_recovery.h"

#include "runtime/audio/audio_engine.h"
#include "runtime/control/control_server.h"
#include "runtime/control/runtime_api.h"
#include "runtime/core/crash_guard.h"
#include "runtime/graph/graph.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace vivid {

namespace {

#ifndef VIVID_CORE_VERSION
#define VIVID_CORE_VERSION "0.0.0"
#endif

const char* current_platform() {
#if defined(__APPLE__)
    return "darwin";
#elif defined(_WIN32)
    return "windows";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

const char* signal_name_for(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS:  return "SIGBUS";
        case SIGABRT: return "SIGABRT";
        case SIGFPE:  return "SIGFPE";
        default:      return "UNKNOWN";
    }
}

std::string iso8601_utc_now() {
    using clock = std::chrono::system_clock;
    auto now = clock::now();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::time_t t = static_cast<std::time_t>(secs);
    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &t);
#else
    gmtime_r(&t, &tm_utc);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                  tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
    return buf;
}

// Filesystem-safe version of iso8601_utc_now() — colons are replaced with '-'
// so the timestamp can be used as a filename on Windows / when rsync'd.
std::string iso8601_filename_now() {
    std::string s = iso8601_utc_now();
    for (char& c : s) if (c == ':') c = '-';
    return s;
}

template <typename T>
T json_get_or(const nlohmann::json& j, const char* key, T fallback) {
    if (!j.is_object()) return fallback;
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    try { return it->get<T>(); } catch (...) { return fallback; }
}

} // namespace

// ---------------------------------------------------------------------------
// CrashRecord
// ---------------------------------------------------------------------------

nlohmann::json CrashRecord::to_json() const {
    return nlohmann::json{
        {"timestamp",           timestamp},
        {"signal",              signal},
        {"signal_name",         signal_name},
        {"pid",                 pid},
        {"vivid_version",       vivid_version},
        {"platform",            platform},
        {"graph_path",          graph_path},
        {"graph_dirty",         graph_dirty},
        {"operator_name",       operator_name},
        {"node_id",             node_id},
        {"node_type",           node_type},
        {"pkg_name",            pkg_name},
        {"pkg_version",         pkg_version},
        {"reload_serial",       reload_serial},
        {"audio_buffer_size",   audio_buffer_size},
        {"audio_sample_rate",   audio_sample_rate},
        {"control_server_port", control_server_port},
        {"mcp_attached",        mcp_attached},
        {"audio_device",        audio_device},
        {"gpu_adapter",         gpu_adapter},
        {"last_mutation",       last_mutation},
    };
}

CrashRecord CrashRecord::from_json(const nlohmann::json& j) {
    CrashRecord r;
    r.timestamp           = json_get_or<std::string>(j, "timestamp", "");
    r.signal              = json_get_or<int>(j, "signal", 0);
    r.signal_name         = json_get_or<std::string>(j, "signal_name", "");
    r.pid                 = json_get_or<int>(j, "pid", 0);
    r.vivid_version       = json_get_or<std::string>(j, "vivid_version", "");
    r.platform            = json_get_or<std::string>(j, "platform", "");
    r.graph_path          = json_get_or<std::string>(j, "graph_path", "");
    r.graph_dirty         = json_get_or<bool>(j, "graph_dirty", false);
    r.operator_name       = json_get_or<std::string>(j, "operator_name", "");
    r.node_id             = json_get_or<std::string>(j, "node_id", "");
    r.node_type           = json_get_or<std::string>(j, "node_type", "");
    r.pkg_name            = json_get_or<std::string>(j, "pkg_name", "");
    r.pkg_version         = json_get_or<std::string>(j, "pkg_version", "");
    r.reload_serial       = json_get_or<uint64_t>(j, "reload_serial", 0);
    r.audio_buffer_size   = json_get_or<uint32_t>(j, "audio_buffer_size", 0);
    r.audio_sample_rate   = json_get_or<uint32_t>(j, "audio_sample_rate", 0);
    r.control_server_port = json_get_or<int>(j, "control_server_port", 0);
    r.mcp_attached        = json_get_or<bool>(j, "mcp_attached", false);
    r.audio_device        = json_get_or<std::string>(j, "audio_device", "");
    r.gpu_adapter         = json_get_or<std::string>(j, "gpu_adapter", "");
    r.last_mutation       = json_get_or<std::string>(j, "last_mutation", "");
    return r;
}

// ---------------------------------------------------------------------------
// CrashRecoveryManager
// ---------------------------------------------------------------------------

CrashRecoveryManager::CrashRecoveryManager(std::string crash_dir)
    : crash_dir_(std::move(crash_dir)) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(crash_dir_, ec);
    marker_path_       = (fs::path(crash_dir_) / "crash.marker").string();
    snapshot_path_     = (fs::path(crash_dir_) / "latest-snapshot.json").string();
    latest_crash_path_ = (fs::path(crash_dir_) / "latest-crash.json").string();
}

std::optional<CrashRecord> CrashRecoveryManager::init() {
    namespace fs = std::filesystem;

    prune_history(kHistoryKeep);

    std::error_code ec;
    if (!fs::exists(marker_path_, ec)) {
        return std::nullopt;
    }

    // Read marker text.
    std::string marker_text;
    {
        std::ifstream ifs(marker_path_);
        std::stringstream ss;
        ss << ifs.rdbuf();
        marker_text = ss.str();
    }

    // Read snapshot JSON (tolerate missing/invalid file).
    nlohmann::json snapshot;
    std::ifstream sifs(snapshot_path_);
    if (sifs) {
        try { sifs >> snapshot; } catch (...) { snapshot = nlohmann::json::object(); }
    } else {
        snapshot = nlohmann::json::object();
    }

    CrashRecord rec = merge_marker_and_snapshot(marker_text, snapshot);

    // Write latest-crash.json and timestamped history entry.
    nlohmann::json out = rec.to_json();
    write_json_atomic(latest_crash_path_, out);

    std::string history_path =
        (fs::path(crash_dir_) / (iso8601_filename_now() + ".json")).string();
    write_json_atomic(history_path, out);

    // Clean up marker so the next run doesn't re-expand it.
    fs::remove(marker_path_, ec);

    // Re-prune in case we just exceeded the cap by adding a history entry.
    prune_history(kHistoryKeep);

    return rec;
}

void CrashRecoveryManager::install_signal_paths() {
    set_crash_marker_paths(marker_path_.c_str(), snapshot_path_.c_str());
}

void CrashRecoveryManager::tick(uint64_t             frame_count,
                                const Graph&         graph,
                                const RuntimeAPI&    api,
                                const AudioEngine&   audio,
                                const ControlServer* server) {
    const uint64_t serial = api.reload_serial();
    const bool serial_changed = (serial != last_reload_serial_);
    const bool interval_elapsed =
        first_tick_ || (frame_count - last_snapshot_frame_ >= kSnapshotEveryFrames);

    if (!first_tick_ && !serial_changed && !interval_elapsed) return;

    nlohmann::json j = build_snapshot_json(graph, api, audio, server);
    write_json_atomic(snapshot_path_, j);

    last_snapshot_frame_ = frame_count;
    last_reload_serial_  = serial;
    first_tick_ = false;
}

void CrashRecoveryManager::force_snapshot(const Graph&         graph,
                                          const RuntimeAPI&    api,
                                          const AudioEngine&   audio,
                                          const ControlServer* server) {
    nlohmann::json j = build_snapshot_json(graph, api, audio, server);
    write_json_atomic(snapshot_path_, j);
    last_reload_serial_ = api.reload_serial();
    first_tick_ = false;
}

void CrashRecoveryManager::clear_latest() {
    std::error_code ec;
    std::filesystem::remove(latest_crash_path_, ec);
}

void CrashRecoveryManager::prune_history(size_t keep) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(crash_dir_, ec)) return;

    // Collect dated history files.  We identify them by filename pattern:
    // anything matching `<digits-and-dashes>.json` that is NOT one of the
    // reserved files (latest-crash.json, latest-snapshot.json).
    std::vector<fs::path> history;
    for (auto& entry : fs::directory_iterator(crash_dir_, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const auto fname = entry.path().filename().string();
        if (fname == "latest-crash.json")    continue;
        if (fname == "latest-snapshot.json") continue;
        if (fname == "crash.marker")         continue;
        if (entry.path().extension() != ".json") continue;
        history.push_back(entry.path());
    }

    if (history.size() <= keep) return;

    // Filenames are ISO-8601-ish timestamps, so lexicographic sort matches
    // chronological order.  Keep the newest `keep` files; delete the rest.
    std::sort(history.begin(), history.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.filename().string() < b.filename().string();
              });
    const size_t to_remove = history.size() - keep;
    for (size_t i = 0; i < to_remove; ++i) {
        fs::remove(history[i], ec);
    }
}

CrashRecord CrashRecoveryManager::merge_for_test(const std::string& marker_path,
                                                 const std::string& snapshot_path) const {
    std::string marker_text;
    std::ifstream mifs(marker_path);
    if (mifs) {
        std::stringstream ss;
        ss << mifs.rdbuf();
        marker_text = ss.str();
    }
    nlohmann::json snapshot = nlohmann::json::object();
    std::ifstream sifs(snapshot_path);
    if (sifs) {
        try { sifs >> snapshot; } catch (...) {}
    }
    return merge_marker_and_snapshot(marker_text, snapshot);
}

// ---- private helpers ------------------------------------------------------

nlohmann::json CrashRecoveryManager::build_snapshot_json(const Graph&         graph,
                                                         const RuntimeAPI&    api,
                                                         const AudioEngine&   audio,
                                                         const ControlServer* server) const {
    nlohmann::json j;
    j["vivid_version"]       = VIVID_CORE_VERSION;
    j["platform"]            = current_platform();
    j["pid"]                 = static_cast<int>(::getpid());
    j["graph_path"]          = graph.source_path();
    j["graph_dirty"]         = api.graph_dirty();
    j["reload_serial"]       = api.reload_serial();
    j["audio_buffer_size"]   = audio.buffer_size();
    j["audio_sample_rate"]   = audio.sample_rate();
    j["control_server_port"] = server ? server->port() : 0;

    bool mcp_attached = false;
    if (server) {
        mcp_attached = server->mcp_last_ping_ms("vivid") != 0
                    || server->mcp_last_ping_ms("opdev") != 0;
    }
    j["mcp_attached"] = mcp_attached;

    // Reserved fields — leave empty for Phase 1.
    j["audio_device"]  = "";
    j["gpu_adapter"]   = "";
    j["last_mutation"] = "";

    // Node map — lets the marker's operator_name be resolved to a node.
    nlohmann::json nodes = nlohmann::json::array();
    for (const auto& n : graph.nodes()) {
        nodes.push_back({
            {"node_id",     n.id},
            {"type_name",   n.type},
            {"pkg_name",    n.pkg_name},
            {"pkg_version", n.pkg_version},
        });
    }
    j["nodes"] = std::move(nodes);

    return j;
}

CrashRecord CrashRecoveryManager::merge_marker_and_snapshot(
        const std::string&    marker_text,
        const nlohmann::json& snapshot) const {
    CrashRecord rec;
    rec.timestamp = iso8601_utc_now();

    // Parse marker: lines of "key=value".
    std::string operator_name;
    int sig = 0;
    {
        size_t pos = 0;
        while (pos < marker_text.size()) {
            size_t nl = marker_text.find('\n', pos);
            std::string line = marker_text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = (nl == std::string::npos) ? marker_text.size() : nl + 1;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            if (key == "signal") {
                try { sig = std::stoi(val); } catch (...) { sig = 0; }
            } else if (key == "operator") {
                operator_name = val;
            }
            // snapshot= path is informational — we already know our own path.
        }
    }

    rec.signal        = sig;
    rec.signal_name   = signal_name_for(sig);
    rec.operator_name = operator_name;

    // Snapshot-sourced fields (defaults if snapshot missing/invalid).
    rec.pid                 = json_get_or<int>(snapshot, "pid", 0);
    rec.vivid_version       = json_get_or<std::string>(snapshot, "vivid_version", "");
    rec.platform            = json_get_or<std::string>(snapshot, "platform", "");
    rec.graph_path          = json_get_or<std::string>(snapshot, "graph_path", "");
    rec.graph_dirty         = json_get_or<bool>(snapshot, "graph_dirty", false);
    rec.reload_serial       = json_get_or<uint64_t>(snapshot, "reload_serial", 0);
    rec.audio_buffer_size   = json_get_or<uint32_t>(snapshot, "audio_buffer_size", 0);
    rec.audio_sample_rate   = json_get_or<uint32_t>(snapshot, "audio_sample_rate", 0);
    rec.control_server_port = json_get_or<int>(snapshot, "control_server_port", 0);
    rec.mcp_attached        = json_get_or<bool>(snapshot, "mcp_attached", false);
    rec.audio_device        = json_get_or<std::string>(snapshot, "audio_device", "");
    rec.gpu_adapter         = json_get_or<std::string>(snapshot, "gpu_adapter", "");
    rec.last_mutation       = json_get_or<std::string>(snapshot, "last_mutation", "");

    // Resolve the operator name against the node map.  CrashGuard records
    // cn.node_id (see frame_executor / audio_executor process_*), so match
    // node_id first.  Fall back to type_name so a future CrashGuard payload
    // change — or a marker hand-authored by a tool — still resolves cleanly.
    // First hit wins.
    if (!operator_name.empty() && snapshot.is_object()) {
        auto it = snapshot.find("nodes");
        if (it != snapshot.end() && it->is_array()) {
            for (const auto& n : *it) {
                const std::string node_id   = json_get_or<std::string>(n, "node_id", "");
                const std::string type_name = json_get_or<std::string>(n, "type_name", "");
                if (node_id == operator_name || type_name == operator_name) {
                    rec.node_id     = node_id;
                    rec.node_type   = type_name;
                    rec.pkg_name    = json_get_or<std::string>(n, "pkg_name", "");
                    rec.pkg_version = json_get_or<std::string>(n, "pkg_version", "");
                    break;
                }
            }
        }
    }

    return rec;
}

void CrashRecoveryManager::write_json_atomic(const std::string&    path,
                                             const nlohmann::json& j) const {
    namespace fs = std::filesystem;
    const std::string tmp_path = path + ".tmp";
    {
        std::ofstream ofs(tmp_path, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            std::fprintf(stderr, "[vivid] crash_recovery: failed to open '%s' for writing\n",
                         tmp_path.c_str());
            return;
        }
        ofs << j.dump(2) << '\n';
    }
    std::error_code ec;
    fs::rename(tmp_path, path, ec);
    if (ec) {
        std::fprintf(stderr, "[vivid] crash_recovery: rename '%s' -> '%s' failed: %s\n",
                     tmp_path.c_str(), path.c_str(), ec.message().c_str());
        fs::remove(tmp_path, ec);
    }
}

} // namespace vivid
