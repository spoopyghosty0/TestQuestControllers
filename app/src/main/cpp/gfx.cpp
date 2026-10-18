#include "gfx.h"

#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>

#include <algorithm>
#include <cstring>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

#include "log.h"

bool Gl::init() {
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr)) {
        LOGE("eglInitialize failed");
        return false;
    }
    const EGLint attribs[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                              EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_SAMPLES, 0,
                              EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                              EGL_NONE};
    EGLint count = 0;
    if (!eglChooseConfig(display, attribs, &config, 1, &count) || count == 0) {
        LOGE("eglChooseConfig found no config");
        return false;
    }
    const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attribs);
    const EGLint surf_attribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    surface = eglCreatePbufferSurface(display, config, surf_attribs);
    if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(display, surface, surface, context)) {
        LOGE("EGL context setup failed: 0x%x", eglGetError());
        return false;
    }
    LOGI("GL: %s / %s", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    return true;
}

void Gl::shutdown() {
    if (display == EGL_NO_DISPLAY) return;
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
    if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
    eglTerminate(display);
    display = EGL_NO_DISPLAY;
}

void Mesh::upload(const std::vector<float> &interleaved, const std::vector<uint32_t> &indices) {
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, interleaved.size() * sizeof(float), interleaved.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(6 * sizeof(float)));
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);
    index_type = GL_UNSIGNED_INT;
    count = (GLsizei)indices.size();
    glBindVertexArray(0);
}

void Mesh::draw() const {
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, count, index_type, nullptr);
}

void Mesh::release() {
    if (ibo) glDeleteBuffers(1, &ibo);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    vao = vbo = ibo = 0;
}

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("shader compile failed: %s", log);
    }
    return s;
}

static GLuint program(const char *vs, const char *fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(p, 0, "a_pos");
    glBindAttribLocation(p, 1, "a_second");
    glBindAttribLocation(p, 2, "a_uv");
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        LOGE("program link failed: %s", log);
    }
    return p;
}

static const char *kLitVs = R"(#version 300 es
in vec3 a_pos;
in vec3 a_second;
in vec2 a_uv;
uniform mat4 u_mvp;
uniform mat4 u_model;
out vec3 v_normal;
out vec2 v_uv;
void main() {
    v_normal = mat3(u_model) * a_second;
    v_uv = a_uv;
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

static const char *kLitFs = R"(#version 300 es
precision mediump float;
in vec3 v_normal;
in vec2 v_uv;
uniform vec3 u_color;
uniform float u_emissive;
uniform sampler2D u_tex;
uniform float u_has_tex;
uniform vec3 u_tint;
uniform float u_tint_amount;
out vec4 o_color;
void main() {
    vec3 n = normalize(v_normal);
    float key = abs(dot(n, normalize(vec3(0.3, 0.8, 0.5))));
    float fill = abs(dot(n, normalize(vec3(-0.6, -0.2, 0.3)))) * 0.3;
    vec3 base = u_color * mix(vec3(1.0), texture(u_tex, v_uv).rgb, u_has_tex);
    float rim = pow(1.0 - abs(n.z), 3.0) * 0.35;  // edges light up: dark plastic keeps its outline
    vec3 lit = base * (0.42 + 0.8 * key + fill) + vec3(0.09, 0.1, 0.12) * rim;
    lit = mix(lit, u_tint * (0.55 + 0.45 * key), u_tint_amount);
    o_color = vec4(mix(lit, u_color, u_emissive), 1.0);
}
)";

static const char *kFlatVs = R"(#version 300 es
in vec3 a_pos;
in vec4 a_second;
uniform mat4 u_mvp;
out vec4 v_color;
void main() {
    v_color = a_second;
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

static const char *kFlatFs = R"(#version 300 es
precision mediump float;
in vec4 v_color;
out vec4 o_color;
void main() { o_color = v_color; }
)";

static const char *kTexVs = R"(#version 300 es
in vec3 a_pos;
in vec4 a_second;
in vec2 a_uv;
uniform mat4 u_mvp;
out vec4 v_color;
out vec2 v_uv;
void main() {
    v_color = a_second;
    v_uv = a_uv;
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

// mode 0: coverage atlas (text); mode 1: premultiplied RGBA image
static const char *kTexFs = R"(#version 300 es
precision mediump float;
in vec4 v_color;
in vec2 v_uv;
uniform sampler2D u_tex;
uniform int u_mode;
out vec4 o_color;
void main() {
    vec4 t = texture(u_tex, v_uv);
    if (u_mode == 0) {
        // blending happens in linear light (sRGB targets): thin the coverage a little so light text on the dark
        // panel keeps the weight it has in gamma-space renderers
        o_color = vec4(v_color.rgb, v_color.a * pow(t.r, 1.35));
    } else {
        o_color = t * v_color;
    }
}
)";

void set_anisotropy() {
    static int max_aniso = -1;
    if (max_aniso < 0) {
        const char *ext = (const char *)glGetString(GL_EXTENSIONS);
        max_aniso = 0;
        if (ext && std::strstr(ext, "GL_EXT_texture_filter_anisotropic")) {
            float m = 0;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &m);
            max_aniso = (int)m;
        }
    }
    if (max_aniso > 1)
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, (float)std::min(max_aniso, 8));
}

static void make_sphere(Mesh &mesh) {
    std::vector<float> v;
    std::vector<uint32_t> idx;
    const int nu = 16, nv = 10;
    for (int j = 0; j <= nv; ++j) {
        float ph = kPi * ((float)j / nv - 0.5f);
        for (int i = 0; i <= nu; ++i) {
            float th = 2 * kPi * i / nu;
            float x = std::cos(ph) * std::cos(th), y = std::sin(ph), z = std::cos(ph) * std::sin(th);
            v.insert(v.end(), {x, y, z, x, y, z, 0, 0});
        }
    }
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nu; ++i) {
            uint32_t a = j * (nu + 1) + i, b = a + 1, c = a + nu + 1, d = c + 1;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }
    mesh.upload(v, idx);
}

bool Renderer::init() {
    lit_ = program(kLitVs, kLitFs);
    lit_mvp_ = glGetUniformLocation(lit_, "u_mvp");
    lit_model_ = glGetUniformLocation(lit_, "u_model");
    lit_color_ = glGetUniformLocation(lit_, "u_color");
    lit_emissive_ = glGetUniformLocation(lit_, "u_emissive");
    lit_tex_ = glGetUniformLocation(lit_, "u_tex");
    lit_has_tex_ = glGetUniformLocation(lit_, "u_has_tex");
    lit_tint_ = glGetUniformLocation(lit_, "u_tint");
    lit_tint_amount_ = glGetUniformLocation(lit_, "u_tint_amount");
    flat_ = program(kFlatVs, kFlatFs);
    flat_mvp_ = glGetUniformLocation(flat_, "u_mvp");
    glGenVertexArrays(1, &flat_vao_);
    glBindVertexArray(flat_vao_);
    glGenBuffers(1, &flat_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, flat_vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FlatVertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(FlatVertex), (void *)(3 * sizeof(float)));
    glBindVertexArray(0);
    tex_ = program(kTexVs, kTexFs);
    tex_mvp_ = glGetUniformLocation(tex_, "u_mvp");
    tex_sampler_ = glGetUniformLocation(tex_, "u_tex");
    tex_mode_ = glGetUniformLocation(tex_, "u_mode");
    glGenVertexArrays(1, &tex_vao_);
    glBindVertexArray(tex_vao_);
    glGenBuffers(1, &tex_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, tex_vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TexVertex), nullptr);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(TexVertex), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(TexVertex), (void *)(5 * sizeof(float)));
    glBindVertexArray(0);
    make_sphere(sphere);
    return lit_ && flat_ && tex_;
}

void Renderer::begin_eye(const Mat4 &view_proj) { view_proj_ = view_proj; }

void Renderer::draw_mesh(const Mesh &mesh, const Mat4 &model, const Vec3 &color, float emissive, GLuint texture,
                         const Vec3 &tint, float tint_amount) {
    glUseProgram(lit_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(lit_tex_, 0);
    glUniform1f(lit_has_tex_, texture ? 1.0f : 0.0f);
    glUniform3f(lit_tint_, tint.x, tint.y, tint.z);
    glUniform1f(lit_tint_amount_, tint_amount);
    Mat4 mvp = view_proj_ * model;
    glUniformMatrix4fv(lit_mvp_, 1, GL_FALSE, mvp.m);
    glUniformMatrix4fv(lit_model_, 1, GL_FALSE, model.m);
    glUniform3f(lit_color_, color.x, color.y, color.z);
    glUniform1f(lit_emissive_, emissive);
    mesh.draw();
}

void Renderer::flat(const std::vector<FlatVertex> &v, GLenum mode) {
    if (v.empty()) return;
    glUseProgram(flat_);
    glUniformMatrix4fv(flat_mvp_, 1, GL_FALSE, view_proj_.m);
    glBindVertexArray(flat_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, flat_vbo_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(FlatVertex), v.data(), GL_STREAM_DRAW);
    glDrawArrays(mode, 0, (GLsizei)v.size());
}

void Renderer::draw_flat(const std::vector<FlatVertex> &triangles) { flat(triangles, GL_TRIANGLES); }
void Renderer::draw_lines(const std::vector<FlatVertex> &lines) { flat(lines, GL_LINES); }

void Renderer::textured(const std::vector<TexVertex> &v, GLuint texture, int mode) {
    if (v.empty()) return;
    glUseProgram(tex_);
    glUniformMatrix4fv(tex_mvp_, 1, GL_FALSE, view_proj_.m);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(tex_sampler_, 0);
    glUniform1i(tex_mode_, mode);
    glBindVertexArray(tex_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, tex_vbo_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(TexVertex), v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)v.size());
}

void Renderer::draw_text(const std::vector<TexVertex> &triangles, GLuint atlas) { textured(triangles, atlas, 0); }
void Renderer::draw_image(const std::vector<TexVertex> &triangles, GLuint texture) {
    textured(triangles, texture, 1);
}

void RenderTexture::create(int w, int h, int mip_levels) {
    width = w;
    height = h;
    levels = mip_levels;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, levels, GL_SRGB8_ALPHA8, w, h);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    set_anisotropy();
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) LOGE("panel framebuffer incomplete: 0x%x", status);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTexture::release() {
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (texture) glDeleteTextures(1, &texture);
    fbo = texture = 0;
}

void EyeTarget::create(const std::vector<GLuint> &color_textures, int w, int h, bool with_depth) {
    width = w;
    height = h;
    if (with_depth) {
        glGenRenderbuffers(1, &depth);
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    }
    fbos.resize(color_textures.size());
    glGenFramebuffers((GLsizei)fbos.size(), fbos.data());
    for (size_t i = 0; i < fbos.size(); ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_textures[i], 0);
        if (depth) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) LOGE("framebuffer incomplete: 0x%x", status);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void EyeTarget::release() {
    if (!fbos.empty()) glDeleteFramebuffers((GLsizei)fbos.size(), fbos.data());
    fbos.clear();
    if (depth) glDeleteRenderbuffers(1, &depth);
    depth = 0;
}
