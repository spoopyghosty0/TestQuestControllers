// What the app draws: the controller models (in the hands and enlarged in front of the user), pointer rays and
// the info panel. Shared by the app and the host renderer (tools/render_host).
#pragma once

#include "gfx.h"
#include "input.h"
#include "models.h"
#include "ui.h"

struct SceneData {
    int current = 0;               // index into kProfiles
    const Input *input = nullptr;
    Pose anchor;                   // the user's place: models and panel are laid out in front of it
    bool flash[2] = {false, false};  // a vibration was just sent to this hand
    PanelInfo panel;
};

class Scene {
public:
    Renderer renderer;
    ModelCache models;
    Panel panel;

    bool init() { return renderer.init(); }
    // Places the panel for `anchor`; call before hit tests and draw().
    void layout(const Pose &anchor);
    // Pose of the enlarged copy of one hand's model: in front of the user, its buttons turned towards them.
    Pose display_pose(const SceneData &d, int hand) const;
    // Draws everything for one eye into the bound framebuffer (cleared by the caller).
    void draw(const Mat4 &view_proj, SceneData &d);

private:
    void draw_model(const ControllerModel &model, int hand, const Pose &base, float scale, bool labels,
                    const SceneData &d, std::vector<FlatVertex> &tris);
};
