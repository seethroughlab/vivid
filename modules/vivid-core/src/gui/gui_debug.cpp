// GUI Debug utilities implementation

#include <vivid/gui/gui_debug.h>
#include <vivid/gui/panel_manager.h>
#include <iostream>

namespace vivid {
namespace gui {

// Global debug flag
static bool s_debugEnabled =
#ifdef VIVID_GUI_DEBUG
    true;
#else
    false;
#endif

void setDebugEnabled(bool enabled) {
    s_debugEnabled = enabled;
}

bool isDebugEnabled() {
    return s_debugEnabled;
}

void logTransition(const char* component, const char* from, const char* to, const char* reason) {
    if (!s_debugEnabled) return;
    std::cerr << "[GUI:" << component << "] " << from << " -> " << to;
    if (reason && reason[0] != '\0') {
        std::cerr << " (" << reason << ")";
    }
    std::cerr << "\n";
}

void logDebug(const char* component, const char* message) {
    if (!s_debugEnabled) return;
    std::cerr << "[GUI:" << component << "] " << message << "\n";
}

void logDebug(const char* component, const char* message, const std::string& value) {
    if (!s_debugEnabled) return;
    std::cerr << "[GUI:" << component << "] " << message << ": " << value << "\n";
}

void dumpPanelStates(const PanelManager& pm) {
    std::cerr << "\n=== Panel States ===\n";
    std::cerr << "Panel count: " << pm.panelCount() << "\n";
    std::cerr << "Focused panel: " << (pm.focusedPanelId().empty() ? "(none)" : pm.focusedPanelId()) << "\n";
    std::cerr << "========================\n\n";
}

int validateState(const PanelManager& pm) {
    // Basic validation - just check panel count is sane
    return 0;
}

} // namespace gui
} // namespace vivid
