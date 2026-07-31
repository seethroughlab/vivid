#pragma once

#include <string>

// ADR-0018 / ADR-0045 (Phase-2 audit P0-01): pick a stable, non-empty crash-attribution
// name for a HOSTED plugin (VST3/CLAP), so a segfault inside `process` on the RT thread is
// named in the crash marker instead of an anonymous SIGSEGV the user can't escape.
//
// Returns a `const char*` that is valid as long as the argument strings outlive it — the
// plugin handle owns them (they are set once at load and not mutated during processing), so
// `.c_str()` is stable for the synchronous `process()` call the CrashGuard wraps. `generic`
// is a string literal (static storage) used when the handle carries no name at all.
//
// Pure + header-only (no alloc, no lock) so it is RT-safe and unit-testable without the
// audio engine.
namespace vivid::session {

inline const char* plugin_crash_name(const std::string& primary,
                                     const std::string& fallback,
                                     const char* generic) {
    if (!primary.empty())  return primary.c_str();
    if (!fallback.empty()) return fallback.c_str();
    return generic;
}

}  // namespace vivid::session
