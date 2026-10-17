#include "gfx.h"

#include <EGL/eglext.h>

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
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));
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
uniform mat4 u_mvp;
uniform mat4 u_model;
out vec3 v_normal;
void main() {
    v_normal = mat3(u_model) * a_second;
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

static const char *kLitFs = R"(#version 300 es
precision mediump float;
in vec3 v_normal;
uniform vec3 u_color;
uniform float u_emissive;
out vec4 o_color;
void main() {
    vec3 n = normalize(v_normal);
    float key = abs(dot(n, normalize(vec3(0.3, 0.8, 0.5))));
    float fill = abs(dot(n, normalize(vec3(-0.6, -0.2, 0.3)))) * 0.25;
    vec3 lit = u_color * (0.22 + 0.7 * key + fill);
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

static void make_sphere(Mesh &mesh) {
    std::vector<float> v;
    std::vector<uint32_t> idx;
    const int nu = 16, nv = 10;
    for (int j = 0; j <= nv; ++j) {
        float ph = kPi * ((float)j / nv - 0.5f);
        for (int i = 0; i <= nu; ++i) {
            float th = 2 * kPi * i / nu;
            float x = std::cos(ph) * std::cos(th), y = std::sin(ph), z = std::cos(ph) * std::sin(th);
            v.insert(v.end(), {x, y, z, x, y, z});
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
    make_sphere(sphere);
    return lit_ && flat_;
}

void Renderer::begin_eye(const Mat4 &view_proj) { view_proj_ = view_proj; }

void Renderer::draw_mesh(const Mesh &mesh, const Mat4 &model, const Vec3 &color, float emissive) {
    glUseProgram(lit_);
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

void EyeTarget::create(const std::vector<GLuint> &color_textures, int w, int h) {
    width = w;
    height = h;
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    fbos.resize(color_textures.size());
    glGenFramebuffers((GLsizei)fbos.size(), fbos.data());
    for (size_t i = 0; i < fbos.size(); ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_textures[i], 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
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
