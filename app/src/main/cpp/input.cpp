#include "input.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "log.h"

const char *result_name(XrInstance instance, XrResult result) {
    static thread_local char buf[XR_MAX_RESULT_STRING_SIZE];
    if (instance == XR_NULL_HANDLE || XR_FAILED(xrResultToString(instance, result, buf)))
        snprintf(buf, sizeof(buf), "XrResult %d", result);
    return buf;
}

static bool ends_with(const std::string &s, const char *suffix) {
    size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

static std::string action_name(const char *subpath) {
    std::string out;
    for (const char *c = subpath + 1; *c; ++c) out += (*c == '/') ? '_' : *c;
    return out;
}

static XrPath path(XrInstance instance, const std::string &s) {
    XrPath p = XR_NULL_PATH;
    xrStringToPath(instance, s.c_str(), &p);
    return p;
}

XrResult Input::create(XrInstance instance, const Profile &profile, const std::set<std::string> &enabled) {
    instance_ = instance;
    rows.clear();
    skipped_notes.clear();
    haptic_results.clear();
    bound = skipped = 0;
    hand_paths[0] = path(instance, "/user/hand/left");
    hand_paths[1] = path(instance, "/user/hand/right");

    XrActionSetCreateInfo set_info{XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(set_info.actionSetName, "controller_test");
    strcpy(set_info.localizedActionSetName, "Controller test");
    XrResult r = xrCreateActionSet(instance, &set_info, &set_);
    if (XR_FAILED(r)) return r;

    std::vector<XrActionSuggestedBinding> bindings;
    std::map<std::string, XrAction> by_subpath;

    auto make_action = [&](const char *subpath, XrActionType type, int hands) -> XrAction {
        auto it = by_subpath.find(subpath);
        if (it != by_subpath.end()) return it->second;
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        std::string name = action_name(subpath);
        snprintf(info.actionName, sizeof(info.actionName), "%s", name.c_str());
        snprintf(info.localizedActionName, sizeof(info.localizedActionName), "%s", subpath);
        info.actionType = type;
        // every action covers both hands so a hand-specific input can be bound on either side
        info.countSubactionPaths = 2;
        info.subactionPaths = hand_paths;
        (void)hands;
        XrAction action = XR_NULL_HANDLE;
        if (XR_FAILED(xrCreateAction(set_, &info, &action))) {
            LOGE("xrCreateAction %s failed", name.c_str());
            return XR_NULL_HANDLE;
        }
        by_subpath[subpath] = action;
        return action;
    };

    for (int i = 0; i < profile.component_count; ++i) {
        const Component &c = profile.components[i];
        std::string part = part_for(c.subpath);
        if (c.system) {
            ++skipped;
            skipped_notes.push_back(std::string(c.subpath) + ": reserved by the system");
            for (int h = 0; h < 2; ++h)
                if (c.hands & (1 << h)) rows.push_back({&c, h, part});
            continue;
        }
        if (c.extension && !enabled.count(c.extension)) {
            ++skipped;
            skipped_notes.push_back(std::string(c.subpath) + ": needs " + c.extension);
            continue;
        }
        XrAction action = make_action(c.subpath, c.type, c.hands);
        if (action == XR_NULL_HANDLE) continue;
        if (std::string(c.subpath) == "/input/grip/pose") grip_action_ = action;
        else if (std::string(c.subpath) == "/input/aim/pose") aim_action_ = action;
        else if (c.type == XR_ACTION_TYPE_VIBRATION_OUTPUT) haptic_actions_[c.subpath] = action;
        for (int h = 0; h < 2; ++h) {
            if (!(c.hands & (1 << h))) continue;
            std::string full = std::string(h == 0 ? "/user/hand/left" : "/user/hand/right") + c.subpath;
            bindings.push_back({action, path(instance, full)});
            ++bound;
            if (c.type != XR_ACTION_TYPE_POSE_INPUT && c.type != XR_ACTION_TYPE_VIBRATION_OUTPUT) {
                InputRow row{&c, h, part, action};
                rows.push_back(row);
            }
        }
    }

    XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = path(instance, profile.path);
    suggested.countSuggestedBindings = (uint32_t)bindings.size();
    suggested.suggestedBindings = bindings.data();
    r = xrSuggestInteractionProfileBindings(instance, &suggested);
    LOGI("profile %s: suggest %u bindings (%d left out) -> %s", profile.path, (unsigned)bindings.size(), skipped,
         result_name(instance, r));
    for (const auto &note : skipped_notes) LOGI("  left out %s", note.c_str());
    return r;
}

XrResult Input::attach(XrSession session) {
    XrSessionActionSetsAttachInfo info{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    info.countActionSets = 1;
    info.actionSets = &set_;
    XrResult r = xrAttachSessionActionSets(session, &info);
    if (XR_FAILED(r)) {
        LOGE("xrAttachSessionActionSets -> %s", result_name(instance_, r));
        return r;
    }
    for (int h = 0; h < 2; ++h) {
        XrActionSpaceCreateInfo space{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        space.poseInActionSpace.orientation.w = 1;
        space.subactionPath = hand_paths[h];
        if (grip_action_) {
            space.action = grip_action_;
            xrCreateActionSpace(session, &space, &grip_space_[h]);
        }
        if (aim_action_) {
            space.action = aim_action_;
            xrCreateActionSpace(session, &space, &aim_space_[h]);
        }
    }
    return r;
}

static void log_change(InputRow &row, float now, const char *text) {
    // log every boolean change; analog values in 0.25 steps
    if (row.value.logged >= 0 && std::fabs(now - row.value.logged) < 0.25f &&
        !(now == 0 && row.value.logged != 0) && !(now == 1 && row.value.logged != 1))
        return;
    row.value.logged = now;
    LOGI("%s %s %s", row.hand == 0 ? "left " : "right", row.component->subpath, text);
}

void Input::sync(XrSession session, XrSpace base, XrTime time) {
    XrActiveActionSet active{set_, XR_NULL_PATH};
    XrActionsSyncInfo sync_info{XR_TYPE_ACTIONS_SYNC_INFO};
    sync_info.countActiveActionSets = 1;
    sync_info.activeActionSets = &active;
    if (XR_FAILED(xrSyncActions(session, &sync_info))) return;

    for (auto &row : rows) {
        if (row.action == XR_NULL_HANDLE) continue;
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = row.action;
        get.subactionPath = hand_paths[row.hand];
        char text[48];
        switch (row.component->type) {
            case XR_ACTION_TYPE_BOOLEAN_INPUT: {
                XrActionStateBoolean s{XR_TYPE_ACTION_STATE_BOOLEAN};
                if (XR_FAILED(xrGetActionStateBoolean(session, &get, &s))) break;
                row.value.active = s.isActive;
                row.value.b = s.isActive && s.currentState;
                row.value.seen |= row.value.b;
                snprintf(text, sizeof(text), "%d", row.value.b ? 1 : 0);
                if ((float)row.value.b != row.value.logged && s.isActive) log_change(row, row.value.b, text);
                break;
            }
            case XR_ACTION_TYPE_FLOAT_INPUT: {
                XrActionStateFloat s{XR_TYPE_ACTION_STATE_FLOAT};
                if (XR_FAILED(xrGetActionStateFloat(session, &get, &s))) break;
                row.value.active = s.isActive;
                row.value.f = s.isActive ? s.currentState : 0;
                row.value.seen |= row.value.f > 0.05f;
                snprintf(text, sizeof(text), "%.2f", row.value.f);
                if (s.isActive) log_change(row, row.value.f, text);
                break;
            }
            case XR_ACTION_TYPE_VECTOR2F_INPUT: {
                XrActionStateVector2f s{XR_TYPE_ACTION_STATE_VECTOR2F};
                if (XR_FAILED(xrGetActionStateVector2f(session, &get, &s))) break;
                row.value.active = s.isActive;
                row.value.v = s.isActive ? s.currentState : XrVector2f{0, 0};
                float mag = std::sqrt(row.value.v.x * row.value.v.x + row.value.v.y * row.value.v.y);
                row.value.seen |= mag > 0.1f;
                snprintf(text, sizeof(text), "%.2f %.2f", row.value.v.x, row.value.v.y);
                if (s.isActive) log_change(row, mag, text);
                break;
            }
            default:
                break;
        }
    }

    for (int h = 0; h < 2; ++h) {
        grip_valid[h] = aim_valid[h] = false;
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if (grip_space_[h] && XR_SUCCEEDED(xrLocateSpace(grip_space_[h], base, time, &loc)) &&
            (loc.locationFlags & need) == need) {
            grip[h] = Pose(loc.pose);
            grip_valid[h] = true;
        }
        loc = {XR_TYPE_SPACE_LOCATION};
        if (aim_space_[h] && XR_SUCCEEDED(xrLocateSpace(aim_space_[h], base, time, &loc)) &&
            (loc.locationFlags & need) == need) {
            aim[h] = Pose(loc.pose);
            aim_valid[h] = true;
        }
    }
}

static PartState demo_part(int hand, const std::string &name, double clock) {
    static const char *kOrder[] = {"trigger", "squeeze", "thumbstick", "x", "y", "a", "b", "menu", "thumbrest",
                                   "trackpad", "back", "stylus"};
    double t = clock >= 0 ? clock
                          : std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    int n = sizeof(kOrder) / sizeof(kOrder[0]);
    int slot = (int)(t / 1.5) % n;
    float phase = (float)std::fmod(t, 1.5) / 1.5f;  // 0..1 within the slot
    PartState st;
    if (name != kOrder[slot] && !(hand == 1 && name == kOrder[(slot + n / 2) % n])) return st;
    float wave = std::sin(phase * 3.14159265f);
    if (name == "trigger" || name == "squeeze" || name == "stylus") {
        st.value = wave;
        st.touched = true;
    } else if (name == "thumbstick" || name == "trackpad") {
        st.touched = true;
        st.x = std::cos(phase * 6.2831853f) * 0.9f;
        st.y = std::sin(phase * 6.2831853f) * 0.9f;
        st.pressed = phase > 0.85f;
    } else {
        st.touched = phase < 0.4f;
        st.pressed = phase >= 0.4f;
    }
    return st;
}

PartState Input::part(int hand, const std::string &name) const {
    if (demo) return demo_part(hand, name, demo_clock);
    PartState st;
    for (const auto &row : rows) {
        if (row.hand != hand || row.part != name || !row.value.active) continue;
        std::string sub = row.component->subpath;
        switch (row.component->type) {
            case XR_ACTION_TYPE_BOOLEAN_INPUT:
                if (!row.value.b) break;
                if (ends_with(sub, "/click")) st.pressed = true;
                else if (ends_with(sub, "/touch")) st.touched = true;
                else st.near = true;  // proximity
                break;
            case XR_ACTION_TYPE_FLOAT_INPUT:
                st.value = std::max(st.value, row.value.f);
                if (ends_with(sub, "/force") && row.value.f > 0.5f) st.pressed = true;
                break;
            case XR_ACTION_TYPE_VECTOR2F_INPUT:
                st.x = row.value.v.x;
                st.y = row.value.v.y;
                break;
            default:
                break;
        }
    }
    return st;
}

bool Input::trigger_down(int hand) const {
    for (const auto &row : rows) {
        if (row.hand != hand || !row.value.active) continue;
        std::string sub = row.component->subpath;
        if (sub == "/input/trigger/value" && row.value.f > 0.75f) return true;
        if (sub == "/input/trigger/click" && row.value.b) return true;
    }
    return false;
}

bool Input::stick_click(int hand) const {
    for (const auto &row : rows) {
        if (row.hand != hand || !row.value.active) continue;
        std::string sub = row.component->subpath;
        if ((sub == "/input/thumbstick/click" || sub == "/input/trackpad/click") && row.value.b) return true;
    }
    return false;
}

std::string Input::haptic_output_for(const std::string &part) const {
    const char *names[2] = {nullptr, nullptr};
    if (part == "trigger") names[0] = "/output/haptic_trigger_fb", names[1] = "/output/haptic_trigger";
    if (part == "thumbstick" || part == "thumbrest")
        names[0] = "/output/haptic_thumb_fb", names[1] = "/output/haptic_thumb";
    for (const char *n : names)
        if (n && haptic_actions_.count(n)) return n;
    return "/output/haptic";
}

XrResult Input::haptic(XrSession session, int hand, float amplitude, float seconds, const std::string &output) {
    auto it = haptic_actions_.find(output);
    if (it == haptic_actions_.end()) it = haptic_actions_.find("/output/haptic");
    if (it == haptic_actions_.end()) return XR_ERROR_ACTION_TYPE_MISMATCH;
    XrHapticVibration vib{XR_TYPE_HAPTIC_VIBRATION};
    vib.amplitude = amplitude;
    vib.duration = (XrDuration)(seconds * 1e9);
    vib.frequency = XR_FREQUENCY_UNSPECIFIED;
    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = it->second;
    info.subactionPath = hand_paths[hand];
    XrResult r = xrApplyHapticFeedback(session, &info, (const XrHapticBaseHeader *)&vib);
    if (!haptic_results.count(it->first)) {
        haptic_results[it->first] = result_name(instance_, r);
        LOGI("vibration %s (%s hand) -> %s", it->first.c_str(), hand == 0 ? "left" : "right",
             haptic_results[it->first].c_str());
    }
    return r;
}

void Input::destroy() {
    for (int h = 0; h < 2; ++h) {
        if (grip_space_[h]) xrDestroySpace(grip_space_[h]);
        if (aim_space_[h]) xrDestroySpace(aim_space_[h]);
        grip_space_[h] = aim_space_[h] = XR_NULL_HANDLE;
        grip_valid[h] = aim_valid[h] = false;
    }
    if (set_) xrDestroyActionSet(set_);  // destroys its actions too
    set_ = XR_NULL_HANDLE;
    grip_action_ = aim_action_ = XR_NULL_HANDLE;
    haptic_actions_.clear();
    rows.clear();
}
