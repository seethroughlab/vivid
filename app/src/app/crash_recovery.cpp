// The App-free half of crash recovery (record model + marker→record + history). Kept separate from
// crash_recovery_snapshot.cpp (which links App/VisualGraph for the warm snapshot) so this compiles
// into the headless test — same split as runtime_health.cpp / runtime_health_collect.cpp.
#include "app/crash_recovery.h"

#include "app/crash_guard.h"     // set_crash_marker_paths

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <vector>

namespace vivid {
namespace fs = std::filesystem;
using nlohmann::json;

namespace {
constexpr int kHistoryKeep = 20;
constexpr const char* kMarkerName   = "crash.marker";
constexpr const char* kSnapshotName = "latest-snapshot.json";
constexpr const char* kLatestName   = "latest-crash.json";

const char* signal_name_of(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV"; case SIGBUS: return "SIGBUS"; case SIGILL: return "SIGILL";
        case SIGFPE:  return "SIGFPE";  case SIGABRT: return "SIGABRT";
        default: return "UNKNOWN";
    }
}

// ISO 8601 UTC. `filename_safe` swaps ':' for '-' (colons are illegal in some filesystems).
std::string iso8601_utc(std::time_t t, bool filename_safe) {
    std::tm tm{}; gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, filename_safe ? "%Y-%m-%dT%H-%M-%SZ" : "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string read_text(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void write_json_atomic(const std::string& path, const json& j) {
    const std::string tmp = path + ".tmp";
    { std::ofstream f(tmp, std::ios::trunc); if (!f) return; f << j.dump(2); }
    std::error_code ec; fs::rename(tmp, path, ec);
}

// Parse the marker's "key=value\n" lines into (signal, operator, snapshot).
void parse_marker(const std::string& text, int& signal, std::string& op, std::string& snap) {
    size_t i = 0;
    while (i < text.size()) {
        size_t nl = text.find('\n', i);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(i, nl - i);
        const size_t eq = line.find('=');
        if (eq != std::string::npos) {
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "signal")        signal = std::atoi(v.c_str());
            else if (k == "operator") op = v;
            else if (k == "snapshot") snap = v;
        }
        i = nl + 1;
    }
}
}  // namespace

json CrashRecord::to_json() const {
    return json{ {"timestamp", timestamp}, {"unix_time", unix_time}, {"signal", signal},
                 {"signal_name", signal_name}, {"operator", operator_name}, {"node_id", node_id},
                 {"node_type", node_type}, {"app_version", app_version} };
}
CrashRecord CrashRecord::from_json(const json& j) {
    CrashRecord r;
    r.timestamp    = j.value("timestamp", std::string());
    r.unix_time    = j.value("unix_time", 0LL);
    r.signal       = j.value("signal", 0);
    r.signal_name  = j.value("signal_name", std::string());
    r.operator_name= j.value("operator", std::string());
    r.node_id      = j.value("node_id", std::string());
    r.node_type    = j.value("node_type", std::string());
    r.app_version  = j.value("app_version", std::string());
    return r;
}

CrashRecovery::CrashRecovery(const std::string& crash_dir) : dir_(crash_dir) {
    std::error_code ec; fs::create_directories(dir_, ec);   // the handler can't mkdir async-safely
    marker_path_   = (fs::path(dir_) / kMarkerName).string();
    snapshot_path_ = (fs::path(dir_) / kSnapshotName).string();
    latest_path_   = (fs::path(dir_) / kLatestName).string();
}

void CrashRecovery::install_signal_paths() const {
    set_crash_marker_paths(marker_path_.c_str(), snapshot_path_.c_str());
}

std::optional<CrashRecord> CrashRecovery::init() {
    std::error_code ec;
    if (!fs::exists(marker_path_, ec)) return std::nullopt;

    int sig = 0; std::string op, snap_path;
    parse_marker(read_text(marker_path_), sig, op, snap_path);

    CrashRecord rec;
    const std::time_t now = std::time(nullptr);
    rec.timestamp   = iso8601_utc(now, /*filename_safe=*/false);
    rec.unix_time   = static_cast<long long>(now);
    rec.signal      = sig;
    rec.signal_name = signal_name_of(sig);
    rec.operator_name = op;
    rec.node_type   = op;   // a visual op's marker name IS its node type

    // From the warm snapshot: the app version at crash time, and the node whose type matches the
    // crashed operator (its id). Tolerant of a missing/corrupt snapshot.
    if (!snap_path.empty()) {
        json snap = json::parse(read_text(snap_path), nullptr, false);
        if (snap.is_object()) {
            rec.app_version = snap.value("app_version", std::string());
            if (snap.contains("nodes") && snap["nodes"].is_array())
                for (const auto& n : snap["nodes"])
                    if (n.value("type", std::string()) == op) { rec.node_id = std::to_string(n.value("node_id", -1)); break; }
        }
    }

    // Persist: a timestamped history entry + latest-crash.json, then drop the marker + prune.
    write_json_atomic((fs::path(dir_) / (iso8601_utc(now, true) + ".json")).string(), rec.to_json());
    write_json_atomic(latest_path_, rec.to_json());
    fs::remove(marker_path_, ec);

    // Prune history to the newest kHistoryKeep (ISO names sort chronologically), skipping reserved files.
    std::vector<std::string> hist;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        const std::string name = e.path().filename().string();
        if (e.path().extension() != ".json") continue;
        if (name == kSnapshotName || name == kLatestName) continue;
        hist.push_back(e.path().string());
    }
    std::sort(hist.begin(), hist.end());
    if (hist.size() > static_cast<size_t>(kHistoryKeep))
        for (size_t i = 0; i + kHistoryKeep < hist.size(); ++i) fs::remove(hist[i], ec);

    return rec;
}

}  // namespace vivid
