#pragma once

#include <signal.h>
#include <unistd.h>

namespace vivid {

// Thread-local pointer to the name of the operator currently being processed.
// Set before each process() call, cleared after. Used by the signal handler
// to identify which operator caused a crash.
inline thread_local const char* g_current_operator = nullptr;
inline thread_local size_t g_current_operator_len = 0;

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
