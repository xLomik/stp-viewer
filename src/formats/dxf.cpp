// DXF ASCII: alambre y caras. Cubre lo que sale de un CAD 2D/3D corriente
// (LINE, LWPOLYLINE y POLYLINE con arcos, ARC, CIRCLE, ELLIPSE, SPLINE, 3DFACE,
// SOLID, bloques, cotas), textos TEXT/MTEXT/ATTRIB, respetando capas apagadas
// y espacio papel.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "../engine/budget.h"
#include "../engine/nurbs.h"
#include "formats.h"

namespace stp {
namespace {

struct Pair {
    int code = 0;
    std::string value;

    double number() const { return std::atof(value.c_str()); }
    int integer() const { return std::atoi(value.c_str()); }
};

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

std::vector<Pair> readPairs(const char* data, std::size_t length) {
    std::vector<Pair> pairs;
    pairs.reserve(length / 16);
    const char* p = data;
    const char* end = data + length;

    while (p < end) {
        const char* lineEnd = static_cast<const char*>(std::memchr(p, '\n', end - p));
        if (!lineEnd) lineEnd = end;
        const std::string codeText = trim(std::string(p, lineEnd));
        p = lineEnd < end ? lineEnd + 1 : end;
        if (p >= end) break;

        lineEnd = static_cast<const char*>(std::memchr(p, '\n', end - p));
        if (!lineEnd) lineEnd = end;
        std::string value = trim(std::string(p, lineEnd));
        p = lineEnd < end ? lineEnd + 1 : end;

        if (codeText.empty()) continue;
        Pair pair;
        pair.code = std::atoi(codeText.c_str());
        pair.value = std::move(value);
        pairs.push_back(std::move(pair));
    }
    return pairs;
}

// Una entidad es la lista de pares hasta el siguiente codigo 0.
struct Entity {
    std::string type;
    std::vector<Pair> pairs;

    double number(int code, double fallback = 0.0) const {
        for (const Pair& pair : pairs) {
            if (pair.code == code) return pair.number();
        }
        return fallback;
    }
    int integer(int code, int fallback = 0) const {
        for (const Pair& pair : pairs) {
            if (pair.code == code) return pair.integer();
        }
        return fallback;
    }
    std::string text(int code) const {
        for (const Pair& pair : pairs) {
            if (pair.code == code) return pair.value;
        }
        return std::string();
    }
    bool has(int code) const {
        for (const Pair& pair : pairs) {
            if (pair.code == code) return true;
        }
        return false;
    }
};

// El sistema de coordenadas del objeto: DXF guarda circulos y arcos en el plano
// de su vector de extrusion, no en el mundo.
Frame objectFrame(const Entity& entity) {
    Vec3 normal(entity.number(210, 0.0), entity.number(220, 0.0), entity.number(230, 1.0));
    if (length(normal) < 1e-12) normal = Vec3(0, 0, 1);
    normal = normalize(normal);

    // Algoritmo del eje arbitrario de AutoCAD.
    Vec3 refX;
    if (std::fabs(normal.x) < 1.0 / 64.0 && std::fabs(normal.y) < 1.0 / 64.0) {
        refX = cross(Vec3(0, 1, 0), normal);
    } else {
        refX = cross(Vec3(0, 0, 1), normal);
    }
    return makeFrame(Vec3(0, 0, 0), normal, refX);
}

// --- Textos ------------------------------------------------------------------

void appendUtf8(std::string* out, std::uint32_t cp) {
    if (cp < 0x80) {
        out->push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool validUtf8(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const int extra = c < 0x80          ? 0
                          : (c >> 5) == 0x6  ? 1
                          : (c >> 4) == 0xE  ? 2
                          : (c >> 3) == 0x1E ? 3
                                             : -1;
        if (extra < 0 || i + extra >= text.size() + (extra == 0)) return false;
        for (int k = 1; k <= extra; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

// Windows-1252, que es lo que usan los DXF anteriores a AutoCAD 2007.
std::string ansiToUtf8(const std::string& text) {
    static const std::uint16_t high[32] = {
        0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,   0x017D, 0x8F,
        0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};
    std::string out;
    out.reserve(text.size() + 8);
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        appendUtf8(&out, c >= 0x80 && c < 0xA0 ? high[c - 0x80] : c);
    }
    return out;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// \U+XXXX en la posicion i (que apunta a la barra). Devuelve cuantos bytes consumio.
std::size_t unicodeEscape(const std::string& text, std::size_t i, std::string* out) {
    if (i + 7 > text.size() || text[i + 1] != 'U' || text[i + 2] != '+') return 0;
    std::uint32_t cp = 0;
    for (std::size_t k = i + 3; k < i + 7; ++k) {
        const int h = hexValue(text[k]);
        if (h < 0) return 0;
        cp = cp * 16 + static_cast<std::uint32_t>(h);
    }
    appendUtf8(out, cp);
    return 7;
}

std::string expandUnicodeEscapes(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\') {
            const std::size_t used = unicodeEscape(text, i, &out);
            if (used) {
                i += used - 1;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

// Codigos de control de TEXT y MTEXT: %%c diametro, %%d grados, %%p mas-menos...
std::string expandPercentCodes(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && text[i + 1] == '%') {
            const char code = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i + 2])));
            if (code == 'c') { appendUtf8(&out, 0x00D8); i += 2; continue; }
            if (code == 'd') { appendUtf8(&out, 0x00B0); i += 2; continue; }
            if (code == 'p') { appendUtf8(&out, 0x00B1); i += 2; continue; }
            if (code == '%') { out.push_back('%'); i += 2; continue; }
            if (code == 'u' || code == 'o' || code == 'k') { i += 2; continue; }
            if (std::isdigit(static_cast<unsigned char>(code))) {
                std::size_t k = i + 2;
                std::uint32_t value = 0;
                while (k < text.size() && k < i + 5 && std::isdigit(static_cast<unsigned char>(text[k]))) {
                    value = value * 10 + static_cast<std::uint32_t>(text[k] - '0');
                    ++k;
                }
                if (value > 0 && value < 256) appendUtf8(&out, value);
                i = k - 1;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

// Quita el formato en linea de MTEXT y deja texto plano con saltos de renglon.
std::string cleanMText(const std::string& text) {
    std::string out;
    const std::size_t n = text.size();
    for (std::size_t i = 0; i < n; ++i) {
        const char c = text[i];
        if (c == '{' || c == '}') continue;
        if (c != '\\' || i + 1 >= n) {
            out.push_back(c);
            continue;
        }
        const char code = text[i + 1];
        switch (code) {
            case 'P':
            case 'N':
                out.push_back('\n');
                ++i;
                break;
            case '~':
                out.push_back(' ');
                ++i;
                break;
            case '\\':
            case '{':
            case '}':
                out.push_back(code);
                ++i;
                break;
            case 'U': {
                const std::size_t used = unicodeEscape(text, i, &out);
                if (used) {
                    i += used - 1;
                } else {
                    out.push_back(code);
                    ++i;
                }
                break;
            }
            case 'L': case 'l': case 'O': case 'o': case 'K': case 'k': case 'X':
                ++i;
                break;
            case 'S': {
                // Apilado: \S1^2; \S1/2; \S1#2; -> 1/2
                const std::size_t end = text.find(';', i + 2);
                const std::string body = text.substr(i + 2, (end == std::string::npos ? n : end) - i - 2);
                std::string top, bottom;
                const std::size_t split = body.find_first_of("^/#");
                top = trim(body.substr(0, split));
                if (split != std::string::npos) bottom = trim(body.substr(split + 1));
                out += top;
                if (!bottom.empty()) out += "/" + bottom;
                i = end == std::string::npos ? n : end;
                break;
            }
            case 'A': case 'C': case 'c': case 'f': case 'F': case 'H': case 'h': case 'Q':
            case 'q': case 'T': case 't': case 'W': case 'w': case 'p': {
                const std::size_t end = text.find(';', i + 2);
                i = end == std::string::npos ? n : end;
                break;
            }
            default:
                out.push_back(code);
                ++i;
                break;
        }
    }
    return out;
}

bool blankText(const std::string& text) {
    for (const char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

std::string upper(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

// Pasos para un arco: uno cada 5 grados, que en pantalla ya se ve redondo.
int arcSteps(double sweep) {
    const int steps = static_cast<int>(std::ceil(std::fabs(sweep) / (kPi / 36.0) - 1e-9));
    return std::max(1, std::min(512, steps));
}

// Vertice de polilinea 2D en coordenadas del objeto. El bulge es la tangente
// de un cuarto del angulo del arco que va al vertice siguiente (0 = recta).
struct PathVertex {
    double x = 0, y = 0, bulge = 0;
};

struct Block {
    Vec3 base;
    std::vector<Entity> entities;
};

class DxfReader {
public:
    DxfReader(Mesh* mesh, int budgetMs) : M(*mesh), m_budget(budgetMs) {}

    bool run(const char* data, std::size_t length, std::string* error, LoadStats* stats);

private:
    void collectLayers(const std::vector<Entity>& entities);
    void collectBlocks(const std::vector<Entity>& entities);
    bool hidden(const Entity& entity) const;
    void emitSequence(const std::vector<Entity>& list, const Mat4& transform, int depth,
                      int paperSpace);
    void emitEntity(const Entity& entity, const Mat4& transform, int depth);
    void emitInsert(const Entity& entity, const Mat4& transform, int depth);
    void emitText(const Entity& entity, const Mat4& transform);
    void emitMText(const Entity& entity, const Mat4& transform);
    void placeText(MeshText text, const Vec3& anchor, const Vec3& direction, const Vec3& up,
                   const Mat4& transform);
    std::string decode(const std::string& raw) const;
    void emitLine(const Vec3& a, const Vec3& b, const Mat4& transform);
    void emitStrip(const std::vector<Vec3>& points, bool closed, const Mat4& transform,
                   bool curve = false);
    // Registran elementos medibles si la transformacion no deforma los circulos.
    void addCircle(const Vec3& center, const Frame& frame, double radius, double start,
                   double sweep, const Mat4& transform);
    void addContour(const std::vector<Vec3>& points, const std::vector<double>& bulges,
                    const Frame& frame, const Mat4& transform);
    void emitArc(const Entity& entity, double startAngle, double sweep, const Mat4& transform);
    void emitEllipse(const Entity& entity, const Mat4& transform);
    void emitSpline(const Entity& entity, const Mat4& transform);
    void emitPath(const std::vector<PathVertex>& vertices, bool closed, double elevation,
                  const Frame& frame, const Mat4& transform);
    void emitQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Mat4& transform);
    void emitPolyline(const std::vector<Entity>& vertices, const Entity& header,
                      const Mat4& transform);

    Mesh& M;
    Budget m_budget;
    bool m_truncated = false;
    std::unordered_map<std::string, Block> m_blocks;       // clave en mayusculas
    std::unordered_map<std::string, bool> m_hiddenLayers;  // clave en mayusculas
    double m_size = 1.0;
    int m_entityCount = 0;
    bool m_ansi = false;  // DXF anterior a 2007: textos en Windows-1252
};

std::string DxfReader::decode(const std::string& raw) const {
    // Aunque la version diga UTF-8, un texto que no lo es se lee como ANSI.
    if (m_ansi || !validUtf8(raw)) return ansiToUtf8(raw);
    return raw;
}

void DxfReader::placeText(MeshText text, const Vec3& anchor, const Vec3& direction,
                          const Vec3& up, const Mat4& transform) {
    if (blankText(text.text)) return;
    const Vec3 d = transform.direction(direction);
    const Vec3 u = transform.direction(up);
    const double ld = length(d), lu = length(u);
    if (ld < 1e-12 || lu < 1e-12) return;
    if (!(text.height > 0)) text.height = 2.5;
    text.position = transform.point(anchor);
    text.direction = d * (1.0 / ld);
    text.up = u * (1.0 / lu);
    text.height *= lu;
    text.widthFactor *= ld / lu;
    text.fitWidth *= ld;
    M.addText(text);
}

void DxfReader::emitText(const Entity& entity, const Mat4& transform) {
    const bool attribute = entity.type == "ATTRIB";
    if (attribute && (entity.integer(70, 0) & 1)) return;  // atributo invisible

    MeshText text;
    text.text = expandPercentCodes(expandUnicodeEscapes(decode(entity.text(1))));
    text.height = entity.number(40, 0.0);
    text.widthFactor = entity.number(41, 1.0);
    if (!(text.widthFactor > 0)) text.widthFactor = 1.0;

    const Frame frame = objectFrame(entity);
    const Vec3 first(entity.number(10), entity.number(20), entity.number(30));
    const Vec3 second(entity.number(11, first.x), entity.number(21, first.y),
                      entity.number(31, first.z));
    const double rotation = entity.number(50, 0.0) * kPi / 180.0;
    Vec3 direction(std::cos(rotation), std::sin(rotation), 0);
    Vec3 up(-std::sin(rotation), std::cos(rotation), 0);
    const int h = entity.integer(72, 0);
    const int v = entity.integer(attribute ? 74 : 73, 0);

    Vec3 anchor = first;
    if ((h == 3 || h == 5) && distance(first, second) > 1e-12) {
        // Alineado o ajustado: el renglon ocupa justo de un punto al otro.
        direction = normalize(second - first);
        up = Vec3(-direction.y, direction.x, 0);
        text.fitWidth = distance(first, second);
    } else if (h != 0 || v != 0) {
        anchor = second;
        text.halign = h == 4 ? 1 : std::min(2, std::max(0, h));
        text.valign = h == 4 ? 2 : std::min(3, std::max(0, v));
    }
    const int generation = entity.integer(71, 0);
    if (generation & 2) direction = -direction;  // espejado en X
    if (generation & 4) up = -up;                // cabeza abajo

    placeText(text, frame.dirToWorld(anchor), frame.dirToWorld(direction), frame.dirToWorld(up),
              transform);
}

void DxfReader::emitMText(const Entity& entity, const Mat4& transform) {
    std::string raw;
    for (const Pair& pair : entity.pairs) {
        if (pair.code == 3 || pair.code == 1) raw += pair.value;
    }
    MeshText text;
    text.text = expandPercentCodes(cleanMText(decode(raw)));
    text.height = entity.number(40, 0.0);
    const int attachment = std::min(9, std::max(1, entity.integer(71, 1)));
    text.halign = (attachment - 1) % 3;
    text.valign = 3 - (attachment - 1) / 3;  // 1-3 arriba, 4-6 medio, 7-9 abajo

    const Frame frame = objectFrame(entity);
    Vec3 direction(entity.number(11, 0.0), entity.number(21, 0.0), entity.number(31, 0.0));
    if (length(direction) < 1e-12) {
        // En grados: la referencia DXF dice radianes, pero AutoCAD y ezdxf escriben grados.
        const double rotation = entity.number(50, 0.0) * kPi / 180.0;
        direction = frame.dirToWorld(Vec3(std::cos(rotation), std::sin(rotation), 0));
    }
    direction = normalize(direction);
    const Vec3 up = normalize(cross(frame.z, direction));
    placeText(text, Vec3(entity.number(10), entity.number(20), entity.number(30)), direction, up,
              transform);
}

bool DxfReader::hidden(const Entity& entity) const {
    if (m_hiddenLayers.empty()) return false;
    const auto it = m_hiddenLayers.find(upper(entity.text(8)));
    return it != m_hiddenLayers.end() && it->second;
}

void DxfReader::emitLine(const Vec3& a, const Vec3& b, const Mat4& transform) {
    M.addSegment(transform.point(a), transform.point(b));
}

void DxfReader::emitStrip(const std::vector<Vec3>& points, bool closed, const Mat4& transform,
                          bool curve) {
    if (points.size() < 2) return;
    Vec3 previous = transform.point(points[0]);
    const Vec3 first = previous;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const Vec3 current = transform.point(points[i]);
        M.addSegment(previous, current, curve);
        previous = current;
    }
    if (closed && points.size() > 2) M.addSegment(previous, first, curve);
}

// Ejes del objeto transformados. Devuelve false si la transformacion deforma
// los circulos (escala distinta en x e y, o ejes que dejan de ser perpendiculares).
bool conformalAxes(const Frame& frame, const Mat4& transform, Vec3* x, Vec3* y, double* scale) {
    *x = transform.direction(frame.x);
    *y = transform.direction(frame.y);
    const double lx = length(*x), ly = length(*y);
    if (lx < 1e-12 || ly < 1e-12) return false;
    if (std::fabs(lx - ly) > 1e-9 * std::max(lx, ly)) return false;
    if (std::fabs(dot(*x, *y)) > 1e-9 * lx * ly) return false;
    *scale = lx;
    return true;
}

void DxfReader::addCircle(const Vec3& center, const Frame& frame, double radius, double start,
                          double sweep, const Mat4& transform) {
    Vec3 x, y;
    double scale = 1.0;
    if (radius <= 0 || !conformalAxes(frame, transform, &x, &y, &scale)) return;
    CircleFeature circle;
    circle.center = transform.point(center);
    circle.xAxis = normalize(x);
    // Con espejo la normal se invierte y el giro sigue siendo el mismo en pantalla.
    circle.normal = normalize(cross(x, y));
    circle.radius = radius * scale;
    circle.startAngle = start;
    circle.sweep = sweep;
    M.features.circles.push_back(circle);
}

void DxfReader::addContour(const std::vector<Vec3>& points, const std::vector<double>& bulges,
                           const Frame& frame, const Mat4& transform) {
    Vec3 x, y;
    double scale = 1.0;
    bool curved = false;
    for (const double b : bulges) curved = curved || std::fabs(b) > 1e-9;
    const bool conformal = conformalAxes(frame, transform, &x, &y, &scale);
    if (curved && !conformal) return;
    if (points.size() < (curved ? 2u : 3u)) return;
    ContourFeature contour;
    for (const Vec3& p : points) contour.points.push_back(transform.point(p));
    contour.bulges = bulges;
    const Vec3 n = cross(transform.direction(frame.x), transform.direction(frame.y));
    if (length(n) < 1e-18) return;
    contour.normal = normalize(n);
    M.features.contours.push_back(std::move(contour));
}

void DxfReader::emitQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                         const Mat4& transform) {
    const Vec3 pa = transform.point(a);
    const Vec3 pb = transform.point(b);
    const Vec3 pc = transform.point(c);
    const Vec3 pd = transform.point(d);

    const std::uint32_t ia = M.addVertex(pa, Vec3(0, 0, 1));
    const std::uint32_t ib = M.addVertex(pb, Vec3(0, 0, 1));
    const std::uint32_t ic = M.addVertex(pc, Vec3(0, 0, 1));
    M.addTriangle(ia, ib, ic);
    if (distance(pc, pd) > 1e-12) {
        const std::uint32_t id = M.addVertex(pd, Vec3(0, 0, 1));
        M.addTriangle(ia, ic, id);
    }
    M.addSegment(pa, pb);
    M.addSegment(pb, pc);
    M.addSegment(pc, pd);
    M.addSegment(pd, pa);
}

void DxfReader::emitArc(const Entity& entity, double startAngle, double sweep,
                        const Mat4& transform) {
    const Frame frame = objectFrame(entity);
    const double radius = entity.number(40, 0.0);
    if (radius <= 0) return;
    // El centro viene en coordenadas del objeto, no del mundo.
    const Vec3 center = frame.x * entity.number(10) + frame.y * entity.number(20) +
                        frame.z * entity.number(30);

    const int steps = std::max(2, arcSteps(sweep));
    std::vector<Vec3> points;
    points.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double angle = startAngle + sweep * i / steps;
        points.push_back(center + frame.x * (radius * std::cos(angle)) +
                         frame.y * (radius * std::sin(angle)));
    }
    emitStrip(points, false, transform, true);

    addCircle(center, frame, radius, startAngle, sweep, transform);
    if (std::fabs(sweep) >= 2 * kPi - 1e-9) {
        // Un circulo es tambien un contorno cerrado: dos medias vueltas.
        addContour({center + frame.x * radius, center - frame.x * radius}, {1.0, 1.0}, frame,
                   transform);
    }
}

void DxfReader::emitEllipse(const Entity& entity, const Mat4& transform) {
    // Centro y semieje mayor en coordenadas del mundo; el menor sale de la normal.
    const Vec3 center(entity.number(10), entity.number(20), entity.number(30));
    const Vec3 major(entity.number(11), entity.number(21), entity.number(31));
    Vec3 normal(entity.number(210, 0.0), entity.number(220, 0.0), entity.number(230, 1.0));
    if (length(normal) < 1e-12) normal = Vec3(0, 0, 1);
    const double ratio = entity.number(40, 1.0);
    if (length(major) < 1e-12 || ratio <= 0) return;
    const Vec3 minor = normalize(cross(normalize(normal), major)) * (length(major) * ratio);

    const double start = entity.number(41, 0.0);
    double sweep = entity.number(42, 2 * kPi) - start;
    while (sweep <= 1e-12) sweep += 2 * kPi;
    const int steps = std::max(2, arcSteps(sweep));
    std::vector<Vec3> points;
    points.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = start + sweep * i / steps;
        points.push_back(center + major * std::cos(t) + minor * std::sin(t));
    }
    emitStrip(points, false, transform, true);
}

void DxfReader::emitSpline(const Entity& entity, const Mat4& transform) {
    NurbsCurve curve;
    curve.degree = std::max(1, entity.integer(71, 3));
    std::vector<Vec3> fit;
    for (const Pair& pair : entity.pairs) {
        switch (pair.code) {
            case 40: curve.knots.push_back(pair.number()); break;
            case 41: curve.weights.push_back(pair.number()); break;
            case 10: curve.controls.emplace_back(pair.number(), 0.0, 0.0); break;
            case 20: if (!curve.controls.empty()) curve.controls.back().y = pair.number(); break;
            case 30: if (!curve.controls.empty()) curve.controls.back().z = pair.number(); break;
            case 11: fit.emplace_back(pair.number(), 0.0, 0.0); break;
            case 21: if (!fit.empty()) fit.back().y = pair.number(); break;
            case 31: if (!fit.empty()) fit.back().z = pair.number(); break;
            default: break;
        }
    }
    const bool closed = (entity.integer(70, 0) & 1) != 0;

    const std::size_t count = curve.controls.size();
    if (count >= 2) {
        curve.degree = std::min(curve.degree, static_cast<int>(count) - 1);
        if (curve.weights.size() != count) curve.weights.clear();
        if (curve.knots.size() != count + curve.degree + 1) {
            // Nudos ausentes o incoherentes: vector uniforme sujeto en los extremos.
            curve.knots.clear();
            const int inner = static_cast<int>(count) - curve.degree;
            for (int i = 0; i <= curve.degree; ++i) curve.knots.push_back(0.0);
            for (int i = 1; i < inner; ++i) curve.knots.push_back(static_cast<double>(i) / inner);
            for (int i = 0; i <= curve.degree; ++i) curve.knots.push_back(1.0);
        }
        if (curve.valid() && curve.tMax() > curve.tMin()) {
            std::vector<Vec3> points;
            const int spans = static_cast<int>(count) - curve.degree;
            const int steps = std::max(16, std::min(4096, spans * 12));
            points.reserve(steps + 1);
            for (int i = 0; i <= steps; ++i) {
                points.push_back(curve.eval(curve.tMin() +
                                            (curve.tMax() - curve.tMin()) * i / steps));
            }
            emitStrip(points, false, transform, true);
            return;
        }
    }

    // Solo puntos de ajuste: Catmull-Rom, que pasa exactamente por ellos.
    if (fit.size() < 2) return;
    if (closed && fit.size() > 2 && distance(fit.front(), fit.back()) > 1e-12) {
        fit.push_back(fit.front());
    }
    std::vector<Vec3> points;
    const int sub = 8;
    for (std::size_t i = 0; i + 1 < fit.size(); ++i) {
        const Vec3& p0 = i > 0 ? fit[i - 1] : fit[i];
        const Vec3& p1 = fit[i];
        const Vec3& p2 = fit[i + 1];
        const Vec3& p3 = i + 2 < fit.size() ? fit[i + 2] : fit[i + 1];
        for (int k = 0; k < sub; ++k) {
            const double t = static_cast<double>(k) / sub;
            const double t2 = t * t, t3 = t2 * t;
            points.push_back((p1 * 2.0 + (p2 - p0) * t + (p0 * 2.0 - p1 * 5.0 + p2 * 4.0 - p3) * t2 +
                              (p1 * 3.0 - p0 - p2 * 3.0 + p3) * t3) * 0.5);
        }
    }
    points.push_back(fit.back());
    emitStrip(points, false, transform, true);
}

void DxfReader::emitPath(const std::vector<PathVertex>& vertices, bool closed, double elevation,
                         const Frame& frame, const Mat4& transform) {
    const std::size_t n = vertices.size();
    if (n < 2) return;
    auto toWorld = [&](double x, double y) { return frame.dirToWorld(Vec3(x, y, elevation)); };

    const std::size_t segments = closed ? n : n - 1;
    for (std::size_t i = 0; i < segments; ++i) {
        const PathVertex& a = vertices[i];
        const PathVertex& b = vertices[(i + 1) % n];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double chord = std::sqrt(dx * dx + dy * dy);
        if (std::fabs(a.bulge) > 1e-9 && chord > 1e-12) {
            // Centro a la izquierda de la cuerda si el bulge es positivo (antihorario).
            const double sweep = 4.0 * std::atan(a.bulge);
            const double offset = chord * (1.0 - a.bulge * a.bulge) / (4.0 * a.bulge);
            const double cx = (a.x + b.x) / 2 - dy / chord * offset;
            const double cy = (a.y + b.y) / 2 + dx / chord * offset;
            const double radius = std::sqrt((a.x - cx) * (a.x - cx) + (a.y - cy) * (a.y - cy));
            const double start = std::atan2(a.y - cy, a.x - cx);
            const int steps = arcSteps(sweep);
            std::vector<Vec3> arc;
            arc.reserve(steps + 1);
            for (int k = 0; k <= steps; ++k) {
                const double angle = start + sweep * k / steps;
                arc.push_back(k == steps ? toWorld(b.x, b.y)
                                         : toWorld(cx + radius * std::cos(angle),
                                                   cy + radius * std::sin(angle)));
            }
            emitStrip(arc, false, transform, true);
            addCircle(toWorld(cx, cy), frame, radius, start, sweep, transform);
        } else {
            emitLine(toWorld(a.x, a.y), toWorld(b.x, b.y), transform);
        }
    }

    if (closed) {
        std::vector<Vec3> points;
        std::vector<double> bulges;
        for (const PathVertex& v : vertices) {
            points.push_back(toWorld(v.x, v.y));
            bulges.push_back(v.bulge);
        }
        addContour(points, bulges, frame, transform);
    }
}

void DxfReader::emitPolyline(const std::vector<Entity>& vertices, const Entity& header,
                             const Mat4& transform) {
    const int flags = header.integer(70, 0);
    const bool polyfaceMesh = (flags & 64) != 0;
    const bool polygonMesh = (flags & 16) != 0;
    const bool is3d = (flags & 8) != 0;
    const bool closed = (flags & 1) != 0;

    if (polyfaceMesh) {
        std::vector<Vec3> points;
        for (const Entity& vertex : vertices) {
            const int vertexFlags = vertex.integer(70, 0);
            if ((vertexFlags & 128) && !(vertexFlags & 64)) {
                // Cara: indices 71..74 (1-based, negativo = arista invisible).
                const int ia = std::abs(vertex.integer(71, 0)) - 1;
                const int ib = std::abs(vertex.integer(72, 0)) - 1;
                const int ic = std::abs(vertex.integer(73, 0)) - 1;
                const int id = std::abs(vertex.integer(74, 0)) - 1;
                const int count = static_cast<int>(points.size());
                if (ia < 0 || ib < 0 || ic < 0 || ia >= count || ib >= count || ic >= count) {
                    continue;
                }
                const Vec3 d = (id >= 0 && id < count) ? points[id] : points[ic];
                emitQuad(points[ia], points[ib], points[ic], d, transform);
            } else {
                points.emplace_back(vertex.number(10), vertex.number(20), vertex.number(30));
            }
        }
        return;
    }

    // Los puntos de control del marco de una polilinea ajustada no se dibujan.
    std::vector<const Entity*> drawn;
    for (const Entity& vertex : vertices) {
        if (!(vertex.integer(70, 0) & 16)) drawn.push_back(&vertex);
    }

    if (polygonMesh) {
        const int rows = header.integer(71, 0);
        const int cols = header.integer(72, 0);
        if (rows >= 2 && cols >= 2 && drawn.size() == static_cast<std::size_t>(rows) * cols) {
            auto at = [&](int r, int c) {
                const Entity& v = *drawn[static_cast<std::size_t>(r) * cols + c];
                return Vec3(v.number(10), v.number(20), v.number(30));
            };
            const bool closedRows = (flags & 1) != 0, closedCols = (flags & 32) != 0;
            for (int r = 0; r < rows - (closedRows ? 0 : 1); ++r) {
                for (int c = 0; c < cols - (closedCols ? 0 : 1); ++c) {
                    const int r1 = (r + 1) % rows, c1 = (c + 1) % cols;
                    emitQuad(at(r, c), at(r1, c), at(r1, c1), at(r, c1), transform);
                }
            }
            return;
        }
    }

    if (is3d || polygonMesh) {
        std::vector<Vec3> points;
        for (const Entity* vertex : drawn) {
            points.emplace_back(vertex->number(10), vertex->number(20), vertex->number(30));
        }
        emitStrip(points, closed, transform);
        return;
    }

    // Polilinea 2D: coordenadas del objeto, elevacion en la cabecera y arcos por bulge.
    std::vector<PathVertex> path;
    for (const Entity* vertex : drawn) {
        path.push_back({vertex->number(10), vertex->number(20), vertex->number(42, 0.0)});
    }
    emitPath(path, closed, header.number(30, 0.0), objectFrame(header), transform);
}

void DxfReader::emitInsert(const Entity& entity, const Mat4& transform, int depth) {
    const auto it = m_blocks.find(upper(entity.text(2)));
    if (it == m_blocks.end()) return;
    const Block& block = it->second;

    const Vec3 origin(entity.number(10), entity.number(20), entity.number(30));
    const double sx = entity.number(41, 1.0);
    const double sy = entity.number(42, 1.0);
    const double sz = entity.number(43, 1.0);
    const double rotation = entity.number(50, 0.0) * kPi / 180.0;
    const double cosR = std::cos(rotation);
    const double sinR = std::sin(rotation);
    // MINSERT: la misma entidad con filas y columnas.
    const int cols = std::max(1, entity.integer(70, 1));
    const int rows = std::max(1, entity.integer(71, 1));
    const double colSpacing = entity.number(44, 0.0);
    const double rowSpacing = entity.number(45, 0.0);
    const Mat4 ocs = frameToMatrix(objectFrame(entity));

    int copies = 0;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (++copies > 10000 || m_truncated) return;
            // OCS * T(insercion) * R * T(celda) * S * T(-base)
            const double ox = c * colSpacing, oy = r * rowSpacing;
            Mat4 local;
            local.m[0] = cosR * sx; local.m[1] = -sinR * sy; local.m[2] = 0;
            local.m[4] = sinR * sx; local.m[5] = cosR * sy;  local.m[6] = 0;
            local.m[8] = 0;         local.m[9] = 0;          local.m[10] = sz;
            const Vec3 base(block.base.x * sx, block.base.y * sy, block.base.z * sz);
            const double tx = ox - base.x, ty = oy - base.y;
            local.m[3] = origin.x + cosR * tx - sinR * ty;
            local.m[7] = origin.y + sinR * tx + cosR * ty;
            local.m[11] = origin.z - base.z;
            emitSequence(block.entities, transform * ocs * local, depth + 1, -1);
        }
    }
}

void DxfReader::emitEntity(const Entity& entity, const Mat4& transform, int depth) {
    if (depth > 8 || m_truncated) return;
    if ((++m_entityCount & 0x3FF) == 0 && m_budget.expired()) {
        m_truncated = true;
        return;
    }

    if (entity.type == "LINE") {
        emitLine(Vec3(entity.number(10), entity.number(20), entity.number(30)),
                 Vec3(entity.number(11), entity.number(21), entity.number(31)), transform);
        return;
    }
    if (entity.type == "CIRCLE") {
        emitArc(entity, 0.0, 2 * kPi, transform);
        return;
    }
    if (entity.type == "ARC") {
        const double start = entity.number(50, 0.0) * kPi / 180.0;
        const double end = entity.number(51, 360.0) * kPi / 180.0;
        double sweep = end - start;
        while (sweep <= 0) sweep += 2 * kPi;
        emitArc(entity, start, sweep, transform);
        return;
    }
    if (entity.type == "ELLIPSE") {
        emitEllipse(entity, transform);
        return;
    }
    if (entity.type == "SPLINE") {
        emitSpline(entity, transform);
        return;
    }
    if (entity.type == "LWPOLYLINE") {
        std::vector<PathVertex> path;
        for (const Pair& pair : entity.pairs) {
            if (pair.code == 10) {
                path.push_back(PathVertex());
                path.back().x = pair.number();
            } else if (pair.code == 20 && !path.empty()) {
                path.back().y = pair.number();
            } else if (pair.code == 42 && !path.empty()) {
                path.back().bulge = pair.number();
            }
        }
        emitPath(path, (entity.integer(70, 0) & 1) != 0, entity.number(38, 0.0),
                 objectFrame(entity), transform);
        return;
    }
    if (entity.type == "LEADER") {
        std::vector<Vec3> points;
        for (const Pair& pair : entity.pairs) {
            if (pair.code == 10) points.emplace_back(pair.number(), 0.0, 0.0);
            else if (pair.code == 20 && !points.empty()) points.back().y = pair.number();
            else if (pair.code == 30 && !points.empty()) points.back().z = pair.number();
        }
        emitStrip(points, false, transform);
        return;
    }
    if (entity.type == "3DFACE" || entity.type == "SOLID" || entity.type == "TRACE") {
        const Vec3 a(entity.number(10), entity.number(20), entity.number(30));
        const Vec3 b(entity.number(11), entity.number(21), entity.number(31));
        const Vec3 c(entity.number(12), entity.number(22), entity.number(32));
        const Vec3 d(entity.number(13, c.x), entity.number(23, c.y), entity.number(33, c.z));
        // SOLID y TRACE numeran las esquinas en zigzag; 3DFACE las da en orden.
        if (entity.type == "3DFACE") emitQuad(a, b, c, d, transform);
        else emitQuad(a, b, d, c, transform);
        return;
    }
    if (entity.type == "INSERT") {
        emitInsert(entity, transform, depth);
        return;
    }
    if (entity.type == "TEXT" || entity.type == "ATTRIB") {
        emitText(entity, transform);
        return;
    }
    if (entity.type == "MTEXT") {
        emitMText(entity, transform);
        return;
    }
    if (entity.type == "DIMENSION") {
        // La cota ya dibujada esta en un bloque anonimo (*D..), en coordenadas del mundo.
        const auto it = m_blocks.find(upper(entity.text(2)));
        if (it != m_blocks.end()) {
            emitSequence(it->second.entities, transform, depth + 1, -1);
        } else if (entity.has(11)) {
            // Sin bloque: al menos el valor en su sitio.
            std::string value = entity.text(1);
            if (value.empty() || value.find("<>") != std::string::npos) {
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%.4g", entity.number(42, 0.0));
                const std::size_t mark = value.find("<>");
                value = mark == std::string::npos ? buffer
                                                  : value.replace(mark, 2, buffer);
            }
            MeshText text;
            text.text = expandPercentCodes(cleanMText(decode(value)));
            text.height = 2.5;
            text.halign = 1;
            text.valign = 2;
            const double angle = entity.number(53, 0.0) * kPi / 180.0;
            placeText(text, Vec3(entity.number(11), entity.number(21), entity.number(31)),
                      Vec3(std::cos(angle), std::sin(angle), 0),
                      Vec3(-std::sin(angle), std::cos(angle), 0), transform);
        }
        return;
    }
    // POINT, HATCH, RAY, XLINE, VIEWPORT...: no aportan a la vista previa o no tienen fin.
}

// Recorre una lista de entidades juntando POLYLINE con sus VERTEX.
// paperSpace: 0 solo modelo, 1 solo papel, -1 todas (dentro de bloques).
void DxfReader::emitSequence(const std::vector<Entity>& list, const Mat4& transform, int depth,
                             int paperSpace) {
    if (depth > 8) return;
    for (std::size_t i = 0; i < list.size() && !m_truncated; ++i) {
        const Entity& entity = list[i];
        if (entity.type == "VERTEX" || entity.type == "SEQEND" || entity.type == "ATTDEF") continue;
        const bool skip = hidden(entity) ||
                          (paperSpace >= 0 && (entity.integer(67, 0) == 1) != (paperSpace == 1));
        if (entity.type == "POLYLINE") {
            std::vector<Entity> vertices;
            std::size_t k = i + 1;
            while (k < list.size() && list[k].type == "VERTEX") {
                vertices.push_back(list[k]);
                ++k;
            }
            if (!skip) emitPolyline(vertices, entity, transform);
            i = k - 1;
            continue;
        }
        if (!skip) emitEntity(entity, transform, depth);
    }
}

void DxfReader::collectLayers(const std::vector<Entity>& entities) {
    for (const Entity& entity : entities) {
        if (entity.type != "LAYER") continue;
        // Bit 1 = congelada; color negativo = apagada.
        const bool off = (entity.integer(70, 0) & 1) != 0 || entity.integer(62, 7) < 0;
        m_hiddenLayers[upper(entity.text(2))] = off;
    }
}

void DxfReader::collectBlocks(const std::vector<Entity>& entities) {
    Block* current = nullptr;
    for (const Entity& entity : entities) {
        if (entity.type == "BLOCK") {
            current = &m_blocks[upper(entity.text(2))];
            current->base = Vec3(entity.number(10), entity.number(20), entity.number(30));
            continue;
        }
        if (entity.type == "ENDBLK") {
            current = nullptr;
            continue;
        }
        if (current) current->entities.push_back(entity);
    }
}

bool DxfReader::run(const char* data, std::size_t length, std::string* error, LoadStats* stats) {
    if (length > 8 && std::strncmp(data, "AutoCAD Binary DXF", 18) == 0) {
        if (error) *error = "DXF binario: guardalo como DXF ASCII";
        return false;
    }

    const std::vector<Pair> pairs = readPairs(data, length);
    if (pairs.empty()) {
        if (error) *error = "archivo DXF vacio";
        return false;
    }

    // AutoCAD 2007 (AC1021) paso los textos a UTF-8; antes eran ANSI.
    for (std::size_t i = 0; i + 1 < pairs.size() && i < 4000; ++i) {
        if (pairs[i].code == 9 && pairs[i].value == "$ACADVER") {
            m_ansi = pairs[i + 1].value < "AC1021";
            break;
        }
    }

    // $INSUNITS: 1 pulgada, 2 pie, 4 mm, 5 cm, 6 m. Sin el dato, mm.
    M.units = LengthUnit::Millimeter;
    for (std::size_t i = 0; i + 1 < pairs.size() && i < 4000; ++i) {
        if (pairs[i].code == 9 && pairs[i].value == "$INSUNITS" && pairs[i + 1].code == 70) {
            switch (pairs[i + 1].integer()) {
                case 1: M.units = LengthUnit::Inch; break;
                case 2: M.units = LengthUnit::Foot; break;
                case 5: M.units = LengthUnit::Centimeter; break;
                case 6: M.units = LengthUnit::Meter; break;
                default: break;
            }
            break;
        }
    }

    // Partir en entidades y quedarse con las secciones TABLES, BLOCKS y ENTITIES.
    std::vector<Entity> tableEntities;
    std::vector<Entity> blockEntities;
    std::vector<Entity> modelEntities;
    std::string section;
    Entity current;
    bool inEntity = false;
    std::vector<Entity>* sink = nullptr;

    auto flush = [&]() {
        if (inEntity && sink) sink->push_back(current);
        inEntity = false;
        current = Entity();
    };

    for (std::size_t i = 0; i < pairs.size(); ++i) {
        const Pair& pair = pairs[i];
        if (pair.code == 0) {
            flush();
            if (pair.value == "SECTION") {
                section.clear();
                if (i + 1 < pairs.size() && pairs[i + 1].code == 2) section = pairs[i + 1].value;
                sink = section == "BLOCKS"     ? &blockEntities
                       : section == "ENTITIES" ? &modelEntities
                       : section == "TABLES"   ? &tableEntities
                                               : nullptr;
                continue;
            }
            if (pair.value == "ENDSEC" || pair.value == "EOF") {
                sink = nullptr;
                continue;
            }
            if (!sink) continue;
            current.type = pair.value;
            inEntity = true;
            continue;
        }
        if (inEntity) current.pairs.push_back(pair);
    }
    flush();

    collectLayers(tableEntities);
    collectBlocks(blockEntities);

    // Espacio modelo; el espacio papel (marco, cajetin) solo si el modelo esta vacio.
    emitSequence(modelEntities, Mat4::identity(), 0, 0);
    if (M.empty()) emitSequence(modelEntities, Mat4::identity(), 0, 1);

    if (M.empty()) {
        if (error) *error = "el DXF no contiene geometria dibujable";
        return false;
    }
    if (!M.indices.empty()) computeFlatNormals(&M);
    else M.solids = 0;

    m_size = M.bounds.diagonal();
    if (stats) {
        stats->faces = static_cast<int>(M.triangleCount());
        stats->triangles = M.triangleCount();
        stats->truncated = m_truncated;
        stats->schema = "DXF";
    }
    return true;
}

}  // namespace

bool loadDxf(const char* data, std::size_t length, Mesh* mesh, std::string* error,
             LoadStats* stats, int budgetMs) {
    DxfReader reader(mesh, budgetMs);
    return reader.run(data, length, error, stats);
}

}  // namespace stp
