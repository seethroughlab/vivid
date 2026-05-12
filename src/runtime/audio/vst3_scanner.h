#pragma once
// vst3_scanner.h — Runtime-level VST3 instrument scanner backed by filesystem
// traversal + dlopen. Lives in the main binary (unlike the operator-local copy
// in operators/shared/vst3_host/) so the control server can call it without
// going through an operator instance.
//
// All functions are main-thread only.

#include <string>
#include <vector>

struct RuntimeVst3PluginInfo {
    std::string key;    // "Name [Vendor]" display key, e.g. "Serum2 [Xfer Records]"
    std::string name;   // raw plugin name
    std::string vendor; // raw vendor name
    std::string path;   // path to .vst3 bundle
};

// Trigger scan (no-op after first call). Call once at startup or on first query.
void runtime_vst3_scan_plugins();

// Return cached results. Empty until runtime_vst3_scan_plugins() has been called.
const std::vector<RuntimeVst3PluginInfo>& runtime_vst3_get_plugins();
