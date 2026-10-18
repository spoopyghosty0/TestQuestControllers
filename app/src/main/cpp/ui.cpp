#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

#include "stb_easy_font.h"

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

// stb_easy_font emits quads of 4 vertices: x, y, z, packed colour (16 bytes each)
struct StbVertex {
    float x, y, z;
    unsigned char c[4];
};

void emit_quads(std::vector<FlatVertex> &t, const std::string &s, const float color[4],
                const std::function<Vec3(float, float)> &map) {
    static StbVertex buf[6000];
    int quads = stb_easy_font_print(0, 0, const_cast<char *>(s.c_str()), nullptr, buf, sizeof(buf));
    for (int q = 0; q < quads; ++q) {
        const StbVertex *v = &buf[q * 4];
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k : order) {
            Vec3 w = map(v[k].x, v[k].y);
            t.push_back({w.x, w.y, w.z, color[0], color[1], color[2], color[3]});
        }
    }
}
}  // namespace

Vec3 Panel::to_world(float u, float v, float z) const {
    return pose.apply(Vec3((u - width / 2) * px, (height / 2 - v) * px, z));
}

void Panel::rect(std::vector<FlatVertex> &t, float x0, float y0, float x1, float y1, const float c[4],
                 float z) const {
    Vec3 a = to_world(x0, y0, z), b = to_world(x1, y0, z), cc = to_world(x1, y1, z), d = to_world(x0, y1, z);
    for (const Vec3 &p : {a, b, cc, a, cc, d}) t.push_back({p.x, p.y, p.z, c[0], c[1], c[2], c[3]});
}

float Panel::text(std::vector<FlatVertex> &t, float x, float y, float scale, const std::string &s,
                  const float c[4]) const {
    emit_quads(t, s, c, [&](float gx, float gy) { return to_world(x + gx * scale, y + gy * scale, 0.002f); });
    return x + stb_easy_font_width(const_cast<char *>(s.c_str())) * scale;
}

static std::string short_path(const char *subpath) {
    std::string s(subpath);
    if (s.rfind("/input/", 0) == 0) s = s.substr(7);
    return s;
}

void Panel::build(const PanelInfo &info, std::vector<FlatVertex> &t) {
    items_.clear();
    rect(t, 0, 0, width, height, kBg, 0);
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
        rect(t, item.x0, item.y0, item.x1, item.y1, bg, 0.001f);
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
            rect(t, it.x0, it.y0, it.x1, it.y1, info.hover_item == idx ? kHover : kRow, 0.001f);
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

void world_text(std::vector<FlatVertex> &tris, const Pose &pose, float m, const std::string &s,
                const float color[4]) {
    float w = stb_easy_font_width(const_cast<char *>(s.c_str()));
    emit_quads(tris, s, color, [&](float gx, float gy) { return pose.apply(Vec3((gx - w / 2) * m, (3.5f - gy) * m, 0)); });
}
