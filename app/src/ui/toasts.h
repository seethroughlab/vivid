#pragma once
#include "app/log.h"     // LogLevel
#include <string>
#include <vector>

namespace vivid::ui {
class Renderer2D;

// ADR-0019 (E4): a transient notification. The frame loop raises one for each new Error-level log
// event (severity-gated — Warning stays the passive header dot, Info/Debug go only to the log view),
// and for a node badge the user clicked to reveal. Auto-expires; drawn bottom-right.
struct Toast { std::string text; LogLevel level; double expiry; };

// Append a toast that expires `ttl` seconds after `now`. Caps the backlog so a storm can't grow
// without bound (oldest dropped).
void push_toast(std::vector<Toast>& toasts, LogLevel level, const std::string& text, double now, double ttl = 6.0);

// Drop expired toasts, then draw the live ones as a bottom-right stack. `now` is the frame time.
void draw_toasts(Renderer2D& r, std::vector<Toast>& toasts, double now, int win_w, int win_h);

}  // namespace vivid::ui
