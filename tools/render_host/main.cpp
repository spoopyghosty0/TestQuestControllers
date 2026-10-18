// Renders the app's scene on a PC (Mesa, surfaceless EGL) to check layout and highlighting without a headset.
//
//   render_host <assets dir> <out.ppm> [profile index] [demo seconds] [--hands | --icon | --panel]
//
// Uses the app's own scene, model and panel code; OpenXR calls are stubbed (xr_stubs.cpp) and demo mode lights the
// parts. --hands also places both controllers in front of the user, the right one pointing at the panel. The panel is
// drawn into its own image and composited over the scene the way a runtime shows the app's quad layer; --panel
// (implies --hands) writes that image instead, texel for texel.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "scene.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <assets dir> <out.ppm> [profile] [seconds] [--hands]\n", argv[0]);
        return 2;
    }
    std::string assets = argv[1], out = argv[2];
    int profile = argc > 3 ? std::atoi(argv[3]) : 0;
    double clock = argc > 4 ? std::atof(argv[4]) : 0.2;
    bool panel_only = argc > 5 && std::strcmp(argv[5], "--panel") == 0;
    bool hands = argc > 5 && (std::strcmp(argv[5], "--hands") == 0 || panel_only);
    bool icon = argc > 5 && std::strcmp(argv[5], "--icon") == 0;  // close-up of the left model, for the app icon
    if (profile < 0 || profile >= kProfileCount) profile = 0;

    auto get_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay display = get_display ? get_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                                     : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(display, nullptr, nullptr)) {
        std::fprintf(stderr, "eglInitialize failed\n");
        return 1;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint cfg_attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_NONE};
    EGLConfig config;
    EGLint n = 0;
    eglChooseConfig(display, cfg_attribs, &config, 1, &n);
    const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(display, n ? config : EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctx_attribs);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        std::fprintf(stderr, "no GLES 3 context (0x%x)\n", eglGetError());
        return 1;
    }

    const int w = icon ? 1024 : 1280, h = icon ? 1024 : 960;
    GLuint tex, depth, fbo;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_SRGB8_ALPHA8, w, h);  // like the headset's swapchain
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);

    Scene scene;
    scene.init();
    scene.models.set_reader([&](const std::string &path, std::vector<uint8_t> &bytes) {
        std::ifstream f(assets + "/" + path, std::ios::binary);
        if (!f) return false;
        bytes.assign(std::istreambuf_iterator<char>(f), {});
        return true;
    });

    Input input;
    std::set<std::string> ext = {"XR_FB_touch_controller_pro", "XR_META_touch_controller_plus",
                                 "XR_FB_touch_controller_proximity"};
    input.create((XrInstance)1, kProfiles[profile], ext);
    input.demo = true;
    input.demo_clock = clock;

    SceneData d;
    d.current = profile;
    d.input = &input;
    d.anchor = Pose(Quat(), Vec3(0, 0, 0));
    scene.layout(d.anchor);
    if (hands) {
        // grip poses: handles tilted forward, about 35 cm below and in front of the eyes
        Quat tilt = Quat::axis_angle({1, 0, 0}, deg(-55));
        input.grip_valid[0] = input.grip_valid[1] = true;
        input.grip[0] = Pose(tilt, Vec3(-0.17f, -0.24f, -0.40f));
        input.grip[1] = Pose(tilt, Vec3(0.15f, -0.22f, -0.42f));
        input.aim_valid[1] = true;
        Vec3 from = input.grip[1].p + Vec3(0, 0.03f, -0.05f), to = scene.panel.pose.apply({-0.12f, -0.13f, 0});
        Vec3 fwd = normalize(to - from), right = normalize(cross(fwd, {0, 1, 0})), up = cross(right, fwd);
        input.aim[1] = Pose(Quat::from_basis(right, up, -fwd), from);
    }
    d.panel.runtime = "host render (Mesa)";
    d.panel.system = "demo";
    d.panel.api = "OpenXR 1.0 instance";
    d.panel.profiles.resize(kProfileCount);
    d.panel.profiles[6].note = "needs OpenXR 1.1 (this runtime has 1.0)";
    d.panel.profiles[6].available = false;
    d.panel.suggest_result = "XR_SUCCESS";
    d.panel.reported[0] = d.panel.reported[1] = kProfiles[profile].path;
    d.panel.hover_item = hands ? 2 : -1;

    glViewport(0, 0, w, h);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(kClearColor[0], kClearColor[1], kClearColor[2], 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    XrFovf fov{deg(-48), deg(48), deg(38), deg(-38)};
    Mat4 view = Mat4::identity();
    if (icon) {
        d.labels = false;
        d.only_hand = 0;
        fov = {deg(-20), deg(20), deg(20), deg(-20)};  // the whole controller (gen_icon.py crops to it)
        Vec3 target = scene.display_pose(d, 0).p + Vec3(0, -0.02f, 0);
        Vec3 fwd = normalize(target), right = normalize(cross(fwd, {0, 1, 0})), up = cross(right, fwd);
        view = Mat4::from_pose(Pose(Quat::from_basis(right, up, -fwd), Vec3()).inverse());
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    Mat4 view_proj = Mat4::projection(fov, 0.05f, 50.0f) * view;
    scene.update_cursors(d);
    scene.draw(view_proj, d);
    if (!icon) {
        // the panel's quad layer: its image as the app makes it for this eye, then composited (mipmapped)
        RenderTexture image;
        int levels = 1;
        while ((scene.panel.image_width() >> levels) > 0 && levels < 8) ++levels;
        image.create(scene.panel.image_width(), scene.panel.image_height(), levels);
        glBindFramebuffer(GL_FRAMEBUFFER, image.fbo);
        Vec3 eye(0, 0, 0);  // the camera (view = identity)
        scene.draw_panel(d, 0, &eye);
        glBindTexture(GL_TEXTURE_2D, image.texture);
        glGenerateMipmap(GL_TEXTURE_2D);
        if (panel_only) {
            int pw = image.width, ph = image.height;
            std::vector<unsigned char> px(pw * ph * 4);
            glReadPixels(0, 0, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            std::ofstream f(out, std::ios::binary);
            f << "P6\n" << pw << " " << ph << "\n255\n";
            // premultiplied: shown over the scene's background colour (sRGB-encoded)
            const unsigned char bg[3] = {60, 63, 72};
            for (int y = ph - 1; y >= 0; --y)
                for (int x = 0; x < pw; ++x) {
                    const unsigned char *p = &px[(y * pw + x) * 4];
                    for (int c = 0; c < 3; ++c) f.put((char)std::min(255, p[c] + bg[c] * (255 - p[3]) / 255));
                }
            std::printf("wrote %s (the panel's image, %dx%d)\n", out.c_str(), pw, ph);
            return 0;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, w, h);
        scene.composite_panel(view_proj, image.texture);
    }

    std::vector<unsigned char> px(w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    std::ofstream f(out, std::ios::binary);
    f << "P6\n" << w << " " << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y)
        for (int x = 0; x < w; ++x) f.write((const char *)&px[(y * w + x) * 4], 3);
    std::printf("wrote %s\n", out.c_str());
    return 0;
}
