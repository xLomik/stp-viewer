#include "step_model.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

#include "budget.h"
#include "surfaces.h"
#include "tessellate.h"

namespace stp {
namespace {

constexpr int kMaxInstances = 20000;
constexpr int kMaxDepth = 32;

double normalizeAngle(double a) {
    while (a <= -kPi) a += 2 * kPi;
    while (a > kPi) a -= 2 * kPi;
    return a;
}

// --- B-spline / NURBS curve -------------------------------------------------

struct BSpline {
    int degree = 1;
    std::vector<Vec3> cps;
    std::vector<double> weights;
    std::vector<double> knots;

    bool valid() const {
        return degree >= 1 && cps.size() >= static_cast<std::size_t>(degree) + 1 &&
               knots.size() == cps.size() + degree + 1;
    }
    double tMin() const { return knots[degree]; }
    double tMax() const { return knots[knots.size() - degree - 1]; }

    Vec3 eval(double t) const {
        const int n = static_cast<int>(cps.size()) - 1;
        t = std::max(tMin(), std::min(tMax(), t));
        int k = degree;
        while (k < n && t >= knots[k + 1]) ++k;

        std::vector<Vec3> d(degree + 1);
        std::vector<double> w(degree + 1, 1.0);
        for (int j = 0; j <= degree; ++j) {
            const int idx = k - degree + j;
            const double wt = weights.empty() ? 1.0 : weights[idx];
            d[j] = cps[idx] * wt;
            w[j] = wt;
        }
        for (int r = 1; r <= degree; ++r) {
            for (int j = degree; j >= r; --j) {
                const int i = k - degree + j;
                const double den = knots[i + degree - r + 1] - knots[i];
                const double alpha = den > 1e-15 ? (t - knots[i]) / den : 0.0;
                d[j] = d[j - 1] * (1.0 - alpha) + d[j] * alpha;
                w[j] = w[j - 1] * (1.0 - alpha) + w[j] * alpha;
            }
        }
        return std::fabs(w[degree]) > 1e-15 ? d[degree] * (1.0 / w[degree]) : d[degree];
    }
};

void appendUnique(std::vector<Vec3>* out, const Vec3& p, double tol) {
    if (!out->empty() && distance(out->back(), p) <= tol) return;
    out->push_back(p);
}

}  // namespace

namespace {

class Builder {
public:
    Builder(const StepFile& file, Mesh* mesh, double quality, int budgetMs)
        : F(file), M(*mesh), m_quality(quality > 0 ? quality : 0.0015), m_budget(budgetMs) {}

    bool run(std::string* error, LoadStats* stats);

private:
    const StepFile& F;
    Mesh& M;
    double m_quality;
    double m_modelSize = 1.0;
    double m_tol = 0.01;
    double m_weldTol = 1e-7;

    Mat4 m_xf;
    std::unordered_map<int, std::vector<Vec3>> m_edgeCache;
    std::unordered_set<int> m_edgeEmitted;
    int m_instances = 0;
    Budget m_budget;
    bool m_truncated = false;

    // Se consulta una vez por cara y por instancia: basta para cortar a tiempo
    // sin encarecer los bucles internos.
    bool outOfTime() {
        if (m_truncated) return true;
        if (!m_budget.expired()) return false;
        m_truncated = true;
        return true;
    }

    const Entity* res(const Value& v) const { return F.get(v); }

    bool pointOf(const Value& v, Vec3* out) const;
    bool dirOf(const Value& v, Vec3* out) const;
    bool frameOf(const Value& v, Frame* out) const;
    bool vertexPoint(const Value& v, Vec3* out) const;
    static double numOf(const std::vector<Value>& p, std::size_t i, double fallback = 0.0) {
        return i < p.size() && p[i].isNum() ? p[i].num : fallback;
    }

    void estimateSize();
    void collectInstances();
    void emitRepresentation(const Entity* rep, const Mat4& xf, int depth);
    void emitItem(const Entity* item, const Mat4& xf, int depth);
    void buildShell(const Entity* shell, const Mat4& xf);
    void buildFace(const Entity* face, const Mat4& xf);
    void buildFaceTriangles(const Entity* face, const Mat4& xf, Surface* surface, bool* known);
    const Entity* basisCurve(const Entity* curve) const;
    void noteCircle(const Entity* edge, const Mat4& xf);
    void detectUnits();
    void emitCurveSet(const Entity* set, const Mat4& xf);

    bool loopPoints(const Entity* loop, std::vector<Vec3>* pts, const Mat4& xf);
    const std::vector<Vec3>* edgePoints(const Entity* edge);
    void sampleCurve(const Entity* curve, const Vec3& p0, const Vec3& p1, bool sameSense,
                     std::vector<Vec3>* out, int depth = 0);
    bool readBSpline(const Entity* curve, BSpline* out) const;
    void sampleBSpline(const BSpline& sp, const Vec3& p0, const Vec3& p1, std::vector<Vec3>* out);
    bool surfaceOf(const Entity* surf, Surface* out) const;
    void surfaceSteps(const Surface& surf, const std::vector<Vec2>& domain, double* uStep,
                      double* vStep) const;
    void addTessellated(const Surface& surf, const std::vector<std::vector<Vec3>>& loops,
                        const Mat4& xf);
    void addFullSurface(const Surface& surf, const Mat4& xf);
    void addPlanarFallback(const std::vector<std::vector<Vec3>>& loops, const Mat4& xf,
                           bool flipped);
};

bool Builder::pointOf(const Value& v, Vec3* out) const {
    const Entity* e = res(v);
    if (!e) return false;
    const std::vector<Value>* p = e->part("CARTESIAN_POINT");
    if (!p) p = e->part("POINT");
    if (!p || p->size() < 2) return false;
    const Value& coords = (*p)[1];
    if (!coords.isList() || coords.items.size() < 2) return false;
    out->x = coords.items[0].num;
    out->y = coords.items[1].num;
    out->z = coords.items.size() > 2 ? coords.items[2].num : 0.0;
    return true;
}

bool Builder::dirOf(const Value& v, Vec3* out) const {
    const Entity* e = res(v);
    if (!e) return false;
    const std::vector<Value>* p = e->part("DIRECTION");
    if (!p || p->size() < 2) return false;
    const Value& coords = (*p)[1];
    if (!coords.isList() || coords.items.size() < 2) return false;
    out->x = coords.items[0].num;
    out->y = coords.items[1].num;
    out->z = coords.items.size() > 2 ? coords.items[2].num : 0.0;
    return true;
}

bool Builder::frameOf(const Value& v, Frame* out) const {
    const Entity* e = res(v);
    if (!e) return false;
    const std::vector<Value>* p = e->part("AXIS2_PLACEMENT_3D");
    if (!p) p = e->part("AXIS2_PLACEMENT_2D");
    if (!p || p->size() < 2) return false;

    Vec3 origin;
    if (!pointOf((*p)[1], &origin)) return false;
    Vec3 axis(0, 0, 1);
    Vec3 refDir(1, 0, 0);
    if (p->size() > 2) dirOf((*p)[2], &axis);
    if (p->size() > 3) dirOf((*p)[3], &refDir);
    *out = makeFrame(origin, axis, refDir);
    return true;
}

bool Builder::vertexPoint(const Value& v, Vec3* out) const {
    const Entity* e = res(v);
    if (!e) return false;
    const std::vector<Value>* p = e->part("VERTEX_POINT");
    if (p && p->size() > 1) return pointOf((*p)[1], out);
    return pointOf(v, out);
}

void Builder::estimateSize() {
    BBox box;
    for (const auto& kv : F.entities()) {
        const std::vector<Value>* p = kv.second.part("CARTESIAN_POINT");
        if (!p || p->size() < 2) continue;
        const Value& c = (*p)[1];
        if (!c.isList() || c.items.size() < 3) continue;
        box.add(Vec3(c.items[0].num, c.items[1].num, c.items[2].num));
    }
    m_modelSize = box.valid() ? box.diagonal() : 1.0;
    if (!(m_modelSize > 1e-9)) m_modelSize = 1.0;
    m_tol = m_modelSize * m_quality;
    m_weldTol = m_modelSize * 1e-7;
}

// --- assembly structure -----------------------------------------------------

void Builder::collectInstances() {
    struct Link {
        int child = 0;
        Mat4 xf;
    };
    std::unordered_map<int, std::vector<Link>> children;
    std::unordered_set<int> isChild;
    std::unordered_set<int> repIds;

    for (const auto& kv : F.entities()) {
        const Entity& e = kv.second;
        if (e.type.find("SHAPE_REPRESENTATION") != std::string::npos ||
            e.type.find("SHAPE_MODEL") != std::string::npos) {
            if (e.params().size() >= 2 && e.params()[1].isList()) repIds.insert(e.id);
        }
    }

    for (const auto& kv : F.entities()) {
        const Entity& e = kv.second;
        const std::vector<Value>* rel = e.part("REPRESENTATION_RELATIONSHIP");
        if (!rel || rel->size() < 4) continue;
        const Value& r1 = (*rel)[2];
        const Value& r2 = (*rel)[3];
        if (!r1.isRef() || !r2.isRef()) continue;

        Mat4 xf;
        const std::vector<Value>* trans = e.part("REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION");
        if (trans && !trans->empty()) {
            const Entity* op = res((*trans)[0]);
            if (op) {
                const std::vector<Value>* idt = op->part("ITEM_DEFINED_TRANSFORMATION");
                if (idt && idt->size() >= 4) {
                    Frame from, to;
                    const bool okFrom = frameOf((*idt)[2], &from);
                    const bool okTo = frameOf((*idt)[3], &to);
                    if (okFrom && okTo) {
                        xf = frameToMatrix(to) * inverseRigid(frameToMatrix(from));
                    }
                }
            }
        }
        // rep_1 is the component placed inside rep_2.
        children[r2.ref].push_back({r1.ref, xf});
        isChild.insert(r1.ref);
    }

    std::vector<int> roots;
    for (int id : repIds) {
        if (!isChild.count(id)) roots.push_back(id);
    }
    std::sort(roots.begin(), roots.end());

    if (roots.empty() && !repIds.empty()) {
        roots.assign(repIds.begin(), repIds.end());
        std::sort(roots.begin(), roots.end());
    }

    std::vector<std::pair<int, Mat4>> stack;
    for (int id : roots) stack.emplace_back(id, Mat4::identity());

    std::vector<std::pair<int, Mat4>> pending = stack;
    int guard = 0;
    while (!pending.empty() && guard++ < kMaxInstances && !outOfTime()) {
        const auto cur = pending.back();
        pending.pop_back();
        const Entity* rep = F.get(cur.first);
        if (!rep) continue;
        emitRepresentation(rep, cur.second, 0);
        const auto it = children.find(cur.first);
        if (it == children.end()) continue;
        for (const Link& link : it->second) {
            pending.emplace_back(link.child, cur.second * link.xf);
        }
    }

    if (M.empty()) {
        // No usable representation graph: fall back to every solid in the file.
        for (const auto& kv : F.entities()) {
            const Entity& e = kv.second;
            if (e.is("MANIFOLD_SOLID_BREP") || e.is("BREP_WITH_VOIDS") || e.is("FACETED_BREP") ||
                e.is("SHELL_BASED_SURFACE_MODEL")) {
                emitItem(&e, Mat4::identity(), 0);
            }
        }
    }
    if (M.empty()) {
        for (const Entity* shell : F.byType("CLOSED_SHELL")) emitItem(shell, Mat4::identity(), 0);
        for (const Entity* shell : F.byType("OPEN_SHELL")) emitItem(shell, Mat4::identity(), 0);
    }
}

void Builder::emitRepresentation(const Entity* rep, const Mat4& xf, int depth) {
    if (!rep || depth > kMaxDepth || ++m_instances > kMaxInstances) return;
    const std::vector<Value>& p = rep->params();
    if (p.size() < 2 || !p[1].isList()) return;
    for (const Value& item : p[1].items) {
        emitItem(res(item), xf, depth);
    }
}

void Builder::emitItem(const Entity* item, const Mat4& xf, int depth) {
    if (!item || depth > kMaxDepth || outOfTime()) return;

    if (item->is("MANIFOLD_SOLID_BREP") || item->is("FACETED_BREP") || item->is("BREP_WITH_VOIDS")) {
        const std::vector<Value>& p = item->params();
        if (p.size() > 1) {
            ++M.solids;
            buildShell(res(p[1]), xf);
        }
        if (item->is("BREP_WITH_VOIDS") && p.size() > 2 && p[2].isList()) {
            for (const Value& v : p[2].items) buildShell(res(v), xf);
        }
        return;
    }
    if (item->is("SHELL_BASED_SURFACE_MODEL") || item->is("FACE_BASED_SURFACE_MODEL")) {
        const std::vector<Value>& p = item->params();
        if (p.size() > 1 && p[1].isList()) {
            ++M.solids;
            for (const Value& v : p[1].items) buildShell(res(v), xf);
        }
        return;
    }
    if (item->is("CLOSED_SHELL") || item->is("OPEN_SHELL") || item->is("CONNECTED_FACE_SET")) {
        ++M.solids;
        buildShell(item, xf);
        return;
    }
    if (item->is("ADVANCED_FACE") || item->is("FACE_SURFACE")) {
        buildFace(item, xf);
        return;
    }
    if (item->is("MAPPED_ITEM")) {
        const std::vector<Value>& p = item->params();
        if (p.size() < 3) return;
        const Entity* map = res(p[1]);
        if (!map) return;
        const std::vector<Value>* rm = map->part("REPRESENTATION_MAP");
        if (!rm || rm->size() < 2) return;
        Frame origin, target;
        Mat4 local;
        if (frameOf((*rm)[0], &origin) && frameOf(p[2], &target)) {
            local = frameToMatrix(target) * inverseRigid(frameToMatrix(origin));
        }
        emitRepresentation(res((*rm)[1]), xf * local, depth + 1);
        return;
    }
    if (item->is("GEOMETRIC_CURVE_SET") || item->is("GEOMETRIC_SET")) {
        emitCurveSet(item, xf);
        return;
    }
}

void Builder::emitCurveSet(const Entity* set, const Mat4& xf) {
    const std::vector<Value>& p = set->params();
    if (p.size() < 2 || !p[1].isList()) return;
    for (const Value& v : p[1].items) {
        const Entity* c = res(v);
        if (!c) continue;
        std::vector<Vec3> pts;
        sampleCurve(c, Vec3(), Vec3(), true, &pts);
        const Entity* basis = basisCurve(c);
        const bool curved = basis && !basis->is("LINE") && !basis->is("POLYLINE");
        for (std::size_t i = 1; i < pts.size(); ++i) {
            M.addSegment(xf.point(pts[i - 1]), xf.point(pts[i]), curved);
        }
    }
}

void Builder::buildShell(const Entity* shell, const Mat4& xf) {
    if (!shell) return;
    const std::vector<Value>& p = shell->params();
    if (p.size() < 2 || !p[1].isList()) return;
    for (const Value& v : p[1].items) {
        if (outOfTime()) return;
        const Entity* face = res(v);
        if (face) buildFace(face, xf);
    }
}

// --- curves -----------------------------------------------------------------

const std::vector<Vec3>* Builder::edgePoints(const Entity* edge) {
    const auto cached = m_edgeCache.find(edge->id);
    if (cached != m_edgeCache.end()) return &cached->second;

    std::vector<Vec3> pts;
    const std::vector<Value>& p = edge->params();
    Vec3 p0, p1;
    const bool hasStart = p.size() > 1 && vertexPoint(p[1], &p0);
    const bool hasEnd = p.size() > 2 && vertexPoint(p[2], &p1);
    const bool sameSense = p.size() > 4 ? p[4].boolValue() : true;
    const Entity* curve = p.size() > 3 ? res(p[3]) : nullptr;

    if (curve && hasStart && hasEnd) {
        sampleCurve(curve, p0, p1, sameSense, &pts);
    } else if (hasStart && hasEnd) {
        pts.push_back(p0);
        pts.push_back(p1);
    }
    if (pts.size() < 2 && hasStart && hasEnd) {
        pts.clear();
        pts.push_back(p0);
        pts.push_back(p1);
    }
    return &m_edgeCache.emplace(edge->id, std::move(pts)).first->second;
}

// La curva geometrica que hay debajo de SURFACE_CURVE, SEAM_CURVE y compania.
const Entity* Builder::basisCurve(const Entity* curve) const {
    for (int depth = 0; curve && depth < 4; ++depth) {
        if (!curve->is("SURFACE_CURVE") && !curve->is("SEAM_CURVE") &&
            !curve->is("INTERSECTION_CURVE") && !curve->is("TRIMMED_CURVE")) {
            return curve;
        }
        const std::vector<Value>& p = curve->params();
        if (p.size() < 2) return nullptr;
        curve = res(p[1]);
    }
    return curve;
}

// Registra la arista como circulo exacto si su curva es CIRCLE. Los angulos se
// calculan igual que en sampleCurve para que el arco coincida con lo dibujado.
void Builder::noteCircle(const Entity* edge, const Mat4& xf) {
    const std::vector<Value>& p = edge->params();
    if (p.size() < 4) return;
    const Entity* curve = basisCurve(res(p[3]));
    if (!curve || !curve->is("CIRCLE")) return;
    const std::vector<Value>& cp = curve->params();
    Frame f;
    if (cp.size() < 3 || !frameOf(cp[1], &f)) return;
    const double radius = numOf(cp, 2, 0.0);
    if (radius <= 0) return;

    Vec3 p0, p1;
    const bool hasStart = p.size() > 1 && vertexPoint(p[1], &p0);
    const bool hasEnd = p.size() > 2 && vertexPoint(p[2], &p1);
    const bool sameSense = p.size() > 4 ? p[4].boolValue() : true;
    auto angleOf = [&](const Vec3& q) {
        const Vec3 d = q - f.origin;
        return std::atan2(dot(d, f.y), dot(d, f.x));
    };
    double start = 0.0, sweep = 2 * kPi;
    if (hasStart && hasEnd && distance(p0, p1) > std::max(m_weldTol, m_tol * 0.05)) {
        start = angleOf(p0);
        sweep = normalizeAngle(angleOf(p1) - start);
        if (sameSense && sweep <= 0) sweep += 2 * kPi;
        if (!sameSense && sweep >= 0) sweep -= 2 * kPi;
    } else if (hasStart) {
        start = angleOf(p0);
        if (!sameSense) sweep = -2 * kPi;
    }

    CircleFeature circle;
    circle.center = xf.point(f.origin);
    circle.xAxis = normalize(xf.direction(f.x));
    circle.normal = normalize(xf.direction(f.z));
    circle.radius = radius * length(xf.direction(f.x));
    circle.startAngle = start;
    circle.sweep = sweep;
    M.features.circles.push_back(circle);
}

// Unidad de longitud: el LENGTH_UNIT de menor numero (casi siempre hay uno solo).
void Builder::detectUnits() {
    int best = 0;
    for (const auto& entry : F.entities()) {
        const Entity& e = entry.second;
        if (!e.part("LENGTH_UNIT") || (best != 0 && e.id > best)) continue;
        LengthUnit unit = LengthUnit::Unknown;
        if (const std::vector<Value>* si = e.part("SI_UNIT")) {
            std::string prefix, name;
            for (const Value& v : *si) {
                if (v.kind != Value::Kind::Enum) continue;
                if (v.text == "METRE") name = v.text;
                else prefix = v.text;
            }
            if (name != "METRE") continue;
            if (prefix == "MILLI") unit = LengthUnit::Millimeter;
            else if (prefix == "CENTI") unit = LengthUnit::Centimeter;
            else if (prefix.empty()) unit = LengthUnit::Meter;
        } else if (const std::vector<Value>* conversion = e.part("CONVERSION_BASED_UNIT")) {
            if (conversion->empty() || (*conversion)[0].kind != Value::Kind::String) continue;
            std::string name = (*conversion)[0].text;
            for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (name == "INCH") unit = LengthUnit::Inch;
            else if (name == "FOOT") unit = LengthUnit::Foot;
            else if (name == "MILLIMETRE" || name == "MILLIMETER") unit = LengthUnit::Millimeter;
        } else {
            continue;
        }
        best = e.id;
        M.units = unit;
    }
}

bool Builder::readBSpline(const Entity* curve, BSpline* out) const {
    const std::vector<Value>* base = curve->part("B_SPLINE_CURVE_WITH_KNOTS");
    const std::vector<Value>* common = curve->part("B_SPLINE_CURVE");
    if (!common) common = base;
    if (!common && !base) {
        common = curve->part("BEZIER_CURVE");
        if (!common) common = curve->part("UNIFORM_CURVE");
        if (!common) common = curve->part("QUASI_UNIFORM_CURVE");
    }
    if (!common || common->size() < 3) return false;

    out->degree = static_cast<int>(numOf(*common, 1, 1));
    const Value& cpList = (*common)[2];
    if (!cpList.isList()) return false;
    for (const Value& v : cpList.items) {
        Vec3 p;
        if (!pointOf(v, &p)) return false;
        out->cps.push_back(p);
    }
    if (out->degree < 1 || out->cps.size() < static_cast<std::size_t>(out->degree) + 1) return false;

    const std::vector<Value>* rational = curve->part("RATIONAL_B_SPLINE_CURVE");
    if (rational && !rational->empty() && rational->back().isList()) {
        for (const Value& v : rational->back().items) out->weights.push_back(v.num);
        if (out->weights.size() != out->cps.size()) out->weights.clear();
    }

    if (base && base->size() >= 9) {
        const Value& mult = (*base)[6];
        const Value& knots = (*base)[7];
        if (mult.isList() && knots.isList() && mult.items.size() == knots.items.size()) {
            for (std::size_t i = 0; i < knots.items.size(); ++i) {
                const int count = static_cast<int>(mult.items[i].num);
                for (int k = 0; k < count; ++k) out->knots.push_back(knots.items[i].num);
            }
        }
    }
    if (out->knots.size() != out->cps.size() + out->degree + 1) {
        // Uniform/Bezier variants: synthesize a clamped knot vector.
        out->knots.clear();
        const int n = static_cast<int>(out->cps.size());
        const int d = out->degree;
        for (int i = 0; i <= d; ++i) out->knots.push_back(0.0);
        for (int i = 1; i < n - d; ++i) out->knots.push_back(static_cast<double>(i) / (n - d));
        for (int i = 0; i <= d; ++i) out->knots.push_back(1.0);
    }
    return out->valid();
}

void Builder::sampleBSpline(const BSpline& sp, const Vec3& p0, const Vec3& p1,
                            std::vector<Vec3>* out) {
    const double t0 = sp.tMin();
    const double t1 = sp.tMax();
    const int coarse = 64;
    std::vector<Vec3> dense;
    dense.reserve(coarse + 1);
    for (int i = 0; i <= coarse; ++i) {
        dense.push_back(sp.eval(t0 + (t1 - t0) * i / coarse));
    }

    // Trim to the edge's own endpoints when the curve extends past them.
    double best0 = 0.0, best1 = 1.0;
    if (distance(p0, p1) > m_weldTol || distance(dense.front(), p0) > m_tol) {
        double d0 = 1e300, d1 = 1e300;
        for (int i = 0; i <= coarse; ++i) {
            const double f = static_cast<double>(i) / coarse;
            const double a = distance(dense[i], p0);
            const double b = distance(dense[i], p1);
            if (a < d0) { d0 = a; best0 = f; }
            if (b < d1) { d1 = b; best1 = f; }
        }
        if (d0 > m_modelSize * 0.02 || d1 > m_modelSize * 0.02) { best0 = 0.0; best1 = 1.0; }
    }
    if (std::fabs(best1 - best0) < 1e-9) { best0 = 0.0; best1 = 1.0; }

    const double span = std::fabs(best1 - best0) * (t1 - t0);
    int steps = 2;
    {
        double len = 0.0;
        for (int i = 1; i <= coarse; ++i) len += distance(dense[i - 1], dense[i]);
        len *= std::fabs(best1 - best0);
        steps = static_cast<int>(std::ceil(std::sqrt(std::max(1.0, len / std::max(m_tol, 1e-9)))) * 2);
        steps = std::max(6, std::min(240, steps));
    }
    (void)span;

    for (int i = 0; i <= steps; ++i) {
        const double f = best0 + (best1 - best0) * i / steps;
        appendUnique(out, sp.eval(t0 + (t1 - t0) * f), m_weldTol);
    }
    if (!out->empty()) {
        if (distance(out->front(), p0) < m_tol * 4) out->front() = p0;
        if (distance(out->back(), p1) < m_tol * 4) out->back() = p1;
    }
}

void Builder::sampleCurve(const Entity* curve, const Vec3& p0, const Vec3& p1, bool sameSense,
                          std::vector<Vec3>* out, int depth) {
    if (!curve || depth > 8) {
        appendUnique(out, p0, m_weldTol);
        appendUnique(out, p1, m_weldTol);
        return;
    }

    if (curve->is("LINE")) {
        appendUnique(out, p0, m_weldTol);
        appendUnique(out, p1, m_weldTol);
        return;
    }

    if (curve->is("CIRCLE") || curve->is("ELLIPSE")) {
        const std::vector<Value>& p = curve->params();
        Frame f;
        if (!frameOf(p[1], &f)) {
            appendUnique(out, p0, m_weldTol);
            appendUnique(out, p1, m_weldTol);
            return;
        }
        const bool ellipse = curve->is("ELLIPSE");
        const double r1 = numOf(p, 2, 1.0);
        const double r2 = ellipse ? numOf(p, 3, r1) : r1;

        auto angleOf = [&](const Vec3& q) {
            const Vec3 d = q - f.origin;
            return std::atan2(dot(d, f.y) / std::max(r2, 1e-12), dot(d, f.x) / std::max(r1, 1e-12));
        };
        const bool closed = distance(p0, p1) <= std::max(m_weldTol, m_tol * 0.05);
        double a0 = angleOf(p0);
        double a1 = angleOf(p1);
        double sweep;
        if (closed) {
            sweep = sameSense ? 2 * kPi : -2 * kPi;
        } else {
            sweep = normalizeAngle(a1 - a0);
            if (sameSense && sweep <= 0) sweep += 2 * kPi;
            if (!sameSense && sweep >= 0) sweep -= 2 * kPi;
        }

        const double rMax = std::max(r1, r2);
        double step = kPi / 12.0;
        if (rMax > 1e-12) {
            const double ratio = std::max(0.0, 1.0 - m_tol / rMax);
            step = 2.0 * std::acos(std::min(1.0, ratio));
            step = std::max(2 * kPi / 180.0, std::min(kPi / 6.0, step));
        }
        int steps = static_cast<int>(std::ceil(std::fabs(sweep) / step));
        steps = std::max(2, std::min(360, steps));

        for (int i = 0; i <= steps; ++i) {
            const double a = a0 + sweep * i / steps;
            const Vec3 q = f.origin + f.x * (r1 * std::cos(a)) + f.y * (r2 * std::sin(a));
            appendUnique(out, q, m_weldTol);
        }
        if (!out->empty() && !closed) {
            out->front() = p0;
            out->back() = p1;
        }
        return;
    }

    if (curve->is("POLYLINE")) {
        const std::vector<Value>& p = curve->params();
        if (p.size() > 1 && p[1].isList()) {
            for (const Value& v : p[1].items) {
                Vec3 q;
                if (pointOf(v, &q)) appendUnique(out, q, m_weldTol);
            }
        }
        if (!sameSense) std::reverse(out->begin(), out->end());
        return;
    }

    if (curve->is("TRIMMED_CURVE")) {
        const std::vector<Value>& p = curve->params();
        const Entity* basis = p.size() > 1 ? res(p[1]) : nullptr;
        Vec3 q0 = p0, q1 = p1;
        if (p.size() > 2 && p[2].isList() && !p[2].items.empty()) pointOf(p[2].items[0], &q0);
        if (p.size() > 3 && p[3].isList() && !p[3].items.empty()) pointOf(p[3].items[0], &q1);
        const bool agree = p.size() > 4 ? p[4].boolValue() : true;
        sampleCurve(basis, q0, q1, sameSense == agree, out, depth + 1);
        return;
    }

    if (curve->is("SURFACE_CURVE") || curve->is("SEAM_CURVE") || curve->is("INTERSECTION_CURVE") ||
        curve->is("BOUNDED_SURFACE_CURVE")) {
        const std::vector<Value>& p = curve->params();
        if (p.size() > 1) {
            sampleCurve(res(p[1]), p0, p1, sameSense, out, depth + 1);
            return;
        }
    }

    if (curve->is("COMPOSITE_CURVE")) {
        const std::vector<Value>& p = curve->params();
        if (p.size() > 1 && p[1].isList()) {
            for (const Value& v : p[1].items) {
                const Entity* seg = res(v);
                if (!seg) continue;
                const std::vector<Value>& sp = seg->params();
                if (sp.size() < 3) continue;
                const bool segSense = sp[1].boolValue();
                std::vector<Vec3> segPts;
                sampleCurve(res(sp[2]), p0, p1, segSense, &segPts, depth + 1);
                for (const Vec3& q : segPts) appendUnique(out, q, m_weldTol);
            }
            if (!sameSense) std::reverse(out->begin(), out->end());
            if (!out->empty()) return;
        }
    }

    BSpline sp;
    if (readBSpline(curve, &sp)) {
        std::vector<Vec3> pts;
        sampleBSpline(sp, sameSense ? p0 : p1, sameSense ? p1 : p0, &pts);
        if (!sameSense) std::reverse(pts.begin(), pts.end());
        for (const Vec3& q : pts) appendUnique(out, q, m_weldTol);
        if (out->size() >= 2) return;
    }

    appendUnique(out, p0, m_weldTol);
    appendUnique(out, p1, m_weldTol);
}

bool Builder::loopPoints(const Entity* loop, std::vector<Vec3>* pts, const Mat4& xf) {
    if (!loop) return false;

    if (loop->is("POLY_LOOP")) {
        const std::vector<Value>& p = loop->params();
        if (p.size() < 2 || !p[1].isList()) return false;
        for (const Value& v : p[1].items) {
            Vec3 q;
            if (pointOf(v, &q)) appendUnique(pts, q, m_weldTol);
        }
        for (std::size_t i = 1; i < pts->size(); ++i) {
            M.addSegment(xf.point((*pts)[i - 1]), xf.point((*pts)[i]));
        }
        if (pts->size() > 2) M.addSegment(xf.point(pts->back()), xf.point(pts->front()));
        return pts->size() >= 3;
    }

    if (!loop->is("EDGE_LOOP")) return false;
    const std::vector<Value>& p = loop->params();
    if (p.size() < 2 || !p[1].isList()) return false;

    for (const Value& v : p[1].items) {
        const Entity* oriented = res(v);
        if (!oriented) continue;
        const std::vector<Value>& op = oriented->params();
        if (op.size() < 5) continue;
        const Entity* edge = res(op[3]);
        if (!edge) continue;
        const bool orientation = op[4].boolValue();

        const std::vector<Vec3>* sampled = edgePoints(edge);
        if (!sampled || sampled->size() < 2) continue;

        if (m_edgeEmitted.insert(edge->id).second) {
            const std::vector<Value>& ep = edge->params();
            const Entity* basis = ep.size() > 3 ? basisCurve(res(ep[3])) : nullptr;
            // Solo las rectas ofrecen extremos y puntos medios para medir.
            const bool curved = basis && !basis->is("LINE") && !basis->is("POLYLINE");
            for (std::size_t i = 1; i < sampled->size(); ++i) {
                M.addSegment(xf.point((*sampled)[i - 1]), xf.point((*sampled)[i]), curved);
            }
            noteCircle(edge, xf);
        }
        if (orientation) {
            for (const Vec3& q : *sampled) appendUnique(pts, q, m_weldTol);
        } else {
            for (auto it = sampled->rbegin(); it != sampled->rend(); ++it) {
                appendUnique(pts, *it, m_weldTol);
            }
        }
    }
    if (pts->size() > 2 && distance(pts->front(), pts->back()) <= m_weldTol) pts->pop_back();

    // Un contorno con decenas de miles de puntos dispara el coste del recorte;
    // se diezma de forma uniforme, que a esa densidad no se nota.
    const std::size_t maxLoopPoints = 6000;
    if (pts->size() > maxLoopPoints) {
        const std::size_t stride = pts->size() / maxLoopPoints + 1;
        std::vector<Vec3> reduced;
        reduced.reserve(pts->size() / stride + 2);
        for (std::size_t i = 0; i < pts->size(); i += stride) reduced.push_back((*pts)[i]);
        pts->swap(reduced);
    }
    return pts->size() >= 3;
}

// --- surfaces and faces -----------------------------------------------------

bool Builder::surfaceOf(const Entity* surf, Surface* out) const {
    if (!surf) return false;
    const std::vector<Value>& p = surf->params();

    if (surf->is("PLANE")) {
        out->type = Surface::Type::Plane;
        return p.size() > 1 && frameOf(p[1], &out->frame);
    }
    if (surf->is("CYLINDRICAL_SURFACE")) {
        out->type = Surface::Type::Cylinder;
        out->radius = numOf(p, 2, 1.0);
        return p.size() > 2 && frameOf(p[1], &out->frame);
    }
    if (surf->is("CONICAL_SURFACE")) {
        out->type = Surface::Type::Cone;
        out->radius = numOf(p, 2, 0.0);
        out->halfAngle = numOf(p, 3, 0.0);
        return p.size() > 3 && frameOf(p[1], &out->frame);
    }
    if (surf->is("SPHERICAL_SURFACE")) {
        out->type = Surface::Type::Sphere;
        out->radius = numOf(p, 2, 1.0);
        return p.size() > 2 && frameOf(p[1], &out->frame);
    }
    if (surf->is("TOROIDAL_SURFACE")) {
        out->type = Surface::Type::Torus;
        out->radius = numOf(p, 2, 1.0);
        out->minorRadius = numOf(p, 3, 0.1);
        return p.size() > 3 && frameOf(p[1], &out->frame);
    }
    return false;  // B-spline and swept surfaces use the planar fallback
}

// Grid spacing that keeps the chord error under the deflection tolerance.
void Builder::surfaceSteps(const Surface& surf, const std::vector<Vec2>& domain, double* uStep,
                           double* vStep) const {
    auto angular = [&](double radius) {
        if (radius <= 1e-12) return kPi / 6.0;
        const double ratio = std::max(0.0, 1.0 - m_tol / radius);
        const double step = 2.0 * std::acos(std::min(1.0, ratio));
        return std::max(2 * kPi / 256.0, std::min(kPi / 6.0, step));
    };

    *uStep = 0.0;
    *vStep = 0.0;
    switch (surf.type) {
        case Surface::Type::Cylinder:
            *uStep = angular(surf.radius);
            break;
        case Surface::Type::Cone: {
            double maxRadius = std::fabs(surf.radius);
            const double slope = std::tan(surf.halfAngle);
            for (const Vec2& p : domain) {
                maxRadius = std::max(maxRadius, std::fabs(surf.radius + p.y * slope));
            }
            *uStep = angular(maxRadius);
            break;
        }
        case Surface::Type::Sphere:
            *uStep = angular(surf.radius);
            *vStep = *uStep;
            break;
        case Surface::Type::Torus:
            *uStep = angular(surf.radius + surf.minorRadius);
            *vStep = angular(surf.minorRadius);
            break;
        case Surface::Type::Plane:
        case Surface::Type::Freeform:
            break;
    }
}

void Builder::addPlanarFallback(const std::vector<std::vector<Vec3>>& loops, const Mat4& xf,
                                bool flipped) {
    if (loops.empty() || loops[0].size() < 3) { ++M.facesFailed; return; }

    // Newell's method gives a stable normal even for slightly non-planar loops.
    Vec3 n;
    Vec3 centroid;
    int count = 0;
    for (const auto& loop : loops) {
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const Vec3& a = loop[i];
            const Vec3& b = loop[(i + 1) % loop.size()];
            n.x += (a.y - b.y) * (a.z + b.z);
            n.y += (a.z - b.z) * (a.x + b.x);
            n.z += (a.x - b.x) * (a.y + b.y);
        }
    }
    for (const Vec3& p : loops[0]) { centroid += p; ++count; }
    if (count == 0 || length(n) < 1e-18) { ++M.facesFailed; return; }
    centroid = centroid * (1.0 / count);

    const Frame f = makeFrame(centroid, normalize(n), loops[0][0] - centroid);

    std::vector<std::vector<Vec2>> uvLoops;
    std::vector<Vec3> flat;
    for (const auto& loop : loops) {
        std::vector<Vec2> uv;
        for (const Vec3& p : loop) {
            const Vec3 d = p - f.origin;
            uv.emplace_back(dot(d, f.x), dot(d, f.y));
            flat.push_back(p);
        }
        uvLoops.push_back(std::move(uv));
    }

    std::vector<Vec2> verts;
    std::vector<std::array<int, 3>> tris;
    std::vector<int> source;
    if (!triangulatePolygon(uvLoops, &verts, &tris, &source)) { ++M.facesFailed; return; }

    Vec3 normalWorld = normalize(xf.direction(f.z));
    if (flipped) normalWorld = -normalWorld;

    std::vector<std::uint32_t> ids(verts.size());
    for (std::size_t i = 0; i < verts.size(); ++i) {
        const int src = i < source.size() ? source[i] : -1;
        const Vec3 p = (src >= 0 && static_cast<std::size_t>(src) < flat.size())
                           ? flat[src]
                           : f.toWorld(Vec3(verts[i].x, verts[i].y, 0));
        ids[i] = M.addVertex(xf.point(p), normalWorld);
    }
    for (const auto& t : tris) {
        const Vec3 a = M.positions[ids[t[0]]];
        const Vec3 b = M.positions[ids[t[1]]];
        const Vec3 c = M.positions[ids[t[2]]];
        if (dot(cross(b - a, c - a), normalWorld) >= 0) {
            M.addTriangle(ids[t[0]], ids[t[1]], ids[t[2]]);
        } else {
            M.addTriangle(ids[t[0]], ids[t[2]], ids[t[1]]);
        }
    }
    ++M.faces;
}

// Closed surfaces (a whole sphere or torus) are written with a VERTEX_LOOP:
// the face is the entire natural parameter domain, with nothing trimmed away.
void Builder::addFullSurface(const Surface& surf, const Mat4& xf) {
    double vLo = 0.0, vHi = 0.0;
    if (surf.type == Surface::Type::Sphere) {
        vLo = -kPi * 0.5;
        vHi = kPi * 0.5;
    } else if (surf.type == Surface::Type::Torus) {
        vLo = -kPi;
        vHi = kPi;
    } else {
        ++M.facesFailed;
        return;
    }

    std::vector<Vec2> domain = {Vec2(-kPi, vLo), Vec2(kPi, vLo), Vec2(kPi, vHi), Vec2(-kPi, vHi)};
    double uStep = 0.0, vStep = 0.0;
    surfaceSteps(surf, domain, &uStep, &vStep);

    std::vector<Vec2> verts;
    std::vector<std::array<int, 3>> tris;
    if (!triangulateGrid(domain, uStep, vStep, &verts, &tris)) { ++M.facesFailed; return; }

    std::vector<std::uint32_t> ids(verts.size());
    for (std::size_t i = 0; i < verts.size(); ++i) {
        const Vec3 p = surf.eval(verts[i].x, verts[i].y);
        const Vec3 n = surf.normal(verts[i].x, verts[i].y);
        ids[i] = M.addVertex(xf.point(p), normalize(xf.direction(n)));
    }
    for (const auto& t : tris) {
        const Vec3 a = M.positions[ids[t[0]]];
        const Vec3 b = M.positions[ids[t[1]]];
        const Vec3 c = M.positions[ids[t[2]]];
        const Vec3 shade = M.normals[ids[t[0]]] + M.normals[ids[t[1]]] + M.normals[ids[t[2]]];
        if (dot(cross(b - a, c - a), shade) >= 0) {
            M.addTriangle(ids[t[0]], ids[t[1]], ids[t[2]]);
        } else {
            M.addTriangle(ids[t[0]], ids[t[2]], ids[t[1]]);
        }
    }
    ++M.faces;
}

void Builder::addTessellated(const Surface& surf, const std::vector<std::vector<Vec3>>& loops,
                             const Mat4& xf) {
    if (loops.empty() || loops[0].size() < 3) { ++M.facesFailed; return; }

    // Project the loops into parameter space, unwrapping periodic directions so
    // an edge that crosses the seam stays continuous.
    std::vector<std::vector<Vec2>> uvLoops;
    std::vector<std::vector<Vec3>> loopPts;
    double outerU0 = 0.0, outerU1 = 0.0;

    for (std::size_t li = 0; li < loops.size(); ++li) {
        const std::vector<Vec3>& loop = loops[li];
        std::vector<Vec2> uv;
        uv.reserve(loop.size());
        double prevU = 0.0, prevV = 0.0;
        for (std::size_t i = 0; i < loop.size(); ++i) {
            Vec2 t = surf.invert(loop[i]);
            if (i > 0) {
                if (surf.uPeriodic()) {
                    while (t.x - prevU > kPi) t.x -= 2 * kPi;
                    while (prevU - t.x > kPi) t.x += 2 * kPi;
                }
                if (surf.vPeriodic()) {
                    while (t.y - prevV > kPi) t.y -= 2 * kPi;
                    while (prevV - t.y > kPi) t.y += 2 * kPi;
                }
            }
            prevU = t.x;
            prevV = t.y;
            uv.push_back(t);
        }
        // A closed loop that wraps the seam ends one period away from its start.
        if (surf.uPeriodic() && uv.size() > 2) {
            const double wrap = uv.back().x - uv.front().x;
            if (std::fabs(std::fabs(wrap) - 2 * kPi) < 0.35) {
                // keep as is: the loop legitimately spans the full period
            }
        }

        double u0 = 1e300, u1 = -1e300;
        for (const Vec2& t : uv) { u0 = std::min(u0, t.x); u1 = std::max(u1, t.x); }
        if (li == 0) {
            outerU0 = u0;
            outerU1 = u1;
        } else if (surf.uPeriodic()) {
            // Shift holes into the same period as the outer loop.
            const double center = (u0 + u1) * 0.5;
            const double target = (outerU0 + outerU1) * 0.5;
            const double shift = std::round((target - center) / (2 * kPi)) * 2 * kPi;
            if (shift != 0.0) {
                for (Vec2& t : uv) t.x += shift;
            }
        }
        uvLoops.push_back(std::move(uv));
        loopPts.push_back(loop);
    }

    std::vector<Vec2> verts;
    std::vector<std::array<int, 3>> tris;
    std::vector<int> source;
    std::vector<Vec3> loopFlat;
    for (const auto& loop : loopPts) {
        for (const Vec3& p : loop) loopFlat.push_back(p);
    }
    std::vector<Vec3> exact;
    std::vector<char> hasExact;

    if (surf.type == Surface::Type::Plane) {
        if (!triangulatePolygon(uvLoops, &verts, &tris, &source)) { ++M.facesFailed; return; }
        // Keep the exact 3D points of the boundary; source maps output vertices
        // back to them because the triangulator may re-orient the loops.
        exact.assign(verts.size(), Vec3());
        hasExact.assign(verts.size(), 0);
        for (std::size_t i = 0; i < verts.size(); ++i) {
            const int src = i < source.size() ? source[i] : -1;
            if (src >= 0 && static_cast<std::size_t>(src) < loopFlat.size()) {
                exact[i] = loopFlat[src];
                hasExact[i] = 1;
            }
        }
    } else {
        std::vector<Vec2> merged;
        if (!mergeLoops(uvLoops, &merged, nullptr)) { ++M.facesFailed; return; }
        double uStep = 0.0, vStep = 0.0;
        surfaceSteps(surf, merged, &uStep, &vStep);
        if (!triangulateGrid(merged, uStep, vStep, &verts, &tris) &&
            !triangulatePolygon(uvLoops, &verts, &tris, &source)) {
            ++M.facesFailed;
            return;
        }
    }

    auto vertexAt = [&](std::size_t i) -> Vec3 {
        if (i < hasExact.size() && hasExact[i]) return exact[i];
        return surf.eval(verts[i].x, verts[i].y);
    };

    std::vector<std::uint32_t> ids(verts.size());
    for (std::size_t i = 0; i < verts.size(); ++i) {
        const Vec3 p = vertexAt(i);
        Vec3 n = surf.normal(verts[i].x, verts[i].y);
        ids[i] = M.addVertex(xf.point(p), normalize(xf.direction(n)));
    }
    for (const auto& t : tris) {
        const Vec3 a = M.positions[ids[t[0]]];
        const Vec3 b = M.positions[ids[t[1]]];
        const Vec3 c = M.positions[ids[t[2]]];
        const Vec3 geo = cross(b - a, c - a);
        const Vec3 shade = M.normals[ids[t[0]]] + M.normals[ids[t[1]]] + M.normals[ids[t[2]]];
        if (dot(geo, shade) >= 0) {
            M.addTriangle(ids[t[0]], ids[t[1]], ids[t[2]]);
        } else {
            M.addTriangle(ids[t[0]], ids[t[2]], ids[t[1]]);
        }
    }
    ++M.faces;
}

void Builder::buildFace(const Entity* face, const Mat4& xf) {
    const std::size_t first = M.triangleCount();
    Surface surface;
    bool known = false;
    buildFaceTriangles(face, xf, &surface, &known);
    const std::size_t count = M.triangleCount() - first;
    if (count == 0) return;

    FaceFeature feature;
    feature.firstTriangle = static_cast<std::uint32_t>(first);
    feature.triangleCount = static_cast<std::uint32_t>(count);
    feature.kind = SurfaceKind::Plane;  // sin superficie reconocida se malla como plano
    if (known) {
        switch (surface.type) {
            case Surface::Type::Plane: feature.kind = SurfaceKind::Plane; break;
            case Surface::Type::Cylinder: feature.kind = SurfaceKind::Cylinder; break;
            case Surface::Type::Cone: feature.kind = SurfaceKind::Cone; break;
            case Surface::Type::Sphere: feature.kind = SurfaceKind::Sphere; break;
            case Surface::Type::Torus: feature.kind = SurfaceKind::Torus; break;
            case Surface::Type::Freeform: feature.kind = SurfaceKind::Other; break;
        }
        feature.radius = surface.radius * length(xf.direction(surface.frame.x));
        feature.axisOrigin = xf.point(surface.frame.origin);
        feature.axisDir = normalize(xf.direction(surface.frame.z));
    }
    M.features.faces.push_back(feature);
}

void Builder::buildFaceTriangles(const Entity* face, const Mat4& xf, Surface* surface, bool* known) {
    if (outOfTime()) return;
    const std::vector<Value>& p = face->params();
    if (p.size() < 3 || !p[1].isList()) return;

    std::vector<std::vector<Vec3>> loops;
    std::vector<char> isOuter;
    for (const Value& v : p[1].items) {
        const Entity* bound = res(v);
        if (!bound) continue;
        const std::vector<Value>& bp = bound->params();
        if (bp.size() < 2) continue;
        std::vector<Vec3> pts;
        if (!loopPoints(res(bp[1]), &pts, xf)) continue;
        const bool orientation = bp.size() > 2 ? bp[2].boolValue() : true;
        if (!orientation) std::reverse(pts.begin(), pts.end());
        loops.push_back(std::move(pts));
        isOuter.push_back(bound->is("FACE_OUTER_BOUND") ? 1 : 0);
    }
    if (loops.empty()) {
        Surface full;
        if (surfaceOf(res(p[2]), &full)) {
            full.flipped = p.size() > 3 ? !p[3].boolValue() : false;
            *surface = full;
            *known = true;
            addFullSurface(full, xf);
        } else {
            ++M.facesFailed;
        }
        return;
    }

    // Put the outer boundary first; without the explicit flag use the largest loop.
    std::size_t outerIdx = 0;
    bool found = false;
    for (std::size_t i = 0; i < loops.size(); ++i) {
        if (isOuter[i]) { outerIdx = i; found = true; break; }
    }
    if (!found) {
        double best = -1.0;
        for (std::size_t i = 0; i < loops.size(); ++i) {
            BBox b;
            for (const Vec3& q : loops[i]) b.add(q);
            if (b.diagonal() > best) { best = b.diagonal(); outerIdx = i; }
        }
    }
    if (outerIdx != 0) std::swap(loops[0], loops[outerIdx]);

    const bool sameSense = p.size() > 3 ? p[3].boolValue() : true;
    Surface surf;
    if (surfaceOf(res(p[2]), &surf)) {
        surf.flipped = !sameSense;
        *surface = surf;
        *known = true;
        addTessellated(surf, loops, xf);
    } else {
        addPlanarFallback(loops, xf, !sameSense);
    }
}

bool Builder::run(std::string* error, LoadStats* stats) {
    detectUnits();
    estimateSize();
    collectInstances();

    if (M.empty()) {
        if (error) *error = "el archivo no contiene solidos ni superficies reconocibles";
        return false;
    }
    if (stats) {
        stats->schema = F.schema();
        stats->name = F.name();
        stats->solids = M.solids;
        stats->faces = M.faces;
        stats->facesFailed = M.facesFailed;
        stats->triangles = M.triangleCount();
        stats->deflection = m_tol;
        stats->truncated = m_truncated;
    }
    return true;
}

}  // namespace

bool buildMesh(const StepFile& file, Mesh* mesh, double quality, std::string* error,
               LoadStats* stats, int budgetMs) {
    Builder builder(file, mesh, quality, budgetMs);
    return builder.run(error, stats);
}

bool loadStepFile(const std::string& path, Mesh* mesh, std::string* error, LoadStats* stats,
                  double quality, int budgetMs) {
    StepFile file;
    if (!file.parseFile(path, error)) return false;
    return buildMesh(file, mesh, quality, error, stats, budgetMs);
}

bool loadStepMemory(const char* data, std::size_t len, Mesh* mesh, std::string* error,
                    LoadStats* stats, double quality, int budgetMs) {
    StepFile file;
    if (!file.parse(data, len, error)) return false;
    return buildMesh(file, mesh, quality, error, stats, budgetMs);
}

}  // namespace stp
