#include "view_cube_math.h"

#include <algorithm>
#include <cmath>

namespace stp {
namespace ui {
namespace {
constexpr double kPitchLimit = 1.5533430;  // 89 grados, el de la orbita del visor
constexpr double kBand = 0.6;              // mas alla, el clic es de arista o esquina

double axis(const Vec3& v, int k) { return k == 0 ? v.x : (k == 1 ? v.y : v.z); }
}  // namespace

CubeRegion cubeRegionAt(const Camera& camera, double dx, double dy, double halfSize) {
    CubeRegion region;
    if (halfSize <= 0) return region;
    const Vec3 right = camera.right(), up = camera.up(), forward = camera.forward();
    const Vec3 origin = right * (dx / halfSize) - up * (dy / halfSize) - forward * 4.0;
    double t0 = -1e300, t1 = 1e300;
    for (int k = 0; k < 3; ++k) {
        const double o = axis(origin, k), d = axis(forward, k);
        if (std::fabs(d) < 1e-12) {
            if (o < -1 || o > 1) return region;
            continue;
        }
        double tn = (-1 - o) / d, tf = (1 - o) / d;
        if (tn > tf) std::swap(tn, tf);
        t0 = std::max(t0, tn);
        t1 = std::min(t1, tf);
        if (t0 > t1) return region;
    }
    const Vec3 p = origin + forward * t0;
    int face = 0;
    for (int k = 1; k < 3; ++k) {
        if (std::fabs(axis(p, k)) > std::fabs(axis(p, face))) face = k;
    }
    int out[3];
    for (int k = 0; k < 3; ++k) {
        const double c = axis(p, k);
        out[k] = (k == face || std::fabs(c) > kBand) ? (c > 0 ? 1 : -1) : 0;
    }
    region.x = out[0];
    region.y = out[1];
    region.z = out[2];
    return region;
}

void cubeOrientation(const CubeRegion& region, double* yaw, double* pitch) {
    const Vec3 dir = normalize(Vec3(region.x, region.y, region.z));
    *pitch = std::max(-kPitchLimit, std::min(kPitchLimit, std::asin(dir.z)));
    *yaw = std::hypot(dir.x, dir.y) > 1e-9 ? std::atan2(dir.y, dir.x) : -kPi / 2;
}

const char* cubeFaceName(int x, int y, int z) {
    if (std::abs(x) + std::abs(y) + std::abs(z) != 1) return nullptr;
    if (x > 0) return "Derecha";
    if (x < 0) return "Izquierda";
    if (y > 0) return "Atr\u00e1s";
    if (y < 0) return "Frente";
    return z > 0 ? "Superior" : "Inferior";
}

void interpolateOrientation(double yaw0, double pitch0, double yaw1, double pitch1, double t, double* yaw,
                            double* pitch) {
    t = std::max(0.0, std::min(1.0, t));
    const double s = t * t * (3 - 2 * t);  // arranca y frena suave
    double dy = std::fmod(yaw1 - yaw0, 2 * kPi);
    if (dy > kPi) dy -= 2 * kPi;
    if (dy < -kPi) dy += 2 * kPi;
    *yaw = t >= 1.0 ? yaw1 : yaw0 + dy * s;
    *pitch = t >= 1.0 ? pitch1 : pitch0 + (pitch1 - pitch0) * s;
}

}  // namespace ui
}  // namespace stp
