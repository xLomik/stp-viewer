#include "planar.h"

#include <cmath>
#include <utility>
#include <vector>

namespace stp {

namespace {

// Diagonaliza una matriz simetrica 3x3 por rotaciones de Jacobi. Deja los
// valores propios en values y los vectores propios en las columnas de vectors.
void symmetricEigen(double a[3][3], double values[3], double vectors[3][3]) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) vectors[i][j] = i == j ? 1.0 : 0.0;
    }
    for (int sweep = 0; sweep < 50; ++sweep) {
        const double off = a[0][1] * a[0][1] + a[0][2] * a[0][2] + a[1][2] * a[1][2];
        const double diag = a[0][0] * a[0][0] + a[1][1] * a[1][1] + a[2][2] * a[2][2];
        if (off <= 1e-30 * diag || off == 0.0) break;
        for (int p = 0; p < 2; ++p) {
            for (int q = p + 1; q < 3; ++q) {
                if (a[p][q] == 0.0) continue;
                const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                const double t = (theta >= 0 ? 1.0 : -1.0) /
                                 (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0);
                const double s = t * c;
                for (int k = 0; k < 3; ++k) {
                    const double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 3; ++k) {
                    const double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 3; ++k) {
                    const double vkp = vectors[k][p], vkq = vectors[k][q];
                    vectors[k][p] = c * vkp - s * vkq;
                    vectors[k][q] = s * vkp + c * vkq;
                }
            }
        }
    }
    for (int i = 0; i < 3; ++i) values[i] = a[i][i];
}

template <typename Visit>
void forEachPoint(const Mesh& mesh, Visit visit) {
    for (const Vec3& p : mesh.positions) visit(p);
    for (const Vec3& p : mesh.edgeLines) visit(p);
    for (const MeshText& text : mesh.texts) {
        Vec3 corners[4];
        textCorners(text, corners);
        for (const Vec3& c : corners) visit(c);
    }
}

// Elige el sentido de la normal y los ejes de pantalla. Planos mas bien
// horizontales: se miran desde arriba con la Y del dibujo hacia arriba.
// Verticales: con la Z hacia arriba y el eje horizontal hacia +X (o +Y).
void orient(Vec3 n, PlanarInfo* info) {
    n = normalize(n);
    if (std::fabs(n.z) >= 0.7071) {
        if (n.z < 0) n = -n;
        const Vec3 up(0, 1, 0);
        info->v = normalize(up - n * dot(up, n));
        info->u = cross(info->v, n);
    } else {
        const Vec3 up(0, 0, 1);
        const Vec3 v = normalize(up - n * dot(up, n));
        Vec3 u = cross(v, n);
        const bool flip = std::fabs(u.x) >= std::fabs(u.y) ? u.x < 0 : u.y < 0;
        if (flip) {
            n = -n;
            u = -u;
        }
        info->v = v;
        info->u = u;
    }
    info->normal = n;
}

}  // namespace

PlanarInfo detectPlanar(const Mesh& mesh) {
    PlanarInfo info;

    std::size_t count = 0;
    Vec3 sum;
    BBox box;
    forEachPoint(mesh, [&](const Vec3& p) {
        sum += p;
        box.add(p);
        ++count;
    });
    if (count < 2 || !box.valid()) return info;
    const double size = length(box.size());
    if (!(size > 1e-12) || !std::isfinite(size)) return info;

    const Vec3 centroid = sum * (1.0 / static_cast<double>(count));
    double cov[3][3] = {};
    forEachPoint(mesh, [&](const Vec3& p) {
        const Vec3 d = p - centroid;
        const double c[3] = {d.x, d.y, d.z};
        for (int i = 0; i < 3; ++i) {
            for (int j = i; j < 3; ++j) cov[i][j] += c[i] * c[j];
        }
    });
    cov[1][0] = cov[0][1];
    cov[2][0] = cov[0][2];
    cov[2][1] = cov[1][2];

    double values[3], vectors[3][3];
    symmetricEigen(cov, values, vectors);
    int order[3] = {0, 1, 2};  // de menor a mayor
    for (int i = 0; i < 3; ++i) {
        for (int j = i + 1; j < 3; ++j) {
            if (values[order[j]] < values[order[i]]) std::swap(order[i], order[j]);
        }
    }
    auto column = [&](int k) { return Vec3(vectors[0][k], vectors[1][k], vectors[2][k]); };

    Vec3 normal = column(order[0]);
    if (values[order[1]] <= 1e-18 * values[order[2]]) {
        // Todo sobre una recta: cualquier plano que la contenga vale; se toma
        // el que deja verla desde arriba o, si es vertical, de frente.
        const Vec3 line = normalize(column(order[2]));
        const Vec3 z(0, 0, 1), y(0, 1, 0);
        normal = std::fabs(dot(line, z)) < 0.9 ? z - line * dot(z, line) : y - line * dot(y, line);
    }
    orient(normal, &info);

    double deviation = 0.0;
    double uLo = 1e300, uHi = -1e300, vLo = 1e300, vHi = -1e300;
    forEachPoint(mesh, [&](const Vec3& p) {
        const Vec3 d = p - centroid;
        deviation = std::max(deviation, std::fabs(dot(d, info.normal)));
        const double pu = dot(d, info.u), pv = dot(d, info.v);
        uLo = std::min(uLo, pu);
        uHi = std::max(uHi, pu);
        vLo = std::min(vLo, pv);
        vHi = std::max(vHi, pv);
    });

    info.planar = deviation <= 1e-3 * size;
    info.width = uHi - uLo;
    info.height = vHi - vLo;
    info.center = centroid + info.u * (0.5 * (uLo + uHi)) + info.v * (0.5 * (vLo + vHi));
    return info;
}

}  // namespace stp
