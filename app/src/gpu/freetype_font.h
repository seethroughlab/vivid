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

}  // namespace vivid
