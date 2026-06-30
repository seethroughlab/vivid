#pragma once

namespace vivid {

/// Keep the process scheduled when backgrounded, so the frame loop (and the MCP
/// control-server drain it runs each tick) keeps pumping even when the app is not the
/// foreground app — required for agent-driven use. macOS holds an NSProcessInfo
/// activity; other platforms are a noop. Idempotent.
void disable_app_nap(const char* reason);

}  // namespace vivid
