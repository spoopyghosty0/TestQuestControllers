// What the app draws: the controller models (in the hands and enlarged in front of the user), pointer rays and
// the info panel. Shared by the app and the host renderer (tools/render_host).
//
// The panel is drawn into its own image at about one texel per display pixel and shown as a quad composition
// layer, so the runtime samples it once (the projection layer would resample it twice). A quad layer covers
// everything in the projection layer, so the panel's image also gets what's in front of it from each eye: holes
// where a controller model is (the model in the projection layer shows through) and the pointer rays.
#pragma once

#include "gfx.h"
#include "input.h"
#include "models.h"
#include "ui.h"

// Background colour (linear RGB): light enough for the black controllers to stand out.
constexpr float kClearColor[3] = {0.045f, 0.05f, 0.065f};

struct SceneData {
    int current = 0;               // index into kProfiles
    const Input *input = nullptr;
    Pose anchor;                   // the user's place: models and panel are laid out in front of it
    bool flash[2] = {false, false};  // a vibration was just sent to this hand
    bool labels = true;              // part names next to the enlarged models
    int only_hand = -1;              // >= 0: draw just this hand's enlarged model, no panel (the app icon)
    PanelInfo panel;
};

class Scene {
public:
    Renderer renderer;
    ModelCache models;
    Panel panel;
    Font font;

    bool init() { return renderer.init(); }
    // Places the panel for `anchor`; call before hit tests and draw().
    void layout(const Pose &anchor);
    // Pose of the enlarged copy of one hand's model: in front of the user, its buttons turned towards them.
    Pose display_pose(const SceneData &d, int hand) const;
    // Where the pointer rays meet the panel (d.panel.cursors); once per frame, before drawing.
    void update_cursors(SceneData &d) const;
    // Draws the scene for one eye into the bound framebuffer (cleared by the caller). The panel is left out (it's
    // its own layer) unless `panel_image` is given: a texture made by draw_panel(), drawn at the panel's place.
    void draw(const Mat4 &view_proj, SceneData &d, GLuint panel_image = 0);
    // Draws the panel's image (panel.image_width() x image_height()) into the bound framebuffer at x: the panel,
    // then, if `eye` (in the scene's space) is given, what that eye sees in front of it (see above).
    void draw_panel(SceneData &d, int x, const Vec3 *eye);
    // Draws a panel image over the bound framebuffer like a compositor shows a quad layer (the host renderer).
    void composite_panel(const Mat4 &view_proj, GLuint image);

private:
    bool font_tried_ = false;
    void ensure_font();
    void draw_models(SceneData &d, bool labels, std::vector<TexVertex> &tris);
    void draw_rays(const SceneData &d);
    void draw_panel_quad(GLuint image);
    void draw_model(const ControllerModel &model, int hand, const Pose &base, float scale, bool labels,
                    const SceneData &d, std::vector<TexVertex> &tris);
};
