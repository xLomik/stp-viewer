// Elementos medibles: lo que los lectores saben de la geometria exacta
// (circulos, caras, contornos cerrados y unidades) y que la malla de
// triangulos y segmentos ya no conserva.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "geom.h"

namespace stp {

enum class LengthUnit { Unknown, Millimeter, Centimeter, Meter, Inch, Foot };

// "mm", "cm", "m", "in", "ft" o "" si el archivo no declara unidades.
const char* unitSuffix(LengthUnit unit);

// Circulo o arco. Los angulos se miden desde xAxis girando alrededor de normal.
struct CircleFeature {
    Vec3 center;
    Vec3 normal{0, 0, 1};
    Vec3 xAxis{1, 0, 0};
    double radius = 0.0;
    double startAngle = 0.0;
    double sweep = 2 * kPi;  // con signo; |sweep| >= 2*pi es circulo completo

    bool full() const { return std::fabs(sweep) >= 2 * kPi - 1e-9; }
    Vec3 pointAt(double angle) const;
};

enum class SurfaceKind { Plane, Cylinder, Cone, Sphere, Torus, Other };

// Cara de un solido: sus triangulos son indices.[3*first, 3*(first+count)).
struct FaceFeature {
    std::uint32_t firstTriangle = 0;
    std::uint32_t triangleCount = 0;
    SurfaceKind kind = SurfaceKind::Other;
    double radius = 0.0;  // cilindro y esfera
    Vec3 axisOrigin;      // cilindro
    Vec3 axisDir{0, 0, 1};
};

// Contorno cerrado de un plano. bulges[i] es el bulge del tramo
// points[i] -> points[(i+1) % n], medido alrededor de normal (0 = recta).
struct ContourFeature {
    std::vector<Vec3> points;
    std::vector<double> bulges;
    Vec3 normal{0, 0, 1};
};

struct MeshFeatures {
    std::vector<CircleFeature> circles;
    std::vector<FaceFeature> faces;
    std::vector<ContourFeature> contours;
};

}  // namespace stp
