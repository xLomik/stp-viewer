// Curvas y superficies NURBS (B-spline racionales), evaluadas con de Boor.
// Las usan el lector IGES y las SPLINE del DXF.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "geom.h"

namespace stp {

struct NurbsCurve {
    int degree = 1;
    std::vector<double> knots;
    std::vector<double> weights;
    std::vector<Vec3> controls;

    bool valid() const {
        return degree >= 1 && controls.size() >= static_cast<std::size_t>(degree) + 1 &&
               knots.size() == controls.size() + degree + 1;
    }
    double tMin() const { return knots[degree]; }
    double tMax() const { return knots[knots.size() - degree - 1]; }

    Vec3 eval(double t) const {
        const int n = static_cast<int>(controls.size()) - 1;
        t = std::max(tMin(), std::min(tMax(), t));
        int span = degree;
        while (span < n && t >= knots[span + 1]) ++span;

        std::vector<Vec3> d(degree + 1);
        std::vector<double> w(degree + 1);
        for (int j = 0; j <= degree; ++j) {
            const int index = span - degree + j;
            const double weight = weights.empty() ? 1.0 : weights[index];
            d[j] = controls[index] * weight;
            w[j] = weight;
        }
        for (int r = 1; r <= degree; ++r) {
            for (int j = degree; j >= r; --j) {
                const int i = span - degree + j;
                const double den = knots[i + degree - r + 1] - knots[i];
                const double alpha = den > 1e-15 ? (t - knots[i]) / den : 0.0;
                d[j] = d[j - 1] * (1.0 - alpha) + d[j] * alpha;
                w[j] = w[j - 1] * (1.0 - alpha) + w[j] * alpha;
            }
        }
        return std::fabs(w[degree]) > 1e-15 ? d[degree] * (1.0 / w[degree]) : d[degree];
    }
};

struct NurbsSurface {
    int degreeU = 1, degreeV = 1;
    int countU = 0, countV = 0;  // numero de puntos de control
    std::vector<double> knotsU, knotsV;
    std::vector<double> weights;   // countU * countV, orden v mas rapido
    std::vector<Vec3> controls;

    bool valid() const {
        return countU > degreeU && countV > degreeV &&
               knotsU.size() == static_cast<std::size_t>(countU + degreeU + 1) &&
               knotsV.size() == static_cast<std::size_t>(countV + degreeV + 1) &&
               controls.size() == static_cast<std::size_t>(countU) * countV;
    }
    double uMin() const { return knotsU[degreeU]; }
    double uMax() const { return knotsU[knotsU.size() - degreeU - 1]; }
    double vMin() const { return knotsV[degreeV]; }
    double vMax() const { return knotsV[knotsV.size() - degreeV - 1]; }

    Vec3 eval(double u, double v) const {
        // De Boor en u sobre curvas aisladas en v.
        u = std::max(uMin(), std::min(uMax(), u));
        v = std::max(vMin(), std::min(vMax(), v));

        int spanU = degreeU;
        while (spanU < countU - 1 && u >= knotsU[spanU + 1]) ++spanU;
        int spanV = degreeV;
        while (spanV < countV - 1 && v >= knotsV[spanV + 1]) ++spanV;

        std::vector<Vec3> temp(degreeU + 1);
        std::vector<double> tempW(degreeU + 1);
        for (int i = 0; i <= degreeU; ++i) {
            const int ui = spanU - degreeU + i;
            std::vector<Vec3> row(degreeV + 1);
            std::vector<double> rowW(degreeV + 1);
            for (int j = 0; j <= degreeV; ++j) {
                const int vj = spanV - degreeV + j;
                const std::size_t index = static_cast<std::size_t>(ui) * countV + vj;
                const double weight = weights.empty() ? 1.0 : weights[index];
                row[j] = controls[index] * weight;
                rowW[j] = weight;
            }
            for (int r = 1; r <= degreeV; ++r) {
                for (int j = degreeV; j >= r; --j) {
                    const int k = spanV - degreeV + j;
                    const double den = knotsV[k + degreeV - r + 1] - knotsV[k];
                    const double alpha = den > 1e-15 ? (v - knotsV[k]) / den : 0.0;
                    row[j] = row[j - 1] * (1.0 - alpha) + row[j] * alpha;
                    rowW[j] = rowW[j - 1] * (1.0 - alpha) + rowW[j] * alpha;
                }
            }
            temp[i] = row[degreeV];
            tempW[i] = rowW[degreeV];
        }
        for (int r = 1; r <= degreeU; ++r) {
            for (int i = degreeU; i >= r; --i) {
                const int k = spanU - degreeU + i;
                const double den = knotsU[k + degreeU - r + 1] - knotsU[k];
                const double alpha = den > 1e-15 ? (u - knotsU[k]) / den : 0.0;
                temp[i] = temp[i - 1] * (1.0 - alpha) + temp[i] * alpha;
                tempW[i] = tempW[i - 1] * (1.0 - alpha) + tempW[i] * alpha;
            }
        }
        return std::fabs(tempW[degreeU]) > 1e-15 ? temp[degreeU] * (1.0 / tempW[degreeU])
                                                 : temp[degreeU];
    }

    Vec3 normal(double u, double v) const {
        const double du = (uMax() - uMin()) * 1e-4 + 1e-9;
        const double dv = (vMax() - vMin()) * 1e-4 + 1e-9;
        const Vec3 p = eval(u, v);
        const Vec3 pu = eval(std::min(u + du, uMax()), v) - p;
        const Vec3 pv = eval(u, std::min(v + dv, vMax())) - p;
        const Vec3 n = cross(pu, pv);
        return length(n) > 1e-18 ? normalize(n) : Vec3(0, 0, 1);
    }
};

}  // namespace stp
