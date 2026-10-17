// Test Quest Controllers: pick a Quest controller type, see what the runtime delivers for it.
//
// The app suggests bindings for exactly one interaction profile (the chosen controller type), like a game written
// for that controller, and shows every input it receives on a 3D model of that controller: in your hands at the
// grip pose and as an enlarged pair in front of you. Changing the type destroys and recreates the session (bindings
// can't change once action sets are attached); the instance is recreated only when the OpenXR version changes
// (1.1 core profiles need an OpenXR 1.1 instance; 1.0 profiles get a 1.0 instance, as most games use).
#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <jni.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "gfx.h"
#include "input.h"
#include "log.h"
#include "models.h"
#include "profiles.h"
#include "ui.h"

namespace {

double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

const char *kOptionalExtensions[] = {
    "XR_FB_touch_controller_pro",
    "XR_META_touch_controller_plus",
    "XR_FB_touch_controller_proximity",
};

const Vec3 kPartIdle(0.11f, 0.11f, 0.12f), kPartUnbound(0.025f, 0.025f, 0.03f), kBody(0.045f, 0.048f, 0.055f),
    kRing(0.06f, 0.063f, 0.07f), kOrange(1.0f, 0.36f, 0.03f), kBlue(0.08f, 0.32f, 1.0f), kNear(0.03f, 0.08f, 0.25f);

struct Swap {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    EyeTarget target;
    int width = 0, height = 0;
};

class App {
public:
    explicit App(android_app *a) : android_(a) {}
    void run();

private:
    android_app *android_;
    Gl gl_;
    Renderer renderer_;
    ModelCache models_;
    Panel panel_;

    XrInstance instance_ = XR_NULL_HANDLE;
    int instance_minor_ = -1;
    bool v11_unsupported_ = false;
    XrSystemId system_ = XR_NULL_SYSTEM_ID;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace local_ = XR_NULL_HANDLE, view_ = XR_NULL_HANDLE;
    XrSessionState state_ = XR_SESSION_STATE_UNKNOWN;
    bool running_ = false, exit_ = false;
    std::set<std::string> available_, enabled_;
    std::vector<XrViewConfigurationView> view_configs_;
    Swap swaps_[2];
    int64_t color_format_ = 0;

    Input input_;
    int current_ = 0, last_good_ = 0, pending_ = -1;
    std::string runtime_, system_name_, api_;
    std::string suggest_result_, reported_[2], message_;
    std::vector<ProfileStatus> statuses_ = std::vector<ProfileStatus>(kProfileCount);
    double focused_since_ = 0, stick_held_since_[2] = {0, 0}, haptic_flash_[2] = {0, 0}, last_report_ = 0;
    bool haptics_ = true, trig_prev_[2] = {false, false};
    int hover_ = -1;
    std::map<std::string, bool> pressed_prev_[2];
    bool placed_ = false;
    Pose anchor_;

    void load_choice();
    void save_choice();
    bool init_loader();
    bool open_instance(int minor);
    void close_instance();
    bool open_session(int index);
    void close_session();
    void switch_to(int index);
    void update_statuses();
    void poll_events();
    void handle_state(XrSessionState s);
    void update_reported();
    void frame();
    void update_ui(XrTime time);
    void place(XrTime time);
    void render_eye(const XrView &view, int eye, uint32_t image);
    void draw_model(const ControllerModel &model, int hand, const Pose &base, float scale, bool labels,
                    std::vector<FlatVertex> &tris);
    Pose display_pose(int hand) const;
    XrSession session() const { return session_; }
};

// ---------------------------------------------------------------- persistence
void App::load_choice() {
    std::ifstream f(std::string(android_->activity->internalDataPath) + "/controller_type.txt");
    std::string p;
    if (f >> p)
        for (int i = 0; i < kProfileCount; ++i)
            if (p == kProfiles[i].path) current_ = i;
}

void App::save_choice() {
    std::ofstream f(std::string(android_->activity->internalDataPath) + "/controller_type.txt");
    f << kProfiles[current_].path << "\n";
}

// ---------------------------------------------------------------- instance / session
bool App::init_loader() {
    PFN_xrInitializeLoaderKHR init = nullptr;
    if (XR_FAILED(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *)&init)) ||
        !init)
        return false;
    XrLoaderInitInfoAndroidKHR info{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    info.applicationVM = android_->activity->vm;
    info.applicationContext = android_->activity->clazz;
    XrResult r = init((const XrLoaderInitInfoBaseHeaderKHR *)&info);
    LOGI("xrInitializeLoaderKHR -> %d", r);
    return XR_SUCCEEDED(r);
}

bool App::open_instance(int minor) {
    uint32_t count = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
    std::vector<XrExtensionProperties> props(count, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, count, &count, props.data());
    available_.clear();
    for (const auto &p : props) available_.insert(p.extensionName);

    std::vector<const char *> exts = {"XR_KHR_android_create_instance", "XR_KHR_opengl_es_enable"};
    enabled_.clear();
    for (const char *e : kOptionalExtensions)
        if (available_.count(e)) {
            exts.push_back(e);
            enabled_.insert(e);
        }

    XrInstanceCreateInfoAndroidKHR android_info{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    android_info.applicationVM = android_->activity->vm;
    android_info.applicationActivity = android_->activity->clazz;
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    info.next = &android_info;
    strcpy(info.applicationInfo.applicationName, "Test Quest Controllers");
    info.applicationInfo.applicationVersion = 1;
    strcpy(info.applicationInfo.engineName, "none");
    info.applicationInfo.apiVersion = minor == 1 ? XR_MAKE_VERSION(1, 1, 0) : XR_MAKE_VERSION(1, 0, 34);
    info.enabledExtensionCount = (uint32_t)exts.size();
    info.enabledExtensionNames = exts.data();
    XrResult r = xrCreateInstance(&info, &instance_);
    LOGI("xrCreateInstance (OpenXR 1.%d, %zu extensions) -> %d", minor, exts.size(), r);
    if (XR_FAILED(r)) {
        instance_ = XR_NULL_HANDLE;
        if (minor == 1) v11_unsupported_ = true;
        return false;
    }
    instance_minor_ = minor;

    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
    xrGetInstanceProperties(instance_, &ip);
    char rt[160];
    snprintf(rt, sizeof(rt), "%s %u.%u.%u", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
             XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));
    runtime_ = rt;
    api_ = minor == 1 ? "OpenXR 1.1 instance" : "OpenXR 1.0 instance";

    XrSystemGetInfo sys{XR_TYPE_SYSTEM_GET_INFO};
    sys.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = xrGetSystem(instance_, &sys, &system_);
    if (XR_FAILED(r)) {
        LOGE("xrGetSystem -> %s", result_name(instance_, r));
        return false;
    }
    XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
    xrGetSystemProperties(instance_, system_, &sp);
    system_name_ = sp.systemName;
    LOGI("runtime: %s, system: %s", runtime_.c_str(), system_name_.c_str());

    PFN_xrGetOpenGLESGraphicsRequirementsKHR reqs = nullptr;
    xrGetInstanceProcAddr(instance_, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&reqs);
    if (reqs) {
        XrGraphicsRequirementsOpenGLESKHR g{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
        reqs(instance_, system_, &g);
    }
    count = 0;
    xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count,
                                      nullptr);
    view_configs_.assign(count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count,
                                      view_configs_.data());
    return count >= 2;
}

void App::close_instance() {
    if (instance_) xrDestroyInstance(instance_);
    instance_ = XR_NULL_HANDLE;
    instance_minor_ = -1;
}

bool App::open_session(int index) {
    const Profile &profile = kProfiles[index];
    XrResult sr = input_.create(instance_, profile, enabled_);
    suggest_result_ = result_name(instance_, sr);
    statuses_[index].note = XR_SUCCEEDED(sr) ? "" : std::string("rejected: ") + suggest_result_;

    XrGraphicsBindingOpenGLESAndroidKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    binding.display = gl_.display;
    binding.config = gl_.config;
    binding.context = gl_.context;
    XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};
    info.next = &binding;
    info.systemId = system_;
    XrResult r = xrCreateSession(instance_, &info, &session_);
    if (XR_FAILED(r)) {
        LOGE("xrCreateSession -> %s", result_name(instance_, r));
        session_ = XR_NULL_HANDLE;
        return false;
    }
    XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    space.poseInReferenceSpace.orientation.w = 1;
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    xrCreateReferenceSpace(session_, &space, &local_);
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    xrCreateReferenceSpace(session_, &space, &view_);

    uint32_t count = 0;
    xrEnumerateSwapchainFormats(session_, 0, &count, nullptr);
    std::vector<int64_t> formats(count);
    xrEnumerateSwapchainFormats(session_, count, &count, formats.data());
    color_format_ = formats.empty() ? GL_SRGB8_ALPHA8 : formats[0];
    for (int64_t f : formats)
        if (f == GL_SRGB8_ALPHA8) color_format_ = f;
    for (int eye = 0; eye < 2; ++eye) {
        Swap &s = swaps_[eye];
        XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sc.format = color_format_;
        sc.sampleCount = 1;
        sc.width = view_configs_[eye].recommendedImageRectWidth;
        sc.height = view_configs_[eye].recommendedImageRectHeight;
        sc.faceCount = 1;
        sc.arraySize = 1;
        sc.mipCount = 1;
        r = xrCreateSwapchain(session_, &sc, &s.handle);
        if (XR_FAILED(r)) {
            LOGE("xrCreateSwapchain -> %s", result_name(instance_, r));
            return false;
        }
        s.width = (int)sc.width;
        s.height = (int)sc.height;
        uint32_t n = 0;
        xrEnumerateSwapchainImages(s.handle, 0, &n, nullptr);
        s.images.assign(n, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
        xrEnumerateSwapchainImages(s.handle, n, &n, (XrSwapchainImageBaseHeader *)s.images.data());
        std::vector<GLuint> tex;
        for (const auto &img : s.images) tex.push_back(img.image);
        s.target.create(tex, s.width, s.height);
    }
    input_.attach(session_);
    {
        std::ifstream demo(std::string(android_->activity->externalDataPath ? android_->activity->externalDataPath
                                                                            : android_->activity->internalDataPath) +
                           "/demo");
        input_.demo = demo.good();
        if (input_.demo) LOGI("demo mode: parts light up in turn");
    }
    reported_[0].clear();
    reported_[1].clear();
    focused_since_ = 0;
    LOGI("session for %s (%s)", profile.path, profile.label);
    return true;
}

void App::close_session() {
    input_.destroy();
    for (auto &s : swaps_) {
        s.target.release();
        if (s.handle) xrDestroySwapchain(s.handle);
        s.handle = XR_NULL_HANDLE;
        s.images.clear();
    }
    if (local_) xrDestroySpace(local_);
    if (view_) xrDestroySpace(view_);
    local_ = view_ = XR_NULL_HANDLE;
    if (session_) xrDestroySession(session_);  // allowed in any session state
    session_ = XR_NULL_HANDLE;
    running_ = false;
    state_ = XR_SESSION_STATE_UNKNOWN;
}

void App::switch_to(int index) {
    int from = current_;
    LOGI("switch: %s -> %s", kProfiles[from].path, kProfiles[index].path);
    close_session();
    int minor = kProfiles[index].min_minor;
    if (minor != instance_minor_) {
        close_instance();
        if (!open_instance(minor)) {
            statuses_[index].note = "needs OpenXR 1.1 (this runtime has 1.0)";
            message_ = std::string(kProfiles[index].label) + ": the runtime refused an OpenXR 1.1 instance";
            index = from;
            if (!open_instance(kProfiles[index].min_minor)) {
                LOGE("cannot reopen the OpenXR instance");
                exit_ = true;
                return;
            }
        }
    }
    current_ = index;
    update_statuses();
    if (!open_session(current_)) {
        exit_ = true;
        return;
    }
    save_choice();
    pressed_prev_[0].clear();
    pressed_prev_[1].clear();
}

void App::update_statuses() {
    for (int i = 0; i < kProfileCount; ++i) {
        ProfileStatus &st = statuses_[i];
        const Profile &p = kProfiles[i];
        st.available = true;
        if (p.extension && instance_ && !available_.count(p.extension)) {
            st.available = false;
            st.note = std::string("runtime doesn't offer ") + p.extension;
        } else if (p.min_minor == 1 && v11_unsupported_) {
            st.available = false;
            st.note = "needs OpenXR 1.1 (this runtime has 1.0)";
        } else if (st.note.rfind("runtime doesn't", 0) == 0) {
            st.note.clear();
        }
    }
}

// ---------------------------------------------------------------- events
void App::handle_state(XrSessionState s) {
    state_ = s;
    LOGI("session state %d", s);
    switch (s) {
        case XR_SESSION_STATE_READY: {
            XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
            begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            XrResult r = xrBeginSession(session_, &begin);
            running_ = XR_SUCCEEDED(r);
            LOGI("xrBeginSession -> %s", result_name(instance_, r));
            break;
        }
        case XR_SESSION_STATE_STOPPING:
            xrEndSession(session_);
            running_ = false;
            break;
        case XR_SESSION_STATE_FOCUSED:
            focused_since_ = now_s();
            break;
        case XR_SESSION_STATE_EXITING:
        case XR_SESSION_STATE_LOSS_PENDING:
            exit_ = true;
            break;
        default:
            break;
    }
}

void App::update_reported() {
    if (!session_) return;
    for (int h = 0; h < 2; ++h) {
        XrInteractionProfileState st{XR_TYPE_INTERACTION_PROFILE_STATE};
        if (XR_FAILED(xrGetCurrentInteractionProfile(session_, input_.hand_paths[h], &st))) continue;
        std::string name;
        if (st.interactionProfile != XR_NULL_PATH) {
            char buf[XR_MAX_PATH_LENGTH];
            uint32_t n = 0;
            if (XR_SUCCEEDED(xrPathToString(instance_, st.interactionProfile, sizeof(buf), &n, buf))) name = buf;
        }
        if (name != reported_[h]) LOGI("runtime reports %s: %s", h == 0 ? "left" : "right", name.c_str());
        reported_[h] = name;
    }
    if (!reported_[0].empty() || !reported_[1].empty()) last_good_ = current_;
}

void App::poll_events() {
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (instance_ && xrPollEvent(instance_, &ev) == XR_SUCCESS) {
        switch (ev.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                auto *e = (XrEventDataSessionStateChanged *)&ev;
                if (e->session == session_) handle_state(e->state);
                break;
            }
            case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                update_reported();
                break;
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                exit_ = true;
                break;
            default:
                break;
        }
        ev = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

// ---------------------------------------------------------------- per frame
void App::place(XrTime time) {
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xrLocateSpace(view_, local_, time, &loc)) ||
        !(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
        return;
    Pose head(loc.pose);
    Vec3 f = head.q.rotate({0, 0, -1});
    float yaw = std::atan2(-f.x, -f.z);
    anchor_ = Pose(Quat::axis_angle({0, 1, 0}, yaw), head.p);
    placed_ = true;
}

// Pose of the enlarged copy of one hand's model: in front of the user, face turned towards them.
Pose App::display_pose(int hand) const {
    float tilt = std::string(kProfiles[current_].family) == "go" ? 0.0f : deg(28);
    float s = std::sin(tilt), c = std::cos(tilt), alpha = deg(30);
    Vec3 n_w(0, std::sin(alpha), std::cos(alpha)), m_w(0, -std::cos(alpha), std::sin(alpha));
    Vec3 y_w = n_w * s + m_w * c, z_w = n_w * (-c) + m_w * s;
    Quat q = Quat::from_basis({1, 0, 0}, y_w, z_w);
    Vec3 offset(hand == 0 ? -0.14f : 0.14f, -0.08f, -0.55f);
    return anchor_ * Pose(q, offset);
}

void App::update_ui(XrTime time) {
    double t = now_s();
    // panel: to the right of the models, turned towards the user
    Vec3 at(0.55f, 0.02f, -0.72f);
    panel_.pose = anchor_ * Pose(Quat::axis_angle({0, 1, 0}, std::atan2(-at.x, -at.z)), at);  // +Z to the user

    int hover = -1;
    for (int h = 0; h < 2; ++h) {
        bool trig = input_.trigger_down(h);
        float u, v;
        Vec3 w;
        if (input_.aim_valid[h] && panel_.hit(input_.aim[h], u, v, w)) {
            int item = panel_.item_at(u, v);
            if (item >= 0) hover = item;
            if (trig && !trig_prev_[h] && item >= 0) {
                const PanelItem &it = panel_.items()[item];
                if (it.kind == kItemProfile && it.index != current_ && statuses_[it.index].available) {
                    pending_ = it.index;
                    message_.clear();
                } else if (it.kind == kItemHaptics) {
                    haptics_ = !haptics_;
                } else if (it.kind == kItemRecenter) {
                    place(time);
                }
            }
        }
        trig_prev_[h] = trig;
        // hold the thumbstick (or touchpad) click for 2 s: next controller type, for when pointing doesn't work
        if (input_.stick_click(h)) {
            if (stick_held_since_[h] == 0) stick_held_since_[h] = t;
            if (t - stick_held_since_[h] > 2.0 && pending_ < 0) {
                for (int k = 1; k <= kProfileCount; ++k) {
                    int next = (current_ + k) % kProfileCount;
                    if (statuses_[next].available) {
                        pending_ = next;
                        break;
                    }
                }
                stick_held_since_[h] = t + 1e9;  // once per hold
            }
        } else {
            stick_held_since_[h] = 0;
        }
        // vibrate on each newly pressed part
        for (const auto &row : input_.rows) {
            if (row.hand != h || row.part.empty()) continue;
            bool p = input_.part(h, row.part).pressed;
            bool &prev = pressed_prev_[h][row.part];
            if (p && !prev && haptics_) {
                input_.haptic(session_, h, 0.5f, 0.05f);
                haptic_flash_[h] = t;
            }
            prev = p;
        }
    }
    hover_ = hover;

    if (t - last_report_ > 1.0) {
        update_reported();
        last_report_ = t;
    }
    // no input at all for 10 s while focused: the runtime doesn't drive this profile; go back
    if (state_ == XR_SESSION_STATE_FOCUSED && current_ != last_good_ && reported_[0].empty() &&
        reported_[1].empty() && focused_since_ > 0 && t - focused_since_ > 10 && pending_ < 0) {
        message_ = std::string("No controller input with ") + kProfiles[current_].label + " for 10 s: back to " +
                   kProfiles[last_good_].label;
        statuses_[current_].note = "no input from the runtime";
        pending_ = last_good_;
    }
}

void App::draw_model(const ControllerModel &model, int hand, const Pose &base, float scale, bool labels,
                     std::vector<FlatVertex> &tris) {
    bool flash = now_s() - haptic_flash_[hand] < 0.12;
    Mat4 base_m = Mat4::from_pose(base, scale);
    for (const ModelNode &node : model.nodes) {
        Vec3 color = kPartIdle;
        float emissive = 0;
        Pose delta;
        const std::string &n = node.name;
        bool bound = false;
        for (const auto &row : input_.rows)
            if (row.hand == hand && row.part == n && row.action) bound = true;
        if (n == "body" || n == "camera") {
            color = flash ? kBlue * 0.35f : kBody;
            emissive = flash ? 0.6f : 0;
        } else if (n == "ring") {
            color = kRing;
        } else {
            PartState st = input_.part(hand, n);
            if (!bound) color = kPartUnbound;
            if (st.near) color = kNear, emissive = 0.3f;
            if (st.touched) color = kBlue, emissive = 0.5f;
            if (st.value > 0.02f) {
                color = color * (1 - st.value) + kOrange * st.value;
                emissive = std::max(emissive, st.value * 0.7f);
            }
            if (st.pressed) color = kOrange, emissive = 0.85f;
            if (n == "trigger") {
                delta.q = Quat::axis_angle({1, 0, 0}, -st.value * deg(25));
            } else if (n == "squeeze") {
                delta.p = Vec3(0, -st.value * 0.003f, 0);
            } else if (n == "thumbstick") {
                delta.q = Quat::axis_angle({0, 0, 1}, -st.x * deg(20)) * Quat::axis_angle({1, 0, 0}, -st.y * deg(20));
                if (st.pressed) delta.p = Vec3(0, -0.0015f, 0);
            } else if (n == "trackpad") {
                if (st.touched || st.pressed || st.x != 0 || st.y != 0) {
                    Pose dot = node.rest * Pose(Quat(), Vec3(st.x * 0.012f, 0.0028f, -st.y * 0.012f));
                    Mat4 m = base_m * Mat4::from_pose(dot, 0.0028f);
                    renderer_.draw_mesh(renderer_.sphere, m, st.pressed ? kOrange : kBlue, 0.9f);
                }
            } else if (st.pressed) {
                delta.p = Vec3(0, -0.0022f, 0);  // buttons go in
            }
            if (labels && st.any() && *part_label(n)) {
                // label in front of the part, facing the user
                Vec3 at = base.apply(node.rest.p * scale) + anchor_.q.rotate({0, 0.012f, 0.03f});
                const float c[4] = {1.0f, 0.55f, 0.15f, 1};
                world_text(tris, Pose(anchor_.q, at), 0.0011f, part_label(n), c);
            }
        }
        renderer_.draw_mesh(node.mesh, base_m * Mat4::from_pose(node.rest * delta), color, emissive);
    }
}

void App::render_eye(const XrView &view, int eye, uint32_t image) {
    Swap &s = swaps_[eye];
    glBindFramebuffer(GL_FRAMEBUFFER, s.target.fbos[image]);
    glViewport(0, 0, s.width, s.height);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.008f, 0.009f, 0.014f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    Pose eye_pose(view.pose);
    Mat4 view_m = Mat4::from_pose(eye_pose.inverse());
    renderer_.begin_eye(Mat4::projection(view.fov, 0.05f, 50.0f) * view_m);

    std::vector<FlatVertex> tris, lines;
    const char *family = kProfiles[current_].family;
    for (int h = 0; h < 2; ++h) {
        const ControllerModel *model = models_.get(family, h);
        if (!model) continue;
        if (input_.grip_valid[h]) draw_model(*model, h, input_.grip[h], 1.0f, false, tris);
        draw_model(*model, h, display_pose(h), 2.4f, true, tris);
        if (input_.aim_valid[h]) {
            const Pose &aim = input_.aim[h];
            float len = 1.5f, u, v;
            Vec3 hit;
            if (panel_.hit(aim, u, v, hit)) {
                len = length(hit - aim.p);
                renderer_.draw_mesh(renderer_.sphere, Mat4::from_pose(Pose(Quat(), hit), 0.006f),
                                    {1.0f, 0.5f, 0.1f}, 1.0f);
            }
            Vec3 a = aim.p, b = aim.apply({0, 0, -len});
            float r = h == 0 ? 0.3f : 1.0f, g = h == 0 ? 0.6f : 0.5f, bl = h == 0 ? 1.0f : 0.1f;
            lines.push_back({a.x, a.y, a.z, r, g, bl, 0.9f});
            lines.push_back({b.x, b.y, b.z, r, g, bl, 0.15f});
        }
    }
    // the panel last: it's partly transparent
    PanelInfo info;
    info.runtime = runtime_;
    info.system = system_name_;
    info.api = api_;
    info.current = current_;
    info.profiles = statuses_;
    info.suggest_result = suggest_result_;
    info.reported[0] = reported_[0];
    info.reported[1] = reported_[1];
    info.message = message_;
    info.haptics = haptics_;
    info.input = &input_;
    info.hover_item = hover_;
    renderer_.draw_lines(lines);
    panel_.build(info, tris);
    renderer_.draw_flat(tris);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void App::frame() {
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState fs{XR_TYPE_FRAME_STATE};
    if (XR_FAILED(xrWaitFrame(session_, &wait, &fs))) return;
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    if (XR_FAILED(xrBeginFrame(session_, &begin))) return;

    std::vector<XrCompositionLayerProjectionView> proj(2, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    const XrCompositionLayerBaseHeader *layers[] = {(const XrCompositionLayerBaseHeader *)&layer};
    bool rendered = false;

    if (fs.shouldRender) {
        XrViewLocateInfo vli{XR_TYPE_VIEW_LOCATE_INFO};
        vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        vli.displayTime = fs.predictedDisplayTime;
        vli.space = local_;
        XrViewState vs{XR_TYPE_VIEW_STATE};
        XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
        uint32_t n = 0;
        if (XR_SUCCEEDED(xrLocateViews(session_, &vli, &vs, 2, &n, views)) && n == 2) {
            input_.sync(session_, local_, fs.predictedDisplayTime);
            if (!placed_) place(fs.predictedDisplayTime);
            update_ui(fs.predictedDisplayTime);
            for (int eye = 0; eye < 2; ++eye) {
                Swap &s = swaps_[eye];
                uint32_t image = 0;
                XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                xrAcquireSwapchainImage(s.handle, &ai, &image);
                XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                wi.timeout = XR_INFINITE_DURATION;
                xrWaitSwapchainImage(s.handle, &wi);
                render_eye(views[eye], eye, image);
                XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                xrReleaseSwapchainImage(s.handle, &ri);
                proj[eye].pose = views[eye].pose;
                proj[eye].fov = views[eye].fov;
                proj[eye].subImage.swapchain = s.handle;
                proj[eye].subImage.imageRect = {{0, 0}, {s.width, s.height}};
            }
            layer.space = local_;
            layer.viewCount = 2;
            layer.views = proj.data();
            rendered = true;
        }
    }
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = fs.predictedDisplayTime;
    end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount = rendered ? 1 : 0;
    end.layers = rendered ? layers : nullptr;
    XrResult r = xrEndFrame(session_, &end);
    if (XR_FAILED(r)) {
        static int logged = 0;
        if (logged++ < 5) LOGE("xrEndFrame -> %s", result_name(instance_, r));
    }
}

// ---------------------------------------------------------------- main loop
void App::run() {
    JNIEnv *env = nullptr;
    android_->activity->vm->AttachCurrentThread(&env, nullptr);
    models_.set_assets(android_->activity->assetManager);
    if (!gl_.init() || !renderer_.init() || !init_loader()) {
        LOGE("startup failed");
        ANativeActivity_finish(android_->activity);
    } else {
        load_choice();
        int minor = kProfiles[current_].min_minor;
        if (!open_instance(minor) && minor == 1) {
            current_ = 0;
            open_instance(0);
        }
        update_statuses();
        if (!instance_ || !open_session(current_)) exit_ = true;
    }

    bool finishing = false;
    while (!android_->destroyRequested) {
        int events = 0;
        android_poll_source *source = nullptr;
        while (ALooper_pollOnce(running_ && !exit_ ? 0 : 100, nullptr, &events, (void **)&source) >= 0) {
            if (source) source->process(android_, source);
            if (android_->destroyRequested) break;
        }
        if (android_->destroyRequested) break;
        if (exit_) {
            if (!finishing) {
                LOGI("exiting");
                ANativeActivity_finish(android_->activity);
                finishing = true;
            }
            continue;
        }
        poll_events();
        if (pending_ >= 0) {
            int next = pending_;
            pending_ = -1;
            switch_to(next);
            continue;
        }
        if (running_) frame();
    }
    close_session();
    close_instance();
    gl_.shutdown();
    android_->activity->vm->DetachCurrentThread();
}

}  // namespace

void android_main(android_app *app) {
    App(app).run();
}
