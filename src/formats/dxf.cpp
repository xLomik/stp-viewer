// DXF ASCII: alambre y caras. Cubre lo que sale de un CAD 2D/3D corriente
// (LINE, LWPOLYLINE, POLYLINE, ARC, CIRCLE, 3DFACE, SOLID) y los bloques.
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "../engine/budget.h"
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

class DxfReader {
public:
    DxfReader(Mesh* mesh, int budgetMs) : M(*mesh), m_budget(budgetMs) {}

    bool run(const char* data, std::size_t length, std::string* error, LoadStats* stats);

private:
    void collectBlocks(const std::vector<Entity>& entities);
    void emitEntity(const Entity& entity, const Mat4& transform, int depth);
    void emitLine(const Vec3& a, const Vec3& b, const Mat4& transform);
    void emitArc(const Entity& entity, double startAngle, double sweep, const Mat4& transform);
    void emitQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Mat4& transform);
    void emitPolyline(const std::vector<Entity>& vertices, const Entity& header,
                      const Mat4& transform);

    Mesh& M;
    Budget m_budget;
    bool m_truncated = false;
    std::unordered_map<std::string, std::vector<Entity>> m_blocks;
    double m_size = 1.0;
    int m_entityCount = 0;
};

void DxfReader::emitLine(const Vec3& a, const Vec3& b, const Mat4& transform) {
    M.addSegment(transform.point(a), transform.point(b));
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

    int steps = static_cast<int>(std::ceil(std::fabs(sweep) / (kPi / 18.0)));
    steps = std::max(2, std::min(256, steps));

    Vec3 previous;
    for (int i = 0; i <= steps; ++i) {
        const double angle = startAngle + sweep * i / steps;
        const Vec3 point = center + frame.x * (radius * std::cos(angle)) +
                           frame.y * (radius * std::sin(angle));
        const Vec3 world = transform.point(point);
        if (i > 0) M.addSegment(previous, world);
        previous = world;
    }
}

void DxfReader::emitPolyline(const std::vector<Entity>& vertices, const Entity& header,
                             const Mat4& transform) {
    const int flags = header.integer(70, 0);
    const bool polyfaceMesh = (flags & 64) != 0;
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

    std::vector<Vec3> points;
    for (const Entity& vertex : vertices) {
        points.emplace_back(vertex.number(10), vertex.number(20), vertex.number(30));
    }
    for (std::size_t i = 1; i < points.size(); ++i) {
        emitLine(points[i - 1], points[i], transform);
    }
    if (closed && points.size() > 2) emitLine(points.back(), points.front(), transform);
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
    if (entity.type == "LWPOLYLINE") {
        const double elevation = entity.number(38, 0.0);
        const Frame frame = objectFrame(entity);
        std::vector<Vec3> points;
        double x = 0;
        bool hasX = false;
        for (const Pair& pair : entity.pairs) {
            if (pair.code == 10) {
                x = pair.number();
                hasX = true;
            } else if (pair.code == 20 && hasX) {
                points.push_back(frame.x * x + frame.y * pair.number() + frame.z * elevation);
                hasX = false;
            }
        }
        for (std::size_t i = 1; i < points.size(); ++i) emitLine(points[i - 1], points[i], transform);
        if ((entity.integer(70, 0) & 1) && points.size() > 2) {
            emitLine(points.back(), points.front(), transform);
        }
        return;
    }
    if (entity.type == "3DFACE" || entity.type == "SOLID") {
        const Vec3 a(entity.number(10), entity.number(20), entity.number(30));
        const Vec3 b(entity.number(11), entity.number(21), entity.number(31));
        const Vec3 c(entity.number(12), entity.number(22), entity.number(32));
        const Vec3 d(entity.number(13, c.x), entity.number(23, c.y), entity.number(33, c.z));
        // SOLID numera las esquinas en zigzag; 3DFACE las da en orden.
        if (entity.type == "SOLID") emitQuad(a, b, d, c, transform);
        else emitQuad(a, b, c, d, transform);
        return;
    }
    if (entity.type == "POINT") {
        return;  // un punto suelto no aporta nada a la vista previa
    }
    if (entity.type == "INSERT") {
        const std::string name = entity.text(2);
        const auto it = m_blocks.find(name);
        if (it == m_blocks.end()) return;

        const Vec3 origin(entity.number(10), entity.number(20), entity.number(30));
        const double sx = entity.number(41, 1.0);
        const double sy = entity.number(42, 1.0);
        const double sz = entity.number(43, 1.0);
        const double rotation = entity.number(50, 0.0) * kPi / 180.0;
        const double cosR = std::cos(rotation);
        const double sinR = std::sin(rotation);

        Mat4 local;
        local.m[0] = cosR * sx; local.m[1] = -sinR * sy; local.m[2] = 0;   local.m[3] = origin.x;
        local.m[4] = sinR * sx; local.m[5] = cosR * sy;  local.m[6] = 0;   local.m[7] = origin.y;
        local.m[8] = 0;         local.m[9] = 0;          local.m[10] = sz; local.m[11] = origin.z;

        const Mat4 combined = transform * local;
        const std::vector<Entity>& contents = it->second;
        for (std::size_t i = 0; i < contents.size(); ++i) {
            if (contents[i].type == "VERTEX") continue;
            if (contents[i].type == "POLYLINE") {
                std::vector<Entity> vertices;
                std::size_t k = i + 1;
                while (k < contents.size() && contents[k].type == "VERTEX") {
                    vertices.push_back(contents[k]);
                    ++k;
                }
                emitPolyline(vertices, contents[i], combined);
                i = k;
                continue;
            }
            emitEntity(contents[i], combined, depth + 1);
        }
        return;
    }
}

void DxfReader::collectBlocks(const std::vector<Entity>& entities) {
    std::string current;
    for (const Entity& entity : entities) {
        if (entity.type == "BLOCK") {
            current = entity.text(2);
            m_blocks[current];
            continue;
        }
        if (entity.type == "ENDBLK") {
            current.clear();
            continue;
        }
        if (!current.empty()) m_blocks[current].push_back(entity);
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

    // Partir en entidades y quedarse con las secciones BLOCKS y ENTITIES.
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
                sink = section == "BLOCKS" ? &blockEntities
                                           : (section == "ENTITIES" ? &modelEntities : nullptr);
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

    collectBlocks(blockEntities);

    for (std::size_t i = 0; i < modelEntities.size(); ++i) {
        if (modelEntities[i].type == "POLYLINE") {
            std::vector<Entity> vertices;
            std::size_t k = i + 1;
            while (k < modelEntities.size() && modelEntities[k].type == "VERTEX") {
                vertices.push_back(modelEntities[k]);
                ++k;
            }
            emitPolyline(vertices, modelEntities[i], Mat4::identity());
            i = k;
            continue;
        }
        if (modelEntities[i].type == "VERTEX" || modelEntities[i].type == "SEQEND") continue;
        emitEntity(modelEntities[i], Mat4::identity(), 0);
    }

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
