#pragma once
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <string>
#include <chrono>
#include <unistd.h>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

static void check_float(float actual, float expected, float tol, const char* msg) {
    if (std::fabs(actual - expected) > tol) {
        std::fprintf(stderr, "  FAIL: %s (expected %f, got %f)\n", msg, expected, actual);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s (%f)\n", msg, actual);
    }
}

static void check_float(float actual, float expected, const char* msg) {
    check_float(actual, expected, 1e-4f, msg);
}

// RAII temporary directory — unique per process, cleaned up on destruction.
struct ScopedTempDir {
    std::filesystem::path path;

    explicit ScopedTempDir(const char* prefix = "vivid_test") {
        auto ns = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               (std::string(prefix) + "_" + std::to_string(getpid()) + "_" +
                std::to_string(ns));
        std::filesystem::create_directories(path);
    }

    ~ScopedTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    ScopedTempDir(const ScopedTempDir&) = delete;
    ScopedTempDir& operator=(const ScopedTempDir&) = delete;

    std::string str() const { return path.string(); }
    std::filesystem::path operator/(const std::string& child) const { return path / child; }
    std::filesystem::path operator/(const char* child) const { return path / child; }
};
