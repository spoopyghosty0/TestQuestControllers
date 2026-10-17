// The info panel: controller type menu, runtime answers and live input values, drawn as flat geometry.
#pragma once

#include <string>
#include <vector>

#include "gfx.h"
#include "input.h"

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
    const Input *input = nullptr;
    int hover_item = -1;
};

enum ItemKind { kItemProfile, kItemHaptics, kItemRecenter };

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

    void build(const PanelInfo &info, std::vector<FlatVertex> &tris);
    // Ray from `aim` (along -Z): panel pixel coordinates of the hit and its world point.
    bool hit(const Pose &aim, float &u, float &v, Vec3 &world) const;
    int item_at(float u, float v) const;
    const std::vector<PanelItem> &items() const { return items_; }

private:
    std::vector<PanelItem> items_;
    Vec3 to_world(float u, float v, float z = 0) const;
    void rect(std::vector<FlatVertex> &t, float x0, float y0, float x1, float y1, const float c[4], float z) const;
    float text(std::vector<FlatVertex> &t, float x, float y, float scale, const std::string &s,
               const float c[4]) const;
};

// Text in world space on the plane of `pose` (x right, y up, facing +Z), centred on its position.
void world_text(std::vector<FlatVertex> &tris, const Pose &pose, float metres_per_px, const std::string &s,
                const float color[4]);
