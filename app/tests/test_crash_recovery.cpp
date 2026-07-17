// Headless tests for ADR-0018 R1/R2 crash attribution + recovery.
//  - R2: a marker + warm snapshot from a prior crash reconstruct an attributed CrashRecord and land
//        in a capped history (deterministic, no real crash needed).
//  - R1: a forked child sets a CrashGuard, crashes, and the async-signal-safe handler writes an
//        ATTRIBUTED marker naming that operator — proving the guard + handler end to end.
#include "app/crash_guard.h"
#include "app/crash_recovery.h"
#include "test_helpers.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

// Are we compiling under a sanitizer? (Portable across gcc/clang — __has_feature must never appear
// in a flat #if on gcc, where it isn't defined; hence the nested guard, mirroring crash_guard.h.)
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#  define VIVID_UNDER_SANITIZER 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#    define VIVID_UNDER_SANITIZER 1
#  endif
#endif
#ifndef VIVID_UNDER_SANITIZER
#  define VIVID_UNDER_SANITIZER 0
#endif

namespace fs = std::filesystem;
using nlohmann::json;

static std::string read_file(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return f ? std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()) : std::string();
}

int main() {
    using namespace vivid;

    const fs::path base = fs::temp_directory_path() / ("vivid_crash_test_" + std::to_string(::getpid()));
    fs::remove_all(base);

    // ---- R2: marker + snapshot -> attributed record + history ----
    {
        const std::string dir = (base / "crashes").string();
        fs::create_directories(dir);
        const std::string snap = (fs::path(dir) / "latest-snapshot.json").string();
        { std::ofstream(snap, std::ios::trunc)
            << json{ {"nodes", json::array({ json{{"node_id", 7}, {"type", "BadOp"}} })} }.dump(); }
        // The marker crash_guard's handler would have written (SIGSEGV=11).
        { std::ofstream(( fs::path(dir) / "crash.marker").string(), std::ios::trunc)
            << "signal=11\noperator=BadOp\nsnapshot=" << snap << "\n"; }

        CrashRecovery rec(dir);
        auto record = rec.init();
        CHECK(record.has_value());
        if (record) {
            CHECK(record->signal == 11);
            CHECK(record->signal_name == "SIGSEGV");
            CHECK(record->operator_name == "BadOp");
            CHECK(record->node_id == "7");          // resolved from the snapshot
            CHECK(record->node_type == "BadOp");
            CHECK(!record->timestamp.empty());
        }
        // The marker is consumed; a latest-crash.json + a timestamped history entry are written.
        CHECK(!fs::exists((fs::path(dir) / "crash.marker")));
        CHECK(fs::exists((fs::path(dir) / "latest-crash.json")));
        int history = 0;
        for (const auto& e : fs::directory_iterator(dir)) {
            const std::string n = e.path().filename().string();
            if (e.path().extension() == ".json" && n != "latest-snapshot.json" && n != "latest-crash.json") ++history;
        }
        CHECK(history == 1);

        // A second init() with no marker returns nullopt (nothing to recover).
        CHECK(!rec.init().has_value());
    }

    // ---- R1: CrashGuard + async-signal-safe handler write an ATTRIBUTED marker ----
    // Skipped under a sanitizer build: install_crash_handlers() is a no-op there (ASan owns SEGV),
    // so the child would die without writing our marker. The handler itself is unchanged.
#if !VIVID_UNDER_SANITIZER
    {
        const std::string marker = (base / "r1.marker").string();
        const std::string snap   = (base / "r1.snapshot").string();
        pid_t pid = fork();
        if (pid == 0) {                       // child: arm the guard + crash inside it
            set_crash_marker_paths(marker.c_str(), snap.c_str());
            install_crash_handlers();
            CrashGuard cg("CrashyOp");
            raise(SIGSEGV);                   // the handler names CrashyOp, writes the marker, re-raises
            _exit(0);                         // unreachable
        }
        CHECK(pid > 0);
        int status = 0; waitpid(pid, &status, 0);
        CHECK(WIFSIGNALED(status));           // the child died from the re-raised signal
        const std::string m = read_file(marker);
        CHECK(m.find("operator=CrashyOp") != std::string::npos);   // attributed to the guarded operator
        CHECK(m.find("signal=") != std::string::npos);
    }
#endif

    fs::remove_all(base);
    return vivid::test::summary("test_crash_recovery");
}
