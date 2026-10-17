#include "scene.h"

#include <algorithm>
#include <cmath>

namespace {
const Vec3 kPartIdle(0.16f, 0.165f, 0.18f), kPartUnbound(0.03f, 0.03f, 0.035f), kBody(0.045f, 0.048f, 0.055f),
    kRing(0.06f, 0.063f, 0.07f), kOrange(1.0f, 0.36f, 0.03f), kBlue(0.08f, 0.32f, 1.0f), kNear(0.03f, 0.08f, 0.25f);

// layout, in the anchor's frame (metres; the user looks along -Z)
const Vec3 kModelLeft(-0.30f, -0.06f, -0.62f), kModelRight(-0.09f, -0.06f, -0.62f);
const float kModelScale = 1.9f;
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
    float tilt = std::string(kProfiles[d.current].family) == "go" ? 0.0f : deg(28);
    float s = std::sin(tilt), c = std::cos(tilt), alpha = deg(20);
    Vec3 n_w(0, std::sin(alpha), std::cos(alpha)), m_w(0, -std::cos(alpha), std::sin(alpha));
    Vec3 y_w = n_w * s + m_w * c, z_w = n_w * (-c) + m_w * s;
    Quat q = Quat::from_basis({1, 0, 0}, y_w, z_w);
    Vec3 at = hand == 0 ? kModelLeft : kModelRight;
    Quat turn = Quat::axis_angle({0, 1, 0}, std::atan2(-at.x, -at.z));  // face the user, not straight ahead
    return d.anchor * Pose(turn * q, at);
}

void Scene::draw_model(const ControllerModel &model, int hand, const Pose &base, float scale, bool labels,
                       const SceneData &d, std::vector<FlatVertex> &tris) {
    const Input &input = *d.input;
    Mat4 base_m = Mat4::from_pose(base, scale);
    for (const ModelNode &node : model.nodes) {
        Vec3 color = kPartIdle;
        float emissive = 0;
        Pose delta;
        const std::string &n = node.name;
        if (n == "body" || n == "camera") {
            color = d.flash[hand] ? kBlue * 0.35f : kBody;
            emissive = d.flash[hand] ? 0.6f : 0;
        } else if (n == "ring") {
            if (labels) continue;  // the enlarged copy is a button diagram: its ring would hide the buttons
            color = kRing;
        } else {
            bool bound = false;
            for (const auto &row : input.rows)
                if (row.hand == hand && row.part == n && row.action) bound = true;
            PartState st = input.part(hand, n);
            if (!bound && !input.demo) color = kPartUnbound;
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
                    renderer.draw_mesh(renderer.sphere, base_m * Mat4::from_pose(dot, 0.0028f),
                                       st.pressed ? kOrange : kBlue, 0.9f);
                }
            } else if (st.pressed) {
                delta.p = Vec3(0, -0.0022f, 0);  // buttons go in
            }
            if (labels && st.any() && *part_label(n)) {
                // label above the part, facing the user
                Vec3 at = base.apply(node.rest.p * scale) + d.anchor.q.rotate({0, 0.026f, 0.03f});
                const float c[4] = {1.0f, 0.55f, 0.15f, 1};
                world_text(tris, Pose(d.anchor.q, at), 0.0024f, part_label(n), c);
            }
        }
        renderer.draw_mesh(node.mesh, base_m * Mat4::from_pose(node.rest * delta), color, emissive);
    }
}

void Scene::draw(const Mat4 &view_proj, SceneData &d) {
    renderer.begin_eye(view_proj);
    std::vector<FlatVertex> tris, lines;
    const Input &input = *d.input;
    const char *family = kProfiles[d.current].family;
    for (int h = 0; h < 2; ++h) {
        const ControllerModel *model = models.get(family, h);
        if (!model) continue;
        if (input.grip_valid[h]) draw_model(*model, h, input.grip[h], 1.0f, false, d, tris);
        draw_model(*model, h, display_pose(d, h), kModelScale, true, d, tris);
        if (input.aim_valid[h]) {
            const Pose &aim = input.aim[h];
            float len = 1.5f, u, v;
            Vec3 hit;
            if (panel.hit(aim, u, v, hit)) {
                len = length(hit - aim.p);
                renderer.draw_mesh(renderer.sphere, Mat4::from_pose(Pose(Quat(), hit), 0.006f), {1.0f, 0.5f, 0.1f},
                                   1.0f);
            }
            Vec3 a = aim.p, b = aim.apply({0, 0, -len});
            float r = h == 0 ? 0.3f : 1.0f, g = h == 0 ? 0.6f : 0.5f, bl = h == 0 ? 1.0f : 0.1f;
            lines.push_back({a.x, a.y, a.z, r, g, bl, 0.9f});
            lines.push_back({b.x, b.y, b.z, r, g, bl, 0.15f});
        }
    }
    d.panel.input = d.input;
    d.panel.current = d.current;
    renderer.draw_lines(lines);
    panel.build(d.panel, tris);  // the panel last: it's partly transparent
    renderer.draw_flat(tris);
}
