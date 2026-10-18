#include "scene.h"

#include <algorithm>
#include <cmath>

#include "log.h"

namespace {
// touched = blue, pressed = orange, pulled (trigger/grip value) = yellow that deepens with the pull; the panel's
// legend (ui.cpp) uses the same colours
const Vec3 kOrange(1.0f, 0.3f, 0.02f), kBlue(0.06f, 0.36f, 1.0f), kYellow(1.0f, 0.78f, 0.05f),
    kNear(0.03f, 0.08f, 0.25f);

// layout, in the anchor's frame (metres; the user looks along -Z)
const Vec3 kModelLeft(-0.34f, -0.07f, -0.66f), kModelRight(-0.08f, -0.07f, -0.66f);
const float kModelScale = 1.6f;
const Vec3 kPanelAt(0.36f, 0.0f, -0.85f);
}  // namespace

void Scene::layout(const Pose &anchor) {
    panel.px = 0.00125f;  // 560 x 680 px = 0.70 x 0.85 m
    // turn the panel's +Z towards the user
    panel.pose = anchor * Pose(Quat::axis_angle({0, 1, 0}, std::atan2(-kPanelAt.x, -kPanelAt.z)), kPanelAt);
}

Pose Scene::display_pose(const SceneData &d, int hand) const {
    // the face's normal in the grip frame is (0, sin t, -cos t) for a face tilted by t; turn it to point at the
    // user and slightly up (alpha), keeping the controller's +X to the right
    float tilt = std::string(kProfiles[d.current].family) == "oculus-go" ? 0.0f : deg(28);
    float s = std::sin(tilt), c = std::cos(tilt), alpha = deg(20);
    Vec3 n_w(0, std::sin(alpha), std::cos(alpha)), m_w(0, -std::cos(alpha), std::sin(alpha));
    Vec3 y_w = n_w * s + m_w * c, z_w = n_w * (-c) + m_w * s;
    Quat q = Quat::from_basis({1, 0, 0}, y_w, z_w);
    Vec3 at = hand == 0 ? kModelLeft : kModelRight;
    Quat turn = Quat::axis_angle({0, 1, 0}, std::atan2(-at.x, -at.z));  // face the user, not straight ahead
    return d.anchor * Pose(turn * q, at);
}

namespace {
struct NodeState {
    Quat r;
    Vec3 t, s;
    bool visible = true;
};

void walk(const ControllerModel &m, const std::vector<NodeState> &st, int n, const Mat4 &parent,
          std::vector<Mat4> &world, std::vector<bool> &visible, bool parent_visible) {
    world[n] = parent * Mat4::from_trs(st[n].r, st[n].t, st[n].s);
    visible[n] = parent_visible && st[n].visible;
    for (int c : m.nodes[n].children) walk(m, st, c, world[n], world, visible, visible[n]);
}
}  // namespace

void Scene::draw_model(const ControllerModel &model, int hand, const Pose &base, float scale, bool labels,
                       const SceneData &d, std::vector<TexVertex> &tris) {
    const Input &input = *d.input;
    // each component's input state (WebXR terms: state default/touched/pressed, button value, axes)
    struct CompState {
        PartState part;
        unsigned state = 1;  // bit of ModelResponse::states
        float button = 0;
        bool bound = false;
    };
    std::vector<CompState> cs(model.components.size());
    for (size_t i = 0; i < model.components.size(); ++i) {
        CompState &c = cs[i];
        c.part = input.part(hand, model.components[i].part);
        c.state = c.part.pressed ? 4 : (c.part.touched ? 2 : 1);
        c.button = c.part.pressed ? std::max(c.part.value, 1.0f) : c.part.value;
        if (model.components[i].part == "trigger" || model.components[i].part == "squeeze") c.button = c.part.value;
        for (const auto &row : input.rows)
            if (row.hand == hand && row.part == model.components[i].part && row.action) c.bound = true;
    }

    std::vector<NodeState> st(model.nodes.size());
    for (size_t n = 0; n < model.nodes.size(); ++n) st[n] = {model.nodes[n].r, model.nodes[n].t, model.nodes[n].s};
    for (const ModelResponse &r : model.responses) {
        const CompState &c = cs[r.component];
        bool active = (r.states & c.state) != 0;
        if (r.visibility) {
            st[r.value].visible = active;
            continue;
        }
        float t = 0;
        if (active) {
            if (r.property == ModelResponse::kButton) t = c.button;
            else if (r.property == ModelResponse::kXAxis) t = (c.part.x + 1) * 0.5f;
            else if (r.property == ModelResponse::kYAxis) t = (1 - c.part.y) * 0.5f;  // WebXR: +y is towards you
        } else if (r.property != ModelResponse::kButton) {
            t = 0.5f;  // axes rest in the middle
        }
        t = std::min(1.0f, std::max(0.0f, t));
        const ModelNode &a = model.nodes[r.min], &b = model.nodes[r.max];
        st[r.value].r = Quat::nlerp(a.r, b.r, t);
        st[r.value].t = a.t + (b.t - a.t) * t;
    }

    std::vector<Mat4> world(model.nodes.size());
    std::vector<bool> visible(model.nodes.size());
    Mat4 base_m = Mat4::from_pose(base, scale);
    for (int root : model.roots) walk(model, st, root, base_m, world, visible, true);

    for (size_t n = 0; n < model.nodes.size(); ++n) {
        const ModelNode &node = model.nodes[n];
        if (!visible[n] || node.prims.empty()) continue;
        Vec3 tint;
        float amount = 0;
        if (node.component >= 0) {
            const CompState &c = cs[node.component];
            const PartState &p = c.part;
            if (!c.bound && !input.demo) tint = Vec3(0.01f, 0.01f, 0.012f), amount = 0.6f;
            if (p.near) tint = kNear, amount = 0.45f;
            if (p.touched) tint = kBlue, amount = 0.75f;
            if (p.value > 0.02f) {  // yellow from the first millimetre, orange at a full pull
                float v = std::min(1.0f, p.value);
                tint = kYellow * (1 - v * v) + kOrange * (v * v);
                amount = 0.45f + 0.5f * v;
            }
            if (p.pressed) tint = kOrange, amount = 1.0f;
        } else if (d.flash[hand]) {
            tint = kBlue, amount = 0.35f;  // a vibration was sent: the body flashes
        }
        for (const Primitive &prim : node.prims)
            renderer.draw_mesh(prim.mesh, world[n], prim.color, 0, prim.texture, tint, amount);
    }

    if (!labels) return;
    for (size_t i = 0; i < model.components.size(); ++i) {
        const ModelComponent &mc = model.components[i];
        if (mc.root < 0 || !cs[i].part.any() || !*part_label(mc.part)) continue;
        const float *m = world[mc.root].m;
        Vec3 at = Vec3(m[12], m[13], m[14]) + d.anchor.q.rotate({0, 0.03f, 0.035f});
        const float c[4] = {1.0f, 0.55f, 0.15f, 1};
        world_text(font, tris, Pose(d.anchor.q, at), 0.022f, part_label(mc.part), c);
    }
}

void Scene::ensure_font() {
    if (font_tried_) return;
    font_tried_ = true;
    std::vector<uint8_t> regular, medium;
    const ModelCache::Reader &read = models.reader();
    if (!read || !read("fonts/Go-Regular.ttf", regular)) {
        LOGE("font missing: fonts/Go-Regular.ttf");
        return;
    }
    read("fonts/Go-Medium.ttf", medium);
    font.load(std::move(regular), std::move(medium));
}

void Scene::update_cursors(SceneData &d) const {
    d.panel.cursors.clear();
    if (d.only_hand >= 0) return;
    for (int h = 0; h < 2; ++h) {
        float u, v;
        Vec3 hit;
        if (d.input->aim_valid[h] && panel.hit(d.input->aim[h], u, v, hit)) d.panel.cursors.push_back({u, v, h});
    }
}

void Scene::draw_models(SceneData &d, bool labels, std::vector<TexVertex> &tris) {
    const Input &input = *d.input;
    const char *family = kProfiles[d.current].family;
    for (int h = 0; h < 2; ++h) {
        const ControllerModel *model = models.get(family, h);
        if (!model || (d.only_hand >= 0 && h != d.only_hand)) continue;
        if (input.grip_valid[h]) draw_model(*model, h, input.grip[h], 1.0f, false, d, tris);
        draw_model(*model, h, display_pose(d, h), kModelScale, labels && d.labels, d, tris);
    }
}

void Scene::draw_rays(const SceneData &d) {
    std::vector<FlatVertex> lines;
    for (int h = 0; h < 2; ++h) {
        if (!d.input->aim_valid[h] || (d.only_hand >= 0 && h != d.only_hand)) continue;
        const Pose &aim = d.input->aim[h];
        float len = 1.5f, u, v;
        Vec3 hit;
        if (panel.hit(aim, u, v, hit)) len = length(hit - aim.p);  // ends on the panel's cursor
        Vec3 a = aim.p, b = aim.apply({0, 0, -len});
        float r = h == 0 ? 0.3f : 1.0f, g = h == 0 ? 0.6f : 0.5f, bl = h == 0 ? 1.0f : 0.1f;
        lines.push_back({a.x, a.y, a.z, r, g, bl, 0.9f});
        lines.push_back({b.x, b.y, b.z, r, g, bl, 0.15f});
    }
    renderer.draw_lines(lines);
}

static void standard_blend() {
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void Scene::draw(const Mat4 &view_proj, SceneData &d, GLuint panel_image) {
    ensure_font();
    d.panel.input = d.input;
    d.panel.current = d.current;
    renderer.begin_eye(view_proj);
    std::vector<TexVertex> labels;
    draw_models(d, true, labels);
    draw_rays(d);
    if (panel_image && d.only_hand < 0) draw_panel_quad(panel_image);  // no quad layer: in the scene
    if (font.loaded()) renderer.draw_text(labels, font.texture());
}

void Scene::draw_panel_quad(GLuint image) {
    // the panel's image (premultiplied alpha) as a textured quad, mipmapped
    float hw = panel.width * panel.px / 2, hh = panel.height * panel.px / 2;
    const Vec3 c[4] = {panel.pose.apply({-hw, hh, 0}), panel.pose.apply({hw, hh, 0}), panel.pose.apply({hw, -hh, 0}),
                       panel.pose.apply({-hw, -hh, 0})};
    const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};  // the image's first row is its bottom (GL)
    std::vector<TexVertex> quad;
    for (int k : {0, 1, 2, 0, 2, 3}) quad.push_back({c[k].x, c[k].y, c[k].z, uv[k][0], uv[k][1], 1, 1, 1, 1});
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    renderer.draw_image(quad, image);
    standard_blend();
}

void Scene::composite_panel(const Mat4 &view_proj, GLuint image) {
    renderer.begin_eye(view_proj);
    glDisable(GL_DEPTH_TEST);  // a quad layer covers the projection layer
    draw_panel_quad(image);
    glEnable(GL_DEPTH_TEST);
}

void Scene::draw_panel(SceneData &d, int x, const Vec3 *eye) {
    ensure_font();
    d.panel.input = d.input;
    d.panel.current = d.current;
    int w = panel.image_width(), h = panel.image_height();
    glViewport(x, 0, w, h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, 0, w, h);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    // drawn over a transparent clear, this leaves premultiplied colour, which is what a layer expects
    standard_blend();
    if (!font.loaded()) return;
    std::vector<TexVertex> tris;
    panel.build(d.panel, font, tris);
    // image texels, y down -> clip space
    Mat4 ortho = Mat4::identity();
    ortho.m[0] = 2.0f / w;
    ortho.m[5] = -2.0f / h;
    ortho.m[12] = -1;
    ortho.m[13] = 1;
    renderer.begin_eye(ortho);
    renderer.draw_text(tris, font.texture());

    if (!eye) return;
    // what's in front of the panel as this eye sees it: a frustum from the eye through the panel's rectangle that
    // ends at the panel (whatever is behind it is hidden by the panel anyway)
    Vec3 le = panel.pose.inverse().apply(*eye);
    if (le.z < 0.05f) return;
    float hw = panel.width * panel.px / 2, hh = panel.height * panel.px / 2;
    XrFovf fov{std::atan((-hw - le.x) / le.z), std::atan((hw - le.x) / le.z), std::atan((hh - le.y) / le.z),
               std::atan((-hh - le.y) / le.z)};
    Mat4 view = Mat4::from_pose(Pose(panel.pose.q, *eye).inverse());
    renderer.begin_eye(Mat4::projection(fov, 0.02f, le.z) * view);
    glBlendFunc(GL_ZERO, GL_ZERO);  // a model in front of the panel: transparent there, so it shows
    std::vector<TexVertex> unused;
    draw_models(d, false, unused);
    standard_blend();
    draw_rays(d);
}
