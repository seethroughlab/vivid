// GUI Debug utilities implementation

#include <vivid/gui/gui_debug.h>
#include <vivid/gui/panel_manager.h>
#include <vivid/gui/dock_manager.h>
#include <vivid/gui/panel_group.h>
#include <vivid/gui/split_container.h>
#include <iostream>
#include <set>

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
    std::cerr << "Layout mode: " << (pm.isLayoutMode() ? "enabled" : "disabled") << "\n";
    std::cerr << "Panel count: " << pm.panelCount() << "\n";
    std::cerr << "Focused panel: " << (pm.focusedPanelId().empty() ? "(none)" : pm.focusedPanelId()) << "\n";

    // Collect panels in layout tree
    std::set<std::string> inLayoutTree;
    if (pm.layoutRoot()) {
        std::vector<Panel*> layoutPanels;
        // Note: const_cast needed because collectPanels is non-const
        const_cast<LayoutNode*>(pm.layoutRoot())->collectPanels(layoutPanels);
        for (Panel* p : layoutPanels) {
            inLayoutTree.insert(p->config().id);
        }
    }

    std::cerr << "\nPanels in layout tree: " << inLayoutTree.size() << "\n";
    for (const auto& id : inLayoutTree) {
        std::cerr << "  - " << id << "\n";
    }

    std::cerr << "\n--- Individual Panels ---\n";
    // Iterate using index since we can't iterate directly
    for (size_t i = 0; i < pm.panelCount(); ++i) {
        // Get panel by iterating (we need to access private members or use public API)
        // For now, just show count - proper iteration would require exposing panels
    }

    std::cerr << "========================\n\n";
}

int validateState(const PanelManager& pm) {
    int issues = 0;

    // Collect panels in layout tree
    std::set<std::string> inLayoutTree;
    if (pm.layoutRoot()) {
        std::vector<Panel*> layoutPanels;
        const_cast<LayoutNode*>(pm.layoutRoot())->collectPanels(layoutPanels);
        for (Panel* p : layoutPanels) {
            if (!inLayoutTree.insert(p->config().id).second) {
                std::cerr << "[GUI:Validate] ERROR: Panel '" << p->config().id
                          << "' appears multiple times in layout tree\n";
                ++issues;
            }
        }
    }

    // Note: Full validation requires access to private members of PanelManager.
    // This basic implementation validates what's accessible via public API.
    // For comprehensive validation, PanelManager should expose more state or
    // make this a friend function.

    if (issues > 0) {
        std::cerr << "[GUI:Validate] Found " << issues << " issue(s)\n";
    }

    return issues;
}

int validateDockState(const DockManager& dm) {
    int issues = 0;

    // Validate drag state consistency
    const DragState& drag = dm.dragState();
    if (drag.isActive()) {
        if (!drag.panel) {
            std::cerr << "[GUI:Validate] ERROR: Active drag with null panel\n";
            ++issues;
        }
        if (drag.type == DragState::Type::Tab && !drag.sourceGroup) {
            std::cerr << "[GUI:Validate] ERROR: Tab drag without source group\n";
            ++issues;
        }
    }

    return issues;
}

} // namespace gui
} // namespace vivid
