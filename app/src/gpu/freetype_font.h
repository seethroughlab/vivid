#pragma once
// A tiny FreeType face wrapper shared by the text operators: the atlas Text op (hinted glyph
// bitmaps) and the vector-text op (glyph outlines -> triangles). Header-only; FreeType is linked
// into the vivid target. Owns the font file bytes (FT_Face references them for its lifetime).
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace vivid {

struct FtFont {
    FT_Library lib = nullptr;
    FT_Face    face = nullptr;
    std::vector<unsigned char> data;   // the .ttf bytes; the face points into them
    int  pixel_size = 0;
    bool ok = false;

    void done() {
        if (face) { FT_Done_Face(face); face = nullptr; }
        if (lib)  { FT_Done_FreeType(lib); lib = nullptr; }
        ok = false;
    }
    ~FtFont() { done(); }

    bool load(const std::string& path, int px) {
        done();
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        if (data.empty()) return false;
        if (FT_Init_FreeType(&lib)) { data.clear(); return false; }
        if (FT_New_Memory_Face(lib, data.data(), static_cast<FT_Long>(data.size()), 0, &face)) { done(); return false; }
        FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(px));
        pixel_size = px;
        ok = true;
        return true;
    }

    bool has_kerning() const { return face && FT_HAS_KERNING(face); }
    // Horizontal kerning advance (pixels) between two chars; 0 if unavailable.
    int kerning(uint32_t left, uint32_t right) const {
        if (!face || !left || !FT_HAS_KERNING(face)) return 0;
        FT_Vector k{};
        if (FT_Get_Kerning(face, FT_Get_Char_Index(face, left), FT_Get_Char_Index(face, right),
                           FT_KERNING_DEFAULT, &k)) return 0;
        return static_cast<int>(k.x >> 6);
    }
    int ascent()  const { return face ? static_cast<int>(face->size->metrics.ascender >> 6) : 0; }
    int descent() const { return face ? static_cast<int>(-(face->size->metrics.descender >> 6)) : 0; }
};

// --- Glyph outlines -> flattened contours (for real vector text geometry) ---
struct FtVec2 { float x, y; };
// A string laid out as a set of closed contours (in pixel space at the face's pixel size, y-up).
// Beziers are flattened to line segments. Holes are just extra contours (fill via even-odd).
struct FtContours {
    std::vector<std::vector<FtVec2>> contours;
    float width = 0.f;   // advance width of the whole string (pixels)
};
namespace ft_detail {
struct DecCtx { std::vector<std::vector<FtVec2>>* out; float ox; std::vector<FtVec2> cur; };
inline float f26(FT_Pos v) { return static_cast<float>(v) / 64.f; }
inline int dec_move(const FT_Vector* to, void* u) {
    auto* c = static_cast<DecCtx*>(u);
    if (!c->cur.empty()) { c->out->push_back(c->cur); c->cur.clear(); }
    c->cur.push_back({ c->ox + f26(to->x), f26(to->y) });
    return 0;
}
inline int dec_line(const FT_Vector* to, void* u) {
    auto* c = static_cast<DecCtx*>(u); c->cur.push_back({ c->ox + f26(to->x), f26(to->y) }); return 0;
}
inline int dec_conic(const FT_Vector* ctrl, const FT_Vector* to, void* u) {
    auto* c = static_cast<DecCtx*>(u);
    const FtVec2 p0 = c->cur.back(), p1 = { c->ox + f26(ctrl->x), f26(ctrl->y) }, p2 = { c->ox + f26(to->x), f26(to->y) };
    const int N = 8;
    for (int i = 1; i <= N; ++i) { float t = float(i) / N, mt = 1 - t;
        c->cur.push_back({ mt*mt*p0.x + 2*mt*t*p1.x + t*t*p2.x, mt*mt*p0.y + 2*mt*t*p1.y + t*t*p2.y }); }
    return 0;
}
inline int dec_cubic(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* u) {
    auto* c = static_cast<DecCtx*>(u);
    const FtVec2 p0 = c->cur.back(), p1 = { c->ox + f26(c1->x), f26(c1->y) },
                 p2 = { c->ox + f26(c2->x), f26(c2->y) }, p3 = { c->ox + f26(to->x), f26(to->y) };
    const int N = 10;
    for (int i = 1; i <= N; ++i) { float t = float(i) / N, mt = 1 - t;
        float a = mt*mt*mt, b = 3*mt*mt*t, cc = 3*mt*t*t, d = t*t*t;
        c->cur.push_back({ a*p0.x + b*p1.x + cc*p2.x + d*p3.x, a*p0.y + b*p1.y + cc*p2.y + d*p3.y }); }
    return 0;
}
}  // namespace ft_detail

inline FtContours ft_string_contours(FtFont& f, const std::string& s) {
    FtContours out;
    if (!f.ok) return out;
    FT_Outline_Funcs funcs{};
    funcs.move_to = ft_detail::dec_move; funcs.line_to = ft_detail::dec_line;
    funcs.conic_to = ft_detail::dec_conic; funcs.cubic_to = ft_detail::dec_cubic;
    float pen = 0.f; uint32_t prev = 0;
    for (unsigned char ch : s) {
        pen += static_cast<float>(f.kerning(prev, ch));
        if (FT_Load_Char(f.face, ch, FT_LOAD_NO_BITMAP)) { prev = ch; continue; }
        ft_detail::DecCtx ctx{ &out.contours, pen, {} };
        FT_Outline_Decompose(&f.face->glyph->outline, &funcs, &ctx);
        if (!ctx.cur.empty()) out.contours.push_back(ctx.cur);
        pen += static_cast<float>(f.face->glyph->advance.x >> 6);
        prev = ch;
    }
    out.width = pen;
    return out;
}

}  // namespace vivid
