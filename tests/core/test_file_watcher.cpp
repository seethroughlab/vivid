#include "runtime/core/file_watcher.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>
#include "test_helpers.h"

namespace fs = std::filesystem;

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

static std::string normalized_path(const fs::path& p) {
    std::error_code ec;
    auto normalized = fs::weakly_canonical(p, ec);
    if (ec)
        normalized = fs::absolute(p, ec);
    if (ec)
        normalized = p;
    return normalized.lexically_normal().string();
}

static bool has_event_for_target(vivid::FileWatcher& fw, const std::string& target) {
    for (int attempt = 0; attempt < 40; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto events = fw.poll_changes();
        for (auto& e : events) {
            if (e.target_name == target)
                return true;
        }
    }
    return false;
}

static bool has_event_for_target_and_path(vivid::FileWatcher& fw,
                                          const std::string& target,
                                          const std::string& path) {
    for (int attempt = 0; attempt < 40; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto events = fw.poll_changes();
        for (auto& e : events) {
            if (e.target_name == target && e.file_path == path)
                return true;
        }
    }
    return false;
}

static void drain_initial_events(vivid::FileWatcher& fw) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    (void)fw.poll_changes();
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
        fs::path root = tmp / "t2";
        fs::path cpp = make_operator_tree(root, "audio", "myop2");

        vivid::FileWatcher fw;
        fw.start(root.string());  // scans myop2.cpp automatically
        drain_initial_events(fw);

        // Touch the file to trigger an event
        touch(cpp);
        bool found = has_event_for_target(fw, "myop2");
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
        drain_initial_events(fw);

        touch(cpp);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        touch(cpp);
        std::this_thread::sleep_for(std::chrono::milliseconds(350));

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
        fs::path pkgs = tmp / "t7_pkgs";
        fs::create_directories(pkgs);

        // Good package: pkgs/good_pkg/operators/audio/myop/myop.cpp
        fs::path good_op = pkgs / "good_pkg" / "operators" / "audio" / "myop";
        fs::create_directories(good_op);
        std::ofstream(good_op / "myop.cpp") << "// stub\n";

        // Bad package: exists but operators/ dir has permissions 000
        fs::path bad_ops = pkgs / "bad_pkg" / "operators";
        fs::create_directories(bad_ops);
        fs::permissions(bad_ops, fs::perms::none);

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

    // --- Test 8: rename-on-save (delete + create) produces event ---
    {
        fs::path root = tmp / "t8";
        fs::path cpp = make_operator_tree(root, "audio", "myop8");

        vivid::FileWatcher fw;
        fw.start(root.string());
        drain_initial_events(fw);

        // Simulate editor rename-on-save: delete original, write new file at same path
        fs::remove(cpp);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        { std::ofstream(cpp) << "// updated\n"; }
        bool found = has_event_for_target(fw, "myop8");
        check(found, "rename-on-save (delete + create) produces event for target");
        fw.stop();
        fs::remove_all(root);
    }

    // --- Test 9: pure delete produces event ---
    {
        fs::path root = tmp / "t9";
        fs::path cpp = make_operator_tree(root, "audio", "myop9");

        vivid::FileWatcher fw;
        fw.start(root.string());
        drain_initial_events(fw);

        fs::remove(cpp);

        bool found = has_event_for_target(fw, "myop9");
        check(found, "pure delete of watched .cpp produces event for target");
        fw.stop();
        fs::remove_all(root);
    }

    // --- Test 10: package watch maps to pkg:<package>:<operator> ---
    {
        fs::path root = tmp / "t10_root";
        make_operator_tree(root, "audio", "dummy10");
        fs::path pkgs = tmp / "t10_pkgs";
        fs::path pkg_op = pkgs / "good_pkg" / "operators" / "audio" / "pkgop";
        fs::create_directories(pkg_op);
        fs::path cpp = pkg_op / "pkgop.cpp";
        std::ofstream(cpp) << "// stub\n";

        vivid::FileWatcher fw;
        fw.start(root.string());
        int count = fw.add_package_watches(pkgs.string());
        check(count > 0, "add_package_watches registers package .cpp files");
        drain_initial_events(fw);

        touch(cpp);

        bool found = has_event_for_target(fw, "pkg:good_pkg:pkgop");
        check(found, "package .cpp modification maps to pkg:<package>:<operator>");
        fw.stop();
        fs::remove_all(root);
        fs::remove_all(pkgs);
    }

    // --- Test 11: shader watch maps .wgsl and ignores non-WGSL ---
    {
        fs::path root = tmp / "t11_root";
        make_operator_tree(root, "audio", "dummy11");
        fs::path shader_dir = tmp / "t11_filters";
        fs::create_directories(shader_dir);
        fs::path wgsl = shader_dir / "ripple.wgsl";
        fs::path ignored = shader_dir / "notes.txt";
        std::ofstream(wgsl) << "// shader\n";
        std::ofstream(ignored) << "ignore me\n";

        vivid::FileWatcher fw;
        fw.start(root.string());
        int count = fw.add_shader_operator_watches(shader_dir.string());
        check(count == 1, "add_shader_operator_watches registers only .wgsl files");
        drain_initial_events(fw);

        touch(ignored);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        auto ignored_events = fw.poll_changes();
        bool saw_ignored = false;
        for (auto& e : ignored_events) {
            if (e.file_path == normalized_path(ignored) || e.target_name.find("notes.txt") != std::string::npos)
                saw_ignored = true;
        }
        check(!saw_ignored, "shader watch ignores non-WGSL files");

        touch(wgsl);

        bool found = has_event_for_target(fw, "shader:" + normalized_path(wgsl));
        check(found, "shader .wgsl modification maps to shader:<absolute path>");
        fw.stop();
        fs::remove_all(root);
        fs::remove_all(shader_dir);
    }

    // --- Test 12: directory watch catches newly created operator .cpp ---
    {
        fs::path root = tmp / "t12";
        make_operator_tree(root, "audio", "existing12");

        vivid::FileWatcher fw;
        fw.start(root.string());
        drain_initial_events(fw);

        fs::path fresh_dir = root / "audio" / "existing12";
        fs::path fresh_cpp = fresh_dir / "extra.cpp";
        std::ofstream(fresh_cpp) << "// new operator\n";

        bool found = has_event_for_target_and_path(fw, "existing12", normalized_path(fresh_cpp));
        check(found, "directory watch catches newly created .cpp under watched operator tree");
        fw.stop();
        fs::remove_all(root);
    }

    fs::remove_all(tmp);
    std::fprintf(stderr, "%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
