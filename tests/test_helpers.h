#pragma once
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <chrono>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef VIVID_TEST_HELPERS_NO_CHECK
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
#endif

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
    std::string file_str(const std::string& child) const { return (path / child).string(); }
    std::string file_str(const char* child) const { return (path / child).string(); }
};

struct ScopedEnvVar {
    std::string key;
    std::string old_value;
    bool had_old_value = false;

    ScopedEnvVar(const char* env_key, const std::string& value)
        : key(env_key ? env_key : "") {
        if (key.empty()) return;
        if (const char* existing = std::getenv(key.c_str())) {
            had_old_value = true;
            old_value = existing;
        }
        setenv(key.c_str(), value.c_str(), 1);
    }

    ScopedEnvVar(const char* env_key, std::nullptr_t)
        : key(env_key ? env_key : "") {
        if (key.empty()) return;
        if (const char* existing = std::getenv(key.c_str())) {
            had_old_value = true;
            old_value = existing;
        }
        unsetenv(key.c_str());
    }

    ~ScopedEnvVar() {
        if (key.empty()) return;
        if (had_old_value) {
            setenv(key.c_str(), old_value.c_str(), 1);
        } else {
            unsetenv(key.c_str());
        }
    }

    ScopedEnvVar(const ScopedEnvVar&) = delete;
    ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;
};

inline int find_free_loopback_port() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;

    int enable = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return 0;
    }

    sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &len) != 0) {
        ::close(fd);
        return 0;
    }

    int port = static_cast<int>(ntohs(bound.sin_port));
    ::close(fd);
    return port;
}
