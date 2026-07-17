// The App-linked half of crash recovery: the warm snapshot (node id ↔ operator type) that lets a
// crash be attributed to a specific node. Kept out of crash_recovery.cpp so that file stays App-free
// and headless-testable (mirrors runtime_health_collect.cpp).
#include "app/crash_recovery.h"

#include "app/app.h"
#include "gpu/visual_graph.h"
#include "version.h"

#include <filesystem>
#include <fstream>

namespace vivid {

void CrashRecovery::write_snapshot(const App& app) const {
    if (!app.vgraph) return;
    nlohmann::json nodes = nlohmann::json::array();
    for (const auto& n : app.vgraph->nodes())
        nodes.push_back(nlohmann::json{ {"node_id", n.id}, {"type", n.op_type} });
    const nlohmann::json j{ {"nodes", nodes}, {"app_version", VIVID_VERSION} };

    // Same atomic tmp→rename write crash_recovery.cpp uses; snapshot_path_ is our own member.
    namespace fs = std::filesystem;
    const std::string tmp = snapshot_path_ + ".tmp";
    { std::ofstream f(tmp, std::ios::trunc); if (!f) return; f << j.dump(2); }
    std::error_code ec; fs::rename(tmp, snapshot_path_, ec);
}

}  // namespace vivid
