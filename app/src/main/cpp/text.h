// TrueType text (stb_truetype): glyphs are rasterised at the exact pixel size they're shown at into one atlas
// texture (single channel, mipmapped for text placed in 3D). The atlas also holds a white block, so panel
// rectangles and text go through the same shader in one draw call.
#pragma once

#include <GLES3/gl3.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "gfx.h"
#include "stb_truetype.h"

enum FontStyle { kRegular = 0, kMedium = 1, kStyleCount };

class Font {
public:
    // TTF files of each style (kept: stb_truetype reads from them while packing new sizes).
    bool load(std::vector<uint8_t> regular, std::vector<uint8_t> medium);
    bool loaded() const { return loaded_; }

    // Appends the glyph quads of `s` (ASCII) at pixel size `px` with the top of the capitals at y. One unit of x/y is
    // one pixel of the target; quads start on whole pixels (crisp when drawn 1:1). Returns the x after the text.
    float layout(FontStyle style, int px, float x, float y, const std::string &s, const float color[4],
                 std::vector<TexVertex> &out);
    float width(FontStyle style, int px, const std::string &s);
    float cap_height(FontStyle style, int px);
    // Texture coordinate of a white texel (solid rectangles).
    float white_u() const { return white_u_; }
    float white_v() const { return white_v_; }
    // Uploads pending glyphs; call before drawing with texture().
    GLuint texture();

private:
    struct Glyph {
        float x0, y0, x1, y1;  // quad relative to the pen on the baseline (pixels)
        float u0, v0, u1, v1;
        float advance;
    };
    struct Size {
        Glyph glyphs[95];  // ' '..'~'
        float cap = 0;  // height of the capitals
        bool ok = false;
    };
    const Size *size(FontStyle style, int px);

    bool loaded_ = false;
    std::vector<uint8_t> ttf_[kStyleCount];
    std::map<int, Size> sizes_;  // key: style * 1000 + px
    std::vector<uint8_t> pixels_;
    stbtt_pack_context pack_{};
    int atlas_w_ = 2048, atlas_h_ = 2048;
    bool dirty_ = true, full_ = false;
    GLuint tex_ = 0;
    float white_u_ = 0, white_v_ = 0;
};
