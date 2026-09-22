// IGES (5.3): lineas, arcos, curvas y superficies NURBS, y superficies
// recortadas, que es como salen las piezas de los CAD que exportan a IGS.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "../engine/budget.h"
#include "../engine/surfaces.h"
#include "../engine/tessellate.h"
#include "formats.h"

namespace stp {
namespace {

struct Directory {
    int type = 0;
    int parameterStart = 0;
    int parameterCount = 0;
    int transform = 0;  // puntero a una entidad 124
    int subordinate = 0;
};

struct Parameters {
    std::vector<std::string> fields;

    double number(std::size_t i, double fallback = 0.0) const {
        if (i >= fields.size() || fields[i].empty()) return fallback;
        std::string text = fields[i];
        // IGES escribe los exponentes con D: 1.5D+02
        for (char& c : text) {
            if (c == 'D' || c == 'd') c = 'E';
        }
        return std::atof(text.c_str());
    }
    int integer(std::size_t i, int fallback = 0) const {
        if (i >= fields.size() || fields[i].empty()) return fallback;
        return std::atoi(fields[i].c_str());
    }
    std::size_t size() const { return fields.size(); }
};

// --- NURBS ------------------------------------------------------------------

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

class IgesReader {
public:
    IgesReader(Mesh* mesh, double quality, int budgetMs)
        : M(*mesh), m_quality(quality > 0 ? quality : 0.0015), m_budget(budgetMs) {}

    bool run(const char* data, std::size_t length, std::string* error, LoadStats* stats);

private:
    bool parseSections(const char* data, std::size_t length, std::string* error);
    const Parameters* parametersOf(int directoryPointer) const;
    Mat4 transformOf(int directoryPointer, int depth = 0) const;

    bool readCurve(int pointer, NurbsCurve* curve) const;
    bool readSurface(int pointer, NurbsSurface* surface) const;
    bool readAnalyticSurface(int pointer, Surface* surface) const;
    bool pointAt(int pointer, Vec3* out) const;
    bool directionAt(int pointer, Vec3* out) const;
    std::vector<Vec2> sampleParameterCurve(int pointer, const NurbsSurface& surface) const;

    void emitLine(const Parameters& p, const Mat4& transform);
    void emitArc(const Parameters& p, const Mat4& transform);
    void emitCurve(int pointer, const Mat4& transform);
    void emitSurface(int pointer, const std::vector<int>& trimCurves, const Mat4& transform);
    void emitBrepFace(int pointer, const Mat4& transform);
    void emitSurfaceWithLoops(const NurbsSurface& surface,
                              const std::vector<std::vector<Vec2>>& loops, const Mat4& transform);
    void emitAnalyticFace(const Surface& surface, const std::vector<std::vector<Vec2>>& loops,
                          const Mat4& transform, bool parametersInDegrees);
    std::vector<Vec2> sampleLoop(int loopPointer, const NurbsSurface& surface) const;
    std::vector<Vec3> sampleModelCurve(int pointer, int depth = 0) const;
    std::vector<Vec2> loopOnAnalytic(int loopPointer, const Surface& surface) const;

    Mesh& M;
    double m_quality;
    Budget m_budget;
    bool m_truncated = false;
    double m_size = 1.0;
    mutable std::vector<int> m_drawnCurves;

    char m_fieldSeparator = ',';
    char m_recordSeparator = ';';
    std::unordered_map<int, Directory> m_directory;
    std::unordered_map<int, Parameters> m_parameters;
};

std::string trimSpaces(const std::string& text) {
    std::size_t begin = 0, end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

bool IgesReader::parseSections(const char* data, std::size_t length, std::string* error) {
    std::vector<std::string> directoryLines;
    std::unordered_map<int, std::string> parameterText;
    std::string global;

    const char* p = data;
    const char* end = data + length;
    while (p < end) {
        const char* lineEnd = static_cast<const char*>(std::memchr(p, '\n', end - p));
        if (!lineEnd) lineEnd = end;
        std::string line(p, lineEnd);
        p = lineEnd < end ? lineEnd + 1 : end;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 73) continue;

        const char section = line[72];
        if (section == 'G') {
            global += line.substr(0, 72);
        } else if (section == 'D') {
            directoryLines.push_back(line.substr(0, 72));
        } else if (section == 'P') {
            // En la seccion de parametros los datos ocupan las columnas 1 a 64;
            // de la 66 a la 72 va el puntero al directorio, que no es dato.
            const int owner = std::atoi(line.substr(65, 7).c_str());
            parameterText[owner] += line.substr(0, 64);
        }
    }

    if (directoryLines.empty()) {
        if (error) *error = "el IGES no tiene seccion de directorio";
        return false;
    }

    // Separadores: los dos primeros campos globales, en formato Hollerith.
    if (global.size() > 2 && global[0] == '1' && (global[1] == 'H' || global[1] == 'h')) {
        m_fieldSeparator = global[2];
        const std::size_t next = global.find(m_fieldSeparator, 3);
        if (next != std::string::npos && global.size() > next + 3 &&
            (global[next + 2] == 'H' || global[next + 2] == 'h')) {
            m_recordSeparator = global[next + 3];
        }
    }

    for (std::size_t i = 0; i + 1 < directoryLines.size(); i += 2) {
        const std::string& first = directoryLines[i];
        const std::string& second = directoryLines[i + 1];
        auto field = [](const std::string& line, int index) {
            const std::size_t start = static_cast<std::size_t>(index) * 8;
            if (start + 8 > line.size()) return std::string();
            return trimSpaces(line.substr(start, 8));
        };

        Directory entry;
        entry.type = std::atoi(field(first, 0).c_str());
        entry.parameterStart = std::atoi(field(first, 1).c_str());
        entry.transform = std::atoi(field(first, 6).c_str());
        entry.parameterCount = std::atoi(field(second, 3).c_str());
        // El numero de estado son cuatro campos de dos digitos pegados:
        // blanqueado, subordinacion, uso y jerarquia.
        std::string status = field(first, 8);
        while (status.size() < 8) status.insert(status.begin(), '0');
        entry.subordinate = std::atoi(status.substr(2, 2).c_str());

        // El puntero de la entidad es su numero de linea impar (1, 3, 5...).
        const int pointer = static_cast<int>(i) + 1;
        m_directory[pointer] = entry;
    }

    for (const auto& kv : parameterText) {
        Parameters params;
        std::string field;
        for (char c : kv.second) {
            if (c == m_fieldSeparator) {
                params.fields.push_back(trimSpaces(field));
                field.clear();
            } else if (c == m_recordSeparator) {
                params.fields.push_back(trimSpaces(field));
                field.clear();
                break;
            } else {
                field.push_back(c);
            }
        }
        if (!field.empty()) params.fields.push_back(trimSpaces(field));
        m_parameters[kv.first] = std::move(params);
    }
    return true;
}

const Parameters* IgesReader::parametersOf(int pointer) const {
    const auto it = m_parameters.find(pointer);
    return it == m_parameters.end() ? nullptr : &it->second;
}

Mat4 IgesReader::transformOf(int pointer, int depth) const {
    if (pointer <= 0 || depth > 8) return Mat4::identity();
    const auto entry = m_directory.find(pointer);
    if (entry == m_directory.end() || entry->second.type != 124) return Mat4::identity();
    const Parameters* p = parametersOf(pointer);
    if (!p || p->size() < 13) return Mat4::identity();

    Mat4 local;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            local.m[row * 4 + col] = p->number(static_cast<std::size_t>(row) * 4 + col + 1);
        }
    }
    // Una matriz puede ir encadenada a otra.
    return transformOf(entry->second.transform, depth + 1) * local;
}

bool IgesReader::readCurve(int pointer, NurbsCurve* curve) const {
    const auto entry = m_directory.find(pointer);
    if (entry == m_directory.end() || entry->second.type != 126) return false;
    const Parameters* p = parametersOf(pointer);
    if (!p || p->size() < 8) return false;

    const int k = p->integer(1);
    const int degree = p->integer(2);
    const int rational = p->integer(4) == 0 ? 1 : 0;  // PROP3: 1 = polinomica
    const int count = k + 1;
    if (count <= degree || degree < 1) return false;

    curve->degree = degree;
    std::size_t index = 7;
    const int knotCount = count + degree + 1;
    for (int i = 0; i < knotCount; ++i) curve->knots.push_back(p->number(index++));
    for (int i = 0; i < count; ++i) {
        const double w = p->number(index++);
        curve->weights.push_back(rational ? w : 1.0);
    }
    for (int i = 0; i < count; ++i) {
        const double x = p->number(index++);
        const double y = p->number(index++);
        const double z = p->number(index++);
        curve->controls.emplace_back(x, y, z);
    }
    return curve->valid();
}

bool IgesReader::readSurface(int pointer, NurbsSurface* surface) const {
    const auto entry = m_directory.find(pointer);
    if (entry == m_directory.end() || entry->second.type != 128) return false;
    const Parameters* p = parametersOf(pointer);
    if (!p || p->size() < 12) return false;

    const int k1 = p->integer(1);
    const int k2 = p->integer(2);
    const int m1 = p->integer(3);
    const int m2 = p->integer(4);
    const int rational = p->integer(8) == 0 ? 1 : 0;  // PROP4
    surface->countU = k1 + 1;
    surface->countV = k2 + 1;
    surface->degreeU = m1;
    surface->degreeV = m2;
    if (m1 < 1 || m2 < 1 || surface->countU <= m1 || surface->countV <= m2) return false;

    std::size_t index = 10;
    for (int i = 0; i < surface->countU + m1 + 1; ++i) surface->knotsU.push_back(p->number(index++));
    for (int i = 0; i < surface->countV + m2 + 1; ++i) surface->knotsV.push_back(p->number(index++));

    const std::size_t total = static_cast<std::size_t>(surface->countU) * surface->countV;
    for (std::size_t i = 0; i < total; ++i) {
        const double w = p->number(index++);
        surface->weights.push_back(rational ? w : 1.0);
    }
    for (std::size_t i = 0; i < total; ++i) {
        const double x = p->number(index++);
        const double y = p->number(index++);
        const double z = p->number(index++);
        surface->controls.emplace_back(x, y, z);
    }
    // IGES guarda los puntos con u variando primero; aqui se espera v primero.
    std::vector<Vec3> reordered(total);
    std::vector<double> reorderedWeights(total);
    for (int iu = 0; iu < surface->countU; ++iu) {
        for (int iv = 0; iv < surface->countV; ++iv) {
            const std::size_t from = static_cast<std::size_t>(iv) * surface->countU + iu;
            const std::size_t to = static_cast<std::size_t>(iu) * surface->countV + iv;
            if (from < total && to < total) {
                reordered[to] = surface->controls[from];
                reorderedWeights[to] = surface->weights[from];
            }
        }
    }
    surface->controls.swap(reordered);
    surface->weights.swap(reorderedWeights);
    return surface->valid();
}

bool IgesReader::pointAt(int pointer, Vec3* out) const {
    const auto entry = m_directory.find(pointer);
    if (entry == m_directory.end() || entry->second.type != 116) return false;
    const Parameters* p = parametersOf(pointer);
    if (!p || p->size() < 4) return false;
    *out = Vec3(p->number(1), p->number(2), p->number(3));
    return true;
}

bool IgesReader::directionAt(int pointer, Vec3* out) const {
    const auto entry = m_directory.find(pointer);
    if (entry == m_directory.end() || entry->second.type != 123) return false;
    const Parameters* p = parametersOf(pointer);
    if (!p || p->size() < 4) return false;
    *out = Vec3(p->number(1), p->number(2), p->number(3));
    return length(*out) > 1e-12;
}

// Superficies analiticas del B-rep de IGES: plano, cilindro, cono, esfera y
// toro. Se traducen a las mismas superficies que ya usa el lector de STEP, asi
// que se mallan con el mismo codigo.
bool IgesReader::readAnalyticSurface(int pointer, Surface* surface) const {
    const auto entry = m_directory.find(pointer);
    if (entry == m_directory.end()) return false;
    const Parameters* p = parametersOf(pointer);
    if (!p || p->size() < 3) return false;

    Vec3 origin, axis(0, 0, 1), refDirection(1, 0, 0);
    switch (entry->second.type) {
        case 190: {  // plano
            if (!pointAt(p->integer(1), &origin) || !directionAt(p->integer(2), &axis)) return false;
            if (p->size() > 3) directionAt(p->integer(3), &refDirection);
            surface->type = Surface::Type::Plane;
            break;
        }
        case 192: {  // cilindro
            if (!pointAt(p->integer(1), &origin) || !directionAt(p->integer(2), &axis)) return false;
            surface->type = Surface::Type::Cylinder;
            surface->radius = p->number(3);
            if (p->size() > 4) directionAt(p->integer(4), &refDirection);
            break;
        }
        case 194: {  // cono
            if (!pointAt(p->integer(1), &origin) || !directionAt(p->integer(2), &axis)) return false;
            surface->type = Surface::Type::Cone;
            surface->radius = p->number(3);
            surface->halfAngle = p->number(4) ;
            if (p->size() > 5) directionAt(p->integer(5), &refDirection);
            break;
        }
        case 196: {  // esfera
            if (!pointAt(p->integer(1), &origin)) return false;
            surface->type = Surface::Type::Sphere;
            surface->radius = p->number(2);
            if (p->size() > 3) directionAt(p->integer(3), &axis);
            if (p->size() > 4) directionAt(p->integer(4), &refDirection);
            break;
        }
        case 198: {  // toro
            if (!pointAt(p->integer(1), &origin) || !directionAt(p->integer(2), &axis)) return false;
            surface->type = Surface::Type::Torus;
            surface->radius = p->number(3);
            surface->minorRadius = p->number(4);
            if (p->size() > 5) directionAt(p->integer(5), &refDirection);
            break;
        }
        default:
            return false;
    }
    surface->frame = makeFrame(origin, axis, refDirection);
    return true;
}

// La curva de recorte vive en el espacio de parametros de la superficie: se
// muestrea y se devuelve como poligono (u, v).
std::vector<Vec2> IgesReader::sampleParameterCurve(int pointer,
                                                   const NurbsSurface& surface) const {
    std::vector<Vec2> uv;
    const auto entry = m_directory.find(pointer);
    if (entry == m_directory.end()) return uv;

    if (entry->second.type == 142) {  // curva sobre superficie parametrica
        const Parameters* p = parametersOf(pointer);
        if (!p || p->size() < 4) return uv;
        // Campos: CRTN, SPTR, BPTR (curva en (u,v)), CPTR (curva 3D), PREF.
        return sampleParameterCurve(p->integer(3), surface);
    }
    if (entry->second.type == 102) {  // curva compuesta
        const Parameters* p = parametersOf(pointer);
        if (!p) return uv;
        const int count = p->integer(1);
        for (int i = 0; i < count; ++i) {
            const std::vector<Vec2> part = sampleParameterCurve(p->integer(2 + i), surface);
            for (const Vec2& q : part) {
                if (uv.empty() || length(uv.back() - q) > 1e-12) uv.push_back(q);
            }
        }
        return uv;
    }
    if (entry->second.type == 110) {  // recta en el espacio de parametros
        const Parameters* p = parametersOf(pointer);
        if (!p || p->size() < 7) return uv;
        uv.emplace_back(p->number(1), p->number(2));
        uv.emplace_back(p->number(4), p->number(5));
        return uv;
    }
    if (entry->second.type == 126) {
        NurbsCurve curve;
        if (!readCurve(pointer, &curve)) return uv;
        const int steps = 48;
        for (int i = 0; i <= steps; ++i) {
            const double t = curve.tMin() + (curve.tMax() - curve.tMin()) * i / steps;
            const Vec3 point = curve.eval(t);
            uv.emplace_back(point.x, point.y);
        }
        return uv;
    }
    return uv;
}

void IgesReader::emitLine(const Parameters& p, const Mat4& transform) {
    if (p.size() < 7) return;
    M.addSegment(transform.point(Vec3(p.number(1), p.number(2), p.number(3))),
                 transform.point(Vec3(p.number(4), p.number(5), p.number(6))));
}

void IgesReader::emitArc(const Parameters& p, const Mat4& transform) {
    if (p.size() < 8) return;
    const double z = p.number(1);
    const Vec3 center(p.number(2), p.number(3), z);
    const Vec3 start(p.number(4), p.number(5), z);
    const Vec3 end(p.number(6), p.number(7), z);

    const double radius = distance(center, start);
    if (radius < 1e-12) return;
    double a0 = std::atan2(start.y - center.y, start.x - center.x);
    double a1 = std::atan2(end.y - center.y, end.x - center.x);
    double sweep = a1 - a0;
    if (distance(start, end) < 1e-9) sweep = 2 * kPi;
    else if (sweep <= 0) sweep += 2 * kPi;

    int steps = static_cast<int>(std::ceil(std::fabs(sweep) / (kPi / 18.0)));
    steps = std::max(2, std::min(256, steps));
    Vec3 previous;
    for (int i = 0; i <= steps; ++i) {
        const double angle = a0 + sweep * i / steps;
        const Vec3 point(center.x + radius * std::cos(angle), center.y + radius * std::sin(angle),
                         z);
        const Vec3 world = transform.point(point);
        if (i > 0) M.addSegment(previous, world);
        previous = world;
    }
}

void IgesReader::emitCurve(int pointer, const Mat4& transform) {
    NurbsCurve curve;
    if (!readCurve(pointer, &curve)) return;
    const int steps = 64;
    Vec3 previous;
    for (int i = 0; i <= steps; ++i) {
        const double t = curve.tMin() + (curve.tMax() - curve.tMin()) * i / steps;
        const Vec3 world = transform.point(curve.eval(t));
        if (i > 0) M.addSegment(previous, world);
        previous = world;
    }
}

// Muestrea una curva del espacio del modelo (recta, arco o NURBS).
std::vector<Vec3> IgesReader::sampleModelCurve(int pointer, int depth) const {
    std::vector<Vec3> points;
    if (depth > 6) return points;
    const auto entry = m_directory.find(pointer);
    if (entry == m_directory.end()) return points;
    const Parameters* p = parametersOf(pointer);
    if (!p) return points;
    const Mat4 transform = transformOf(entry->second.transform);

    if (entry->second.type == 110 && p->size() >= 7) {
        points.push_back(transform.point(Vec3(p->number(1), p->number(2), p->number(3))));
        points.push_back(transform.point(Vec3(p->number(4), p->number(5), p->number(6))));
        return points;
    }
    if (entry->second.type == 100 && p->size() >= 8) {
        const double z = p->number(1);
        const Vec3 center(p->number(2), p->number(3), z);
        const Vec3 start(p->number(4), p->number(5), z);
        const Vec3 end(p->number(6), p->number(7), z);
        const double radius = distance(center, start);
        if (radius < 1e-12) return points;
        const double a0 = std::atan2(start.y - center.y, start.x - center.x);
        double sweep = std::atan2(end.y - center.y, end.x - center.x) - a0;
        if (distance(start, end) < 1e-9) sweep = 2 * kPi;
        else if (sweep <= 0) sweep += 2 * kPi;

        int steps = static_cast<int>(std::ceil(std::fabs(sweep) / (kPi / 24.0)));
        steps = std::max(2, std::min(256, steps));
        for (int i = 0; i <= steps; ++i) {
            const double angle = a0 + sweep * i / steps;
            points.push_back(transform.point(Vec3(center.x + radius * std::cos(angle),
                                                  center.y + radius * std::sin(angle), z)));
        }
        return points;
    }
    if (entry->second.type == 126) {
        NurbsCurve curve;
        if (!readCurve(pointer, &curve)) return points;
        const int steps = 32;
        for (int i = 0; i <= steps; ++i) {
            const double t = curve.tMin() + (curve.tMax() - curve.tMin()) * i / steps;
            points.push_back(transform.point(curve.eval(t)));
        }
        return points;
    }
    if (entry->second.type == 102 && p->size() >= 2) {
        const int count = p->integer(1);
        for (int i = 0; i < count; ++i) {
            const std::vector<Vec3> part = sampleModelCurve(p->integer(2 + i), depth + 1);
            for (const Vec3& q : part) {
                if (points.empty() || distance(points.back(), q) > 1e-12) points.push_back(q);
            }
        }
    }
    return points;
}

// Bucle sin curvas en el espacio de parametros: se toman las aristas 3D de la
// lista 504 y se proyectan sobre la superficie, que es lo que hace el lector de
// STEP. Es el caso que escribe OpenCASCADE.
std::vector<Vec2> IgesReader::loopOnAnalytic(int loopPointer, const Surface& surface) const {
    std::vector<Vec2> uv;
    const auto entry = m_directory.find(loopPointer);
    if (entry == m_directory.end() || entry->second.type != 508) return uv;
    const Parameters* p = parametersOf(loopPointer);
    if (!p || p->size() < 2) return uv;

    const int edgeCount = p->integer(1);
    std::size_t index = 2;
    std::vector<Vec3> points;

    for (int e = 0; e < edgeCount && index + 4 < p->size(); ++e) {
        ++index;  // TYPE (0 = arista de una lista 504)
        const int listPointer = p->integer(index++);
        const int listIndex = p->integer(index++);
        const int orientation = p->integer(index++);
        const int curveCount = p->integer(index++);
        index += static_cast<std::size_t>(curveCount) * 2;  // se ignoran las pcurves

        const Parameters* list = parametersOf(listPointer);
        if (!list) continue;
        // 504: N y luego, por arista, CURV, SVP, SV, TVP, TV.
        const std::size_t base = 2 + static_cast<std::size_t>(listIndex - 1) * 5;
        if (base >= list->size()) continue;
        const int curvePointer = list->integer(base);
        std::vector<Vec3> part = sampleModelCurve(curvePointer);
        if (part.size() < 2) continue;
        // Las aristas del solido se dibujan una sola vez, aunque las compartan
        // dos caras.
        if (std::find(m_drawnCurves.begin(), m_drawnCurves.end(), curvePointer) ==
            m_drawnCurves.end()) {
            m_drawnCurves.push_back(curvePointer);
            for (std::size_t k = 1; k < part.size(); ++k) M.addSegment(part[k - 1], part[k]);
        }
        if (orientation == 0) std::reverse(part.begin(), part.end());
        for (const Vec3& q : part) {
            if (points.empty() || distance(points.back(), q) > 1e-12) points.push_back(q);
        }
    }
    if (points.size() < 3) return uv;
    if (distance(points.front(), points.back()) < 1e-9) points.pop_back();

    // Proyeccion al espacio de parametros, desenrollando la costura.
    double previousU = 0.0, previousV = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        Vec2 t = surface.invert(points[i]);
        if (i > 0) {
            if (surface.uPeriodic()) {
                while (t.x - previousU > kPi) t.x -= 2 * kPi;
                while (previousU - t.x > kPi) t.x += 2 * kPi;
            }
            if (surface.vPeriodic()) {
                while (t.y - previousV > kPi) t.y -= 2 * kPi;
                while (previousV - t.y > kPi) t.y += 2 * kPi;
            }
        }
        previousU = t.x;
        previousV = t.y;
        uv.push_back(t);
    }
    return uv;
}

// Un bucle (508) enlaza aristas; de cada una interesa su curva en el espacio de
// parametros de la cara, que es lo que recorta la superficie.
std::vector<Vec2> IgesReader::sampleLoop(int loopPointer, const NurbsSurface& surface) const {
    std::vector<Vec2> loop;
    const auto entry = m_directory.find(loopPointer);
    if (entry == m_directory.end() || entry->second.type != 508) return loop;
    const Parameters* p = parametersOf(loopPointer);
    if (!p || p->size() < 2) return loop;

    const int edgeCount = p->integer(1);
    std::size_t index = 2;
    for (int e = 0; e < edgeCount && index + 4 < p->size(); ++e) {
        index += 3;  // TYPE, EDGE (lista), INDEX
        const int orientation = p->integer(index++);
        const int curveCount = p->integer(index++);
        for (int c = 0; c < curveCount && index + 1 < p->size(); ++c) {
            ++index;  // ISOFLAG
            const int curvePointer = p->integer(index++);
            std::vector<Vec2> part = sampleParameterCurve(curvePointer, surface);
            if (orientation == 0) std::reverse(part.begin(), part.end());
            for (const Vec2& q : part) {
                if (loop.empty() || length(loop.back() - q) > 1e-12) loop.push_back(q);
            }
        }
    }
    if (loop.size() > 2 && length(loop.front() - loop.back()) < 1e-9) loop.pop_back();
    return loop;
}

// Cara de un modelo B-rep (510): superficie base mas sus bucles de recorte.
void IgesReader::emitBrepFace(int pointer, const Mat4& transform) {
    (void)transform;
    const Parameters* p = parametersOf(pointer);
    if (!p || p->size() < 4) return;

    const int surfacePointer = p->integer(1);
    const int loopCount = p->integer(2);
    // Campo OF: si es 0, la normal de la superficie apunta hacia dentro.
    const bool outward = p->integer(3, 1) != 0;

    Surface analytic;
    NurbsSurface nurbs;
    const bool isAnalytic = readAnalyticSurface(surfacePointer, &analytic);
    if (!isAnalytic && !readSurface(surfacePointer, &nurbs)) return;

    if (isAnalytic) {
        const Mat4 surfaceTransform = transformOf(m_directory[surfacePointer].transform);
        const Frame& frame = analytic.frame;
        Frame world;
        world.origin = surfaceTransform.point(frame.origin);
        world.x = normalize(surfaceTransform.direction(frame.x));
        world.y = normalize(surfaceTransform.direction(frame.y));
        world.z = normalize(surfaceTransform.direction(frame.z));
        analytic.frame = world;
    }

    std::vector<std::vector<Vec2>> loops;
    for (int i = 0; i < loopCount && 4 + i < static_cast<int>(p->size()); ++i) {
        const int loopPointer = p->integer(4 + i);
        // En superficies analiticas se proyectan las aristas 3D en vez de usar
        // las curvas en parametros: evita depender de como escriba cada CAD esa
        // parametrizacion (grados, origenes distintos) y da el mismo resultado
        // que el lector de STEP.
        std::vector<Vec2> loop =
            isAnalytic ? loopOnAnalytic(loopPointer, analytic) : sampleLoop(loopPointer, nurbs);
        if (loop.size() >= 3) loops.push_back(std::move(loop));
    }

    analytic.flipped = !outward;
    if (isAnalytic) {
        // Los bucles se muestrean ya en coordenadas del mundo, asi que la
        // superficie se lleva tambien al mundo y se malla sin transformar otra
        // vez: de lo contrario la cara sale desplazada.
        emitAnalyticFace(analytic, loops, Mat4::identity(), false);
    } else {
        emitSurfaceWithLoops(nurbs, loops, transform);
    }
}

// Cara sobre superficie analitica: misma rejilla recortada que usa el STEP.
void IgesReader::emitAnalyticFace(const Surface& surface,
                                  const std::vector<std::vector<Vec2>>& loops,
                                  const Mat4& transform, bool parametersInDegrees) {
    if (loops.empty()) return;

    // Las curvas de recorte de IGES vienen en grados; las proyecciones de
    // aristas ya salen en radianes.
    const bool uIsAngle = parametersInDegrees && surface.type != Surface::Type::Plane;
    const bool vIsAngle = parametersInDegrees && (surface.type == Surface::Type::Sphere ||
                                                  surface.type == Surface::Type::Torus);
    std::vector<std::vector<Vec2>> scaled = loops;
    if (uIsAngle || vIsAngle) {
        const double toRadians = kPi / 180.0;
        for (std::vector<Vec2>& loop : scaled) {
            for (Vec2& q : loop) {
                if (uIsAngle) q.x *= toRadians;
                if (vIsAngle) q.y *= toRadians;
            }
        }
    }

    std::vector<Vec2> merged;
    if (!mergeLoops(scaled, &merged, nullptr)) return;

    const double tolerance = std::max(m_size * m_quality, 1e-9);
    auto angular = [&](double radius) {
        if (radius <= 1e-12) return kPi / 6.0;
        const double ratio = std::max(0.0, 1.0 - tolerance / radius);
        return std::max(2 * kPi / 256.0, std::min(kPi / 6.0, 2.0 * std::acos(std::min(1.0, ratio))));
    };
    double uStep = 0.0, vStep = 0.0;
    switch (surface.type) {
        case Surface::Type::Cylinder:
        case Surface::Type::Cone:
            uStep = angular(std::max(surface.radius, m_size * 0.01));
            break;
        case Surface::Type::Sphere:
            uStep = vStep = angular(surface.radius);
            break;
        case Surface::Type::Torus:
            uStep = angular(surface.radius + surface.minorRadius);
            vStep = angular(surface.minorRadius);
            break;
        default:
            break;
    }

    std::vector<Vec2> verts;
    std::vector<std::array<int, 3>> tris;
    if (!triangulateGrid(merged, uStep, vStep, &verts, &tris)) return;

    std::vector<std::uint32_t> ids(verts.size());
    for (std::size_t i = 0; i < verts.size(); ++i) {
        ids[i] = M.addVertex(transform.point(surface.eval(verts[i].x, verts[i].y)),
                             normalize(transform.direction(surface.normal(verts[i].x, verts[i].y))));
    }
    for (const auto& t : tris) M.addTriangle(ids[t[0]], ids[t[1]], ids[t[2]]);
}

void IgesReader::emitSurface(int pointer, const std::vector<int>& trimCurves,
                             const Mat4& transform) {
    NurbsSurface surface;
    if (!readSurface(pointer, &surface)) return;

    std::vector<std::vector<Vec2>> loops;
    for (int trim : trimCurves) {
        std::vector<Vec2> loop = sampleParameterCurve(trim, surface);
        if (loop.size() >= 3) loops.push_back(std::move(loop));
    }
    emitSurfaceWithLoops(surface, loops, transform);
}

void IgesReader::emitSurfaceWithLoops(const NurbsSurface& surface,
                                      const std::vector<std::vector<Vec2>>& loops,
                                      const Mat4& transform) {

    // Tamano aproximado del parche para elegir la densidad de la rejilla.
    const Vec3 corner00 = surface.eval(surface.uMin(), surface.vMin());
    const Vec3 corner11 = surface.eval(surface.uMax(), surface.vMax());
    const Vec3 corner01 = surface.eval(surface.uMin(), surface.vMax());
    const double extent = std::max(distance(corner00, corner11), distance(corner00, corner01));
    const double tolerance = std::max(m_size * m_quality, 1e-9);
    int steps = static_cast<int>(std::ceil(std::sqrt(extent / tolerance)));
    steps = std::max(4, std::min(64, steps));

    std::vector<Vec2> verts;
    std::vector<std::array<int, 3>> tris;
    if (!loops.empty()) {
        std::vector<Vec2> merged;
        if (mergeLoops(loops, &merged, nullptr)) {
            const double uStep = (surface.uMax() - surface.uMin()) / steps;
            const double vStep = (surface.vMax() - surface.vMin()) / steps;
            triangulateGrid(merged, uStep, vStep, &verts, &tris);
        }
    }
    if (tris.empty()) {
        // Sin recorte utilizable: rejilla completa del parche.
        verts.clear();
        for (int i = 0; i <= steps; ++i) {
            for (int j = 0; j <= steps; ++j) {
                verts.emplace_back(surface.uMin() + (surface.uMax() - surface.uMin()) * i / steps,
                                   surface.vMin() + (surface.vMax() - surface.vMin()) * j / steps);
            }
        }
        for (int i = 0; i < steps; ++i) {
            for (int j = 0; j < steps; ++j) {
                const int a = i * (steps + 1) + j;
                const int b = a + 1;
                const int c = a + steps + 1;
                const int d = c + 1;
                tris.push_back({a, c, b});
                tris.push_back({b, c, d});
            }
        }
    }

    std::vector<std::uint32_t> ids(verts.size());
    for (std::size_t i = 0; i < verts.size(); ++i) {
        const Vec3 position = transform.point(surface.eval(verts[i].x, verts[i].y));
        const Vec3 normal = normalize(transform.direction(surface.normal(verts[i].x, verts[i].y)));
        ids[i] = M.addVertex(position, normal);
    }
    for (const auto& t : tris) {
        M.addTriangle(ids[t[0]], ids[t[1]], ids[t[2]]);
    }
    ++M.faces;
}

bool IgesReader::run(const char* data, std::size_t length, std::string* error, LoadStats* stats) {
    if (!parseSections(data, length, error)) return false;

    // Tamano del modelo a partir de los puntos de control, para la tolerancia.
    BBox box;
    for (const auto& kv : m_parameters) {
        const auto entry = m_directory.find(kv.first);
        if (entry == m_directory.end()) continue;
        const Parameters& p = kv.second;
        if (entry->second.type == 110 && p.size() >= 7) {
            box.add(Vec3(p.number(1), p.number(2), p.number(3)));
            box.add(Vec3(p.number(4), p.number(5), p.number(6)));
        }
        if (entry->second.type == 128 || entry->second.type == 126) {
            for (std::size_t i = 1; i + 2 < p.size(); i += 3) {
                box.add(Vec3(p.number(i), p.number(i + 1), p.number(i + 2)));
            }
        }
    }
    m_size = box.valid() ? box.diagonal() : 1.0;
    if (!(m_size > 1e-9)) m_size = 1.0;

    // Las superficies recortadas (144) mandan sobre la superficie que usan:
    // esta no debe dibujarse tambien entera.
    std::vector<int> pointers;
    for (const auto& kv : m_directory) pointers.push_back(kv.first);
    std::sort(pointers.begin(), pointers.end());

    std::vector<int> trimmedSurfaces;
    for (int pointer : pointers) {
        if (m_directory[pointer].type != 144) continue;
        const Parameters* p = parametersOf(pointer);
        if (p && p->size() >= 2) trimmedSurfaces.push_back(p->integer(1));
    }

    int surfaces = 0;
    for (int pointer : pointers) {
        if (m_budget.expired()) {
            m_truncated = true;
            break;
        }
        const Directory& entry = m_directory[pointer];
        const Parameters* p = parametersOf(pointer);
        if (!p) continue;
        const Mat4 transform = transformOf(entry.transform);

        switch (entry.type) {
            case 128: {
                // Una superficie usada por una cara ya se dibuja recortada.
                if (entry.subordinate == 1 || entry.subordinate == 3) break;
                const bool trimmedElsewhere =
                    std::find(trimmedSurfaces.begin(), trimmedSurfaces.end(), pointer) !=
                    trimmedSurfaces.end();
                if (!trimmedElsewhere) {
                    emitSurface(pointer, {}, transform);
                    ++surfaces;
                }
                break;
            }
            case 510:
                emitBrepFace(pointer, transform);
                ++surfaces;
                break;
            case 144: {
                const int surfacePointer = p->integer(1);
                const int outer = p->integer(4);
                const int innerCount = p->integer(3);
                std::vector<int> trims;
                if (outer > 0) trims.push_back(outer);
                for (int i = 0; i < innerCount; ++i) trims.push_back(p->integer(5 + i));
                emitSurface(surfacePointer, trims, transformOf(m_directory[surfacePointer].transform));
                ++surfaces;
                break;
            }
            default:
                break;
        }
    }

    // Las curvas sueltas solo se dibujan en archivos de alambre: en un solido
    // las mismas entidades aparecen como aristas de las caras y, peor aun, las
    // curvas de recorte viven en el espacio de parametros, no en el del modelo.
    if (surfaces == 0) {
        for (int pointer : pointers) {
            if (m_budget.expired()) {
                m_truncated = true;
                break;
            }
            const Directory& entry = m_directory[pointer];
            const Parameters* p = parametersOf(pointer);
            if (!p || entry.subordinate == 1 || entry.subordinate == 3) continue;
            const Mat4 transform = transformOf(entry.transform);
            if (entry.type == 110) emitLine(*p, transform);
            else if (entry.type == 100) emitArc(*p, transform);
            else if (entry.type == 126) emitCurve(pointer, transform);
        }
    }

    if (M.empty()) {
        if (error) *error = "el IGES no contiene geometria reconocible";
        return false;
    }
    if (stats) {
        stats->schema = "IGES";
        stats->faces = surfaces;
        stats->solids = surfaces > 0 ? 1 : 0;
        stats->triangles = M.triangleCount();
        stats->truncated = m_truncated;
    }
    return true;
}

}  // namespace

bool loadIges(const char* data, std::size_t length, Mesh* mesh, std::string* error,
              LoadStats* stats, double quality, int budgetMs) {
    IgesReader reader(mesh, quality, budgetMs);
    return reader.run(data, length, error, stats);
}

}  // namespace stp
