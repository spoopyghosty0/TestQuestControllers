// The info panel: controller type menu, runtime answers and live input values. It's drawn into its own image at
// about one texel per display pixel (see Scene), with text rasterised at the size it's shown at.
#pragma once

#include <string>
#include <vector>

#include "gfx.h"
#include "input.h"
#include "text.h"

struct ProfileStatus {
    std::string note;  // why it can't be picked or what the runtime said, "" if fine
    bool available = true;
};

struct PanelInfo {
    std::string runtime, system, api;
    int current = 0;
    std::vector<ProfileStatus> profiles;
    std::string suggest_result;
    std::string reported[2];
    std::string message;  // e.g. "No input with ...: back to ..."
    bool haptics = true;
    bool passthrough = false;
    std::string passthrough_note;  // how passthrough works here, or why it doesn't
    std::string haptic_result;     // first result of each haptic output, e.g. "haptic XR_SUCCESS"
    const Input *input = nullptr;
    int hover_item = -1;
    struct Cursor {
        float u, v;  // panel pixels
        int hand;
    };
    std::vector<Cursor> cursors;  // where the pointer rays meet the panel
};

enum ItemKind { kItemProfile, kItemHaptics, kItemPassthrough, kItemRecenter };

struct PanelItem {
    ItemKind kind;
    int index;
    float x0, y0, x1, y1;  // panel pixels
};

class Panel {
public:
    Pose pose;                  // centre of the panel; it faces +Z
    float px = 0.0015f;         // metres per panel pixel
    float width = 560, height = 680;
    float texel_scale = 2;      // texels of the panel's image per panel pixel
    int image_width() const { return (int)(width * texel_scale + 0.5f); }
    int image_height() const { return (int)(height * texel_scale + 0.5f); }

    // Triangles in image texels (0, 0 = top left), solid rectangles and text from `font`'s atlas.
    void build(const PanelInfo &info, Font &font, std::vector<TexVertex> &tris);
    // Ray from `aim` (along -Z): panel pixel coordinates of the hit and its world point.
    bool hit(const Pose &aim, float &u, float &v, Vec3 &world) const;
    int item_at(float u, float v) const;
    const std::vector<PanelItem> &items() const { return items_; }

private:
    std::vector<PanelItem> items_;
    Font *font_ = nullptr;
    void rect(std::vector<TexVertex> &t, float x0, float y0, float x1, float y1, const float c[4]) const;
    void disc(std::vector<TexVertex> &t, float cx, float cy, float r, const float c[4]) const;
    // x, y: panel pixels (top of the line); scale 1 = body text. Returns the x after the text.
    float text(std::vector<TexVertex> &t, float x, float y, float scale, const std::string &s,
               const float c[4]) const;
};

// Text in world space on the plane of `pose` (x right, y up, facing +Z), centred on its position; `em` = font
// size in metres.
void world_text(Font &font, std::vector<TexVertex> &tris, const Pose &pose, float em, const std::string &s,
                const float color[4]);
