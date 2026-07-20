#pragma once
// Audit #7: control_server.cpp's handler table, split by concern. Each register_<family>_handlers
// free function populates the shared method->handler map; ControlServer::register_handlers() just
// calls them in turn. This slim header (decls only) is what control_server.cpp includes — the
// family .cpp files include control_handlers_internal.h for the shared helpers.
//
// The definitions name their Handlers& parameter `handlers_` so the MCP parity test's
// handlers_["..."] regex matches uniformly across every split file (mcp/tests/test_mcp_parity.py).
#include "cli/control_server.h"   // ControlServer::Handler

#include <string>
#include <unordered_map>

namespace vivid {

using Handlers = std::unordered_map<std::string, ControlServer::Handler>;

void register_introspection_handlers(Handlers& handlers_);   // status/version/health + discovery + get_*
void register_visuals_handlers(Handlers& handlers_);         // node-graph construction
void register_visual_analysis_handlers(Handlers& handlers_); // ADR-0024 Phase 6: capture/analyze/compare frames
void register_mappings_handlers(Handlers& handlers_);        // the bridge (connect/disconnect mapping)
void register_audio_handlers(Handlers& handlers_);           // authoring + clip warp + pool + native audio ops + graph
void register_project_handlers(Handlers& handlers_);         // session author/persist + project workflow
void register_package_handlers(Handlers& handlers_);         // ADR-0024 Phase 7: operator-package authoring
void register_edit_handlers(Handlers& handlers_);            // ADR-0017 undo/redo

}  // namespace vivid
