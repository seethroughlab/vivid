#pragma once
#include <string>
#include <vector>

// Reading what a plugin IS out of its factory — WITHOUT instantiating it.
//
// This is the expensive, dangerous operation in the whole catalog: it loads a third-party dylib
// into our address space and runs its static initializers. It must therefore never run on the main
// or audio thread, and it must survive the plugin being broken. What it does NOT do is create a
// plugin instance — that's where the real cost lives (Surge XT's CLAP blocks for ~90s inside
// create_plugin; reading its descriptor takes milliseconds).
//
// VST3 is the slow side (CFBundle load + bundleEntry + GetPluginFactory: 50ms-5s); CLAP is cheap
// (entry->init + get_plugin_descriptor).
namespace vivid::session {

// One class inside a bundle (a VST3 bundle may ship several; a CLAP factory may list several).
struct ProbedClass {
    std::string uid;      // VST3 class cid hex ("" for CLAP) — lets the loader pick the EXACT class
    std::string id;       // CLAP plugin id ("" for VST3)
    std::string name;
    std::string vendor;
    int cls = 0;          // PluginClass
};

struct ProbeResult {
    bool ok = false;
    std::string error;
    std::vector<ProbedClass> classes;
};

// SLOW + UNSAFE (third-party code runs IN THIS PROCESS). Only the probe subprocess calls this.
ProbeResult probe_plugin(const std::string& path, int format);

// How the probe actually gets run: re-exec ourselves as `--probe-plugin <path> --format <n>`, so a
// plugin that crashes or hangs takes down a throwaway child instead of the app. This is not
// paranoia — probing in-process crashed the app on two different installed plugins, one per launch,
// which would have made it effectively unlaunchable.
//
// Returns the child's verdict. `timeout_ms` guards against a plugin that hangs (you cannot safely
// kill a hung in-process thread — that alone forces a subprocess).
struct ProbeRun {
    int cls = 0;                  // PluginClass
    std::string name, vendor, uid;
    bool crashed = false;         // the child died on a signal, or blew the timeout
};
ProbeRun probe_plugin_subprocess(const std::string& path, int format, int timeout_ms = 30000);

// The child side: run the probe, print one JSON line to stdout, and exit. Called from main() before
// any window/audio/GPU init when --probe-plugin is present. Returns the process exit code.
int run_probe_subprocess_main(const std::string& path, int format);

}  // namespace vivid::session
