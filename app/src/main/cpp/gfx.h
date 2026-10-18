// EGL context, shaders and meshes (GLES 3).
#pragma once

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <vector>

#include "xmath.h"

struct Gl {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    bool init();
    void shutdown();
};

struct Mesh {
    GLuint vao = 0, vbo = 0, ibo = 0;
    GLsizei count = 0;
    GLenum index_type = GL_UNSIGNED_SHORT;
    // position, normal and texture coordinate interleaved (8 floats per vertex)
    void upload(const std::vector<float> &interleaved, const std::vector<uint32_t> &indices);
    void draw() const;
    void release();
};

// A flat-coloured vertex for panels, text and rays.
struct FlatVertex {
    float x, y, z;
    float r, g, b, a;
};

// A textured vertex: glyph quads (single-channel coverage) and solid rectangles (the atlas' white block), or an
// image (the panel when it's drawn into the scene).
struct TexVertex {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};

// Anisotropic filtering for the bound 2D texture, where the GPU offers it.
void set_anisotropy();

class Renderer {
public:
    bool init();
    void begin_eye(const Mat4 &view_proj);
    // Lit mesh: color in linear RGB (multiplies the texture if one is given), emissive 0..1 blends towards the flat
    // color; tint_amount 0..1 blends the lit result towards `tint` (input highlights).
    void draw_mesh(const Mesh &mesh, const Mat4 &model, const Vec3 &color, float emissive, GLuint texture = 0,
                   const Vec3 &tint = Vec3(), float tint_amount = 0);
    void draw_flat(const std::vector<FlatVertex> &triangles);
    void draw_lines(const std::vector<FlatVertex> &lines);
    // Text and rectangles from a coverage atlas (colour * atlas red as alpha).
    void draw_text(const std::vector<TexVertex> &triangles, GLuint atlas);
    // An RGBA image with premultiplied alpha, tinted by the vertex colour (the blend state is the caller's).
    void draw_image(const std::vector<TexVertex> &triangles, GLuint texture);
    Mesh sphere;  // unit sphere for markers

private:
    GLuint lit_ = 0, flat_ = 0;
    GLint lit_mvp_ = -1, lit_model_ = -1, lit_color_ = -1, lit_emissive_ = -1, flat_mvp_ = -1, lit_tex_ = -1,
          lit_has_tex_ = -1, lit_tint_ = -1, lit_tint_amount_ = -1;
    GLuint flat_vao_ = 0, flat_vbo_ = 0;
    GLuint tex_ = 0, tex_vao_ = 0, tex_vbo_ = 0;
    GLint tex_mvp_ = -1, tex_sampler_ = -1, tex_mode_ = -1;
    void textured(const std::vector<TexVertex> &v, GLuint texture, int mode);
    Mat4 view_proj_ = Mat4::identity();
    void flat(const std::vector<FlatVertex> &v, GLenum mode);
};

// A texture with a framebuffer to draw into (the panel's image where no quad layer takes it).
struct RenderTexture {
    GLuint texture = 0, fbo = 0;
    int width = 0, height = 0, levels = 1;
    void create(int w, int h, int mip_levels);
    void release();
};

// Framebuffers for one swapchain (one per image) with a shared depth buffer.
struct EyeTarget {
    std::vector<GLuint> fbos;
    GLuint depth = 0;
    int width = 0, height = 0;
    // with_depth false: colour only (the panel's quad layer)
    void create(const std::vector<GLuint> &color_textures, int w, int h, bool with_depth = true);
    void release();
};
