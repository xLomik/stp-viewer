// Basic 3D math shared by the STEP engine, the renderer and the apps.
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace stp {

constexpr double kPi = 3.14159265358979323846;

struct Vec2 {
    double x = 0.0, y = 0.0;
    Vec2() = default;
    Vec2(double X, double Y) : x(X), y(Y) {}
};

inline Vec2 operator+(const Vec2& a, const Vec2& b) { return Vec2(a.x + b.x, a.y + b.y); }
inline Vec2 operator-(const Vec2& a, const Vec2& b) { return Vec2(a.x - b.x, a.y - b.y); }
inline Vec2 operator*(const Vec2& a, double s) { return Vec2(a.x * s, a.y * s); }
inline double cross2(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
inline double length(const Vec2& a) { return std::sqrt(a.x * a.x + a.y * a.y); }

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
    Vec3() = default;
    Vec3(double X, double Y, double Z) : x(X), y(Y), z(Z) {}
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return Vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return Vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vec3 operator-(const Vec3& a) { return Vec3(-a.x, -a.y, -a.z); }
inline Vec3 operator*(const Vec3& a, double s) { return Vec3(a.x * s, a.y * s, a.z * s); }
inline Vec3 operator*(double s, const Vec3& a) { return a * s; }
inline Vec3& operator+=(Vec3& a, const Vec3& b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline double length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline double distance(const Vec3& a, const Vec3& b) { return length(a - b); }

inline Vec3 normalize(const Vec3& a) {
    const double n = length(a);
    return n > 1e-300 ? a * (1.0 / n) : Vec3(0, 0, 1);
}

// Right-handed orthonormal frame, as produced by AXIS2_PLACEMENT_3D.
struct Frame {
    Vec3 origin;
    Vec3 x{1, 0, 0};
    Vec3 y{0, 1, 0};
    Vec3 z{0, 0, 1};

    Vec3 toWorld(const Vec3& local) const {
        return origin + x * local.x + y * local.y + z * local.z;
    }
    Vec3 dirToWorld(const Vec3& local) const {
        return x * local.x + y * local.y + z * local.z;
    }
    Vec3 toLocal(const Vec3& world) const {
        const Vec3 d = world - origin;
        return Vec3(dot(d, x), dot(d, y), dot(d, z));
    }
};

// Builds the third axis and orthonormalizes; mirrors the STEP placement rules.
inline Frame makeFrame(const Vec3& origin, const Vec3& axisZ, const Vec3& refX) {
    Frame f;
    f.origin = origin;
    f.z = normalize(axisZ);
    Vec3 x = refX - f.z * dot(refX, f.z);
    if (length(x) < 1e-12) {
        const Vec3 alt = std::fabs(f.z.x) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
        x = alt - f.z * dot(alt, f.z);
    }
    f.x = normalize(x);
    f.y = cross(f.z, f.x);
    return f;
}

// Affine transform (row-major 3x4 with implicit bottom row).
struct Mat4 {
    double m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};

    static Mat4 identity() { return Mat4(); }

    Vec3 point(const Vec3& p) const {
        return Vec3(m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3],
                    m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
                    m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]);
    }
    Vec3 direction(const Vec3& d) const {
        return Vec3(m[0] * d.x + m[1] * d.y + m[2] * d.z,
                    m[4] * d.x + m[5] * d.y + m[6] * d.z,
                    m[8] * d.x + m[9] * d.y + m[10] * d.z);
    }
    bool isIdentity() const {
        static const Mat4 id;
        for (int i = 0; i < 12; ++i) {
            if (std::fabs(m[i] - id.m[i]) > 1e-15) return false;
        }
        return true;
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            r.m[row * 4 + col] = a.m[row * 4 + 0] * b.m[0 * 4 + col] +
                                 a.m[row * 4 + 1] * b.m[1 * 4 + col] +
                                 a.m[row * 4 + 2] * b.m[2 * 4 + col];
        }
        r.m[row * 4 + 3] = a.m[row * 4 + 0] * b.m[3] + a.m[row * 4 + 1] * b.m[7] +
                           a.m[row * 4 + 2] * b.m[11] + a.m[row * 4 + 3];
    }
    return r;
}

// Frame interpreted as a placement: maps local coordinates into world space.
inline Mat4 frameToMatrix(const Frame& f) {
    Mat4 t;
    t.m[0] = f.x.x; t.m[1] = f.y.x; t.m[2] = f.z.x; t.m[3] = f.origin.x;
    t.m[4] = f.x.y; t.m[5] = f.y.y; t.m[6] = f.z.y; t.m[7] = f.origin.y;
    t.m[8] = f.x.z; t.m[9] = f.y.z; t.m[10] = f.z.z; t.m[11] = f.origin.z;
    return t;
}

inline Mat4 inverseRigid(const Mat4& a) {
    Mat4 r;
    r.m[0] = a.m[0]; r.m[1] = a.m[4]; r.m[2] = a.m[8];
    r.m[4] = a.m[1]; r.m[5] = a.m[5]; r.m[6] = a.m[9];
    r.m[8] = a.m[2]; r.m[9] = a.m[6]; r.m[10] = a.m[10];
    const Vec3 t(a.m[3], a.m[7], a.m[11]);
    r.m[3] = -(r.m[0] * t.x + r.m[1] * t.y + r.m[2] * t.z);
    r.m[7] = -(r.m[4] * t.x + r.m[5] * t.y + r.m[6] * t.z);
    r.m[11] = -(r.m[8] * t.x + r.m[9] * t.y + r.m[10] * t.z);
    return r;
}

struct BBox {
    Vec3 lo{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max()};
    Vec3 hi{-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
            -std::numeric_limits<double>::max()};

    bool valid() const { return hi.x >= lo.x && hi.y >= lo.y && hi.z >= lo.z; }
    void add(const Vec3& p) {
        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
    }
    Vec3 center() const { return (lo + hi) * 0.5; }
    Vec3 size() const { return valid() ? hi - lo : Vec3(0, 0, 0); }
    double diagonal() const { return valid() ? length(hi - lo) : 0.0; }
};

}  // namespace stp
