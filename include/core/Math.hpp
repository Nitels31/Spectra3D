#pragma once
#include <cmath>

namespace s3d {

constexpr float kPi = 3.14159265358979f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    Vec3() = default;
    Vec3(float ix, float iy, float iz) : x(ix), y(iy), z(iz) {}

    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return *this * (1.0f / s); }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

    float lengthSq() const { return x * x + y * y + z * z; }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const {
        const float l = length();
        return l > 1e-12f ? *this * (1.0f / l) : Vec3{};
    }
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float component(const Vec3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }

struct Quat {
    float w = 1.0f, x = 0.0f, y = 0.0f, z = 0.0f;
    Quat() = default;
    Quat(float iw, float ix, float iy, float iz) : w(iw), x(ix), y(iy), z(iz) {}

    Quat operator*(const Quat& o) const {
        return {w * o.w - x * o.x - y * o.y - z * o.z,
                w * o.x + x * o.w + y * o.z - z * o.y,
                w * o.y - x * o.z + y * o.w + z * o.x,
                w * o.z + x * o.y - y * o.x + z * o.w};
    }
    Quat operator+(const Quat& o) const { return {w + o.w, x + o.x, y + o.y, z + o.z}; }
    Quat operator*(float s) const { return {w * s, x * s, y * s, z * s}; }
    Quat conjugate() const { return {w, -x, -y, -z}; }

    Quat normalized() const {
        const float l = std::sqrt(w * w + x * x + y * y + z * z);
        if (l < 1e-12f) return {};
        const float inv = 1.0f / l;
        return {w * inv, x * inv, y * inv, z * inv};
    }

    static Quat axisAngle(const Vec3& axis, float radians) {
        const float h = radians * 0.5f;
        const float s = std::sin(h);
        const Vec3 a = axis.normalized();
        return {std::cos(h), a.x * s, a.y * s, a.z * s};
    }

    static Quat fromAxisX(const Vec3& dir) {
        const Vec3 xAxis{1.0f, 0.0f, 0.0f};
        const Vec3 axis = cross(xAxis, dir);
        const float sa = axis.length();
        const float ca = dot(xAxis, dir);
        if (sa > 1e-4f)
            return Quat::axisAngle(axis * (1.0f / sa), std::acos(clampf(ca, -1.0f, 1.0f)));
        return ca > 0.0f ? Quat{} : Quat::axisAngle(Vec3{0.0f, 1.0f, 0.0f}, kPi);
    }
};

// Interpolates two rotations by lerping the components and normalising. Not the
// shortest-arc path slerp takes, but for the small angle between two consecutive
// fixed physics steps the difference is invisible, and it is one lerp and one
// normalise against slerp's trig. Takes the shortest way round: a quaternion and
// its negation are the same rotation, and lerping the long way would spin the
// long way.
inline Quat nlerp(const Quat& a, const Quat& b, float t) {
    const float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    const float s = d < 0.0f ? -1.0f : 1.0f;
    return Quat{lerpf(a.w, b.w * s, t), lerpf(a.x, b.x * s, t),
                lerpf(a.y, b.y * s, t), lerpf(a.z, b.z * s, t)}
        .normalized();
}

// Turns a vector by a quaternion: v + 2w(u x v) + 2u x (u x v).
//
// The middle term is the one that matters and the one that is easy to get
// wrong: it is 2w times the CROSS PRODUCT of the axis and the vector, not 2w
// times the axis. With the axis in its place, a vector perpendicular to the
// axis would keep its direction and only shrink - a quarter turn about Y would
// take (1,0,0) to (0,1,0) instead of to (0,0,-1), and the vector would never
// swing around the axis at all. Mat3::fromQuat builds the same rotation as a
// matrix and agrees with this.
inline Vec3 rotate(const Quat& q, const Vec3& v) {
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}

inline Quat integrate(const Quat& q, const Vec3& worldOmega, float dt) {
    const Quat dw{0.0f, worldOmega.x * 0.5f, worldOmega.y * 0.5f, worldOmega.z * 0.5f};
    return (q + (dw * q) * dt).normalized();
}

struct Mat3 {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

    Vec3 operator*(const Vec3& v) const {
        return {m[0] * v.x + m[1] * v.y + m[2] * v.z,
                m[3] * v.x + m[4] * v.y + m[5] * v.z,
                m[6] * v.x + m[7] * v.y + m[8] * v.z};
    }
    Mat3 operator*(const Mat3& o) const {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r.m[i * 3 + j] = m[i * 3] * o.m[j] + m[i * 3 + 1] * o.m[3 + j] +
                                 m[i * 3 + 2] * o.m[6 + j];
        return r;
    }
    Mat3 transposed() const {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r.m[i * 3 + j] = m[j * 3 + i];
        return r;
    }

    static Mat3 diag(float a, float b, float c) {
        Mat3 r;
        r.m[0] = a;
        r.m[4] = b;
        r.m[8] = c;
        return r;
    }
    static Mat3 fromQuat(const Quat& q) {
        const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        Mat3 r;
        r.m[0] = 1 - 2 * (yy + zz);
        r.m[1] = 2 * (xy - wz);
        r.m[2] = 2 * (xz + wy);
        r.m[3] = 2 * (xy + wz);
        r.m[4] = 1 - 2 * (xx + zz);
        r.m[5] = 2 * (yz - wx);
        r.m[6] = 2 * (xz - wy);
        r.m[7] = 2 * (yz + wx);
        r.m[8] = 1 - 2 * (xx + yy);
        return r;
    }
};

// A 4x4 matrix in the layout GL wants: four columns of four floats each, so
// that it can be handed to glUniformMatrix4fv without a copy.
//
// The renderer had its matrices as bare float[16] arrays, and the shadow work
// needs four of them at once - the model, the view, the projection and the
// light's - so they are a type now.
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static Mat4 identity() { return Mat4{}; }

    static Mat4 translation(const Vec3& t) {
        Mat4 r;
        r.m[12] = t.x;
        r.m[13] = t.y;
        r.m[14] = t.z;
        return r;
    }

    static Mat4 scale(const Vec3& s) {
        Mat4 r;
        r.m[0] = s.x;
        r.m[5] = s.y;
        r.m[10] = s.z;
        return r;
    }

    static Mat4 rotation(const Quat& q) {
        const Mat3 r = Mat3::fromQuat(q);
        Mat4 out;
        out.m[0] = r.m[0]; out.m[1] = r.m[1]; out.m[2] = r.m[2];
        out.m[4] = r.m[3]; out.m[5] = r.m[4]; out.m[6] = r.m[5];
        out.m[8] = r.m[6]; out.m[9] = r.m[7]; out.m[10] = r.m[8];
        return out;
    }

    // The matrix of a body: where it is, how it is turned and how big it is.
    static Mat4 compose(const Vec3& position, const Quat& orientation, const Vec3& scale) {
        Mat4 out = rotation(orientation);
        out.m[0] *= scale.x; out.m[1] *= scale.x; out.m[2] *= scale.x;
        out.m[4] *= scale.y; out.m[5] *= scale.y; out.m[6] *= scale.y;
        out.m[8] *= scale.z; out.m[9] *= scale.z; out.m[10] *= scale.z;
        out.m[12] = position.x;
        out.m[13] = position.y;
        out.m[14] = position.z;
        return out;
    }

    // this * other, in the column-major layout.
    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) sum += m[k * 4 + row] * o.m[col * 4 + k];
                r.m[col * 4 + row] = sum;
            }
        }
        return r;
    }

    // The point, with the translation and the perspective divide.
    Vec3 transformPoint(const Vec3& v) const {
        const float x = m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12];
        const float y = m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13];
        const float z = m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14];
        const float w = m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15];
        if (w != 0.0f && w != 1.0f) return Vec3{x / w, y / w, z / w};
        return Vec3{x, y, z};
    }

    // The direction: no translation, and no perspective divide.
    Vec3 transformDirection(const Vec3& v) const {
        return Vec3{m[0] * v.x + m[4] * v.y + m[8] * v.z,
                    m[1] * v.x + m[5] * v.y + m[9] * v.z,
                    m[2] * v.x + m[6] * v.y + m[10] * v.z};
    }
};

// The matrix that looks from the eye at the given point, with up as up.
//
// The view of the world, the way GL wants it: the camera is at the origin,
// turned around, and the world is what moves.
inline Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 forward = (target - eye).normalized();
    Vec3 side = cross(forward, up);
    // Straight up and straight down: there is no side to look from, and the
    // picture would be a line. Any side will do.
    if (side.lengthSq() < 1e-10f) side = cross(forward, Vec3{1.0f, 0.0f, 0.0f});
    if (side.lengthSq() < 1e-10f) side = Vec3{1.0f, 0.0f, 0.0f};
    side = side.normalized();
    const Vec3 upward = cross(side, forward);

    Mat4 r;
    r.m[0] = side.x;   r.m[4] = side.y;   r.m[8] = side.z;
    r.m[1] = upward.x; r.m[5] = upward.y; r.m[9] = upward.z;
    r.m[2] = -forward.x; r.m[6] = -forward.y; r.m[10] = -forward.z;
    r.m[12] = -dot(side, eye);
    r.m[13] = -dot(upward, eye);
    r.m[14] = dot(forward, eye);
    return r;
}

// The matrix of an orthographic box: the camera is parallel, and the box is
// what it can see.
inline Mat4 ortho(float left, float right, float bottom, float top, float nearPlane,
                  float farPlane) {
    Mat4 r;
    r.m[0] = 2.0f / (right - left);
    r.m[5] = 2.0f / (top - bottom);
    r.m[10] = -2.0f / (farPlane - nearPlane);
    r.m[12] = -(right + left) / (right - left);
    r.m[13] = -(top + bottom) / (top - bottom);
    r.m[14] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    return r;
}

// The matrix of a frustum: the box the camera sees, cut at the near and at
// the far plane.
//
// The same numbers glFrustum uses, so that the picture the fixed pipeline
// draws and the picture the shader draws are the same picture.
inline Mat4 frustum(float left, float right, float bottom, float top, float nearPlane,
                    float farPlane) {
    Mat4 r;
    r.m[0] = 2.0f * nearPlane / (right - left);
    r.m[5] = 2.0f * nearPlane / (top - bottom);
    r.m[8] = (right + left) / (right - left);
    r.m[9] = (top + bottom) / (top - bottom);
    r.m[10] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    r.m[11] = -1.0f;
    r.m[14] = -2.0f * farPlane * nearPlane / (farPlane - nearPlane);
    return r;
}


} // namespace s3d