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

// OnEdge es "Mas cercano": el punto de una arista mas cercano al cursor.
enum class SnapKind { None, Endpoint, Center, Midpoint, OnEdge, OnFace, OnPlane,
                      Quadrant, Intersection, Extension, Perpendicular, Tangent };

// Modos de enganche (mascara de bits) que el usuario enciende y apaga.
enum SnapMode : unsigned {
    kSnapEndpoint = 1u << 0,
    kSnapMidpoint = 1u << 1,
    kSnapCenter = 1u << 2,
    kSnapQuadrant = 1u << 3,
    kSnapIntersection = 1u << 4,
    kSnapExtension = 1u << 5,
    kSnapPerpendicular = 1u << 6,
    kSnapTangent = 1u << 7,
    kSnapNearest = 1u << 8,
};
constexpr unsigned kSnapDefaultModes =
    kSnapEndpoint | kSnapMidpoint | kSnapCenter | kSnapQuadrant | kSnapIntersection;
constexpr unsigned kSnapAllModes = 0x1FF;

// Modo que ofrece ese tipo de enganche; 0 para cara, plano y ninguno.
unsigned snapModeOf(SnapKind kind);
// Nombre para mostrar, en UTF-8 ("Cuadrante"); "" para cara, plano y ninguno.
const char* snapKindName(SnapKind kind);

struct SnapResult {
    SnapKind kind = SnapKind::None;
    Vec3 point;
    int triangle = -1;  // triangulo bajo el cursor, si lo hay
    int segment = -1;   // segmento del que sale el punto, si lo hay
    int circle = -1;    // circulo o arco del que sale el punto, si lo hay
    double pixels = 0.0;  // distancia al cursor (a su elemento en Perpendicular y Tangente)
};

struct SnapOptions {
    double radiusPixels = 8.0;
    bool snapping = true;  // Shift y F3 lo apagan
    unsigned modes = kSnapDefaultModes;
    const Vec3* from = nullptr;  // primer punto de la medida: habilita Perpendicular y Tangente
    const PlanarInfo* plane = nullptr;
};

class PickIndex {
public:
    void build(const Mesh& mesh);
    bool empty() const { return m_triangles.empty() && m_segments.empty(); }
    bool raycast(const Mesh& mesh, const Ray& ray, double* t, int* triangle) const;
    // Mejor candidato de snapAll o, si no hay, el punto de la cara o del plano.
    SnapResult snap(const Mesh& mesh, const Camera& camera, int width, int height, double sx,
                    double sy, const SnapOptions& options) const;
    // Candidatos de enganche bajo el cursor, del mejor al peor (Tab los recorre).
    std::vector<SnapResult> snapAll(const Mesh& mesh, const Camera& camera, int width, int height,
                                    double sx, double sy, const SnapOptions& options) const;
    // Punto de la cara bajo el cursor (3D) o del plano del dibujo (2D), sin enganche.
    SnapResult surfaceAt(const Mesh& mesh, const Camera& camera, int width, int height, double sx,
                         double sy, const PlanarInfo* plane) const;
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
    Bvh m_circles;
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
    bool onCircle = false;  // salio de un circulo o arco (no de una cara cilindrica)
    std::string error;
};
// tolerance: distancia maxima (unidades del modelo) del punto al circulo o a su centro.
RadiusResult measureRadius(const Mesh& mesh, const Vec3& point, int triangle, double tolerance);
// Punto que conviene guardar para volver a medir el radio: sobre el circulo
// exacto (el clic cae en la cuerda muestreada) o el mismo punto si es una cara.
Vec3 radiusAnchor(const RadiusResult& result, const Vec3& point);

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
