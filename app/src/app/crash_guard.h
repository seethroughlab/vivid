#pragma once

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <stddef.h>

// ADR-0018 (R1): attribute a crash to the operator that caused it. Operators are third-party dylibs
// called on the frame + audio hot paths; a segfault inside one is otherwise an anonymous SIGSEGV.
//
// A CrashGuard sets a thread-local "current operator" name around each process_*() call (a plain
// pointer store — no alloc, no lock, RT-safe on the audio thread). An async-signal-safe handler
// reads it and writes a pre-formatted marker file, so the NEXT launch can reconstruct a full crash
// record (crash_recovery.*). Lifted from vivid-classic's crash_guard.h.
//
// Async-signal-safety is mandatory in the handler: ONLY open/write/close/raise/signal, no malloc,
// no printf, no locks. All paths live in .bss and are filled before any operator runs.
namespace vivid {

// Thread-local name of the operator currently in process(); set/cleared by CrashGuard. The
// precomputed length lets the handler avoid strlen() (not async-signal-safe).
inline thread_local const char* g_current_operator = nullptr;
inline thread_local size_t      g_current_operator_len = 0;

// Process-wide marker/snapshot paths, read by the handler on ANY thread. In .bss (async-safe access);
// filled once at startup by set_crash_marker_paths(). Empty ⇒ "no marker configured" (handler skips).
inline char g_marker_path[1024]   = {};
inline char g_snapshot_path[1024] = {};

// Copy the marker + snapshot paths into the handler-accessible buffers. No allocation; truncates on
// overflow. Safe to call before install_crash_handlers().
inline void set_crash_marker_paths(const char* marker, const char* snapshot) {
    size_t i = 0;
    for (; marker && marker[i] && i + 1 < sizeof(g_marker_path); ++i) g_marker_path[i] = marker[i];
    g_marker_path[i] = 0;
    i = 0;
    for (; snapshot && snapshot[i] && i + 1 < sizeof(g_snapshot_path); ++i) g_snapshot_path[i] = snapshot[i];
    g_snapshot_path[i] = 0;
}

// RAII: set g_current_operator for the scope of one process_*() call. A plain pointer store both
// ways — safe on the audio RT thread (no alloc/lock). `name` must outlive the guard (operators own
// a stable heap string for their type name).
struct CrashGuard {
    explicit CrashGuard(const char* name) {
        g_current_operator = name;
        size_t len = 0; if (name) { while (name[len]) ++len; }
        g_current_operator_len = len;
    }
    ~CrashGuard() { g_current_operator = nullptr; g_current_operator_len = 0; }
    CrashGuard(const CrashGuard&) = delete;
    CrashGuard& operator=(const CrashGuard&) = delete;
};

// The fatal-signal handler: names the active operator on stderr, writes the marker, and re-raises
// with the default handler (core dump / normal termination). Async-signal-safe only.
//   marker format (trailing newlines matter — crash_recovery parses key=value lines):
//     signal=<decimal>\n  operator=<name-or-empty>\n  snapshot=<absolute-path>\n
inline void crash_signal_handler(int sig) {
    struct SigEntry { int sig; const char* name; size_t len; };
    static constexpr SigEntry entries[] = {
        { SIGSEGV, "SIGSEGV", 7 }, { SIGBUS, "SIGBUS", 6 }, { SIGILL, "SIGILL", 6 },
        { SIGFPE,  "SIGFPE",  6 }, { SIGABRT, "SIGABRT", 7 },
    };
    const char* sig_name = "UNKNOWN"; size_t sig_len = 7;
    for (const auto& e : entries) if (e.sig == sig) { sig_name = e.name; sig_len = e.len; break; }

    write(STDERR_FILENO, "\n[vivid] Fatal signal: ", 23);
    write(STDERR_FILENO, sig_name, sig_len);
    const char* name = g_current_operator;
    if (name) { write(STDERR_FILENO, " in operator: ", 14); write(STDERR_FILENO, name, g_current_operator_len); }
    else        write(STDERR_FILENO, " (not in operator process())", 28);
    write(STDERR_FILENO, "\n", 1);

    if (g_marker_path[0]) {
        int fd = open(g_marker_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd >= 0) {
            char numbuf[16]; int nlen = 0; int s = sig;   // hand-rolled itoa (sprintf isn't async-safe)
            if (s <= 0) numbuf[nlen++] = '0';
            else { char rev[16]; int r = 0; while (s > 0 && r < 16) { rev[r++] = char('0' + (s % 10)); s /= 10; }
                   while (r > 0) numbuf[nlen++] = rev[--r]; }
            write(fd, "signal=", 7); write(fd, numbuf, nlen);
            write(fd, "\noperator=", 10); if (name) write(fd, name, g_current_operator_len);
            write(fd, "\nsnapshot=", 10);
            size_t sp = 0; while (sp < sizeof(g_snapshot_path) && g_snapshot_path[sp]) ++sp;
            write(fd, g_snapshot_path, sp);
            write(fd, "\n", 1);
            close(fd);
        }
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

// Install the fatal-signal handlers. Call once at startup, after the config paths are set. A no-op
// under a sanitizer build (ASan/TSan install their own SEGV handler and must keep it to report).
inline void install_crash_handlers() {
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    return;
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
    return;
#  endif
#endif
    signal(SIGSEGV, crash_signal_handler);
    signal(SIGBUS,  crash_signal_handler);
    signal(SIGILL,  crash_signal_handler);
    signal(SIGFPE,  crash_signal_handler);
    signal(SIGABRT, crash_signal_handler);
}

}  // namespace vivid
