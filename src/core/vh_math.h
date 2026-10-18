// Small linear algebra helpers used by the converter.
// Matrices are column-major and multiply column vectors (M * v), matching
// both glTF and the MT Framework mod3 file layout.
#pragma once

#include <cmath>
#include <cstring>
#include <algorithm>

namespace vh {

struct Vec2 {
    float x = 0, y = 0;
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3& a, const Vec3& fallback = {0, 1, 0}) {
    float l = length(a);
    return l > 1e-12f ? a * (1.0f / l) : fallback;
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    Vec4() = default;
    Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    Vec3 xyz() const { return {x, y, z}; }
};

// 3x3 rotation/linear matrix, column-major: c[col] is a column.
struct Mat3 {
    Vec3 c[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    static Mat3 identity() { return Mat3(); }
    static Mat3 fromColumns(const Vec3& a, const Vec3& b, const Vec3& d) {
        Mat3 m; m.c[0] = a; m.c[1] = b; m.c[2] = d; return m;
    }
    Vec3 operator*(const Vec3& v) const { return c[0] * v.x + c[1] * v.y + c[2] * v.z; }
    Mat3 operator*(const Mat3& o) const { return fromColumns((*this) * o.c[0], (*this) * o.c[1], (*this) * o.c[2]); }
    Mat3 transposed() const {
        return fromColumns({c[0].x, c[1].x, c[2].x}, {c[0].y, c[1].y, c[2].y}, {c[0].z, c[1].z, c[2].z});
    }
    float det() const { return dot(c[0], cross(c[1], c[2])); }
    Mat3 inverse() const {
        float d = det();
        if (std::fabs(d) < 1e-20f) return identity();
        Vec3 r0 = cross(c[1], c[2]), r1 = cross(c[2], c[0]), r2 = cross(c[0], c[1]);
        // rows of the inverse are r0/d, r1/d, r2/d
        Mat3 rows = fromColumns(r0 * (1 / d), r1 * (1 / d), r2 * (1 / d));
        return rows.transposed();
    }
};

// Rotation matrix about a unit axis.
inline Mat3 axisAngle(const Vec3& axis, float angle) {
    Vec3 a = normalize(axis);
    float c = std::cos(angle), s = std::sin(angle), t = 1 - c;
    return Mat3::fromColumns(
        {t * a.x * a.x + c, t * a.x * a.y + s * a.z, t * a.x * a.z - s * a.y},
        {t * a.x * a.y - s * a.z, t * a.y * a.y + c, t * a.y * a.z + s * a.x},
        {t * a.x * a.z + s * a.y, t * a.y * a.z - s * a.x, t * a.z * a.z + c});
}

// Smallest rotation taking unit direction `from` onto unit direction `to`.
inline Mat3 rotationBetween(const Vec3& fromIn, const Vec3& toIn) {
    Vec3 from = normalize(fromIn), to = normalize(toIn);
    float d = std::clamp(dot(from, to), -1.0f, 1.0f);
    if (d > 0.999999f) return Mat3::identity();
    Vec3 axis = cross(from, to);
    if (length(axis) < 1e-6f) {
        // opposite vectors: rotate 180 degrees around any perpendicular axis
        axis = cross(from, {1, 0, 0});
        if (length(axis) < 1e-3f) axis = cross(from, {0, 1, 0});
    }
    return axisAngle(axis, std::acos(d));
}

// Orthonormal basis from a primary axis and a secondary hint.
// Returns columns (primary, secondaryOrtho, primary x secondary).
inline Mat3 basisFrom(const Vec3& primary, const Vec3& secondaryHint) {
    Vec3 a = normalize(primary);
    Vec3 b = secondaryHint - a * dot(a, secondaryHint);
    b = normalize(b, std::fabs(a.y) < 0.9f ? normalize(cross(a, {0, 1, 0})) : normalize(cross(a, {1, 0, 0})));
    return Mat3::fromColumns(a, b, cross(a, b));
}

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // column-major
    static Mat4 identity() { return Mat4(); }
    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }
    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += at(rr, k) * o.at(k, c);
                r.at(rr, c) = s;
            }
        return r;
    }
    Vec3 transformPoint(const Vec3& v) const {
        return {at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z + at(0, 3),
                at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z + at(1, 3),
                at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z + at(2, 3)};
    }
    Vec3 transformDir(const Vec3& v) const {
        return {at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z,
                at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z,
                at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z};
    }
    Vec3 translation() const { return {at(0, 3), at(1, 3), at(2, 3)}; }
    Mat3 linear() const {
        return Mat3::fromColumns({at(0, 0), at(1, 0), at(2, 0)}, {at(0, 1), at(1, 1), at(2, 1)},
                                 {at(0, 2), at(1, 2), at(2, 2)});
    }
    static Mat4 fromLinearTranslation(const Mat3& l, const Vec3& t) {
        Mat4 r;
        for (int c = 0; c < 3; ++c) {
            r.at(0, c) = l.c[c].x; r.at(1, c) = l.c[c].y; r.at(2, c) = l.c[c].z; r.at(3, c) = 0;
        }
        r.at(0, 3) = t.x; r.at(1, 3) = t.y; r.at(2, 3) = t.z; r.at(3, 3) = 1;
        return r;
    }
    // General 4x4 inverse (cofactor expansion).
    Mat4 inverse() const {
        const float* a = m;
        float inv[16];
        inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
        inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
        inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
        inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
        inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
        inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
        inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
        inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
        inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
        inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
        inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
        inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
        inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
        inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
        inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
        inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
        float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
        Mat4 r;
        if (std::fabs(det) < 1e-30f) return identity();
        float id = 1.0f / det;
        for (int i = 0; i < 16; ++i) r.m[i] = inv[i] * id;
        return r;
    }
    static Mat4 fromTRS(const Vec3& t, const Vec4& q, const Vec3& s) {
        float x = q.x, y = q.y, z = q.z, w = q.w;
        Mat3 r = Mat3::fromColumns({1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)},
                                   {2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)},
                                   {2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)});
        r.c[0] = r.c[0] * s.x; r.c[1] = r.c[1] * s.y; r.c[2] = r.c[2] * s.z;
        return fromLinearTranslation(r, t);
    }
};

inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

} // namespace vh
