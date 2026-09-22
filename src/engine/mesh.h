// Triangle mesh plus the model edges used for the CAD-style line overlay.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "geom.h"

namespace stp {

struct Mesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<std::uint32_t> indices;
    std::vector<Vec3> edgeLines;  // consecutive pairs form one segment each
    BBox bounds;

    int faces = 0;          // faces successfully tessellated
    int facesFailed = 0;    // faces that produced no triangles
    int solids = 0;

    bool empty() const { return indices.empty() && edgeLines.empty(); }
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
};

}  // namespace stp
