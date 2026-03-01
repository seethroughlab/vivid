#pragma once

#include <signal.h>
#include <unistd.h>
#include <cstring>

namespace vivid {

// Thread-local pointer to the name of the operator currently being processed.
// Set before each process() call, cleared after. Used by the signal handler
// to identify which operator caused a crash.
inline thread_local const char* g_current_operator = nullptr;

// RAII guard that sets g_current_operator for the duration of a scope.
struct CrashGuard {
    explicit CrashGuard(const char* name) { g_current_operator = name; }
    ~CrashGuard() { g_current_operator = nullptr; }
    CrashGuard(const CrashGuard&) = delete;
    CrashGuard& operator=(const CrashGuard&) = delete;
};

// Signal handler that prints which operator was active when the crash occurred.
// Uses only async-signal-safe functions (write, strlen).
inline void crash_signal_handler(int sig) {
    const char* sig_name = (sig == SIGSEGV) ? "SIGSEGV" :
                           (sig == SIGBUS)  ? "SIGBUS"  :
                           (sig == SIGABRT) ? "SIGABRT" :
                           (sig == SIGFPE)  ? "SIGFPE"  : "UNKNOWN";

    write(STDERR_FILENO, "\n[vivid] Fatal signal: ", 22);
    write(STDERR_FILENO, sig_name, strlen(sig_name));

    const char* name = g_current_operator;
    if (name) {
        write(STDERR_FILENO, " in operator: ", 14);
        write(STDERR_FILENO, name, strlen(name));
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
