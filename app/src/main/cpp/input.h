// Actions for one controller type: one action per input, bindings suggested for that profile only.
#pragma once

#include <openxr/openxr.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "profiles.h"
#include "xmath.h"

// What a model part shows for one hand.
struct PartState {
    bool pressed = false, touched = false, near = false;
    float value = 0;     // largest analog value (trigger, grip, force, curl, slide)
    float x = 0, y = 0;  // thumbstick / trackpad
    bool any() const { return pressed || touched || near || value > 0.02f || x * x + y * y > 0.0004f; }
};

struct InputValue {
    bool active = false;  // the runtime delivers this input (isActive)
    bool b = false;
    float f = 0;
    XrVector2f v{0, 0};
    bool seen = false;    // has been non-zero at least once
    float logged = -1;    // last logged value (logging in steps)
};

struct InputRow {  // one input of one hand, for the panel
    const Component *component;
    int hand;  // 0 left, 1 right
    std::string part;
    XrAction action = XR_NULL_HANDLE;
    InputValue value;
};

class Input {
public:
    // Creates the action set and suggests bindings for `profile`. Returns the runtime's answer to the suggestion.
    XrResult create(XrInstance instance, const Profile &profile, const std::set<std::string> &enabled_extensions);
    XrResult attach(XrSession session);
    void sync(XrSession session, XrSpace base, XrTime time);
    void destroy();

    void haptic(XrSession session, int hand, float amplitude, float seconds);

    PartState part(int hand, const std::string &name) const;
    bool trigger_down(int hand) const;
    bool stick_click(int hand) const;
    bool grip_valid[2] = {false, false}, aim_valid[2] = {false, false};
    Pose grip[2], aim[2];

    // Demo mode (a file named "demo" in the app's files folder): parts light up in turn without a controller,
    // for screenshots and for checking the models on a headset nobody wears.
    bool demo = false;
    double demo_clock = -1;  // seconds; < 0 = real time (the host renderer sets it)

    std::vector<InputRow> rows;
    int bound = 0, skipped = 0;  // inputs bound / left out (system buttons, extension not enabled)
    std::vector<std::string> skipped_notes;
    XrPath hand_paths[2] = {XR_NULL_PATH, XR_NULL_PATH};

private:
    XrInstance instance_ = XR_NULL_HANDLE;
    XrActionSet set_ = XR_NULL_HANDLE;
    XrAction grip_action_ = XR_NULL_HANDLE, aim_action_ = XR_NULL_HANDLE, haptic_action_ = XR_NULL_HANDLE;
    XrSpace grip_space_[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE}, aim_space_[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
    std::vector<XrAction> extra_actions_;
};

const char *result_name(XrInstance instance, XrResult result);
