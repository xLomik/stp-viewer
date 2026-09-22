// STL (ASCII y binario), OBJ y PLY: mallas ya trianguladas, sin superficies.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "formats.h"

namespace stp {
namespace {

// Vertices repetidos se funden para que las aristas compartidas no dupliquen
// trabajo de render y las normales puedan suavizarse.
class VertexWelder {
public:
    explicit VertexWelder(double quantum) : m_quantum(quantum > 0 ? quantum : 1e-9) {}

    std::uint32_t add(const Vec3& p, Mesh* mesh) {
        const long long kx = static_cast<long long>(std::llround(p.x / m_quantum));
        const long long ky = static_cast<long long>(std::llround(p.y / m_quantum));
        const long long kz = static_cast<long long>(std::llround(p.z / m_quantum));
        const Key composite{kx, ky, kz};
        const auto it = m_map.find(composite);
        if (it != m_map.end()) return it->second;
        const std::uint32_t index = mesh->addVertex(p, Vec3(0, 0, 1));
        m_map.emplace(composite, index);
        return index;
    }

private:
    struct Key {
        long long x, y, z;
        bool operator==(const Key& other) const {
            return x == other.x && y == other.y && z == other.z;
        }
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const {
            std::size_t h = static_cast<std::size_t>(k.x) * 73856093u;
            h ^= static_cast<std::size_t>(k.y) * 19349663u;
            h ^= static_cast<std::size_t>(k.z) * 83492791u;
            return h;
        }
    };

    double m_quantum;
    std::unordered_map<Key, std::uint32_t, KeyHash> m_map;
};

float readFloat(const char* p) {
    float value = 0;
    std::memcpy(&value, p, 4);
    return value;
}

std::uint32_t readUint32(const char* p) {
    std::uint32_t value = 0;
    std::memcpy(&value, p, 4);
    return value;
}

bool looksLikeAsciiStl(const char* data, std::size_t length) {
    if (length < 6) return false;
    std::size_t i = 0;
    while (i < length && std::isspace(static_cast<unsigned char>(data[i]))) ++i;
    return length - i >= 5 && std::strncmp(data + i, "solid", 5) == 0 &&
           std::memchr(data, 'f', std::min<std::size_t>(length, 512)) != nullptr;
}

void addSharpEdges(Mesh* mesh, double angleThresholdDegrees) {
    // Dibuja como arista solo los quiebres marcados: en una malla, todas las
    // aristas serian ruido visual.
    struct EdgeInfo {
        Vec3 normal;
        std::uint32_t a = 0, b = 0;
        int count = 0;
        bool sharp = false;
    };
    std::unordered_map<std::uint64_t, EdgeInfo> edges;
    edges.reserve(mesh->indices.size());

    const double cosLimit = std::cos(angleThresholdDegrees * kPi / 180.0);
    for (std::size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
        const std::uint32_t idx[3] = {mesh->indices[i], mesh->indices[i + 1], mesh->indices[i + 2]};
        const Vec3& a = mesh->positions[idx[0]];
        const Vec3& b = mesh->positions[idx[1]];
        const Vec3& c = mesh->positions[idx[2]];
        const Vec3 normal = normalize(cross(b - a, c - a));
        for (int e = 0; e < 3; ++e) {
            std::uint32_t u = idx[e];
            std::uint32_t v = idx[(e + 1) % 3];
            if (u > v) std::swap(u, v);
            const std::uint64_t key = (static_cast<std::uint64_t>(u) << 32) | v;
            auto it = edges.find(key);
            if (it == edges.end()) {
                EdgeInfo info;
                info.normal = normal;
                info.a = u;
                info.b = v;
                info.count = 1;
                edges.emplace(key, info);
            } else {
                it->second.count++;
                if (dot(it->second.normal, normal) < cosLimit) it->second.sharp = true;
            }
        }
    }
    for (const auto& kv : edges) {
        // Aristas de borde (una sola cara) o quiebres marcados.
        if (kv.second.count == 1 || kv.second.sharp) {
            mesh->addSegment(mesh->positions[kv.second.a], mesh->positions[kv.second.b]);
        }
    }
}

}  // namespace

// Una malla no trae informacion de suavizado: promediar normales entre caras
// vecinas emborrona los cantos vivos, asi que cada triangulo recibe su propia
// copia de los vertices con la normal de su plano.
void computeFlatNormals(Mesh* mesh) {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<std::uint32_t> indices;
    positions.reserve(mesh->indices.size());
    normals.reserve(mesh->indices.size());
    indices.reserve(mesh->indices.size());

    for (std::size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
        const Vec3& a = mesh->positions[mesh->indices[i]];
        const Vec3& b = mesh->positions[mesh->indices[i + 1]];
        const Vec3& c = mesh->positions[mesh->indices[i + 2]];
        const Vec3 face = cross(b - a, c - a);
        const Vec3 normal = length(face) > 1e-18 ? normalize(face) : Vec3(0, 0, 1);
        for (const Vec3& vertex : {a, b, c}) {
            indices.push_back(static_cast<std::uint32_t>(positions.size()));
            positions.push_back(vertex);
            normals.push_back(normal);
        }
    }
    mesh->positions.swap(positions);
    mesh->normals.swap(normals);
    mesh->indices.swap(indices);
    for (const Vec3& p : mesh->positions) mesh->bounds.add(p);
}

bool loadStl(const char* data, std::size_t length, Mesh* mesh, std::string* error) {
    if (!data || length < 15) {
        if (error) *error = "archivo STL vacio";
        return false;
    }

    if (looksLikeAsciiStl(data, length)) {
        const std::string text(data, length);
        std::size_t pos = 0;
        std::vector<Vec3> triangle;
        VertexWelder welder(1e-6);
        while ((pos = text.find("vertex", pos)) != std::string::npos) {
            pos += 6;
            double x = 0, y = 0, z = 0;
            if (std::sscanf(text.c_str() + pos, "%lf %lf %lf", &x, &y, &z) != 3) break;
            triangle.emplace_back(x, y, z);
            if (triangle.size() == 3) {
                const std::uint32_t ia = welder.add(triangle[0], mesh);
                const std::uint32_t ib = welder.add(triangle[1], mesh);
                const std::uint32_t ic = welder.add(triangle[2], mesh);
                if (ia != ib && ib != ic && ia != ic) mesh->addTriangle(ia, ib, ic);
                triangle.clear();
            }
        }
    } else {
        if (length < 84) {
            if (error) *error = "archivo STL binario incompleto";
            return false;
        }
        const std::uint32_t count = readUint32(data + 80);
        const std::size_t needed = 84 + static_cast<std::size_t>(count) * 50;
        const std::uint32_t usable =
            needed <= length ? count : static_cast<std::uint32_t>((length - 84) / 50);

        VertexWelder welder(1e-6);
        for (std::uint32_t t = 0; t < usable; ++t) {
            const char* record = data + 84 + static_cast<std::size_t>(t) * 50;
            const Vec3 a(readFloat(record + 12), readFloat(record + 16), readFloat(record + 20));
            const Vec3 b(readFloat(record + 24), readFloat(record + 28), readFloat(record + 32));
            const Vec3 c(readFloat(record + 36), readFloat(record + 40), readFloat(record + 44));
            const std::uint32_t ia = welder.add(a, mesh);
            const std::uint32_t ib = welder.add(b, mesh);
            const std::uint32_t ic = welder.add(c, mesh);
            if (ia != ib && ib != ic && ia != ic) mesh->addTriangle(ia, ib, ic);
        }
    }

    if (mesh->indices.empty()) {
        if (error) *error = "el STL no contiene triangulos";
        return false;
    }
    addSharpEdges(mesh, 35.0);
    computeFlatNormals(mesh);
    return true;
}

bool loadObj(const char* data, std::size_t length, Mesh* mesh, std::string* error) {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    const char* p = data;
    const char* end = data + length;

    auto parseIndex = [&](const char* token, int count) -> int {
        const int value = std::atoi(token);
        if (value > 0) return value - 1;
        if (value < 0) return count + value;
        return -1;
    };

    while (p < end) {
        const char* lineEnd = static_cast<const char*>(std::memchr(p, '\n', end - p));
        if (!lineEnd) lineEnd = end;
        const std::string line(p, lineEnd);
        p = lineEnd + 1;
        if (line.size() < 2 || line[0] == '#') continue;

        if (line[0] == 'v' && (line[1] == ' ' || line[1] == '\t')) {
            double x = 0, y = 0, z = 0;
            if (std::sscanf(line.c_str() + 1, "%lf %lf %lf", &x, &y, &z) == 3) {
                positions.emplace_back(x, y, z);
            }
        } else if (line[0] == 'v' && line[1] == 'n') {
            double x = 0, y = 0, z = 0;
            if (std::sscanf(line.c_str() + 2, "%lf %lf %lf", &x, &y, &z) == 3) {
                normals.emplace_back(x, y, z);
            }
        } else if (line[0] == 'f' && (line[1] == ' ' || line[1] == '\t')) {
            std::vector<int> face;
            std::size_t i = 1;
            while (i < line.size()) {
                while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
                if (i >= line.size()) break;
                const std::size_t start = i;
                while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
                const int index =
                    parseIndex(line.c_str() + start, static_cast<int>(positions.size()));
                if (index >= 0 && index < static_cast<int>(positions.size())) face.push_back(index);
            }
            // Abanico: los poligonos de OBJ suelen ser convexos.
            for (std::size_t k = 2; k < face.size(); ++k) {
                mesh->indices.push_back(static_cast<std::uint32_t>(face[0]));
                mesh->indices.push_back(static_cast<std::uint32_t>(face[k - 1]));
                mesh->indices.push_back(static_cast<std::uint32_t>(face[k]));
            }
        }
    }

    if (positions.empty() || mesh->indices.empty()) {
        if (error) *error = "el OBJ no contiene caras";
        return false;
    }
    mesh->positions = std::move(positions);
    for (const Vec3& v : mesh->positions) mesh->bounds.add(v);
    addSharpEdges(mesh, 35.0);
    computeFlatNormals(mesh);
    return true;
}

bool loadPly(const char* data, std::size_t length, Mesh* mesh, std::string* error) {
    const std::string head(data, std::min<std::size_t>(length, 4096));
    if (head.compare(0, 3, "ply") != 0) {
        if (error) *error = "no es un archivo PLY";
        return false;
    }
    const std::size_t headerEnd = head.find("end_header");
    if (headerEnd == std::string::npos) {
        if (error) *error = "cabecera PLY incompleta";
        return false;
    }
    std::size_t bodyStart = head.find('\n', headerEnd);
    if (bodyStart == std::string::npos) return false;
    ++bodyStart;

    const bool ascii = head.find("format ascii") != std::string::npos;
    const bool binaryLittle = head.find("format binary_little_endian") != std::string::npos;
    if (!ascii && !binaryLittle) {
        if (error) *error = "solo se leen PLY ascii o binario little endian";
        return false;
    }

    long vertexCount = 0, faceCount = 0;
    std::size_t pos = head.find("element vertex");
    if (pos != std::string::npos) vertexCount = std::atol(head.c_str() + pos + 14);
    pos = head.find("element face");
    if (pos != std::string::npos) faceCount = std::atol(head.c_str() + pos + 12);
    if (vertexCount <= 0) {
        if (error) *error = "el PLY no declara vertices";
        return false;
    }
    // Solo se admite el diseno habitual: x, y, z como float o double.
    const bool doublePrecision = head.find("property double x") != std::string::npos;

    if (ascii) {
        const char* p = data + bodyStart;
        const char* end = data + length;
        for (long i = 0; i < vertexCount && p < end; ++i) {
            double x = 0, y = 0, z = 0;
            if (std::sscanf(p, "%lf %lf %lf", &x, &y, &z) != 3) break;
            mesh->addVertex(Vec3(x, y, z), Vec3(0, 0, 1));
            const char* nl = static_cast<const char*>(std::memchr(p, '\n', end - p));
            if (!nl) break;
            p = nl + 1;
        }
        for (long i = 0; i < faceCount && p < end; ++i) {
            int count = 0;
            int a = 0, b = 0, c = 0;
            if (std::sscanf(p, "%d %d %d %d", &count, &a, &b, &c) >= 4 && count >= 3) {
                mesh->addTriangle(a, b, c);
            }
            const char* nl = static_cast<const char*>(std::memchr(p, '\n', end - p));
            if (!nl) break;
            p = nl + 1;
        }
    } else {
        const std::size_t stride = doublePrecision ? 24 : 12;
        const char* p = data + bodyStart;
        if (bodyStart + stride * vertexCount > length) {
            if (error) *error = "PLY binario con propiedades no soportadas";
            return false;
        }
        for (long i = 0; i < vertexCount; ++i) {
            if (doublePrecision) {
                double xyz[3] = {};
                std::memcpy(xyz, p, 24);
                mesh->addVertex(Vec3(xyz[0], xyz[1], xyz[2]), Vec3(0, 0, 1));
            } else {
                mesh->addVertex(Vec3(readFloat(p), readFloat(p + 4), readFloat(p + 8)),
                                Vec3(0, 0, 1));
            }
            p += stride;
        }
        const char* end = data + length;
        for (long i = 0; i < faceCount && p < end; ++i) {
            const std::uint8_t count = static_cast<std::uint8_t>(*p++);
            if (count < 3 || p + 4 * count > end) break;
            std::vector<std::uint32_t> face(count);
            std::memcpy(face.data(), p, 4 * count);
            p += 4 * count;
            for (std::uint8_t k = 2; k < count; ++k) {
                mesh->addTriangle(face[0], face[k - 1], face[k]);
            }
        }
    }

    if (mesh->indices.empty()) {
        if (error) *error = "el PLY no contiene caras";
        return false;
    }
    addSharpEdges(mesh, 35.0);
    computeFlatNormals(mesh);
    return true;
}

}  // namespace stp
