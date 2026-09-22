#include "surfaces.h"

namespace stp {

Vec3 Surface::eval(double u, double v) const {
    switch (type) {
        case Type::Plane:
        case Type::Freeform:
            return frame.origin + frame.x * u + frame.y * v;
        case Type::Cylinder: {
            const Vec3 radial = frame.x * std::cos(u) + frame.y * std::sin(u);
            return frame.origin + radial * radius + frame.z * v;
        }
        case Type::Cone: {
            const Vec3 radial = frame.x * std::cos(u) + frame.y * std::sin(u);
            const double r = radius + v * std::tan(halfAngle);
            return frame.origin + radial * r + frame.z * v;
        }
        case Type::Sphere: {
            const Vec3 radial = frame.x * std::cos(u) + frame.y * std::sin(u);
            return frame.origin + radial * (radius * std::cos(v)) + frame.z * (radius * std::sin(v));
        }
        case Type::Torus: {
            const Vec3 radial = frame.x * std::cos(u) + frame.y * std::sin(u);
            const double r = radius + minorRadius * std::cos(v);
            return frame.origin + radial * r + frame.z * (minorRadius * std::sin(v));
        }
    }
    return frame.origin;
}

Vec3 Surface::normal(double u, double v) const {
    Vec3 n;
    switch (type) {
        case Type::Plane:
        case Type::Freeform:
            n = frame.z;
            break;
        case Type::Cylinder:
            n = frame.x * std::cos(u) + frame.y * std::sin(u);
            break;
        case Type::Cone: {
            const Vec3 radial = frame.x * std::cos(u) + frame.y * std::sin(u);
            n = radial * std::cos(halfAngle) - frame.z * std::sin(halfAngle);
            break;
        }
        case Type::Sphere:
            n = normalize(eval(u, v) - frame.origin);
            break;
        case Type::Torus: {
            const Vec3 radial = frame.x * std::cos(u) + frame.y * std::sin(u);
            n = radial * std::cos(v) + frame.z * std::sin(v);
            break;
        }
    }
    n = normalize(n);
    return flipped ? -n : n;
}

Vec2 Surface::invert(const Vec3& p) const {
    const Vec3 d = p - frame.origin;
    const double lx = dot(d, frame.x);
    const double ly = dot(d, frame.y);
    const double lz = dot(d, frame.z);
    switch (type) {
        case Type::Plane:
        case Type::Freeform:
            return Vec2(lx, ly);
        case Type::Cylinder:
        case Type::Cone:
            return Vec2(std::atan2(ly, lx), lz);
        case Type::Sphere: {
            const double r = radius > 1e-12 ? radius : 1.0;
            const double s = std::max(-1.0, std::min(1.0, lz / r));
            return Vec2(std::atan2(ly, lx), std::asin(s));
        }
        case Type::Torus: {
            const double rho = std::sqrt(lx * lx + ly * ly) - radius;
            return Vec2(std::atan2(ly, lx), std::atan2(lz, rho));
        }
    }
    return Vec2(lx, ly);
}

}  // namespace stp
