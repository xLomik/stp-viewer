// Triangle mesh plus the model edges used for the CAD-style line overlay.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "geom.h"

namespace stp {

// Texto de un plano (DXF): se dibuja encima del render, no se rasteriza.
struct MeshText {
    std::string text;         // UTF-8; '\n' separa renglones
    Vec3 position;            // punto de alineacion
    Vec3 direction{1, 0, 0};  // linea base, unitaria
    Vec3 up{0, 1, 0};         // hacia arriba de las letras, unitaria
    double height = 1.0;      // altura de las mayusculas, en unidades del modelo
    double widthFactor = 1.0;
    double fitWidth = 0.0;    // > 0: estirar el renglon hasta este ancho
    int halign = 0;           // 0 izquierda, 1 centro, 2 derecha
    int valign = 0;           // 0 linea base, 1 abajo, 2 medio, 3 arriba
};

// Separacion entre renglones de MTEXT, en alturas de letra (la de AutoCAD).
constexpr double kTextLineSpacing = 5.0 / 3.0;

// Rectangulo aproximado que ocupa un texto, sin medir la fuente: sirve para
// encuadrar el dibujo, no para dibujarlo.
inline void textCorners(const MeshText& text, Vec3 corners[4]) {
    std::size_t lines = 1, chars = 0, longest = 0;
    for (const char c : text.text) {
        if (c == '\n') {
            ++lines;
            chars = 0;
        } else if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
            longest = std::max(longest, ++chars);
        }
    }
    const double h = text.height;
    const double width = text.fitWidth > 0 ? text.fitWidth
                                           : 0.75 * h * text.widthFactor * static_cast<double>(longest);
    const double block = h + static_cast<double>(lines - 1) * kTextLineSpacing * h;
    const double x0 = -width * 0.5 * text.halign;
    double y0 = 0;  // borde inferior respecto al punto de alineacion
    switch (text.valign) {
        case 1: y0 = 0; break;
        case 2: y0 = -block * 0.5; break;
        case 3: y0 = -block; break;
        default: y0 = h - block; break;  // linea base del primer renglon
    }
    const Vec3& p = text.position;
    corners[0] = p + text.direction * x0 + text.up * y0;
    corners[1] = p + text.direction * (x0 + width) + text.up * y0;
    corners[2] = p + text.direction * (x0 + width) + text.up * (y0 + block);
    corners[3] = p + text.direction * x0 + text.up * (y0 + block);
}

struct Mesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<std::uint32_t> indices;
    std::vector<Vec3> edgeLines;  // consecutive pairs form one segment each
    std::vector<MeshText> texts;
    BBox bounds;

    int faces = 0;          // faces successfully tessellated
    int facesFailed = 0;    // faces that produced no triangles
    int solids = 0;

    bool empty() const { return indices.empty() && edgeLines.empty() && texts.empty(); }
    std::size_t triangleCount() const { return indices.size() / 3; }

    std::uint32_t addVertex(const Vec3& p, const Vec3& n) {
        positions.push_back(p);
        normals.push_back(n);
        bounds.add(p);
        return static_cast<std::uint32_t>(positions.size() - 1);
    }
    void addTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        indices.push_back(a);
        indices.push_back(b);
        indices.push_back(c);
    }
    void addSegment(const Vec3& a, const Vec3& b) {
        edgeLines.push_back(a);
        edgeLines.push_back(b);
        bounds.add(a);
        bounds.add(b);
    }
    void addText(const MeshText& text) {
        texts.push_back(text);
        Vec3 corners[4];
        textCorners(text, corners);
        for (const Vec3& c : corners) bounds.add(c);
    }
};

}  // namespace stp
