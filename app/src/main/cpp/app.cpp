// Test Quest Controllers: pick a Quest controller type, see what the runtime delivers for it.
//
// The app suggests bindings for exactly one interaction profile (the chosen controller type), like a game written
// for that controller, and shows every input it receives on a 3D model of that controller: in your hands at the
// grip pose and as an enlarged pair in front of you. Changing the type destroys and recreates the session (bindings
// can't change once action sets are attached); the instance is recreated only when the OpenXR version changes
// (1.1 core profiles need an OpenXR 1.1 instance; 1.0 profiles get a 1.0 instance, as most games use).
#include <android/asset_manager.h>
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
#include <cstdlib>
#include <map>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "gfx.h"
#include "input.h"
#include "log.h"
#include "profiles.h"
#include "scene.h"

namespace {

double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

const char *kOptionalExtensions[] = {
    "XR_FB_touch_controller_pro",
    "XR_META_touch_controller_plus",
    "XR_FB_touch_controller_proximity",
    "XR_FB_passthrough",
};

// Haptic feedback while testing: a tick when a finger touches a part, a pulse when it's pressed, and while the
// trigger or grip is held a rumble that follows how far it's pulled.
struct HapticTrack {
    bool touched = false, pressed = false;
    double last_rumble = 0;
};

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
    Scene scene_;

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
    std::map<std::string, HapticTrack> haptic_track_[2];

    // passthrough: XR_FB_passthrough (Quest; what Quest games use), else the ALPHA_BLEND blend mode
    bool passthrough_wanted_ = false;
    std::string passthrough_mode_;  // "" off, "XR_FB_passthrough" or "ALPHA_BLEND"
    std::string passthrough_note_;
    std::vector<XrEnvironmentBlendMode> blend_modes_;
    XrPassthroughFB pt_ = XR_NULL_HANDLE;
    XrPassthroughLayerFB pt_layer_ = XR_NULL_HANDLE;
    void set_passthrough(bool on);
    void stop_passthrough();
    void update_haptics(int hand, double t);
    bool placed_ = false;
    Pose anchor_;

    bool cycle_ = false;
    double last_cycle_ = 0;
    bool steamvr_ = false;         // SteamVR's runtime (vrclient.so) is in the process: switching restarts the app
    double close_at_ = 0;          // > 0: close the app then (after a switch on SteamVR)
    int switching_to_ = -1;        // controller type to switch to once the current session has ended
    double switch_started_ = 0;
    bool pending_finish_ = false;  // the session ended: make the new one (outside the event loop)
    void finish_switch();
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
    XrSession session() const { return session_; }
};

// ---------------------------------------------------------------- persistence
void App::load_choice() {
    std::ifstream f(std::string(android_->activity->internalDataPath) + "/controller_type.txt");
    std::string p, pt;
    if (f >> p)
        for (int i = 0; i < kProfileCount; ++i)
            if (p == kProfiles[i].path) current_ = i;
    if (f >> pt) passthrough_wanted_ = pt == "passthrough";
}

void App::save_choice() {
    std::ofstream f(std::string(android_->activity->internalDataPath) + "/controller_type.txt");
    f << kProfiles[current_].path << "\n" << (passthrough_wanted_ ? "passthrough" : "opaque") << "\n";
}

// ---------------------------------------------------------------- passthrough
void App::stop_passthrough() {
    passthrough_mode_.clear();
    if (!instance_ || (!pt_ && !pt_layer_)) return;
    auto destroy_layer = (PFN_xrDestroyPassthroughLayerFB)nullptr;
    auto destroy = (PFN_xrDestroyPassthroughFB)nullptr;
    xrGetInstanceProcAddr(instance_, "xrDestroyPassthroughLayerFB", (PFN_xrVoidFunction *)&destroy_layer);
    xrGetInstanceProcAddr(instance_, "xrDestroyPassthroughFB", (PFN_xrVoidFunction *)&destroy);
    if (pt_layer_ && destroy_layer) destroy_layer(pt_layer_);
    if (pt_ && destroy) destroy(pt_);
    pt_layer_ = XR_NULL_HANDLE;
    pt_ = XR_NULL_HANDLE;
    passthrough_mode_.clear();
}

void App::set_passthrough(bool on) {
    stop_passthrough();
    passthrough_wanted_ = on;
    if (!on || !session_) return;
    if (enabled_.count("XR_FB_passthrough")) {
        PFN_xrCreatePassthroughFB create = nullptr;
        PFN_xrCreatePassthroughLayerFB create_layer = nullptr;
        xrGetInstanceProcAddr(instance_, "xrCreatePassthroughFB", (PFN_xrVoidFunction *)&create);
        xrGetInstanceProcAddr(instance_, "xrCreatePassthroughLayerFB", (PFN_xrVoidFunction *)&create_layer);
        XrResult r = XR_ERROR_FUNCTION_UNSUPPORTED;
        if (create && create_layer) {
            XrPassthroughCreateInfoFB info{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
            info.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
            r = create(session_, &info, &pt_);
            if (XR_SUCCEEDED(r)) {
                XrPassthroughLayerCreateInfoFB layer{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
                layer.passthrough = pt_;
                layer.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
                layer.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
                r = create_layer(session_, &layer, &pt_layer_);
            }
        }
        LOGI("passthrough via XR_FB_passthrough -> %s", result_name(instance_, r));
        if (XR_SUCCEEDED(r)) {
            passthrough_mode_ = "XR_FB_passthrough";
            passthrough_note_ = "XR_FB_passthrough";
            return;
        }
        stop_passthrough();
        passthrough_note_ = std::string("XR_FB_passthrough failed: ") + result_name(instance_, r);
    }
    for (XrEnvironmentBlendMode m : blend_modes_)
        if (m == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND) {
            passthrough_mode_ = "ALPHA_BLEND";
            passthrough_note_ = "blend mode ALPHA_BLEND";
            LOGI("passthrough via the ALPHA_BLEND blend mode");
            return;
        }
    if (passthrough_note_.empty()) passthrough_note_ = "not offered by this runtime";
    LOGI("passthrough: %s", passthrough_note_.c_str());
}

// ---------------------------------------------------------------- instance / session
bool App::init_loader() {
    PFN_xrInitializeLoaderKHR init = nullptr;
    if (XR_FAILED(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *)&init)) ||
        !init) {
        LOGW("no xrInitializeLoaderKHR (a loader replaced by another one?): going on without it");
        return true;
    }
    XrLoaderInitInfoAndroidKHR info{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    info.applicationVM = android_->activity->vm;
    info.applicationContext = android_->activity->clazz;
    XrResult r = init((const XrLoaderInitInfoBaseHeaderKHR *)&info);
    LOGI("xrInitializeLoaderKHR -> %d", r);
    return true;  // xrCreateInstance reports a loader that really doesn't work
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
    {
        // SteamVR's runtime library in the process (whatever name the runtime or a layer in between reports)
        std::ifstream maps("/proc/self/maps");
        std::string line;
        while (!steamvr_ && std::getline(maps, line)) steamvr_ = line.find("vrclient.so") != std::string::npos;
        if (steamvr_) LOGI("SteamVR runtime: switching controller types restarts the app");
    }
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
        uint32_t n = 0;
        xrEnumerateEnvironmentBlendModes(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &n, nullptr);
        blend_modes_.assign(n, XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
        xrEnumerateEnvironmentBlendModes(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, n, &n,
                                         blend_modes_.data());
        passthrough_note_.clear();
        set_passthrough(passthrough_wanted_);
    }
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
    bool wanted = passthrough_wanted_;
    stop_passthrough();
    passthrough_wanted_ = wanted;
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
    // Only the session is recreated: the instance lives for the whole run. Destroying it unloads the runtime, and
    // layers between the app and the runtime (e.g. FramePort's) keep pointers into the old one: the next OpenXR
    // call crashed. The 1.1 types need the 1.1 instance the app asks for at start (greyed out under 1.0).
    if (kProfiles[index].min_minor > instance_minor_) {
        message_ = std::string(kProfiles[index].label) + " needs OpenXR 1.1; this run has a 1.0 instance";
        return;
    }
    (void)from;
    // SteamVR (the Steam Frame) can't run more than about two sessions in one process: its in-process renderer binds
    // its shared GPU resources to one Vulkan device (a new GL context -> assert "m_pDevice == m_pExportDevice",
    // abort; the same context -> vkQueueSubmit -13 and no frames, a frozen picture). There the choice is saved and
    // the app closes; it starts with that type next time.
    if (steamvr_) {
        current_ = index;
        save_choice();
        message_ = std::string(kProfiles[index].label) + " is chosen. Start the app again to use it (closing...)";
        LOGI("switch on SteamVR: saved %s, closing the app", kProfiles[index].path);
        close_at_ = now_s() + 2.5;
        return;
    }
    // Elsewhere a running session is ended the way OpenXR expects (request exit -> STOPPING -> xrEndSession ->
    // IDLE/EXITING) before it's destroyed; finish_switch() makes the new session once it ended.
    switching_to_ = index;
    if (session_ && running_) {
        switch_started_ = now_s();
        XrResult r = xrRequestExitSession(session_);
        LOGI("switch: ending the session first -> %s", result_name(instance_, r));
        if (XR_SUCCEEDED(r)) return;
    }
    finish_switch();
}

void App::finish_switch() {
    int index = switching_to_;
    switching_to_ = -1;
    pending_finish_ = false;
    if (index < 0) return;
    close_session();
    current_ = index;
    update_statuses();
    if (!open_session(current_)) {
        exit_ = true;
        return;
    }
    save_choice();
    haptic_track_[0].clear();
    haptic_track_[1].clear();
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
        case XR_SESSION_STATE_IDLE:
            if (switching_to_ >= 0 && !running_) pending_finish_ = true;  // ended for a switch: make the new one
            break;
        case XR_SESSION_STATE_EXITING:
            if (switching_to_ >= 0) {  // the exit asked for to switch controller types, not the user's
                pending_finish_ = true;
                break;
            }
            exit_ = true;
            break;
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

void App::update_haptics(int h, double t) {
    std::set<std::string> parts;
    for (const auto &row : input_.rows)
        if (row.hand == h && !row.part.empty() && row.action) parts.insert(row.part);
    for (const std::string &part : parts) {
        PartState st = input_.part(h, part);
        HapticTrack &tr = haptic_track_[h][part];
        std::string out = input_.haptic_output_for(part);
        bool sent = false;
        if (haptics_) {
            // a press is a strong, clear kick; a touch only a faint tick
            if (st.pressed && !tr.pressed) sent = XR_SUCCEEDED(input_.haptic(session_, h, 1.0f, 0.08f, out));
            else if ((st.touched || st.near) && !tr.touched && !st.pressed)
                sent = XR_SUCCEEDED(input_.haptic(session_, h, 0.05f, 0.01f, out));
            bool analog = part == "trigger" || part == "squeeze";
            if (analog && st.value > 0.05f && t - tr.last_rumble > 0.08) {
                // refreshed every 80 ms for 100 ms: a continuous rumble as strong as the pull
                sent = XR_SUCCEEDED(input_.haptic(session_, h, 0.05f + 0.9f * st.value, 0.1f, out)) || sent;
                tr.last_rumble = t;
            }
        }
        if (sent) haptic_flash_[h] = t;
        tr.pressed = st.pressed;
        tr.touched = st.touched || st.near;
    }
}

void App::update_ui(XrTime time) {
    double t = now_s();
    scene_.layout(anchor_);

    int hover = -1;
    for (int h = 0; h < 2; ++h) {
        bool trig = input_.trigger_down(h);
        float u, v;
        Vec3 w;
        if (input_.aim_valid[h] && scene_.panel.hit(input_.aim[h], u, v, w)) {
            int item = scene_.panel.item_at(u, v);
            if (item >= 0) hover = item;
            if (trig && !trig_prev_[h] && item >= 0) {
                const PanelItem &it = scene_.panel.items()[item];
                if (it.kind == kItemProfile && it.index != current_ && statuses_[it.index].available) {
                    pending_ = it.index;
                    message_.clear();
                } else if (it.kind == kItemHaptics) {
                    haptics_ = !haptics_;
                } else if (it.kind == kItemPassthrough) {
                    set_passthrough(!passthrough_wanted_);
                    save_choice();
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
        update_haptics(h, t);
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

void App::render_eye(const XrView &view, int eye, uint32_t image) {
    Swap &s = swaps_[eye];
    glBindFramebuffer(GL_FRAMEBUFFER, s.target.fbos[image]);
    glViewport(0, 0, s.width, s.height);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (passthrough_mode_.empty()) glClearColor(kClearColor[0], kClearColor[1], kClearColor[2], 1);
    else glClearColor(0, 0, 0, 0);  // the real world shows through
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    SceneData d;
    d.current = current_;
    d.input = &input_;
    d.anchor = anchor_;
    for (int h = 0; h < 2; ++h) d.flash[h] = now_s() - haptic_flash_[h] < 0.12;
    d.panel.runtime = runtime_;
    d.panel.system = system_name_;
    d.panel.api = api_;
    d.panel.profiles = statuses_;
    d.panel.suggest_result = suggest_result_;
    d.panel.reported[0] = reported_[0];
    d.panel.reported[1] = reported_[1];
    d.panel.message = message_;
    d.panel.haptics = haptics_;
    d.panel.passthrough = !passthrough_mode_.empty();
    d.panel.passthrough_note = passthrough_note_;
    for (const auto &hr : input_.haptic_results)
        d.panel.haptic_result += (d.panel.haptic_result.empty() ? "" : ", ") + hr.first.substr(8) + " " + hr.second;
    d.panel.hover_item = hover_;
    Mat4 view_m = Mat4::from_pose(Pose(view.pose).inverse());
    scene_.draw(Mat4::projection(view.fov, 0.05f, 50.0f) * view_m, d);
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
    XrCompositionLayerPassthroughFB pt_layer{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
    pt_layer.layerHandle = pt_layer_;
    const XrCompositionLayerBaseHeader *layers[2];
    uint32_t layer_count = 0;
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
            if (!passthrough_mode_.empty())
                layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                                   XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
            layer.viewCount = 2;
            layer.views = proj.data();
            rendered = true;
        }
    }
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = fs.predictedDisplayTime;
    end.environmentBlendMode =
        passthrough_mode_ == "ALPHA_BLEND" ? XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND : XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    if (pt_layer_) layers[layer_count++] = (const XrCompositionLayerBaseHeader *)&pt_layer;  // under the scene
    if (rendered) layers[layer_count++] = (const XrCompositionLayerBaseHeader *)&layer;
    end.layerCount = layer_count;
    end.layers = layer_count ? layers : nullptr;
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
    AAssetManager *assets = android_->activity->assetManager;
    scene_.models.set_reader([assets](const std::string &path, std::vector<uint8_t> &out) {
        AAsset *asset = AAssetManager_open(assets, path.c_str(), AASSET_MODE_BUFFER);
        if (!asset) return false;
        out.resize(AAsset_getLength(asset));
        AAsset_read(asset, out.data(), out.size());
        AAsset_close(asset);
        return true;
    });
    if (!gl_.init() || !scene_.init() || !init_loader()) {
        LOGE("startup failed");
        ANativeActivity_finish(android_->activity);
    } else {
        load_choice();
        // one instance for the whole run (see switch_to): OpenXR 1.1 if the runtime takes it, so every type can be
        // picked (1.0 profiles work on a 1.1 instance too), else 1.0
        if (!open_instance(1) && !open_instance(0)) exit_ = true;
        if (kProfiles[current_].min_minor > instance_minor_) current_ = 0;
        {
            const char *dir = android_->activity->externalDataPath ? android_->activity->externalDataPath
                                                                    : android_->activity->internalDataPath;
            cycle_ = std::ifstream(std::string(dir) + "/cycle").good();
            if (cycle_) LOGI("cycle mode: the next controller type every 8 s");
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
        if (close_at_ > 0 && now_s() >= close_at_) {  // a switch on SteamVR: the message was shown, close
            exit_ = true;
            continue;
        }
        if (switching_to_ >= 0 && (pending_finish_ || now_s() - switch_started_ > 3)) {
            if (!pending_finish_) LOGW("switch: the session didn't end within 3 s; replacing it anyway");
            finish_switch();
            continue;
        }
        // debug: a file named "cycle" in the app's files folder switches the controller type every 8 s (to test
        // switching on a headset nobody wears)
        if (cycle_ && running_ && pending_ < 0 && switching_to_ < 0 && now_s() - last_cycle_ > 8) {
            last_cycle_ = now_s();
            for (int k = 1; k <= kProfileCount; ++k) {
                int next = (current_ + k) % kProfileCount;
                if (statuses_[next].available) {
                    pending_ = next;
                    break;
                }
            }
        }
        if (pending_ >= 0 && close_at_ > 0) pending_ = -1;  // closing for a switch already
        if (pending_ >= 0 && switching_to_ < 0) {
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
    // Finishing a NativeActivity leaves its process running, and a container (e.g. the Steam Frame's Lepton) only
    // ends the game once the process is gone: without this, closing after a switch left a running app with no
    // picture. OpenXR and GL are shut down already.
    std::exit(0);
}
