#include "runtime/core/file_watcher.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

// Create a temp directory with the layout FileWatcher::start() expects:
//   <root>/<domain>/<name>/<name>.cpp
static fs::path make_operator_tree(const fs::path& base, const char* domain,
                                   const char* name) {
    fs::path op_dir = base / domain / name;
    fs::create_directories(op_dir);
    fs::path cpp = op_dir / (std::string(name) + ".cpp");
    std::ofstream(cpp) << "// stub\n";
    return cpp;
}

static void touch(const fs::path& p) {
    std::ofstream f(p, std::ios::app);
    f << " ";
}

int main() {
    std::fprintf(stderr, "--- test_file_watcher ---\n");

    // Shared temp root
    fs::path tmp = fs::temp_directory_path() / "vivid_fw_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    // --- Test 1: start() on valid operator tree returns true ---
    {
        fs::path root = tmp / "t1";
        make_operator_tree(root, "audio", "myop");

        vivid::FileWatcher fw;
        bool ok = fw.start(root.string());
        check(ok, "start() on valid operator tree returns true");
        fw.stop();
        fs::remove_all(root);
    }

    // --- Test 2: add_watch() + write → poll returns event with correct target ---
    {
        // Use add_watch() directly (no start() scan needed, just need kqueue open)
        // We must call start() so kq_ is initialised, which requires valid tree.
        fs::path root = tmp / "t2";
        fs::path cpp = make_operator_tree(root, "audio", "myop2");

        vivid::FileWatcher fw;
        fw.start(root.string());  // scans myop2.cpp automatically

        // Touch the file to trigger an event
        touch(cpp);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        auto events = fw.poll_changes();
        bool found = false;
        for (auto& e : events) {
            if (e.target_name == "myop2") { found = true; break; }
        }
        check(found, "write to watched file produces event with correct target_name");
        fw.stop();
        fs::remove_all(root);
    }

    // --- Test 3: debounce — two rapid writes → at most 1 event ---
    {
        fs::path root = tmp / "t3";
        fs::path cpp = make_operator_tree(root, "audio", "myop3");

        vivid::FileWatcher fw;
        fw.start(root.string());

        touch(cpp);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        touch(cpp);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        auto events = fw.poll_changes();
        int count = 0;
        for (auto& e : events) {
            if (e.target_name == "myop3") count++;
        }
        check(count <= 1, "debounce: two rapid writes produce at most 1 event");
        fw.stop();
        fs::remove_all(root);
    }

    // --- Test 4: stop() after start() doesn't crash ---
    {
        fs::path root = tmp / "t4";
        make_operator_tree(root, "audio", "myop4");

        vivid::FileWatcher fw;
        fw.start(root.string());
        fw.stop();  // should not crash or hang
        check(true, "stop() after start() doesn't crash");
        fs::remove_all(root);
    }

    // --- Test 5: add_watch() on nonexistent file returns false ---
    {
        fs::path root = tmp / "t5";
        make_operator_tree(root, "audio", "myop5");

        vivid::FileWatcher fw;
        fw.start(root.string());

        bool ok = fw.add_watch("/nonexistent/path/nope.cpp", "nope");
        check(!ok, "add_watch() on nonexistent file returns false");
        fw.stop();
        fs::remove_all(root);
    }

    // --- Test 6: poll_changes() on idle watcher returns empty ---
    {
        fs::path root = tmp / "t6";
        make_operator_tree(root, "audio", "myop6");

        vivid::FileWatcher fw;
        fw.start(root.string());

        auto events = fw.poll_changes();
        check(events.empty(), "poll_changes() on idle watcher returns empty");
        fw.stop();
        fs::remove_all(root);
    }

    // --- Test 7: add_package_watches skips unreadable subdirectory, still counts good ones ---
    {
        // Build a packages dir with two packages: one good, one whose operators/ dir
        // is not readable. The good package should still be watched.
        fs::path pkgs = tmp / "t7_pkgs";
        fs::create_directories(pkgs);

        // Good package: pkgs/good_pkg/operators/audio/myop/myop.cpp
        fs::path good_op = pkgs / "good_pkg" / "operators" / "audio" / "myop";
        fs::create_directories(good_op);
        std::ofstream(good_op / "myop.cpp") << "// stub\n";

        // Bad package: exists but operators/ dir has permissions 000
        fs::path bad_ops = pkgs / "bad_pkg" / "operators";
        fs::create_directories(bad_ops);
        // Remove read permission so directory_iterator will fail
        fs::permissions(bad_ops, fs::perms::none);

        // We need a running FileWatcher (start() requires a valid operators tree).
        fs::path root = tmp / "t7_root";
        make_operator_tree(root, "audio", "dummy7");
        vivid::FileWatcher fw;
        fw.start(root.string());

        int count = fw.add_package_watches(pkgs.string());

        // Restore permissions before cleanup
        fs::permissions(bad_ops, fs::perms::all);

        check(count > 0,
              "add_package_watches: unreadable subdirectory doesn't abort scan of other packages");

        fw.stop();
        fs::remove_all(root);
        fs::remove_all(pkgs);
    }

    fs::remove_all(tmp);
    std::fprintf(stderr, "%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
