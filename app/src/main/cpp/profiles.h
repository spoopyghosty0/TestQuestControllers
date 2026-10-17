// Controller types (interaction profiles) and their inputs, generated from the OpenXR registry.
#pragma once

#include <openxr/openxr.h>

#include <cstring>
#include <string>

enum Hand { kLeft = 1, kRight = 2, kBoth = 3 };

struct Component {
    int hands;              // Hand mask
    const char *subpath;    // e.g. "/input/x/click"
    XrActionType type;
    bool system;            // reserved by the runtime: shown, never bound
    const char *extension;  // bound only when this extension is enabled, else nullptr
};

struct Profile {
    const char *path;       // interaction profile path
    const char *label;      // menu label
    const char *title;      // registry title
    const char *extension;  // extension that defines the profile, nullptr for core profiles
    int min_minor;          // OpenXR 1.<min_minor> needed (1 = core profile of OpenXR 1.1)
    const char *family;     // model: WebXR Input Profiles id (assets/webxr/<family>/)
    const Component *components;
    int component_count;
};

#include "profiles_gen.inc"

constexpr int kProfileCount = sizeof(kProfiles) / sizeof(kProfiles[0]);

// The model part an input lights up: the identifier after /input/ with the profile-specific spellings folded
// together (trigger_curl -> trigger, thumb_fb -> thumbrest, stylus_fb -> stylus, ...). "" for poses and outputs.
// tools/test_tables.py checks the parts against the models' components.
inline std::string part_for(const char *subpath) {
    if (std::strncmp(subpath, "/input/", 7) != 0) return "";
    std::string id(subpath + 7);
    id = id.substr(0, id.find('/'));
    if (id == "grip" || id == "aim") return "";
    if (id.rfind("trigger", 0) == 0) return "trigger";
    if (id.rfind("thumbstick", 0) == 0) return "thumbstick";
    if (id.rfind("thumb", 0) == 0) return "thumbrest";  // thumbrest, thumb_fb, thumb_meta, thumb_resting_surfaces
    if (id.rfind("stylus", 0) == 0) return "stylus";
    return id;
}

// Short label for a part, shown next to the floating models.
inline const char *part_label(const std::string &part) {
    static const struct { const char *part, *label; } kLabels[] = {
        {"x", "X"}, {"y", "Y"}, {"a", "A"}, {"b", "B"}, {"menu", "Menu"}, {"system", "Meta"},
        {"back", "Back"}, {"trigger", "Trigger"}, {"squeeze", "Grip"}, {"thumbstick", "Stick"},
        {"thumbrest", "Thumbrest"}, {"trackpad", "Touchpad"}, {"stylus", "Stylus"},
    };
    for (const auto &l : kLabels)
        if (part == l.part) return l.label;
    return "";
}
