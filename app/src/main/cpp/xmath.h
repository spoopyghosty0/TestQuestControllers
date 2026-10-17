// Small vector/quaternion/matrix helpers (column-major 4x4 like OpenGL).
#pragma once

#include <cmath>
#include <openxr/openxr.h>

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit Vec3(const XrVector3f &v) : x(v.x), y(v.y), z(v.z) {}
    Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
};

inline float dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3 &a, const Vec3 &b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3 &v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3 &v) {
    float l = length(v);
    return l > 0 ? v * (1.0f / l) : v;
}

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    Quat() = default;
    Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    explicit Quat(const XrQuaternionf &q) : x(q.x), y(q.y), z(q.z), w(q.w) {}
    Quat operator*(const Quat &o) const {
        return {w * o.x + x * o.w + y * o.z - z * o.y, w * o.y - x * o.z + y * o.w + z * o.x,
                w * o.z + x * o.y - y * o.x + z * o.w, w * o.w - x * o.x - y * o.y - z * o.z};
    }
    Vec3 rotate(const Vec3 &v) const {
        Vec3 u(x, y, z);
        Vec3 t = cross(u, v) * 2.0f;
        return v + t * w + cross(u, t);
    }
    Quat conjugate() const { return {-x, -y, -z, w}; }
    static Quat axis_angle(const Vec3 &axis, float radians) {
        Vec3 a = normalize(axis);
        float s = std::sin(radians * 0.5f);
        return {a.x * s, a.y * s, a.z * s, std::cos(radians * 0.5f)};
    }
    // Rotation whose columns (images of +X, +Y, +Z) are the given orthonormal vectors.
    static Quat from_basis(const Vec3 &cx, const Vec3 &cy, const Vec3 &cz) {
        float m00 = cx.x, m10 = cx.y, m20 = cx.z, m01 = cy.x, m11 = cy.y, m21 = cy.z, m02 = cz.x, m12 = cz.y,
              m22 = cz.z;
        float t = m00 + m11 + m22;
        Quat q;
        if (t > 0) {
            float s = std::sqrt(t + 1.0f) * 2;
            q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
        } else if (m00 > m11 && m00 > m22) {
            float s = std::sqrt(1.0f + m00 - m11 - m22) * 2;
            q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
        } else if (m11 > m22) {
            float s = std::sqrt(1.0f + m11 - m00 - m22) * 2;
            q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
        } else {
            float s = std::sqrt(1.0f + m22 - m00 - m11) * 2;
            q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
        }
        return q;
    }
};

struct Pose {
    Quat q;
    Vec3 p;
    Pose() = default;
    Pose(const Quat &q_, const Vec3 &p_) : q(q_), p(p_) {}
    explicit Pose(const XrPosef &x) : q(x.orientation), p(x.position) {}
    Pose operator*(const Pose &child) const { return {q * child.q, p + q.rotate(child.p)}; }
    Vec3 apply(const Vec3 &v) const { return p + q.rotate(v); }
    Pose inverse() const {
        Quat c = q.conjugate();
        return {c, c.rotate(-p)};
    }
};

struct Mat4 {
    float m[16];  // column-major
    static Mat4 identity() {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
        return r;
    }
    Mat4 operator*(const Mat4 &o) const {
        Mat4 r{};
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += m[k * 4 + rr] * o.m[c * 4 + k];
                r.m[c * 4 + rr] = s;
            }
        return r;
    }
    static Mat4 from_pose(const Pose &p, float scale = 1.0f) {
        Mat4 r = identity();
        Vec3 cx = p.q.rotate({1, 0, 0}) * scale, cy = p.q.rotate({0, 1, 0}) * scale,
             cz = p.q.rotate({0, 0, 1}) * scale;
        r.m[0] = cx.x, r.m[1] = cx.y, r.m[2] = cx.z;
        r.m[4] = cy.x, r.m[5] = cy.y, r.m[6] = cy.z;
        r.m[8] = cz.x, r.m[9] = cz.y, r.m[10] = cz.z;
        r.m[12] = p.p.x, r.m[13] = p.p.y, r.m[14] = p.p.z;
        return r;
    }
    // Projection for an OpenXR field of view (GL clip space, z in -1..1).
    static Mat4 projection(const XrFovf &fov, float near_z, float far_z) {
        float l = std::tan(fov.angleLeft), r = std::tan(fov.angleRight);
        float d = std::tan(fov.angleDown), u = std::tan(fov.angleUp);
        float w = r - l, h = u - d;
        Mat4 p{};
        p.m[0] = 2 / w;
        p.m[5] = 2 / h;
        p.m[8] = (r + l) / w;
        p.m[9] = (u + d) / h;
        p.m[10] = -(far_z + near_z) / (far_z - near_z);
        p.m[11] = -1;
        p.m[14] = -2 * far_z * near_z / (far_z - near_z);
        return p;
    }
};

constexpr float kPi = 3.14159265358979f;
inline float deg(float d) { return d * kPi / 180.0f; }
