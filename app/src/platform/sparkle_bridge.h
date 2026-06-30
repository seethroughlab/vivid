#pragma once
// Auto-update entry point (P4.5). The real backend is the Sparkle framework, embedded +
// signed at release time (see docs/release/README.md); the in-tree backend is a no-op stub
// so the build never depends on the Sparkle framework being present. Both calls are safe
// on any build — sparkle_available() reports whether a real updater is wired.
namespace vivid {

bool sparkle_available();          // true only when a real Sparkle backend is linked
void sparkle_check_for_updates();  // ask the updater to check (no-op when unavailable)

}  // namespace vivid
