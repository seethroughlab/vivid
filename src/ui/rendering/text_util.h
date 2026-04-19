#pragma once
#include "ui/rendering/renderer_2d.h"
#include <string>

namespace vivid::ui {

// Truncate `text` so it fits within `max_w` pixels at the given scale, appending
// a U+2026 ellipsis when trimming is needed. The ellipsis width is included in
// the fit check so the result is guaranteed to render within `max_w`. Empty
// input or a max_w too small to even contain the ellipsis returns an empty
// string; otherwise at least the ellipsis glyph is returned.
inline std::string truncate_text(Renderer2D& tr, const std::string& text,
                                 float max_w, float scale = 1.0f) {
    if (text.empty() || max_w <= 0.0f) return {};
    if (tr.text_width(text.c_str(), scale) <= max_w) return text;
    static constexpr const char* kEllipsis = "\xe2\x80\xa6";
    if (tr.text_width(kEllipsis, scale) > max_w) return {};
    std::string out = text;
    while (!out.empty() &&
           tr.text_width((out + kEllipsis).c_str(), scale) > max_w) {
        out.pop_back();
    }
    return out.empty() ? std::string(kEllipsis) : out + kEllipsis;
}

}  // namespace vivid::ui
