// ADR-0045 (Phase-2 audit P0-01): a crash inside a HOSTED plugin's process() on the RT thread must
// be ATTRIBUTED, not an anonymous SIGSEGV. The render sites (audio/vst3_host_render.cpp) now wrap the
// four plugin-process calls in a CrashGuard named by plugin_crash_name(). This test locks in:
//   1. the name-selection logic (primary → fallback → generic), and
//   2. end-to-end attribution: a forked child that crashes inside a plugin-named CrashGuard writes a
//      marker naming the plugin (the same guard+handler path the render sites use).
#include "audio/plugin_crash_name.h"
#include "app/crash_guard.h"
#include "test_helpers.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

// Are we under a sanitizer? install_crash_handlers() is a no-op there (ASan/TSan own SEGV), so the
// forked child would die without writing our marker. Mirror the nested guard in crash_guard.h.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#  define VIVID_UNDER_SANITIZER 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#    define VIVID_UNDER_SANITIZER 1
#  endif
#endif
#ifndef VIVID_UNDER_SANITIZER
#  define VIVID_UNDER_SANITIZER 0
#endif

namespace fs = std::filesystem;
using vivid::session::plugin_crash_name;

int main() {
    using namespace vivid;

    // ---- 1. name selection: primary wins; fall back; then the generic literal ----
    {
        const std::string name = "Surge XT", vendor = "Surge Synth Team", none;
        CHECK(std::string(plugin_crash_name(name, vendor, "VST3 plugin")) == "Surge XT");
        CHECK(std::string(plugin_crash_name(none, vendor, "VST3 plugin")) == "Surge Synth Team");
        CHECK(std::string(plugin_crash_name(none, none, "VST3 plugin")) == "VST3 plugin");
        CHECK(std::string(plugin_crash_name(none, none, "CLAP plugin")) == "CLAP plugin");
    }

    // ---- 2. a crash inside a plugin-named CrashGuard writes an ATTRIBUTED marker ----
    // Skipped under a sanitizer build (handlers are no-ops there — see above).
#if !VIVID_UNDER_SANITIZER
    {
        const fs::path base = fs::temp_directory_path() / ("vivid_plugin_crash_" + std::to_string(::getpid()));
        fs::remove_all(base);
        fs::create_directories(base);
        const std::string marker = (base / "marker").string();
        const std::string snap   = (base / "snapshot").string();

        pid_t pid = fork();
        if (pid == 0) {                       // child: arm the guard exactly as a render site does, then crash
            set_crash_marker_paths(marker.c_str(), snap.c_str());
            install_crash_handlers();
            const std::string plugin_name = "Surge XT", fallback;   // as a live Vst3Handle would carry
            CrashGuard cg(plugin_crash_name(plugin_name, fallback, "VST3 plugin"));
            raise(SIGSEGV);                   // handler names the plugin, writes the marker, re-raises
            _exit(0);                         // unreachable
        }
        CHECK(pid > 0);
        int status = 0; waitpid(pid, &status, 0);
        CHECK(WIFSIGNALED(status));           // died from the re-raised signal
        std::ifstream f(marker, std::ios::binary);
        const std::string m((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK(m.find("operator=Surge XT") != std::string::npos);   // attributed to the plugin, not anonymous
        CHECK(m.find("signal=") != std::string::npos);

        fs::remove_all(base);
    }
#endif

    return vivid::test::summary("test_plugin_crash_attrib");
}
