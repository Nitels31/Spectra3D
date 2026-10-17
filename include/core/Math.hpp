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

inline Vec3 rotate(const Quat& q, const Vec3& v) {
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = cross(u, v) * 2.0f;
    return v + u * (q.w * 2.0f) + cross(u, t);
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
            for (int j = 0; j < 3; ++j)
                r.m[i * 3 + j] = m[j * 3 + i];
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

} // namespace s3d