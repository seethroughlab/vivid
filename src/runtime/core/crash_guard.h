#pragma once

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <stddef.h>

namespace vivid {

// Thread-local pointer to the name of the operator currently being processed.
// Set before each process() call, cleared after. Used by the signal handler
// to identify which operator caused a crash.
inline thread_local const char* g_current_operator = nullptr;
inline thread_local size_t g_current_operator_len = 0;

// Process-wide (not thread-local) paths for the crash-recovery marker and
// snapshot files.  Set once on startup via set_crash_marker_paths().  Read by
// the signal handler — which may run on any thread — to persist attribution
// across the fatal crash.  Both buffers live in .bss so access is async-safe.
inline char g_marker_path[1024]   = {};
inline char g_snapshot_path[1024] = {};

// Copy marker and snapshot paths into the handler-accessible buffers.
// No allocation; truncates to buffer size on overflow.  Safe to call before
// install_crash_handlers() — defaults are empty strings, which the handler
// treats as "no marker configured".
inline void set_crash_marker_paths(const char* marker, const char* snapshot) {
    size_t i = 0;
    for (; marker && marker[i] && i + 1 < sizeof(g_marker_path); ++i)
        g_marker_path[i] = marker[i];
    g_marker_path[i] = 0;
    i = 0;
    for (; snapshot && snapshot[i] && i + 1 < sizeof(g_snapshot_path); ++i)
        g_snapshot_path[i] = snapshot[i];
    g_snapshot_path[i] = 0;
}

// RAII guard that sets g_current_operator for the duration of a scope.
struct CrashGuard {
    explicit CrashGuard(const char* name) {
        g_current_operator = name;
        // Pre-compute length so the signal handler doesn't need strlen().
        size_t len = 0;
        if (name) { while (name[len]) ++len; }
        g_current_operator_len = len;
    }
    ~CrashGuard() { g_current_operator = nullptr; g_current_operator_len = 0; }
    CrashGuard(const CrashGuard&) = delete;
    CrashGuard& operator=(const CrashGuard&) = delete;
};

// Signal handler that prints which operator was active when the crash occurred.
// Uses only async-signal-safe functions (write, raise, signal).
inline void crash_signal_handler(int sig) {
    // Signal names and their lengths are compile-time constants.
    struct SigEntry { int sig; const char* name; size_t len; };
    static constexpr SigEntry entries[] = {
        { SIGSEGV, "SIGSEGV", 7 },
        { SIGBUS,  "SIGBUS",  6 },
        { SIGABRT, "SIGABRT", 7 },
        { SIGFPE,  "SIGFPE",  6 },
    };
    const char* sig_name = "UNKNOWN";
    size_t sig_len = 7;
    for (const auto& e : entries) {
        if (e.sig == sig) { sig_name = e.name; sig_len = e.len; break; }
    }

    write(STDERR_FILENO, "\n[vivid] Fatal signal: ", 22);
    write(STDERR_FILENO, sig_name, sig_len);

    const char* name = g_current_operator;
    if (name) {
        write(STDERR_FILENO, " in operator: ", 14);
        write(STDERR_FILENO, name, g_current_operator_len);
    } else {
        write(STDERR_FILENO, " (not in operator process())", 28);
    }
    write(STDERR_FILENO, "\n", 1);

    // Persist a structured marker so the next launch can recover attribution.
    // Uses only async-signal-safe calls.  Format (trailing newlines matter):
    //   signal=<decimal>\n
    //   operator=<name-or-empty>\n
    //   snapshot=<absolute-path>\n
    if (g_marker_path[0]) {
        int fd = open(g_marker_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd >= 0) {
            // Hand-rolled itoa — strlen/sprintf are not async-signal-safe.
            char numbuf[16];
            int nlen = 0;
            int s = sig;
            if (s <= 0) {
                numbuf[nlen++] = '0';
            } else {
                char rev[16];
                int r = 0;
                while (s > 0 && r < 16) { rev[r++] = '0' + (s % 10); s /= 10; }
                while (r > 0) numbuf[nlen++] = rev[--r];
            }
            write(fd, "signal=", 7);
            write(fd, numbuf, nlen);
            write(fd, "\noperator=", 10);
            if (name) write(fd, name, g_current_operator_len);
            write(fd, "\nsnapshot=", 10);
            size_t sp_len = 0;
            while (sp_len < sizeof(g_snapshot_path) && g_snapshot_path[sp_len]) ++sp_len;
            write(fd, g_snapshot_path, sp_len);
            write(fd, "\n", 1);
            close(fd);
        }
    }

    // Re-raise with default handler to get core dump / normal termination
    signal(sig, SIG_DFL);
    raise(sig);
}

// Install crash signal handlers. Call once at startup.
inline void install_crash_handlers() {
    signal(SIGSEGV, crash_signal_handler);
    signal(SIGBUS,  crash_signal_handler);
    signal(SIGABRT, crash_signal_handler);
    signal(SIGFPE,  crash_signal_handler);
}

} // namespace vivid
