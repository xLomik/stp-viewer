#include "tessellate.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numeric>

namespace stp {
namespace {

bool onSegment(const Vec2& a, const Vec2& b, const Vec2& p, double eps) {
    if (std::fabs(cross2(b - a, p - a)) > eps) return false;
    return p.x >= std::min(a.x, b.x) - eps && p.x <= std::max(a.x, b.x) + eps &&
           p.y >= std::min(a.y, b.y) - eps && p.y <= std::max(a.y, b.y) + eps;
}

// True when the open segments a-b and c-d cross. Shared endpoints do not count.
bool segmentsCross(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d, double eps) {
    const double d1 = cross2(b - a, c - a);
    const double d2 = cross2(b - a, d - a);
    const double d3 = cross2(d - c, a - c);
    const double d4 = cross2(d - c, b - c);
    if (((d1 > eps && d2 < -eps) || (d1 < -eps && d2 > eps)) &&
        ((d3 > eps && d4 < -eps) || (d3 < -eps && d4 > eps))) {
        return true;
    }
    if (std::fabs(d1) <= eps && std::fabs(d2) <= eps) {
        const bool cOn = onSegment(a, b, c, eps);
        const bool dOn = onSegment(a, b, d, eps);
        const bool aOn = onSegment(c, d, a, eps);
        const bool bOn = onSegment(c, d, b, eps);
        const int shared = (length(a - c) < eps) + (length(a - d) < eps) + (length(b - c) < eps) +
                           (length(b - d) < eps);
        if (shared >= 2) return true;
        if ((cOn || dOn || aOn || bOn) && shared == 0) return true;
    }
    return false;
}

bool pointInPolygon(const std::vector<Vec2>& pts, const std::vector<int>& idx, const Vec2& p) {
    bool inside = false;
    const std::size_t n = idx.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const Vec2& a = pts[idx[i]];
        const Vec2& b = pts[idx[j]];
        if ((a.y > p.y) != (b.y > p.y)) {
            const double t = (p.y - a.y) / (b.y - a.y);
            if (p.x < a.x + t * (b.x - a.x)) inside = !inside;
        }
    }
    return inside;
}

bool pointInTriangle(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& p, double eps) {
    const double d1 = cross2(b - a, p - a);
    const double d2 = cross2(c - b, p - b);
    const double d3 = cross2(a - c, p - c);
    const bool neg = (d1 < -eps) || (d2 < -eps) || (d3 < -eps);
    const bool pos = (d1 > eps) || (d2 > eps) || (d3 > eps);
    return !(neg && pos);
}

void earClip(const std::vector<Vec2>& pts, std::vector<int> poly, double eps,
             std::vector<std::array<int, 3>>* out) {
    if (poly.size() < 3) return;

    int guard = static_cast<int>(poly.size()) * 3 + 16;
    while (poly.size() > 3 && guard-- > 0) {
        bool clipped = false;
        const int n = static_cast<int>(poly.size());
        for (int i = 0; i < n; ++i) {
            const int ia = poly[(i + n - 1) % n];
            const int ib = poly[i];
            const int ic = poly[(i + 1) % n];
            const Vec2& a = pts[ia];
            const Vec2& b = pts[ib];
            const Vec2& c = pts[ic];
            if (cross2(b - a, c - a) <= eps) continue;  // reflex or degenerate

            bool contains = false;
            for (int k = 0; k < n && !contains; ++k) {
                const int ip = poly[k];
                if (ip == ia || ip == ib || ip == ic) continue;
                const int kp = poly[(k + n - 1) % n];
                const int kn = poly[(k + 1) % n];
                // Only reflex vertices can block an ear.
                if (cross2(pts[ip] - pts[kp], pts[kn] - pts[kp]) > eps) continue;
                if (pointInTriangle(a, b, c, pts[ip], eps)) contains = true;
            }
            if (contains) continue;

            out->push_back({ia, ib, ic});
            poly.erase(poly.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) {
            // Degenerate geometry: drop the sharpest vertex and keep going.
            int worst = 0;
            double worstArea = 1e300;
            const int m = static_cast<int>(poly.size());
            for (int i = 0; i < m; ++i) {
                const Vec2& a = pts[poly[(i + m - 1) % m]];
                const Vec2& b = pts[poly[i]];
                const Vec2& c = pts[poly[(i + 1) % m]];
                const double area = std::fabs(cross2(b - a, c - a));
                if (area < worstArea) { worstArea = area; worst = i; }
            }
            poly.erase(poly.begin() + worst);
        }
    }
    if (poly.size() == 3) out->push_back({poly[0], poly[1], poly[2]});
}

double polygonEps(const std::vector<Vec2>& pts) {
    double extent = 0.0;
    for (const Vec2& p : pts) extent = std::max(extent, std::max(std::fabs(p.x), std::fabs(p.y)));
    return std::max(1e-12, extent * 1e-9);
}

// Sutherland-Hodgman clip against one axis-aligned half plane.
enum class Side { MinU, MaxU, MinV, MaxV };

std::vector<Vec2> clipHalfPlane(const std::vector<Vec2>& poly, Side side, double value) {
    auto inside = [&](const Vec2& p) {
        switch (side) {
            case Side::MinU: return p.x >= value;
            case Side::MaxU: return p.x <= value;
            case Side::MinV: return p.y >= value;
            case Side::MaxV: return p.y <= value;
        }
        return true;
    };
    auto intersect = [&](const Vec2& a, const Vec2& b) {
        double t = 0.0;
        if (side == Side::MinU || side == Side::MaxU) {
            t = std::fabs(b.x - a.x) > 1e-300 ? (value - a.x) / (b.x - a.x) : 0.0;
        } else {
            t = std::fabs(b.y - a.y) > 1e-300 ? (value - a.y) / (b.y - a.y) : 0.0;
        }
        return Vec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
    };

    std::vector<Vec2> out;
    out.reserve(poly.size() + 4);
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const Vec2& cur = poly[i];
        const Vec2& nxt = poly[(i + 1) % poly.size()];
        const bool curIn = inside(cur);
        const bool nxtIn = inside(nxt);
        if (curIn) out.push_back(cur);
        if (curIn != nxtIn) out.push_back(intersect(cur, nxt));
    }
    return out;
}

std::vector<Vec2> clipToCell(const std::vector<Vec2>& poly, double u0, double u1, double v0,
                             double v1) {
    std::vector<Vec2> out = clipHalfPlane(poly, Side::MinU, u0);
    if (out.size() < 3) return {};
    out = clipHalfPlane(out, Side::MaxU, u1);
    if (out.size() < 3) return {};
    out = clipHalfPlane(out, Side::MinV, v0);
    if (out.size() < 3) return {};
    out = clipHalfPlane(out, Side::MaxV, v1);
    return out.size() >= 3 ? out : std::vector<Vec2>();
}

// Welds vertices that land on the same parameter point so neighbouring cells
// share them and no cracks appear.
class VertexPool {
public:
    explicit VertexPool(double quantum) : m_quantum(quantum > 0 ? quantum : 1e-9) {}

    int add(const Vec2& p, std::vector<Vec2>* vertices) {
        const std::pair<std::int64_t, std::int64_t> key(
            static_cast<std::int64_t>(std::llround(p.x / m_quantum)),
            static_cast<std::int64_t>(std::llround(p.y / m_quantum)));
        const auto it = m_map.find(key);
        if (it != m_map.end()) return it->second;
        const int idx = static_cast<int>(vertices->size());
        vertices->push_back(p);
        m_map.emplace(key, idx);
        return idx;
    }

private:
    double m_quantum;
    std::map<std::pair<std::int64_t, std::int64_t>, int> m_map;
};

}  // namespace

double signedArea(const std::vector<Vec2>& loop) {
    double a = 0.0;
    for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++) {
        a += (loop[j].x * loop[i].y) - (loop[i].x * loop[j].y);
    }
    return a * 0.5;
}

bool mergeLoops(const std::vector<std::vector<Vec2>>& loopsIn, std::vector<Vec2>* polygon,
                std::vector<int>* sourceIndex) {
    polygon->clear();
    if (sourceIndex) sourceIndex->clear();
    if (loopsIn.empty() || loopsIn[0].size() < 3) return false;

    // Orient: outer counter-clockwise, holes clockwise. Track where each point
    // came from so callers can keep their own per-point data aligned.
    std::vector<std::vector<Vec2>> loops = loopsIn;
    std::vector<std::vector<int>> origin(loops.size());
    int running = 0;
    for (std::size_t i = 0; i < loops.size(); ++i) {
        origin[i].resize(loops[i].size());
        for (std::size_t j = 0; j < loops[i].size(); ++j) origin[i][j] = running++;
    }
    for (std::size_t i = 0; i < loops.size(); ++i) {
        if (loops[i].size() < 3) continue;
        const double area = signedArea(loops[i]);
        const bool wantCcw = (i == 0);
        if ((area < 0.0) == wantCcw) {
            std::reverse(loops[i].begin(), loops[i].end());
            std::reverse(origin[i].begin(), origin[i].end());
        }
    }

    std::vector<Vec2> pts;
    std::vector<int> src;
    std::vector<std::vector<int>> loopIdx(loops.size());
    for (std::size_t i = 0; i < loops.size(); ++i) {
        for (std::size_t j = 0; j < loops[i].size(); ++j) {
            loopIdx[i].push_back(static_cast<int>(pts.size()));
            pts.push_back(loops[i][j]);
            src.push_back(origin[i][j]);
        }
    }
    const double eps = polygonEps(pts);

    std::vector<int> poly = loopIdx[0];

    std::vector<std::size_t> order;
    for (std::size_t i = 1; i < loops.size(); ++i) {
        if (loops[i].size() >= 3) order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        double ma = -1e300, mb = -1e300;
        for (const Vec2& p : loops[a]) ma = std::max(ma, p.x);
        for (const Vec2& p : loops[b]) mb = std::max(mb, p.x);
        return ma > mb;
    });

    std::vector<bool> merged(loops.size(), false);
    for (std::size_t h : order) {
        const std::vector<int>& hole = loopIdx[h];
        int bestPoly = -1, bestHole = -1;
        double bestDist = 1e300;

        for (std::size_t hi = 0; hi < hole.size(); ++hi) {
            for (std::size_t pi = 0; pi < poly.size(); ++pi) {
                const Vec2& hp = pts[hole[hi]];
                const Vec2& pp = pts[poly[pi]];
                const double d = length(hp - pp);
                if (d >= bestDist) continue;

                bool blocked = false;
                for (std::size_t k = 0; k < poly.size() && !blocked; ++k) {
                    const Vec2& a = pts[poly[k]];
                    const Vec2& b = pts[poly[(k + 1) % poly.size()]];
                    if (poly[k] == poly[pi] || poly[(k + 1) % poly.size()] == poly[pi]) continue;
                    if (segmentsCross(hp, pp, a, b, eps)) blocked = true;
                }
                for (std::size_t g = 1; g < loops.size() && !blocked; ++g) {
                    if (merged[g] || loops[g].size() < 3) continue;
                    const std::vector<int>& other = loopIdx[g];
                    for (std::size_t k = 0; k < other.size() && !blocked; ++k) {
                        if (g == h && (other[k] == hole[hi] ||
                                       other[(k + 1) % other.size()] == hole[hi])) {
                            continue;
                        }
                        const Vec2& a = pts[other[k]];
                        const Vec2& b = pts[other[(k + 1) % other.size()]];
                        if (segmentsCross(hp, pp, a, b, eps)) blocked = true;
                    }
                }
                if (blocked) continue;

                const Vec2 mid((hp.x + pp.x) * 0.5, (hp.y + pp.y) * 0.5);
                if (!pointInPolygon(pts, poly, mid)) continue;
                bool insideHole = false;
                for (std::size_t g = 1; g < loops.size() && !insideHole; ++g) {
                    if (merged[g] || loops[g].size() < 3) continue;
                    if (pointInPolygon(pts, loopIdx[g], mid)) insideHole = true;
                }
                if (insideHole) continue;

                bestDist = d;
                bestPoly = static_cast<int>(pi);
                bestHole = static_cast<int>(hi);
            }
        }
        if (bestPoly < 0 || bestHole < 0) continue;  // unbridgeable hole: ignore it

        std::vector<int> spliced;
        spliced.reserve(poly.size() + hole.size() + 2);
        spliced.insert(spliced.end(), poly.begin(), poly.begin() + bestPoly + 1);
        for (std::size_t k = 0; k < hole.size(); ++k) {
            spliced.push_back(hole[(bestHole + k) % hole.size()]);
        }
        spliced.push_back(hole[bestHole]);
        spliced.push_back(poly[bestPoly]);
        spliced.insert(spliced.end(), poly.begin() + bestPoly + 1, poly.end());
        poly.swap(spliced);
        merged[h] = true;
    }

    polygon->reserve(poly.size());
    for (int idx : poly) {
        polygon->push_back(pts[idx]);
        if (sourceIndex) sourceIndex->push_back(src[idx]);
    }
    return polygon->size() >= 3;
}

bool triangulatePolygon(const std::vector<std::vector<Vec2>>& loopsIn,
                        std::vector<Vec2>* vertices,
                        std::vector<std::array<int, 3>>* triangles,
                        std::vector<int>* sourceIndex) {
    triangles->clear();
    if (!mergeLoops(loopsIn, vertices, sourceIndex)) return false;

    std::vector<int> poly(vertices->size());
    std::iota(poly.begin(), poly.end(), 0);
    earClip(*vertices, poly, polygonEps(*vertices), triangles);
    return !triangles->empty();
}

bool triangulateGrid(const std::vector<Vec2>& polygon, double uStep, double vStep,
                     std::vector<Vec2>* vertices, std::vector<std::array<int, 3>>* triangles) {
    vertices->clear();
    triangles->clear();
    if (polygon.size() < 3) return false;

    double u0 = 1e300, u1 = -1e300, v0 = 1e300, v1 = -1e300;
    for (const Vec2& p : polygon) {
        u0 = std::min(u0, p.x);
        u1 = std::max(u1, p.x);
        v0 = std::min(v0, p.y);
        v1 = std::max(v1, p.y);
    }
    const double uSpan = std::max(u1 - u0, 1e-12);
    const double vSpan = std::max(v1 - v0, 1e-12);

    const int maxCells = 400;
    int nu = uStep > 0 ? static_cast<int>(std::ceil(uSpan / uStep)) : 1;
    int nv = vStep > 0 ? static_cast<int>(std::ceil(vSpan / vStep)) : 1;
    nu = std::max(1, std::min(maxCells, nu));
    nv = std::max(1, std::min(maxCells, nv));
    const double du = uSpan / nu;
    const double dv = vSpan / nv;

    // Mark the cells the boundary passes through; the rest are fully in or out.
    std::vector<char> boundary(static_cast<std::size_t>(nu) * nv, 0);
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Vec2& a = polygon[i];
        const Vec2& b = polygon[(i + 1) % polygon.size()];
        int i0 = static_cast<int>(std::floor((std::min(a.x, b.x) - u0) / du));
        int i1 = static_cast<int>(std::floor((std::max(a.x, b.x) - u0) / du));
        int j0 = static_cast<int>(std::floor((std::min(a.y, b.y) - v0) / dv));
        int j1 = static_cast<int>(std::floor((std::max(a.y, b.y) - v0) / dv));
        i0 = std::max(0, std::min(nu - 1, i0));
        i1 = std::max(0, std::min(nu - 1, i1));
        j0 = std::max(0, std::min(nv - 1, j0));
        j1 = std::max(0, std::min(nv - 1, j1));
        for (int j = j0; j <= j1; ++j) {
            for (int i = i0; i <= i1; ++i) boundary[static_cast<std::size_t>(j) * nu + i] = 1;
        }
    }

    VertexPool pool(std::min(du, dv) * 1e-6);
    std::vector<int> allIdx(polygon.size());
    std::iota(allIdx.begin(), allIdx.end(), 0);

    for (int j = 0; j < nv; ++j) {
        const double cv0 = v0 + j * dv;
        const double cv1 = cv0 + dv;
        for (int i = 0; i < nu; ++i) {
            const double cu0 = u0 + i * du;
            const double cu1 = cu0 + du;

            if (!boundary[static_cast<std::size_t>(j) * nu + i]) {
                const Vec2 center((cu0 + cu1) * 0.5, (cv0 + cv1) * 0.5);
                if (!pointInPolygon(polygon, allIdx, center)) continue;
                const int a = pool.add(Vec2(cu0, cv0), vertices);
                const int b = pool.add(Vec2(cu1, cv0), vertices);
                const int c = pool.add(Vec2(cu1, cv1), vertices);
                const int d = pool.add(Vec2(cu0, cv1), vertices);
                triangles->push_back({a, b, c});
                triangles->push_back({a, c, d});
                continue;
            }

            const std::vector<Vec2> piece = clipToCell(polygon, cu0, cu1, cv0, cv1);
            if (piece.size() < 3) continue;

            std::vector<Vec2> local = piece;
            if (signedArea(local) < 0) std::reverse(local.begin(), local.end());
            std::vector<int> localIdx(local.size());
            std::iota(localIdx.begin(), localIdx.end(), 0);
            std::vector<std::array<int, 3>> localTris;
            earClip(local, localIdx, polygonEps(local), &localTris);

            for (const auto& t : localTris) {
                const Vec2& a = local[t[0]];
                const Vec2& b = local[t[1]];
                const Vec2& c = local[t[2]];
                if (std::fabs(cross2(b - a, c - a)) < du * dv * 1e-9) continue;
                triangles->push_back({pool.add(a, vertices), pool.add(b, vertices),
                                      pool.add(c, vertices)});
            }
        }
    }
    return !triangles->empty();
}

}  // namespace stp
