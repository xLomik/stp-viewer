#include "measure.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace stp {
namespace {

double axisOf(const Vec3& v, int axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }

bool rayHitsBox(const Ray& ray, const Vec3& lo, const Vec3& hi) {
    double t0 = ray.ortho ? -1e300 : 0.0, t1 = 1e300;
    for (int axis = 0; axis < 3; ++axis) {
        const double o = axisOf(ray.origin, axis), d = axisOf(ray.dir, axis);
        const double l = axisOf(lo, axis), h = axisOf(hi, axis);
        if (std::fabs(d) < 1e-300) {
            if (o < l || o > h) return false;
            continue;
        }
        double tn = (l - o) / d, tf = (h - o) / d;
        if (tn > tf) std::swap(tn, tf);
        t0 = std::max(t0, tn);
        t1 = std::min(t1, tf);
        if (t0 > t1) return false;
    }
    return true;
}

// Punto del segmento ab mas cercano a la recta del rayo.
Vec3 closestOnSegment(const Vec3& a, const Vec3& b, const Ray& ray) {
    const Vec3 u = b - a, w = a - ray.origin;
    const double A = dot(u, u), B = dot(u, ray.dir), C = dot(ray.dir, ray.dir);
    const double D = dot(u, w), E = dot(ray.dir, w);
    const double den = A * C - B * B;
    double s = (A > 1e-300 && den > 1e-18 * A * C) ? (B * E - C * D) / den : 0.0;
    s = std::max(0.0, std::min(1.0, s));
    return a + u * s;
}

struct PointKey {
    long long x, y, z;
    bool operator==(const PointKey& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct PointKeyHash {
    std::size_t operator()(const PointKey& k) const {
        return static_cast<std::size_t>(k.x * 73856093LL ^ k.y * 19349663LL ^ k.z * 83492791LL);
    }
};

}  // namespace

Ray pixelRay(const Camera& camera, int width, int height, double sx, double sy) {
    const double aspect = height > 0 ? static_cast<double>(width) / height : 1.0;
    const double nx = (sx / std::max(1, width) - 0.5) * 2.0;
    const double ny = (0.5 - sy / std::max(1, height)) * 2.0;
    const Vec3 right = camera.right(), up = camera.up(), forward = camera.forward();
    Ray ray;
    ray.ortho = camera.ortho;
    if (camera.ortho) {
        const double halfH = camera.orthoHeight * 0.5;
        ray.origin = camera.eye() + right * (nx * halfH * aspect) + up * (ny * halfH);
        ray.dir = forward;
    } else {
        const double t = std::tan(camera.fov * 0.5);
        ray.origin = camera.eye();
        ray.dir = normalize(forward + right * (nx * t * aspect) + up * (ny * t));
    }
    return ray;
}

double worldPerPixel(const Camera& camera, int height, double t) {
    const double h = std::max(1, height);
    if (camera.ortho) return camera.orthoHeight / h;
    return 2.0 * std::tan(camera.fov * 0.5) * std::max(t, 0.0) / h;
}

bool rayTriangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c, double* t) {
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 p = cross(ray.dir, e2);
    const double det = dot(e1, p);
    if (std::fabs(det) < 1e-300) return false;
    const double inv = 1.0 / det;
    const Vec3 s = ray.origin - a;
    const double u = dot(s, p) * inv;
    if (u < -1e-12 || u > 1 + 1e-12) return false;
    const Vec3 q = cross(s, e1);
    const double v = dot(ray.dir, q) * inv;
    if (v < -1e-12 || u + v > 1 + 1e-12) return false;
    const double hit = dot(e2, q) * inv;
    if (!ray.ortho && hit <= 1e-12) return false;
    *t = hit;
    return true;
}

void PickIndex::build(const Mesh& mesh) {
    m_size = std::max(1e-9, mesh.bounds.valid() ? mesh.bounds.diagonal() : 1.0);

    m_triangles.build(mesh.triangleCount(), [&](std::size_t i, Vec3* lo, Vec3* hi) {
        BBox box;
        for (int k = 0; k < 3; ++k) box.add(mesh.positions[mesh.indices[3 * i + k]]);
        *lo = box.lo;
        *hi = box.hi;
    });
    m_segments.build(mesh.edgeLines.size() / 2, [&](std::size_t i, Vec3* lo, Vec3* hi) {
        BBox box;
        box.add(mesh.edgeLines[2 * i]);
        box.add(mesh.edgeLines[2 * i + 1]);
        *lo = box.lo;
        *hi = box.hi;
    });

    // Puntos notables: extremos y medios de las rectas, centros y extremos de arcos.
    m_snapPoints.clear();
    const double quantum = m_size * 1e-9;
    std::unordered_set<PointKey, PointKeyHash> seen;
    auto add = [&](const Vec3& p, SnapKind kind, int segment, int circle) {
        const PointKey key{std::llround(p.x / quantum), std::llround(p.y / quantum),
                           std::llround(p.z / quantum)};
        if (!seen.insert(key).second) return;
        m_snapPoints.push_back({p, kind, segment, circle});
    };
    const std::size_t segments = mesh.edgeLines.size() / 2;
    for (std::size_t i = 0; i < segments; ++i) {
        if (i < mesh.edgeCurve.size() && mesh.edgeCurve[i]) continue;
        const Vec3& a = mesh.edgeLines[2 * i];
        const Vec3& b = mesh.edgeLines[2 * i + 1];
        add(a, SnapKind::Endpoint, static_cast<int>(i), -1);
        add(b, SnapKind::Endpoint, static_cast<int>(i), -1);
    }
    for (std::size_t i = 0; i < segments; ++i) {
        if (i < mesh.edgeCurve.size() && mesh.edgeCurve[i]) continue;
        add((mesh.edgeLines[2 * i] + mesh.edgeLines[2 * i + 1]) * 0.5, SnapKind::Midpoint,
            static_cast<int>(i), -1);
    }
    for (std::size_t i = 0; i < mesh.features.circles.size(); ++i) {
        const CircleFeature& c = mesh.features.circles[i];
        add(c.center, SnapKind::Center, -1, static_cast<int>(i));
        if (!c.full()) {
            add(c.pointAt(c.startAngle), SnapKind::Endpoint, -1, static_cast<int>(i));
            add(c.pointAt(c.startAngle + c.sweep), SnapKind::Endpoint, -1, static_cast<int>(i));
        }
    }
    m_points.build(m_snapPoints.size(), [&](std::size_t i, Vec3* lo, Vec3* hi) {
        *lo = m_snapPoints[i].point;
        *hi = m_snapPoints[i].point;
    });
}

bool PickIndex::raycast(const Mesh& mesh, const Ray& ray, double* t, int* triangle) const {
    double best = 1e300;
    int bestTriangle = -1;
    m_triangles.query([&](const Vec3& lo, const Vec3& hi) { return rayHitsBox(ray, lo, hi); },
                      [&](std::uint32_t i) {
                          double hit;
                          if (rayTriangle(ray, mesh.positions[mesh.indices[3 * i]],
                                          mesh.positions[mesh.indices[3 * i + 1]],
                                          mesh.positions[mesh.indices[3 * i + 2]], &hit) &&
                              hit < best) {
                              best = hit;
                              bestTriangle = static_cast<int>(i);
                          }
                      });
    if (bestTriangle < 0) return false;
    *t = best;
    *triangle = bestTriangle;
    return true;
}

SnapResult PickIndex::snap(const Mesh& mesh, const Camera& camera, int width, int height,
                           double sx, double sy, const SnapOptions& options) const {
    SnapResult result;
    const Ray ray = pixelRay(camera, width, height, sx, sy);
    double hitT = 0.0;
    int hitTriangle = -1;
    const bool hit = raycast(mesh, ray, &hitT, &hitTriangle);
    result.triangle = hit ? hitTriangle : -1;

    // Un punto tapado por la pieza no se engancha.
    const double occlusion = m_size * 1e-3;
    auto visible = [&](const Vec3& p) { return !hit || dot(p - ray.origin, ray.dir) <= hitT + occlusion; };
    auto pixels = [&](const Vec3& p, double* d) {
        double px, py;
        if (!projectPoint(camera, width, height, p, &px, &py)) return false;
        *d = std::hypot(px - sx, py - sy);
        return true;
    };
    auto enter = [&](const Vec3& lo, const Vec3& hi) {
        const Vec3 center = (lo + hi) * 0.5;
        const double t = std::max(0.0, dot(center - ray.origin, ray.dir)) + length(hi - lo) * 0.5;
        const double r = options.radiusPixels * worldPerPixel(camera, height, t);
        return rayHitsBox(ray, lo - Vec3(r, r, r), hi + Vec3(r, r, r));
    };

    if (options.snapping) {
        int bestPriority = 99;
        double bestDistance = 1e300;
        const SnapPoint* best = nullptr;
        m_points.query(enter, [&](std::uint32_t i) {
            const SnapPoint& s = m_snapPoints[i];
            double d;
            if (!pixels(s.point, &d) || d > options.radiusPixels || !visible(s.point)) return;
            const int priority = s.kind == SnapKind::Endpoint ? 0 : (s.kind == SnapKind::Center ? 1 : 2);
            if (priority < bestPriority || (priority == bestPriority && d < bestDistance)) {
                bestPriority = priority;
                bestDistance = d;
                best = &s;
            }
        });
        if (best) {
            result.kind = best->kind;
            result.point = best->point;
            result.segment = best->segment;
            result.circle = best->circle;
            return result;
        }

        double bestEdge = 1e300;
        m_segments.query(enter, [&](std::uint32_t i) {
            const Vec3 q = closestOnSegment(mesh.edgeLines[2 * i], mesh.edgeLines[2 * i + 1], ray);
            double d;
            if (!pixels(q, &d) || d > options.radiusPixels || !visible(q) || d >= bestEdge) return;
            bestEdge = d;
            result.kind = SnapKind::OnEdge;
            result.point = q;
            result.segment = static_cast<int>(i);
        });
        if (result.kind == SnapKind::OnEdge) return result;
    }

    if (hit) {
        result.kind = SnapKind::OnFace;
        result.point = ray.origin + ray.dir * hitT;
        return result;
    }
    if (options.plane && options.plane->planar) {
        const double denominator = dot(ray.dir, options.plane->normal);
        if (std::fabs(denominator) > 1e-12) {
            const double t = dot(options.plane->center - ray.origin, options.plane->normal) / denominator;
            result.kind = SnapKind::OnPlane;
            result.point = ray.origin + ray.dir * t;
        }
    }
    return result;
}

}  // namespace stp
