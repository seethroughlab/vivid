#include "runtime/core/hot_reload.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

static bool wait_until_ms(const std::function<bool()>& pred, int timeout_ms) {
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        if (pred()) return true;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count() >= timeout_ms) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

int main() {
    std::fprintf(stderr, "\n=== Test: HotReloader queue coalescing ===\n");

    std::filesystem::path tmp = std::filesystem::temp_directory_path() / "vivid_test_hot_reloader_queue";
    std::filesystem::create_directories(tmp);

    vivid::HotReloader hr;
    check(hr.start(tmp.string()), "start() succeeds");

    std::atomic<int> compile_calls{0};
    std::atomic<bool> first_started{false};

    hr.set_package_compiler([&](const std::string& target) -> vivid::ReloadResult {
        vivid::ReloadResult rr;
        rr.target_name = target;
        rr.success = true;
        int call_index = ++compile_calls;
        if (call_index == 1) {
            first_started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        return rr;
    });

    const std::string target = "pkg:vivid-test:op";

    // Burst before compile starts: should collapse to one queued job.
    hr.queue_rebuild(target);
    hr.queue_rebuild(target);
    hr.queue_rebuild(target);

    check(wait_until_ms([&] { return first_started.load(); }, 1000),
          "first compile started");

    // Burst during in-flight compile: should schedule exactly one deferred pass.
    hr.queue_rebuild(target);
    hr.queue_rebuild(target);
    hr.queue_rebuild(target);

    check(wait_until_ms([&] { return compile_calls.load() >= 2; }, 2000),
          "deferred compile pass executed");

    // Give thread time to settle; there should be no third compile.
    std::this_thread::sleep_for(std::chrono::milliseconds(220));
    check(compile_calls.load() == 2, "duplicate bursts coalesced to 2 total compiles");

    auto results = hr.poll_ready();
    check(results.size() == 2, "poll_ready returns 2 compile results");
    if (results.size() == 2) {
        check(results[0].success && results[1].success, "both compile results succeeded");
    }

    hr.stop();
    std::filesystem::remove_all(tmp);

    // --- Test: exception safety — throwing compile fn must not permanently stall the reloader ---
    std::fprintf(stderr, "\n=== Test: HotReloader exception safety ===\n");
    {
        std::filesystem::path tmp2 = std::filesystem::temp_directory_path() / "vivid_test_hot_reloader_exc";
        std::filesystem::create_directories(tmp2);

        vivid::HotReloader hr2;
        check(hr2.start(tmp2.string()), "exc: start() succeeds");

        std::atomic<int> exc_calls{0};
        hr2.set_package_compiler([&](const std::string& target) -> vivid::ReloadResult {
            ++exc_calls;
            throw std::runtime_error("simulated compile failure");
        });

        const std::string t2 = "pkg:vivid-test:throwing-op";

        // First rebuild — compile fn throws.
        hr2.queue_rebuild(t2);
        check(wait_until_ms([&] { return exc_calls.load() >= 1; }, 1000),
              "exc: throwing compile fn was called");

        // Give the thread time to process the exception and clear in_flight_targets_.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // A failed result should have been emitted.
        auto exc_results = hr2.poll_ready();
        check(!exc_results.empty(), "exc: failed result emitted after exception");
        if (!exc_results.empty()) {
            check(!exc_results[0].success, "exc: result.success == false");
            check(!exc_results[0].error_output.empty(), "exc: result.error_output is populated");
        }

        // Second rebuild — reloader must accept a new job (in_flight_targets_ must be clear).
        std::atomic<int> exc_calls2{0};
        hr2.set_package_compiler([&](const std::string& target) -> vivid::ReloadResult {
            ++exc_calls2;
            vivid::ReloadResult rr;
            rr.target_name = target;
            rr.success = true;
            return rr;
        });
        hr2.queue_rebuild(t2);
        check(wait_until_ms([&] { return exc_calls2.load() >= 1; }, 1000),
              "exc: reloader accepts second rebuild after exception (not permanently stalled)");

        hr2.stop();
        std::filesystem::remove_all(tmp2);
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
