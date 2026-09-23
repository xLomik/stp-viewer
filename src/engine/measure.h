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
    // Triangulo que contiene point (a menos de tolerance de su plano), o -1.
    int triangleAt(const Mesh& mesh, const Vec3& point, double tolerance) const;

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

// --- Calculos ----------------------------------------------------------------

struct DistanceResult {
    double total = 0.0;
    Vec3 delta;  // en ejes del mundo; con plano, (du, dv, 0) en los ejes del dibujo
};
DistanceResult measureDistance(const Vec3& a, const Vec3& b, const PlanarInfo* plane);

// Angulo en vertex entre las semirrectas hacia a y hacia b, en grados (0..180).
double angleAt(const Vec3& a, const Vec3& vertex, const Vec3& b);
// Angulo entre dos rectas, del lado de los puntos donde se hizo clic, en grados.
double angleBetweenLines(const Vec3& a0, const Vec3& a1, const Vec3& pickA, const Vec3& b0,
                         const Vec3& b1, const Vec3& pickB);

struct RadiusResult {
    bool ok = false;
    double radius = 0.0;
    Vec3 center;
    Vec3 normal{0, 0, 1};
    std::string error;
};
// tolerance: distancia maxima (unidades del modelo) del punto al circulo o a su centro.
RadiusResult measureRadius(const Mesh& mesh, const Vec3& point, int triangle, double tolerance);

struct AreaResult {
    bool ok = false;
    double area = 0.0;
    double perimeter = 0.0;             // borde exterior mas bordes de agujeros
    std::vector<std::uint32_t> triangles;  // cara medida (3D)
    int contour = -1;                   // contorno medido (plano)
    std::vector<int> holes;             // contornos restados
    std::string error;
};
AreaResult measureArea(const Mesh& mesh, const Vec3& point, int triangle, const PlanarInfo* plane);

double contourArea(const ContourFeature& contour);
double contourPerimeter(const ContourFeature& contour);
bool contourContains(const ContourFeature& contour, const Vec3& point);
std::vector<Vec3> contourOutline(const ContourFeature& contour);

// Numeros con hasta `decimals` decimales, sin ceros sobrantes y con punto.
std::string formatNumber(double value, int decimals);
std::string formatLength(double value, LengthUnit unit);  // "220 mm"
std::string formatArea(double value, LengthUnit unit);    // "78.54 mm²" (2 decimales)
std::string formatAngle(double degrees);                  // "45.5°"

}  // namespace stp
