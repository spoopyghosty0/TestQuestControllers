#define STB_TRUETYPE_IMPLEMENTATION
#include "text.h"

#include <algorithm>
#include <cmath>

#include "log.h"

namespace {
const int kWhiteRows = 16;  // the top rows of the atlas: a white block for solid rectangles
const int kPadding = 4;     // between glyphs, so the first mip levels don't bleed
}  // namespace

bool Font::load(std::vector<uint8_t> regular, std::vector<uint8_t> medium) {
    ttf_[kRegular] = std::move(regular);
    ttf_[kMedium] = std::move(medium);
    if (ttf_[kMedium].empty()) ttf_[kMedium] = ttf_[kRegular];
    if (ttf_[kRegular].empty()) return false;
    pixels_.assign((size_t)atlas_w_ * atlas_h_, 0);
    for (int y = 2; y < kWhiteRows - 2; ++y)
        for (int x = 2; x < kWhiteRows - 2; ++x) pixels_[(size_t)y * atlas_w_ + x] = 255;
    white_u_ = (kWhiteRows / 2.0f) / atlas_w_;
    white_v_ = (kWhiteRows / 2.0f) / atlas_h_;
    if (!stbtt_PackBegin(&pack_, pixels_.data() + (size_t)kWhiteRows * atlas_w_, atlas_w_, atlas_h_ - kWhiteRows,
                         atlas_w_, kPadding, nullptr)) {
        LOGE("font atlas: stbtt_PackBegin failed");
        return false;
    }
    stbtt_PackSetOversampling(&pack_, 1, 1);  // glyphs are shown at the size they're rasterised at
    loaded_ = true;
    dirty_ = true;
    return true;
}

const Font::Size *Font::size(FontStyle style, int px) {
    if (!loaded_ || px < 4) return nullptr;
    px = std::min(px, 160);
    int key = style * 1000 + px;
    auto it = sizes_.find(key);
    if (it != sizes_.end()) return it->second.ok ? &it->second : nullptr;
    Size &sz = sizes_[key];
    if (full_) return nullptr;
    stbtt_packedchar pc[95];
    const unsigned char *data = ttf_[style].data();
    if (!stbtt_PackFontRange(&pack_, data, 0, STBTT_POINT_SIZE((float)px), 32, 95, pc)) {
        LOGE("font atlas full at %d px", px);
        full_ = true;
        return nullptr;
    }
    sz.cap = -pc['H' - 32].yoff;
    for (int i = 0; i < 95; ++i) {
        const stbtt_packedchar &c = pc[i];
        Glyph &g = sz.glyphs[i];
        g.x0 = c.xoff;
        g.y0 = c.yoff;
        g.x1 = c.xoff + (c.x1 - c.x0);
        g.y1 = c.yoff + (c.y1 - c.y0);
        g.u0 = (float)c.x0 / atlas_w_;
        g.u1 = (float)c.x1 / atlas_w_;
        g.v0 = (float)(c.y0 + kWhiteRows) / atlas_h_;
        g.v1 = (float)(c.y1 + kWhiteRows) / atlas_h_;
        g.advance = c.xadvance;
    }
    sz.ok = true;
    dirty_ = true;
    return &sz;
}

float Font::cap_height(FontStyle style, int px) {
    const Size *sz = size(style, px);
    return sz ? sz->cap : px * 0.7f;
}

float Font::width(FontStyle style, int px, const std::string &s) {
    const Size *sz = size(style, px);
    if (!sz) return 0;
    float w = 0;
    for (unsigned char ch : s)
        if (ch >= 32 && ch < 127) w += sz->glyphs[ch - 32].advance;
    return w;
}

float Font::layout(FontStyle style, int px, float x, float y, const std::string &s, const float color[4],
                   std::vector<TexVertex> &out) {
    const Size *sz = size(style, px);
    if (!sz) return x;
    float baseline = std::round(y + sz->cap);  // y = the top of the capitals
    for (unsigned char ch : s) {
        if (ch < 32 || ch >= 127) ch = '?';
        const Glyph &g = sz->glyphs[ch - 32];
        if (g.x1 > g.x0) {
            // whole pixels: the glyph's texels land exactly on the target's
            float x0 = std::round(x + g.x0), y0 = baseline + g.y0;
            float x1 = x0 + (g.x1 - g.x0), y1 = y0 + (g.y1 - g.y0);
            const TexVertex a{x0, y0, 0, g.u0, g.v0, color[0], color[1], color[2], color[3]},
                b{x1, y0, 0, g.u1, g.v0, color[0], color[1], color[2], color[3]},
                c{x1, y1, 0, g.u1, g.v1, color[0], color[1], color[2], color[3]},
                d{x0, y1, 0, g.u0, g.v1, color[0], color[1], color[2], color[3]};
            out.insert(out.end(), {a, b, c, a, c, d});
        }
        x += g.advance;
    }
    return x;
}

GLuint Font::texture() {
    if (!tex_) {
        glGenTextures(1, &tex_);
        glBindTexture(GL_TEXTURE_2D, tex_);
        glTexStorage2D(GL_TEXTURE_2D, 4, GL_R8, atlas_w_, atlas_h_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 3);
        set_anisotropy();
    }
    if (dirty_ && loaded_) {
        glBindTexture(GL_TEXTURE_2D, tex_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, atlas_w_, atlas_h_, GL_RED, GL_UNSIGNED_BYTE, pixels_.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        dirty_ = false;
    }
    return tex_;
}
