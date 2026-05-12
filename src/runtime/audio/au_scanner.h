#pragma once
// au_scanner.h — Runtime-level AU instrument scanner backed by Core Audio's
// component registry. Lives in the main binary (unlike the operator-local copy
// in operators/shared/au_host/) so the control server can call it without
// going through an operator instance.
//
// All functions are main-thread only.

#ifdef __APPLE__
#include <string>
#include <vector>

struct RuntimeAUPluginInfo {
    std::string name; // from AudioComponentCopyName, e.g. "Toontrack: EZdrummer 3"
};

// Trigger scan (no-op after first call). Call once at startup or on first query.
void runtime_au_scan_plugins();

// Return cached results. Empty until runtime_au_scan_plugins() has been called.
const std::vector<RuntimeAUPluginInfo>& runtime_au_get_plugins();

#endif // __APPLE__
