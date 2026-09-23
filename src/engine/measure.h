// Medir sobre la malla: rayo desde un pixel, indice de seleccion, enganche a
// puntos notables y calculos de distancia, angulo, radio, area y perimetro.
// Sin dependencias de Windows: se prueba en Linux.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../render/renderer.h"
#include "bvh.h"
#include "mesh.h"
#include "planar.h"

namespace stp {

// ortho: la camara es ortografica y el rayo es una recta completa (la geometria
// puede estar detras del plano del ojo y aun asi se dibuja).
struct Ray {
    Vec3 origin;
    Vec3 dir{0, 0, -1};
    bool ortho = true;
};

Ray pixelRay(const Camera& camera, int width, int height, double sx, double sy);
// Lo que mide un pixel en unidades del modelo a la distancia t sobre el rayo.
double worldPerPixel(const Camera& camera, int height, double t);
bool rayTriangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c, double* t);

enum class SnapKind { None, Endpoint, Center, Midpoint, OnEdge, OnFace, OnPlane };

struct SnapResult {
    SnapKind kind = SnapKind::None;
    Vec3 point;
    int triangle = -1;  // triangulo bajo el cursor, si lo hay
    int segment = -1;   // Endpoint, Midpoint y OnEdge sobre un segmento
    int circle = -1;    // Center y extremos de arcos
};

struct SnapOptions {
    double radiusPixels = 8.0;
    bool snapping = true;  // Shift lo apaga
    const PlanarInfo* plane = nullptr;
};

class PickIndex {
public:
    void build(const Mesh& mesh);
    bool empty() const { return m_triangles.empty() && m_segments.empty(); }
    bool raycast(const Mesh& mesh, const Ray& ray, double* t, int* triangle) const;
    SnapResult snap(const Mesh& mesh, const Camera& camera, int width, int height, double sx,
                    double sy, const SnapOptions& options) const;

private:
    struct SnapPoint {
        Vec3 point;
        SnapKind kind = SnapKind::Endpoint;
        int segment = -1;
        int circle = -1;
    };
    Bvh m_triangles;
    Bvh m_segments;
    Bvh m_points;
    std::vector<SnapPoint> m_snapPoints;
    double m_size = 1.0;
};

}  // namespace stp
