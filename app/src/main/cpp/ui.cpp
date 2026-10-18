#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>


namespace {
const float kBg[4] = {0.012f, 0.014f, 0.02f, 0.94f};
const float kRow[4] = {0.03f, 0.035f, 0.05f, 1};
const float kHover[4] = {0.06f, 0.09f, 0.16f, 1};
const float kCurrent[4] = {0.45f, 0.13f, 0.01f, 1};
const float kText[4] = {0.85f, 0.85f, 0.88f, 1};
const float kDim[4] = {0.25f, 0.26f, 0.3f, 1};
const float kHead[4] = {1.0f, 0.42f, 0.05f, 1};
const float kBlue[4] = {0.2f, 0.5f, 1.0f, 1};
const float kGood[4] = {0.3f, 0.85f, 0.4f, 1};
const float kBad[4] = {1.0f, 0.3f, 0.25f, 1};

// body text size in panel pixels (1 panel pixel = 1.25 mm, see Scene::layout)
const float kEm = 10.5f;
}  // namespace

void Panel::rect(std::vector<TexVertex> &t, float x0, float y0, float x1, float y1, const float c[4]) const {
    // whole texels: crisp edges
    float s = texel_scale, u = font_->white_u(), v = font_->white_v();
    x0 = std::round(x0 * s), y0 = std::round(y0 * s), x1 = std::round(x1 * s), y1 = std::round(y1 * s);
    const TexVertex a{x0, y0, 0, u, v, c[0], c[1], c[2], c[3]}, b{x1, y0, 0, u, v, c[0], c[1], c[2], c[3]},
        cc{x1, y1, 0, u, v, c[0], c[1], c[2], c[3]}, d{x0, y1, 0, u, v, c[0], c[1], c[2], c[3]};
    t.insert(t.end(), {a, b, cc, a, cc, d});
}

void Panel::disc(std::vector<TexVertex> &t, float cx, float cy, float r, const float c[4]) const {
    float s = texel_scale, u = font_->white_u(), v = font_->white_v();
    const int n = 24;
    for (int i = 0; i < n; ++i) {
        float a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
        t.push_back({cx * s, cy * s, 0, u, v, c[0], c[1], c[2], c[3]});
        t.push_back({(cx + r * std::cos(a0)) * s, (cy + r * std::sin(a0)) * s, 0, u, v, c[0], c[1], c[2], c[3]});
        t.push_back({(cx + r * std::cos(a1)) * s, (cy + r * std::sin(a1)) * s, 0, u, v, c[0], c[1], c[2], c[3]});
    }
}

float Panel::text(std::vector<TexVertex> &t, float x, float y, float scale, const std::string &s,
                  const float c[4]) const {
    FontStyle style = scale > 1.1f ? kMedium : kRegular;  // headings a little heavier
    int size = (int)std::lround(kEm * scale * texel_scale);
    float end = font_->layout(style, size, std::round(x * texel_scale), std::round(y * texel_scale), s, c, t);
    return end / texel_scale;
}

static std::string short_path(const char *subpath) {
    std::string s(subpath);
    if (s.rfind("/input/", 0) == 0) s = s.substr(7);
    return s;
}

void Panel::build(const PanelInfo &info, Font &font, std::vector<TexVertex> &t) {
    font_ = &font;
    items_.clear();
    rect(t, 0, 0, width, height, kBg);
    float y = 14;
    text(t, 16, y, 2.0f, "Test Quest Controllers", kHead);
    y += 26;
    text(t, 16, y, 1, "Runtime: " + info.runtime, kText);
    y += 12;
    text(t, 16, y, 1, "System: " + info.system + "   " + info.api, kText);
    y += 20;

    text(t, 16, y, 1.3f, "Controller type (point + trigger)", kHead);
    y += 16;
    for (int i = 0; i < kProfileCount; ++i) {
        const ProfileStatus &st = info.profiles[i];
        PanelItem item{kItemProfile, i, 12, y - 2, width - 12, y + 13};
        int idx = (int)items_.size();
        items_.push_back(item);
        const float *bg = i == info.current ? kCurrent : (info.hover_item == idx ? kHover : kRow);
        rect(t, item.x0, item.y0, item.x1, item.y1, bg);
        float x = text(t, 18, y + 2, 1, kProfiles[i].label, st.available ? kText : kDim);
        if (!st.note.empty()) text(t, x + 10, y + 2, 1, st.note, st.available ? kBlue : kDim);
        y += 17;
    }
    y += 4;
    {
        struct Toggle { ItemKind kind; float x0, x1; std::string label; };
        const Toggle toggles[] = {
            {kItemHaptics, 12, 186, info.haptics ? "Vibration: on" : "Vibration: off"},
            {kItemPassthrough, 196, 380, info.passthrough ? "Passthrough: on" : "Passthrough: off"},
            {kItemRecenter, 390, 548, "Recenter"},
        };
        for (const Toggle &tg : toggles) {
            PanelItem it{tg.kind, 0, tg.x0, y - 2, tg.x1, y + 13};
            int idx = (int)items_.size();
            items_.push_back(it);
            rect(t, it.x0, it.y0, it.x1, it.y1, info.hover_item == idx ? kHover : kRow);
            text(t, tg.x0 + 6, y + 2, 1, tg.label, kText);
        }
        y += 17;
        std::string notes;
        if (!info.passthrough_note.empty()) notes = "Passthrough: " + info.passthrough_note;
        if (!info.haptic_result.empty()) notes += (notes.empty() ? "" : "   ") + std::string("Vibration: ") + info.haptic_result;
        if (!notes.empty()) text(t, 16, y + 1, 1, notes, kDim);
    }
    y += 22;

    const Profile &p = kProfiles[info.current];
    text(t, 16, y, 1.3f, p.title, kHead);
    y += 16;
    text(t, 16, y, 1, p.path, kText);
    y += 12;
    bool ok = info.suggest_result.find("SUCCESS") != std::string::npos;
    text(t, 16, y, 1, "Suggested bindings: " + info.suggest_result, ok ? kGood : kBad);
    y += 12;
    for (int h = 0; h < 2; ++h) {
        std::string r = info.reported[h].empty() ? "(none yet)" : info.reported[h];
        text(t, 16, y, 1, std::string(h == 0 ? "Runtime reports left:  " : "Runtime reports right: ") + r,
             info.reported[h] == p.path ? kGood : kText);
        y += 12;
    }
    if (!info.message.empty()) {
        text(t, 16, y, 1, info.message, kBad);
        y += 12;
    }
    y += 8;

    // live inputs, one column per hand
    if (info.input) {
        float col_y[2] = {y, y};
        for (int h = 0; h < 2; ++h) text(t, 16 + h * 276, col_y[h], 1.3f, h == 0 ? "Left" : "Right", kHead);
        col_y[0] += 16;
        col_y[1] += 16;
        for (const auto &row : info.input->rows) {
            int h = row.hand;
            float x = 16 + h * 276;
            const InputValue &v = row.value;
            char val[40] = "";
            bool on = false;
            if (row.component->system) {
                snprintf(val, sizeof(val), "system");
            } else if (!v.active) {
                snprintf(val, sizeof(val), "inactive");
            } else if (row.component->type == XR_ACTION_TYPE_BOOLEAN_INPUT) {
                snprintf(val, sizeof(val), "%s", v.b ? "ON" : "off");
                on = v.b;
            } else if (row.component->type == XR_ACTION_TYPE_FLOAT_INPUT) {
                snprintf(val, sizeof(val), "%.2f", v.f);
                on = v.f > 0.05f;
            } else {
                snprintf(val, sizeof(val), "%+.2f %+.2f", v.v.x, v.v.y);
                on = v.v.x * v.v.x + v.v.y * v.v.y > 0.01f;
            }
            const float *color = on ? kHead : (v.seen ? kText : kDim);
            text(t, x, col_y[h], 1, (v.seen ? "* " : "  ") + short_path(row.component->subpath), color);
            text(t, x + 190, col_y[h], 1, val, color);
            col_y[h] += 11;
        }
        y = std::max(col_y[0], col_y[1]) + 6;
        // colour legend, the same colours as the model highlights (scene.cpp)
        const float touch_c[4] = {0.15f, 0.5f, 1.0f, 1}, press_c[4] = {1.0f, 0.42f, 0.08f, 1},
                    pull_c[4] = {1.0f, 0.82f, 0.15f, 1};
        float x = text(t, 16, y, 1.2f, "Touched", touch_c);
        x = text(t, x + 14, y, 1.2f, "Pressed", press_c);
        text(t, x + 14, y, 1.2f, "Pulled", pull_c);
        y += 15;
        text(t, 16, y, 1, "* = has fired.  Inactive = the runtime sends nothing for it.", kDim);
        y += 12;
        if (!info.input->skipped_notes.empty()) {
            text(t, 16, y, 1, "Left out: " + std::to_string(info.input->skipped) + " input(s), see log", kDim);
            y += 12;
        }
    }
    (void)y;

    // where the rays point: drawn into the panel (the panel's layer covers the rest of the scene)
    for (const PanelInfo::Cursor &c : info.cursors) {
        const float ring[4] = {0.02f, 0.02f, 0.03f, 0.9f}, dot[4] = {1.0f, 0.5f, 0.1f, 1};
        disc(t, c.u, c.v, 4.5f, ring);
        disc(t, c.u, c.v, 3.2f, dot);
    }
}

bool Panel::hit(const Pose &aim, float &u, float &v, Vec3 &world) const {
    Vec3 origin = aim.p, dir = aim.q.rotate({0, 0, -1});
    Vec3 n = pose.q.rotate({0, 0, 1});
    float denom = dot(dir, n);
    if (std::fabs(denom) < 1e-4f) return false;
    float d = dot(pose.p - origin, n) / denom;
    if (d < 0 || d > 5) return false;
    world = origin + dir * d;
    Vec3 local = pose.inverse().apply(world);
    u = local.x / px + width / 2;
    v = height / 2 - local.y / px;
    return u >= 0 && u <= width && v >= 0 && v <= height;
}

int Panel::item_at(float u, float v) const {
    for (size_t i = 0; i < items_.size(); ++i)
        if (u >= items_[i].x0 && u <= items_[i].x1 && v >= items_[i].y0 && v <= items_[i].y1) return (int)i;
    return -1;
}

void world_text(Font &font, std::vector<TexVertex> &tris, const Pose &pose, float em, const std::string &s,
                const float color[4]) {
    const int size = 48;  // rasterised once at this size; mipmaps + anisotropic filtering scale it down
    float m = em / size, w = font.width(kMedium, size, s), cap = font.cap_height(kMedium, size);
    size_t first = tris.size();
    font.layout(kMedium, size, 0, 0, s, color, tris);
    for (size_t i = first; i < tris.size(); ++i) {
        TexVertex &v = tris[i];
        Vec3 p = pose.apply(Vec3((v.x - w / 2) * m, (cap / 2 - v.y) * m, 0));  // capitals centred on the pose
        v.x = p.x, v.y = p.y, v.z = p.z;
    }
}
