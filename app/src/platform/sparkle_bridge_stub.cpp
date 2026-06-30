// No-op auto-update backend (P4.5). The real macOS Sparkle backend (a .mm that links the
// embedded Sparkle.framework) is wired at release time per docs/release/README.md; until
// then this stub satisfies the sparkle_bridge.h API so nothing in the build depends on the
// framework. Compiled on every platform.
#include "platform/sparkle_bridge.h"

namespace vivid {

bool sparkle_available() { return false; }
void sparkle_check_for_updates() {}

}  // namespace vivid
