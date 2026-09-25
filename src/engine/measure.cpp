#include "measure.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <unordered_map>
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

int snapPriority(SnapKind kind) {
    switch (kind) {
        case SnapKind::Intersection: return 0;
        case SnapKind::Endpoint: return 1;
        case SnapKind::Center: return 2;
        case SnapKind::Quadrant: return 3;
        case SnapKind::Midpoint: return 4;
        case SnapKind::Perpendicular: return 5;
        case SnapKind::Tangent: return 6;
        case SnapKind::Extension: return 7;
        default: return 8;  // Mas cercano
    }
}

// true si p (sobre el circulo) cae dentro del barrido del arco, con tolerance
// en unidades del modelo medida a lo largo de la curva.
bool inArc(const CircleFeature& c, const Vec3& p, double tolerance) {
    if (c.full()) return true;
    const Vec3 d = p - c.center;
    const Vec3 inPlane = d - c.normal * dot(d, c.normal);
    if (length(inPlane) < 1e-300) return false;
    const Vec3 y = cross(c.normal, c.xAxis);
    double a = std::atan2(dot(inPlane, y), dot(inPlane, c.xAxis)) - c.startAngle;
    if (c.sweep < 0) a = -a;
    a = std::fmod(a, 2 * kPi);
    if (a < 0) a += 2 * kPi;
    const double slack = c.radius > 0 ? tolerance / c.radius : 0.0;
    return a <= std::fabs(c.sweep) + slack || a >= 2 * kPi - slack;
}

// Ejes del plano del circulo para los cuadrantes: X del mundo proyectado (o Y
// si X es perpendicular al plano) y su giro de 90 grados.
void quadrantAxes(const CircleFeature& c, Vec3* e1, Vec3* e2) {
    const Vec3 n = normalize(c.normal);
    Vec3 ref = Vec3(1, 0, 0) - n * n.x;
    if (length(ref) < 1e-6) ref = Vec3(0, 1, 0) - n * n.y;
    *e1 = normalize(ref);
    *e2 = cross(n, *e1);
}

// Punto del circulo (o del arco) mas cercano al rayo. false si el circulo se ve de canto.
bool closestOnCircle(const CircleFeature& c, const Ray& ray, Vec3* out) {
    const double den = dot(ray.dir, c.normal);
    if (std::fabs(den) < 1e-9) return false;
    const Vec3 p = ray.origin + ray.dir * (dot(c.center - ray.origin, c.normal) / den);
    Vec3 d = p - c.center;
    d = d - c.normal * dot(d, c.normal);
    if (length(d) < 1e-300) return false;
    Vec3 q = c.center + normalize(d) * c.radius;
    if (!inArc(c, q, 0.0)) {
        const Vec3 s = c.pointAt(c.startAngle), e = c.pointAt(c.startAngle + c.sweep);
        q = distance(q, s) < distance(q, e) ? s : e;
    }
    *out = q;
    return true;
}

// Cruce de las rectas p0 + s*u y q0 + t*v. false si son paralelas o no se tocan.
bool crossLines(const Vec3& p0, const Vec3& u, const Vec3& q0, const Vec3& v, double tolerance,
                double* s, double* t) {
    const double A = dot(u, u), B = dot(u, v), C = dot(v, v);
    const double den = A * C - B * B;
    if (A < 1e-300 || C < 1e-300 || den <= 1e-12 * A * C) return false;
    const Vec3 w = p0 - q0;
    const double D = dot(u, w), E = dot(v, w);
    *s = (B * E - C * D) / den;
    *t = (A * E - B * D) / den;
    return distance(p0 + u * *s, q0 + v * *t) <= tolerance;
}

// Cruces del tramo ab con el circulo (o arco) c en su plano. Devuelve cuantos.
int segmentCircle(const Vec3& a, const Vec3& b, const CircleFeature& c, double tolerance, Vec3 out[2]) {
    if (std::fabs(dot(a - c.center, c.normal)) > tolerance ||
        std::fabs(dot(b - c.center, c.normal)) > tolerance) {
        return 0;
    }
    const Vec3 d = b - a;
    const double A = dot(d, d);
    if (A < 1e-300) return 0;
    const double length = std::sqrt(A);
    const double s0 = dot(c.center - a, d) / A;
    const double h = distance(a + d * s0, c.center);
    if (h > c.radius + tolerance) return 0;
    const double half = std::sqrt(std::max(0.0, c.radius * c.radius - h * h)) / length;
    const double slack = tolerance / length;
    const double roots[2] = {s0 - half, s0 + half};
    int count = 0;
    for (int k = 0; k < (half * length < tolerance ? 1 : 2); ++k) {
        const double s = roots[k];
        if (s < -slack || s > 1 + slack) continue;
        const Vec3 p = a + d * s;
        if (inArc(c, p, tolerance)) out[count++] = p;
    }
    return count;
}

// Cruces de dos circulos (o arcos) del mismo plano. Devuelve cuantos.
int circleCircle(const CircleFeature& c1, const CircleFeature& c2, double tolerance, Vec3 out[2]) {
    if (length(cross(c1.normal, c2.normal)) > 1e-9 ||
        std::fabs(dot(c2.center - c1.center, c1.normal)) > tolerance) {
        return 0;
    }
    const Vec3 between = c2.center - c1.center;
    const double d = length(between);
    if (d < tolerance || d > c1.radius + c2.radius + tolerance ||
        d < std::fabs(c1.radius - c2.radius) - tolerance) {
        return 0;
    }
    const Vec3 e = between * (1.0 / d);
    const double a = (c1.radius * c1.radius - c2.radius * c2.radius + d * d) / (2 * d);
    const double h = std::sqrt(std::max(0.0, c1.radius * c1.radius - a * a));
    const Vec3 base = c1.center + e * a;
    const Vec3 side = normalize(cross(c1.normal, e));
    const Vec3 points[2] = {base + side * h, base - side * h};
    int count = 0;
    for (int k = 0; k < (h < tolerance ? 1 : 2); ++k) {
        if (inArc(c1, points[k], tolerance) && inArc(c2, points[k], tolerance)) out[count++] = points[k];
    }
    return count;
}

struct PointKey {
    long long x, y, z;
    int kind;
    bool operator==(const PointKey& o) const { return x == o.x && y == o.y && z == o.z && kind == o.kind; }
};
struct PointKeyHash {
    std::size_t operator()(const PointKey& k) const {
        return static_cast<std::size_t>(k.x * 73856093LL ^ k.y * 19349663LL ^ k.z * 83492791LL ^
                                        k.kind * 2654435761LL);
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

    // Puntos notables: extremos y medios de rectas y arcos, centros y cuadrantes.
    m_snapPoints.clear();
    const double quantum = m_size * 1e-9;
    std::unordered_set<PointKey, PointKeyHash> seen;
    auto add = [&](const Vec3& p, SnapKind kind, int segment, int circle) {
        const PointKey key{std::llround(p.x / quantum), std::llround(p.y / quantum),
                           std::llround(p.z / quantum), static_cast<int>(kind)};
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
        const int index = static_cast<int>(i);
        add(c.center, SnapKind::Center, -1, index);
        if (!c.full()) {
            add(c.pointAt(c.startAngle), SnapKind::Endpoint, -1, index);
            add(c.pointAt(c.startAngle + c.sweep), SnapKind::Endpoint, -1, index);
            add(c.pointAt(c.startAngle + c.sweep * 0.5), SnapKind::Midpoint, -1, index);
        }
        Vec3 e1, e2;
        quadrantAxes(c, &e1, &e2);
        const Vec3 quadrants[4] = {c.center + e1 * c.radius, c.center + e2 * c.radius,
                                   c.center - e1 * c.radius, c.center - e2 * c.radius};
        for (const Vec3& q : quadrants) {
            if (inArc(c, q, m_size * 1e-9)) add(q, SnapKind::Quadrant, -1, index);
        }
    }
    m_circles.build(mesh.features.circles.size(), [&](std::size_t i, Vec3* lo, Vec3* hi) {
        const CircleFeature& c = mesh.features.circles[i];
        const Vec3 r(c.radius, c.radius, c.radius);
        *lo = c.center - r;
        *hi = c.center + r;
    });
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

unsigned snapModeOf(SnapKind kind) {
    switch (kind) {
        case SnapKind::Endpoint: return kSnapEndpoint;
        case SnapKind::Midpoint: return kSnapMidpoint;
        case SnapKind::Center: return kSnapCenter;
        case SnapKind::Quadrant: return kSnapQuadrant;
        case SnapKind::Intersection: return kSnapIntersection;
        case SnapKind::Extension: return kSnapExtension;
        case SnapKind::Perpendicular: return kSnapPerpendicular;
        case SnapKind::Tangent: return kSnapTangent;
        case SnapKind::OnEdge: return kSnapNearest;
        default: return 0;
    }
}

const char* snapKindName(SnapKind kind) {
    switch (kind) {
        case SnapKind::Endpoint: return "Extremo";
        case SnapKind::Midpoint: return "Punto medio";
        case SnapKind::Center: return "Centro";
        case SnapKind::Quadrant: return "Cuadrante";
        case SnapKind::Intersection: return "Intersección";
        case SnapKind::Extension: return "Extensión";
        case SnapKind::Perpendicular: return "Perpendicular";
        case SnapKind::Tangent: return "Tangente";
        case SnapKind::OnEdge: return "Más cercano";
        default: return "";
    }
}

SnapResult PickIndex::surfaceAt(const Mesh& mesh, const Camera& camera, int width, int height,
                                double sx, double sy, const PlanarInfo* plane) const {
    SnapResult result;
    const Ray ray = pixelRay(camera, width, height, sx, sy);
    double hitT = 0.0;
    int hitTriangle = -1;
    if (raycast(mesh, ray, &hitT, &hitTriangle)) {
        result.kind = SnapKind::OnFace;
        result.triangle = hitTriangle;
        result.point = ray.origin + ray.dir * hitT;
        return result;
    }
    if (plane && plane->planar) {
        const double denominator = dot(ray.dir, plane->normal);
        if (std::fabs(denominator) > 1e-12) {
            const double t = dot(plane->center - ray.origin, plane->normal) / denominator;
            result.kind = SnapKind::OnPlane;
            result.point = ray.origin + ray.dir * t;
        }
    }
    return result;
}

SnapResult PickIndex::snap(const Mesh& mesh, const Camera& camera, int width, int height,
                           double sx, double sy, const SnapOptions& options) const {
    const std::vector<SnapResult> all = snapAll(mesh, camera, width, height, sx, sy, options);
    if (!all.empty()) return all.front();
    return surfaceAt(mesh, camera, width, height, sx, sy, options.plane);
}

std::vector<SnapResult> PickIndex::snapAll(const Mesh& mesh, const Camera& camera, int width,
                                           int height, double sx, double sy,
                                           const SnapOptions& options) const {
    std::vector<SnapResult> found;
    const unsigned modes = options.modes;
    if (!options.snapping || modes == 0) return found;
    const Ray ray = pixelRay(camera, width, height, sx, sy);
    double hitT = 0.0;
    int hitTriangle = -1;
    const int triangle = raycast(mesh, ray, &hitT, &hitTriangle) ? hitTriangle : -1;
    const double R = options.radiusPixels;
    const double tolerance = m_size * 1e-7;

    // Un punto tapado por la pieza no se engancha. Se prueba con el rayo que pasa
    // justo por el punto, no con el del cursor: cerca de una cara vista de canto el
    // rayo del cursor la toca bastante antes que una esquina que si se ve.
    const double occlusion = m_size * 1e-4;
    auto visible = [&](const Vec3& p) {
        double px, py;
        if (!projectPoint(camera, width, height, p, &px, &py)) return false;
        const Ray through = pixelRay(camera, width, height, px, py);
        double t = 0.0;
        int hidden = -1;
        if (!raycast(mesh, through, &t, &hidden)) return true;
        return dot(p - through.origin, through.dir) <= t + occlusion;
    };
    auto pixels = [&](const Vec3& p, double* d) {
        double px, py;
        if (!projectPoint(camera, width, height, p, &px, &py)) return false;
        *d = std::hypot(px - sx, py - sy);
        return true;
    };
    // Cajas que pasan a menos de factor * R pixeles del rayo del cursor.
    auto near = [&](double factor) {
        return [&, factor](const Vec3& lo, const Vec3& hi) {
            const Vec3 center = (lo + hi) * 0.5;
            const double t = std::max(0.0, dot(center - ray.origin, ray.dir)) + length(hi - lo) * 0.5;
            const double r = factor * R * worldPerPixel(camera, height, t);
            return rayHitsBox(ray, lo - Vec3(r, r, r), hi + Vec3(r, r, r));
        };
    };
    auto offer = [&](SnapKind kind, const Vec3& p, double d, int segment, int circle) {
        if (!(modes & snapModeOf(kind))) return false;
        for (SnapResult& s : found) {
            if (s.kind == kind && distance(s.point, p) <= tolerance) {
                s.pixels = std::min(s.pixels, d);
                return true;
            }
        }
        if (!visible(p)) return false;
        SnapResult r;
        r.kind = kind;
        r.point = p;
        r.triangle = triangle;
        r.segment = segment;
        r.circle = circle;
        r.pixels = d;
        found.push_back(r);
        return true;
    };
    auto straight = [&](std::size_t i) { return i >= mesh.edgeCurve.size() || !mesh.edgeCurve[i]; };

    // Puntos fijos: extremos, medios, centros y cuadrantes.
    if (modes & (kSnapEndpoint | kSnapMidpoint | kSnapCenter | kSnapQuadrant)) {
        m_points.query(near(1.0), [&](std::uint32_t i) {
            const SnapPoint& s = m_snapPoints[i];
            double d;
            if (!(modes & snapModeOf(s.kind)) || !pixels(s.point, &d) || d > R) return;
            offer(s.kind, s.point, d, s.segment, s.circle);
        });
    }

    // Elementos bajo el cursor, del mas cercano al mas lejano.
    struct Near {
        int index;
        double pixels;
        Vec3 point;
    };
    auto byPixels = [](const Near& a, const Near& b) { return a.pixels < b.pixels; };
    std::vector<Near> segments, circles;
    if (modes & (kSnapIntersection | kSnapPerpendicular | kSnapNearest)) {
        m_segments.query(near(1.0), [&](std::uint32_t i) {
            const Vec3 q = closestOnSegment(mesh.edgeLines[2 * i], mesh.edgeLines[2 * i + 1], ray);
            double d;
            if (pixels(q, &d) && d <= R) segments.push_back({static_cast<int>(i), d, q});
        });
        std::sort(segments.begin(), segments.end(), byPixels);
    }
    if (modes & (kSnapIntersection | kSnapPerpendicular | kSnapTangent)) {
        m_circles.query(near(1.0), [&](std::uint32_t i) {
            Vec3 q;
            double d;
            if (closestOnCircle(mesh.features.circles[i], ray, &q) && pixels(q, &d) && d <= R) {
                circles.push_back({static_cast<int>(i), d, q});
            }
        });
        std::sort(circles.begin(), circles.end(), byPixels);
        if (circles.size() > 16) circles.resize(16);
    }
    std::vector<Near> lines;
    for (const Near& n : segments) {
        if (straight(static_cast<std::size_t>(n.index)) && lines.size() < 32) lines.push_back(n);
    }
    auto lineA = [&](const Near& n) { return mesh.edgeLines[2 * n.index]; };
    auto lineB = [&](const Near& n) { return mesh.edgeLines[2 * n.index + 1]; };
    auto arcEnds = [&](const CircleFeature& c, const Vec3& p) {
        return !c.full() && (distance(p, c.pointAt(c.startAngle)) <= tolerance ||
                             distance(p, c.pointAt(c.startAngle + c.sweep)) <= tolerance);
    };
    auto offerCross = [&](const Vec3& p, int segment, int circle) {
        double d;
        if (pixels(p, &d) && d <= R) offer(SnapKind::Intersection, p, d, segment, circle);
    };

    if (modes & kSnapIntersection) {
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const Vec3 a1 = lineA(lines[i]), b1 = lineB(lines[i]);
            for (std::size_t j = i + 1; j < lines.size(); ++j) {
                const Vec3 a2 = lineA(lines[j]), b2 = lineB(lines[j]);
                double s, t;
                if (!crossLines(a1, b1 - a1, a2, b2 - a2, tolerance, &s, &t)) continue;
                const double e1 = tolerance / std::max(1e-300, distance(a1, b1));
                const double e2 = tolerance / std::max(1e-300, distance(a2, b2));
                if (s < -e1 || s > 1 + e1 || t < -e2 || t > 1 + e2) continue;
                const bool end1 = s <= e1 || s >= 1 - e1, end2 = t <= e2 || t >= 1 - e2;
                if (end1 && end2) continue;  // esquina de polilinea: ya es un extremo
                offerCross(a1 + (b1 - a1) * s, lines[i].index, -1);
            }
            for (const Near& c : circles) {
                const CircleFeature& circle = mesh.features.circles[c.index];
                Vec3 hits[2];
                const int count = segmentCircle(a1, b1, circle, tolerance, hits);
                for (int k = 0; k < count; ++k) {
                    const bool lineEnd = distance(hits[k], a1) <= tolerance || distance(hits[k], b1) <= tolerance;
                    if (lineEnd && arcEnds(circle, hits[k])) continue;  // union recta-arco
                    offerCross(hits[k], lines[i].index, c.index);
                }
            }
        }
        for (std::size_t i = 0; i < circles.size(); ++i) {
            const CircleFeature& c1 = mesh.features.circles[circles[i].index];
            for (std::size_t j = i + 1; j < circles.size(); ++j) {
                const CircleFeature& c2 = mesh.features.circles[circles[j].index];
                Vec3 hits[2];
                const int count = circleCircle(c1, c2, tolerance, hits);
                for (int k = 0; k < count; ++k) {
                    if (arcEnds(c1, hits[k]) && arcEnds(c2, hits[k])) continue;  // union arco-arco
                    offerCross(hits[k], -1, circles[i].index);
                }
            }
        }
    }

    if (options.from && (modes & kSnapPerpendicular)) {
        const Vec3& from = *options.from;
        for (const Near& n : lines) {
            const Vec3 a = lineA(n), u = lineB(n) - a;
            const double A = dot(u, u);
            if (A < 1e-300) continue;
            const Vec3 foot = a + u * (dot(from - a, u) / A);
            if (distance(foot, from) > tolerance) offer(SnapKind::Perpendicular, foot, n.pixels, n.index, -1);
        }
        for (const Near& n : circles) {
            const CircleFeature& c = mesh.features.circles[n.index];
            Vec3 d = from - c.center;
            d = d - c.normal * dot(d, c.normal);
            if (length(d) <= tolerance) continue;
            const Vec3 dir = normalize(d);
            const Vec3 feet[2] = {c.center + dir * c.radius, c.center - dir * c.radius};
            int best = -1;
            double bestPixels = 1e300;
            for (int k = 0; k < 2; ++k) {
                double px;
                if (inArc(c, feet[k], tolerance) && pixels(feet[k], &px) && px < bestPixels) {
                    bestPixels = px;
                    best = k;
                }
            }
            if (best >= 0) offer(SnapKind::Perpendicular, feet[best], n.pixels, -1, n.index);
        }
    }

    if (options.from && (modes & kSnapTangent)) {
        for (const Near& n : circles) {
            const CircleFeature& c = mesh.features.circles[n.index];
            const Vec3 fromInPlane = *options.from - c.normal * dot(*options.from - c.center, c.normal);
            const Vec3 v = fromInPlane - c.center;
            const double dist = length(v);
            if (dist <= c.radius + tolerance) continue;
            const Vec3 e = v * (1.0 / dist), side = cross(c.normal, e);
            const double cosA = c.radius / dist, sinA = std::sqrt(std::max(0.0, 1 - cosA * cosA));
            const Vec3 touch[2] = {c.center + (e * cosA + side * sinA) * c.radius,
                                   c.center + (e * cosA - side * sinA) * c.radius};
            int best = -1;
            double bestPixels = 1e300;
            for (int k = 0; k < 2; ++k) {
                double px;
                if (inArc(c, touch[k], tolerance) && pixels(touch[k], &px) && px < bestPixels) {
                    bestPixels = px;
                    best = k;
                }
            }
            if (best >= 0) offer(SnapKind::Tangent, touch[best], n.pixels, -1, n.index);
        }
    }

    if (modes & kSnapExtension) {
        // En una esquina redondeada las rectas terminan lejos del vertice: se buscan
        // hasta 12 radios, pero solo las que, prolongadas, pasan bajo el cursor.
        std::vector<Near> reach;
        m_segments.query(near(12.0), [&](std::uint32_t i) {
            if (!straight(i)) return;
            double ax, ay, bx, by;
            if (!projectPoint(camera, width, height, mesh.edgeLines[2 * i], &ax, &ay) ||
                !projectPoint(camera, width, height, mesh.edgeLines[2 * i + 1], &bx, &by)) {
                return;
            }
            const double lx = bx - ax, ly = by - ay, len = std::hypot(lx, ly);
            if (len < 1e-9 || std::fabs((sx - ax) * ly - (sy - ay) * lx) / len > R) return;
            const double s = std::max(0.0, std::min(1.0, ((sx - ax) * lx + (sy - ay) * ly) / (len * len)));
            const double d = std::hypot(ax + lx * s - sx, ay + ly * s - sy);
            if (d <= 12.0 * R) reach.push_back({static_cast<int>(i), d, Vec3()});
        });
        std::sort(reach.begin(), reach.end(), byPixels);
        if (reach.size() > 24) reach.resize(24);
        for (std::size_t i = 0; i < reach.size(); ++i) {
            const Vec3 a1 = lineA(reach[i]), b1 = lineB(reach[i]);
            for (std::size_t j = i + 1; j < reach.size(); ++j) {
                const Vec3 a2 = lineA(reach[j]), b2 = lineB(reach[j]);
                double s, t;
                if (!crossLines(a1, b1 - a1, a2, b2 - a2, tolerance, &s, &t)) continue;
                const double e1 = tolerance / std::max(1e-300, distance(a1, b1));
                const double e2 = tolerance / std::max(1e-300, distance(a2, b2));
                const bool inside = s >= -e1 && s <= 1 + e1 && t >= -e2 && t <= 1 + e2;
                if (inside) continue;  // es un cruce de verdad o un extremo
                const Vec3 p = a1 + (b1 - a1) * s;
                double d;
                if (pixels(p, &d) && d <= R) offer(SnapKind::Extension, p, d, reach[i].index, -1);
            }
        }
    }

    if (modes & kSnapNearest) {
        for (const Near& n : segments) {
            if (offer(SnapKind::OnEdge, n.point, n.pixels, n.index, -1)) break;
        }
    }

    // "Mas cercano" solo si no hay nada mejor; empate a 1,5 px: gana la prioridad.
    const bool other = std::any_of(found.begin(), found.end(),
                                   [](const SnapResult& s) { return s.kind != SnapKind::OnEdge; });
    if (other) {
        found.erase(std::remove_if(found.begin(), found.end(),
                                   [](const SnapResult& s) { return s.kind == SnapKind::OnEdge; }),
                    found.end());
    }
    double best = 1e300;
    for (const SnapResult& s : found) best = std::min(best, s.pixels);
    std::stable_sort(found.begin(), found.end(), [&](const SnapResult& a, const SnapResult& b) {
        const bool farA = a.pixels > best + 1.5, farB = b.pixels > best + 1.5;
        if (farA != farB) return !farA;
        const int pa = snapPriority(a.kind), pb = snapPriority(b.kind);
        if (pa != pb) return pa < pb;
        return a.pixels < b.pixels;
    });
    return found;
}

// --- Calculos ----------------------------------------------------------------

DistanceResult measureDistance(const Vec3& a, const Vec3& b, const PlanarInfo* plane) {
    DistanceResult result;
    const Vec3 d = b - a;
    result.total = length(d);
    result.delta = (plane && plane->planar) ? Vec3(dot(d, plane->u), dot(d, plane->v), 0.0) : d;
    return result;
}

ConstrainedPoint applyConstraint(const Vec3& from, const Vec3& to, const SnapConstraint& constraint,
                                 const Camera& camera, const PlanarInfo* plane) {
    ConstrainedPoint result;
    result.point = to;
    const Vec3 d = to - from;
    if (constraint.kind == ConstraintKind::None || length(d) < 1e-300) return result;
    const bool planar = plane && plane->planar;
    const Vec3 sx = planar ? plane->u : camera.right();
    const Vec3 sy = planar ? plane->v : camera.up();

    if (constraint.kind == ConstraintKind::Ortho) {
        if (planar) {
            const double du = dot(d, plane->u), dv = dot(d, plane->v);
            const bool horizontal = std::fabs(du) >= std::fabs(dv);
            result.point = from + (horizontal ? plane->u * du : plane->v * dv);
            result.axisName = horizontal ? "Horizontal" : "Vertical";
            result.applied = true;
            return result;
        }
        // Eje del mundo cuya imagen en pantalla apunta mas parecido al cursor.
        const Vec3 axes[3] = {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)};
        static const char* const names[3] = {"X", "Y", "Z"};
        const double mx = dot(d, sx), my = dot(d, sy), ml = std::hypot(mx, my);
        int best = -1;
        double bestCos = -1.0;
        for (int k = 0; k < 3; ++k) {
            const double ax = dot(axes[k], sx), ay = dot(axes[k], sy), al = std::hypot(ax, ay);
            if (al < 0.2) continue;  // eje casi de punta: no sirve para elegir con el cursor
            const double c = ml > 1e-300 ? std::fabs(ax * mx + ay * my) / (al * ml) : 0.0;
            if (c > bestCos) {
                bestCos = c;
                best = k;
            }
        }
        if (best < 0) return result;
        result.point = from + axes[best] * dot(d, axes[best]);
        result.axisName = names[best];
        result.applied = true;
        return result;
    }

    const double step = constraint.polarStepDegrees;
    if (!(step > 0.0 && step <= 180.0)) return result;
    const double x = dot(d, sx), y = dot(d, sy);
    if (std::hypot(x, y) < 1e-300) return result;
    double angle = std::atan2(y, x) * 180.0 / kPi;
    if (angle < 0) angle += 360.0;
    double snapped = std::round(angle / step) * step;
    if (std::fabs(angle - snapped) > 3.0) return result;
    if (snapped >= 360.0) snapped -= 360.0;
    const double r = snapped * kPi / 180.0;
    const Vec3 dir = sx * std::cos(r) + sy * std::sin(r);
    result.point = from + dir * dot(d, dir);
    result.angleDegrees = snapped;
    result.applied = true;
    return result;
}

const char* distanceAxisName(const Vec3& a, const Vec3& b, const PlanarInfo* plane, double tolerance) {
    const Vec3 d = b - a;
    if (length(d) <= tolerance) return nullptr;
    if (plane && plane->planar) {
        const double du = std::fabs(dot(d, plane->u)), dv = std::fabs(dot(d, plane->v));
        if (dv <= tolerance) return "Horizontal";
        if (du <= tolerance) return "Vertical";
        return nullptr;
    }
    const double x = std::fabs(d.x), y = std::fabs(d.y), z = std::fabs(d.z);
    if (y <= tolerance && z <= tolerance) return "X";
    if (x <= tolerance && z <= tolerance) return "Y";
    if (x <= tolerance && y <= tolerance) return "Z";
    return nullptr;
}

double angleAt(const Vec3& a, const Vec3& vertex, const Vec3& b) {
    const Vec3 u = a - vertex, v = b - vertex;
    const double lu = length(u), lv = length(v);
    if (lu < 1e-300 || lv < 1e-300) return 0.0;
    const double c = std::max(-1.0, std::min(1.0, dot(u, v) / (lu * lv)));
    return std::acos(c) * 180.0 / kPi;
}

double angleBetweenLines(const Vec3& a0, const Vec3& a1, const Vec3& pickA, const Vec3& b0,
                         const Vec3& b1, const Vec3& pickB) {
    const Vec3 u = normalize(a1 - a0), v = normalize(b1 - b0);
    const Vec3 w = a0 - b0;
    const double B = dot(u, v), D = dot(u, w), E = dot(v, w);
    const double den = 1.0 - B * B;
    if (den < 1e-18) return 0.0;  // paralelas
    // Puntos mas cercanos entre las dos rectas; su punto medio es el cruce.
    const double s = (B * E - D) / den;
    const double t = (E - B * D) / den;
    const Vec3 cross = ((a0 + u * s) + (b0 + v * t)) * 0.5;
    const Vec3 da = dot(pickA - cross, u) >= 0 ? u : -u;
    const Vec3 db = dot(pickB - cross, v) >= 0 ? v : -v;
    return angleAt(cross + da, cross, cross + db);
}

namespace {

// Distancia de p al circulo (o al arco) c.
double distanceToCircle(const CircleFeature& c, const Vec3& p) {
    const Vec3 d = p - c.center;
    const double h = dot(d, c.normal);
    const Vec3 inPlane = d - c.normal * h;
    const double rho = length(inPlane);
    double onCurve = std::sqrt(h * h + (rho - c.radius) * (rho - c.radius));
    if (!c.full() && rho > 1e-300) {
        const Vec3 y = cross(c.normal, c.xAxis);
        double angle = std::atan2(dot(inPlane, y), dot(inPlane, c.xAxis)) - c.startAngle;
        const double sweep = c.sweep;
        // Llevar el angulo al rango del barrido, con su signo.
        while (sweep >= 0 && angle < 0) angle += 2 * kPi;
        while (sweep < 0 && angle > 0) angle -= 2 * kPi;
        const bool inside = sweep >= 0 ? angle <= sweep : angle >= sweep;
        if (!inside) {
            onCurve = std::min(distance(p, c.pointAt(c.startAngle)),
                               distance(p, c.pointAt(c.startAngle + c.sweep)));
        }
    }
    return std::min(onCurve, distance(p, c.center));
}

int faceOfTriangle(const Mesh& mesh, int triangle) {
    const std::vector<FaceFeature>& faces = mesh.features.faces;
    if (triangle < 0 || faces.empty()) return -1;
    const auto it = std::upper_bound(faces.begin(), faces.end(), static_cast<std::uint32_t>(triangle),
                                     [](std::uint32_t t, const FaceFeature& f) { return t < f.firstTriangle; });
    if (it == faces.begin()) return -1;
    const FaceFeature& f = *(it - 1);
    if (static_cast<std::uint32_t>(triangle) >= f.firstTriangle + f.triangleCount) return -1;
    return static_cast<int>(it - 1 - faces.begin());
}

struct VertexKey {
    long long x, y, z;
    bool operator<(const VertexKey& o) const {
        return x != o.x ? x < o.x : (y != o.y ? y < o.y : z < o.z);
    }
};

// Bulge -> datos del arco en el plano del contorno: devuelve el barrido con signo y el radio.
void bulgeArc(double chord, double bulge, double* sweep, double* radius) {
    *sweep = 4.0 * std::atan(bulge);
    *radius = chord / (2.0 * std::sin(std::fabs(*sweep) * 0.5));
}

// Base ortonormal del plano del contorno.
void contourBasis(const ContourFeature& c, Vec3* e1, Vec3* e2) {
    const Vec3 ref = std::fabs(c.normal.x) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    *e1 = normalize(ref - c.normal * dot(ref, c.normal));
    *e2 = cross(c.normal, *e1);
}

// Contorno muestreado en 2D (arcos a `arcSamples` tramos).
std::vector<Vec2> contourPolygon(const ContourFeature& c, int arcSamples) {
    Vec3 e1, e2;
    contourBasis(c, &e1, &e2);
    const Vec3 origin = c.points.empty() ? Vec3() : c.points[0];
    auto flat = [&](const Vec3& p) { return Vec2(dot(p - origin, e1), dot(p - origin, e2)); };
    std::vector<Vec2> polygon;
    const std::size_t n = c.points.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = flat(c.points[i]);
        const Vec2 b = flat(c.points[(i + 1) % n]);
        polygon.push_back(a);
        const double bulge = i < c.bulges.size() ? c.bulges[i] : 0.0;
        const double chord = length(b - a);
        if (std::fabs(bulge) <= 1e-9 || chord < 1e-12) continue;
        const double sweep = 4.0 * std::atan(bulge);
        const double offset = chord * (1.0 - bulge * bulge) / (4.0 * bulge);
        const Vec2 mid = (a + b) * 0.5;
        const Vec2 center(mid.x - (b.y - a.y) / chord * offset, mid.y + (b.x - a.x) / chord * offset);
        const double radius = length(a - center);
        const double start = std::atan2(a.y - center.y, a.x - center.x);
        for (int k = 1; k < arcSamples; ++k) {
            const double angle = start + sweep * k / arcSamples;
            polygon.emplace_back(center.x + radius * std::cos(angle), center.y + radius * std::sin(angle));
        }
    }
    return polygon;
}

}  // namespace

RadiusResult measureRadius(const Mesh& mesh, const Vec3& point, int triangle, double tolerance) {
    RadiusResult result;
    double best = 1e300;
    for (const CircleFeature& c : mesh.features.circles) {
        const double d = distanceToCircle(c, point);
        if (d <= tolerance && d < best) {
            best = d;
            result.ok = true;
            result.radius = c.radius;
            result.center = c.center;
            result.normal = c.normal;
            result.onCircle = true;
        }
    }
    if (result.ok) return result;

    const int face = faceOfTriangle(mesh, triangle);
    if (face >= 0) {
        const FaceFeature& f = mesh.features.faces[static_cast<std::size_t>(face)];
        if (f.kind == SurfaceKind::Cylinder || f.kind == SurfaceKind::Sphere) {
            result.ok = true;
            result.radius = f.radius;
            result.normal = f.axisDir;
            result.center = f.kind == SurfaceKind::Sphere
                                ? f.axisOrigin
                                : f.axisOrigin + f.axisDir * dot(point - f.axisOrigin, f.axisDir);
            return result;
        }
    }
    result.error = (mesh.features.circles.empty() && mesh.features.faces.empty())
                       ? "Este formato no guarda circulos; usa distancia"
                       : "No hay un circulo aqui";
    return result;
}

Vec3 radiusAnchor(const RadiusResult& result, const Vec3& point) {
    if (!result.ok || !result.onCircle) return point;
    const Vec3 d = point - result.center;
    const Vec3 inPlane = d - result.normal * dot(d, result.normal);
    if (length(inPlane) < 1e-300) return point;
    return result.center + normalize(inPlane) * result.radius;
}

double contourArea(const ContourFeature& c) {
    const std::size_t n = c.points.size();
    if (n < 2) return 0.0;
    double area = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3& a = c.points[i];
        const Vec3& b = c.points[(i + 1) % n];
        area += dot(cross(a - c.points[0], b - c.points[0]), c.normal) * 0.5;
        const double bulge = i < c.bulges.size() ? c.bulges[i] : 0.0;
        const double chord = distance(a, b);
        if (std::fabs(bulge) > 1e-9 && chord > 1e-12) {
            double sweep, radius;
            bulgeArc(chord, bulge, &sweep, &radius);
            const double segment = radius * radius * 0.5 * (std::fabs(sweep) - std::sin(std::fabs(sweep)));
            area += bulge > 0 ? segment : -segment;
        }
    }
    return std::fabs(area);
}

double contourPerimeter(const ContourFeature& c) {
    const std::size_t n = c.points.size();
    double total = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double chord = distance(c.points[i], c.points[(i + 1) % n]);
        const double bulge = i < c.bulges.size() ? c.bulges[i] : 0.0;
        if (std::fabs(bulge) > 1e-9 && chord > 1e-12) {
            double sweep, radius;
            bulgeArc(chord, bulge, &sweep, &radius);
            total += radius * std::fabs(sweep);
        } else {
            total += chord;
        }
    }
    return total;
}

bool contourContains(const ContourFeature& c, const Vec3& point) {
    if (c.points.empty()) return false;
    const std::vector<Vec2> polygon = contourPolygon(c, 32);
    Vec3 e1, e2;
    contourBasis(c, &e1, &e2);
    const Vec2 p(dot(point - c.points[0], e1), dot(point - c.points[0], e2));
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Vec2& a = polygon[i];
        const Vec2& b = polygon[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

std::vector<Vec3> contourOutline(const ContourFeature& c) {
    std::vector<Vec3> outline;
    if (c.points.empty()) return outline;
    Vec3 e1, e2;
    contourBasis(c, &e1, &e2);
    for (const Vec2& q : contourPolygon(c, 32)) outline.push_back(c.points[0] + e1 * q.x + e2 * q.y);
    return outline;
}

AreaResult measureArea(const Mesh& mesh, const Vec3& point, int triangle, const PlanarInfo* plane) {
    AreaResult result;
    const std::vector<ContourFeature>& contours = mesh.features.contours;

    if (plane && plane->planar && !contours.empty()) {
        // El contorno mas chico que contiene el punto, menos los que tiene adentro.
        int chosen = -1;
        double chosenArea = 1e300;
        std::vector<double> areas(contours.size());
        for (std::size_t i = 0; i < contours.size(); ++i) {
            areas[i] = contourArea(contours[i]);
            if (areas[i] < chosenArea && contourContains(contours[i], point)) {
                chosen = static_cast<int>(i);
                chosenArea = areas[i];
            }
        }
        if (chosen >= 0) {
            std::vector<int> inside;
            for (std::size_t i = 0; i < contours.size(); ++i) {
                if (static_cast<int>(i) == chosen || areas[i] >= chosenArea || contours[i].points.empty()) continue;
                if (contourContains(contours[static_cast<std::size_t>(chosen)], contours[i].points[0])) {
                    inside.push_back(static_cast<int>(i));
                }
            }
            // Solo los hijos directos: los que no estan dentro de otro de la lista.
            for (int i : inside) {
                bool nested = false;
                for (int j : inside) {
                    if (i != j && areas[static_cast<std::size_t>(j)] > areas[static_cast<std::size_t>(i)] &&
                        contourContains(contours[static_cast<std::size_t>(j)], contours[static_cast<std::size_t>(i)].points[0])) {
                        nested = true;
                    }
                }
                if (!nested) result.holes.push_back(i);
            }
            result.ok = true;
            result.contour = chosen;
            result.area = chosenArea;
            result.perimeter = contourPerimeter(contours[static_cast<std::size_t>(chosen)]);
            for (int h : result.holes) {
                result.area -= areas[static_cast<std::size_t>(h)];
                result.perimeter += contourPerimeter(contours[static_cast<std::size_t>(h)]);
            }
            return result;
        }
    }

    if (triangle >= 0 && static_cast<std::size_t>(triangle) < mesh.triangleCount()) {
        const int face = faceOfTriangle(mesh, triangle);
        if (face >= 0) {
            const FaceFeature& f = mesh.features.faces[static_cast<std::size_t>(face)];
            for (std::uint32_t t = f.firstTriangle; t < f.firstTriangle + f.triangleCount; ++t) {
                result.triangles.push_back(t);
            }
        } else {
            // Sin caras (STL, OBJ, PLY): triangulos contiguos con la misma normal.
            const double quantum = std::max(1e-12, mesh.bounds.diagonal() * 1e-9);
            auto key = [&](std::uint32_t v) {
                const Vec3& p = mesh.positions[v];
                return VertexKey{std::llround(p.x / quantum), std::llround(p.y / quantum), std::llround(p.z / quantum)};
            };
            auto normalOf = [&](std::size_t t) {
                const Vec3& a = mesh.positions[mesh.indices[3 * t]];
                return normalize(cross(mesh.positions[mesh.indices[3 * t + 1]] - a, mesh.positions[mesh.indices[3 * t + 2]] - a));
            };
            std::map<std::pair<VertexKey, VertexKey>, std::vector<std::uint32_t>> edges;
            for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
                for (int k = 0; k < 3; ++k) {
                    VertexKey a = key(mesh.indices[3 * t + k]), b = key(mesh.indices[3 * t + (k + 1) % 3]);
                    if (b < a) std::swap(a, b);
                    edges[{a, b}].push_back(static_cast<std::uint32_t>(t));
                }
            }
            const Vec3 seedNormal = normalOf(static_cast<std::size_t>(triangle));
            std::vector<char> taken(mesh.triangleCount(), 0);
            std::vector<std::uint32_t> stack = {static_cast<std::uint32_t>(triangle)};
            taken[static_cast<std::size_t>(triangle)] = 1;
            while (!stack.empty()) {
                const std::uint32_t t = stack.back();
                stack.pop_back();
                result.triangles.push_back(t);
                for (int k = 0; k < 3; ++k) {
                    VertexKey a = key(mesh.indices[3 * t + k]), b = key(mesh.indices[3 * t + (k + 1) % 3]);
                    if (b < a) std::swap(a, b);
                    for (const std::uint32_t other : edges[{a, b}]) {
                        if (taken[other] || std::fabs(dot(normalOf(other), seedNormal)) < 0.99985) continue;  // 1 grado
                        taken[other] = 1;
                        stack.push_back(other);
                    }
                }
            }
        }

        // Area: suma de triangulos. Perimetro: aristas que usa un solo triangulo de la cara.
        const double quantum = std::max(1e-12, mesh.bounds.diagonal() * 1e-9);
        std::map<std::pair<VertexKey, VertexKey>, std::pair<int, double>> border;
        for (const std::uint32_t t : result.triangles) {
            const Vec3& a = mesh.positions[mesh.indices[3 * t]];
            const Vec3& b = mesh.positions[mesh.indices[3 * t + 1]];
            const Vec3& c = mesh.positions[mesh.indices[3 * t + 2]];
            result.area += length(cross(b - a, c - a)) * 0.5;
            for (int k = 0; k < 3; ++k) {
                const Vec3& p = mesh.positions[mesh.indices[3 * t + k]];
                const Vec3& q = mesh.positions[mesh.indices[3 * t + (k + 1) % 3]];
                VertexKey kp{std::llround(p.x / quantum), std::llround(p.y / quantum), std::llround(p.z / quantum)};
                VertexKey kq{std::llround(q.x / quantum), std::llround(q.y / quantum), std::llround(q.z / quantum)};
                if (kq < kp) std::swap(kp, kq);
                if (kp.x == kq.x && kp.y == kq.y && kp.z == kq.z) continue;  // arista degenerada
                auto& entry = border[{kp, kq}];
                ++entry.first;
                entry.second = distance(p, q);
            }
        }
        for (const auto& e : border) {
            if (e.second.first == 1) result.perimeter += e.second.second;
        }
        result.ok = !result.triangles.empty();
        return result;
    }

    result.error = "No hay un contorno cerrado aqui";
    return result;
}

int PickIndex::triangleAt(const Mesh& mesh, const Vec3& point, double tolerance) const {
    int found = -1;
    const Vec3 pad(tolerance, tolerance, tolerance);
    m_triangles.query(
        [&](const Vec3& lo, const Vec3& hi) {
            const Vec3 l = lo - pad, h = hi + pad;
            return point.x >= l.x && point.y >= l.y && point.z >= l.z && point.x <= h.x &&
                   point.y <= h.y && point.z <= h.z;
        },
        [&](std::uint32_t i) {
            if (found >= 0) return;
            const Vec3& a = mesh.positions[mesh.indices[3 * i]];
            const Vec3& b = mesh.positions[mesh.indices[3 * i + 1]];
            const Vec3& c = mesh.positions[mesh.indices[3 * i + 2]];
            const Vec3 n = cross(b - a, c - a);
            const double area2 = length(n);
            if (area2 < 1e-300) return;
            const Vec3 unit = n * (1.0 / area2);
            if (std::fabs(dot(point - a, unit)) > tolerance) return;
            // Coordenadas baricentricas en el plano del triangulo.
            const double wa = dot(cross(c - b, point - b), unit) / area2;
            const double wb = dot(cross(a - c, point - c), unit) / area2;
            const double wc = 1.0 - wa - wb;
            const double slack = -1e-9;
            if (wa >= slack && wb >= slack && wc >= slack) found = static_cast<int>(i);
        });
    return found;
}

std::string formatNumber(double value, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    std::string text = buffer;
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
    }
    if (text == "-0") text = "0";
    return text;
}

std::string formatLength(double value, LengthUnit unit) {
    const std::string suffix = unitSuffix(unit);
    return formatNumber(value, 3) + (suffix.empty() ? "" : " " + suffix);
}

std::string formatArea(double value, LengthUnit unit) {
    const std::string suffix = unitSuffix(unit);
    return formatNumber(value, 2) + (suffix.empty() ? "" : " " + suffix + "\xC2\xB2");
}

std::string formatAngle(double degrees) { return formatNumber(degrees, 2) + "\xC2\xB0"; }

}  // namespace stp
