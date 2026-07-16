#include "ui/toasts.h"

#include "ui/renderer_2d.h"
#include "ui/ui_style.h"

#include <algorithm>

namespace vivid::ui {

static const float* toast_color(LogLevel l) {
    const Style& s = style();
    switch (l) {
        case LogLevel::Error:   return s.red;
        case LogLevel::Warning: return s.gold;
        default:                return s.gpu;
    }
}

void push_toast(std::vector<Toast>& toasts, LogLevel level, const std::string& text, double now, double ttl) {
    toasts.push_back({ text, level, now + ttl });
    constexpr size_t kMaxToasts = 6;
    if (toasts.size() > kMaxToasts) toasts.erase(toasts.begin(), toasts.begin() + (toasts.size() - kMaxToasts));
}

void draw_toasts(Renderer2D& r, std::vector<Toast>& toasts, double now, int win_w, int win_h) {
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(),
                                [now](const Toast& t) { return t.expiry <= now; }),
                 toasts.end());
    if (toasts.empty()) return;
    const Style& s = style();
    const float w = 340.f, h = 30.f, pad = 10.f;
    float y = static_cast<float>(win_h) - pad - h;
    // Newest at the bottom: draw the list bottom-up so a fresh toast appears closest to the corner.
    for (auto it = toasts.rbegin(); it != toasts.rend(); ++it) {
        const float x = static_cast<float>(win_w) - pad - w;
        r.draw_shadow(x, y, w, h);
        r.draw_rect(x, y, w, h, s.panel[0], s.panel[1], s.panel[2], 0.97f);
        r.draw_rect_outline(x, y, w, h, 1.f, s.border[0], s.border[1], s.border[2], 1.0f);
        const float* c = toast_color(it->level);
        r.draw_rect(x, y, s.accent_bar, h, c[0], c[1], c[2], 1.0f);   // severity stripe
        char msg[80]; std::snprintf(msg, sizeof msg, "%.74s", it->text.c_str());
        r.draw_text(x + 12.f, y + 9.f, msg, s.text[0], s.text[1], s.text[2], 1.0f, 0.80f);
        y -= h + 6.f;
        if (y < 40.f) break;   // don't run into the transport bar
    }
}

}  // namespace vivid::ui
