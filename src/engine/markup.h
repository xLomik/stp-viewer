// Marcas de revision: medidas, notas, trazos y formas, con las vistas en que
// se dibujaron, y su archivo de texto <modelo>.marcas.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../render/renderer.h"
#include "geom.h"

namespace stp {

enum class MarkKind { Distance, Radius, Angle, Area, Note, Highlight, Underline, Pen, Rectangle, Ellipse, Cloud };

constexpr std::uint32_t kMarkRed = 0xFFE03C31;
constexpr std::uint32_t kMarkYellow = 0xFFF2C12E;
constexpr std::uint32_t kMarkGreen = 0xFF2E9E4F;
constexpr std::uint32_t kMarkBlue = 0xFF2F6FDB;
constexpr std::uint32_t kMarkBlack = 0xFF1A1F24;
constexpr std::uint32_t kHighlightYellow = 0xFFFFE14D;
constexpr std::uint32_t kHighlightGreen = 0xFF7CE08A;
constexpr std::uint32_t kHighlightPink = 0xFFFF7EB6;
constexpr std::uint32_t kMeasureColor = 0xFFFF8C1A;

struct Mark {
    int id = 0;
    MarkKind kind = MarkKind::Distance;
    // Distancia: a, b. Radio: punto clicado. Angulo: a, vertice, b (o 4 puntos
    // de dos rectas + 2 de clic, ver markup_tools). Area: punto clicado.
    // Nota: ancla, etiqueta. Trazos: la polilinea. Formas: dos esquinas.
    std::vector<Vec3> points;
    std::uint32_t color = kMarkRed;  // ARGB
    int view = 0;                    // 0 = anclada al modelo; si no, MarkupView::id
    std::string text;                // notas (UTF-8)
};

struct MarkupView {
    int id = 0;
    std::string name;
    Camera camera;
};

struct MarkupDocument {
    std::string modelName;
    std::uint64_t modelSize = 0;
    std::string modelDate;  // "AAAA-MM-DDTHH:MM:SS"
    std::vector<MarkupView> views;
    std::vector<Mark> marks;
    int nextId = 1;

    int newId() { return nextId++; }
    const MarkupView* findView(int id) const;
};

struct MarkupParseReport {
    bool recognized = false;  // tenia la cabecera stp-viewer-marcas
    int badLines = 0;         // lineas que no se entendieron y se ignoraron
    bool newerVersion = false;
};

bool isMeasurement(MarkKind kind);
std::string serializeMarkup(const MarkupDocument& doc);
MarkupParseReport parseMarkup(const std::string& text, MarkupDocument* doc);

// Misma orientacion y mismo encuadre, con 0,5 % de tolerancia.
bool sameView(const Camera& a, const Camera& b);
bool markVisible(const Mark& mark, const MarkupDocument& doc, const Camera& camera);
bool modelChanged(const MarkupDocument& doc, std::uint64_t size, const std::string& date);

}  // namespace stp
