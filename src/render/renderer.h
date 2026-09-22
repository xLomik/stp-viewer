// Software renderer: orbit camera, z-buffered triangles, depth-tested edges.
// No GPU API, so it runs the same inside the Explorer thumbnail host and in the
// standalone viewer.
#pragma once

#include <cstdint>
#include <vector>

#include "../engine/mesh.h"

namespace stp {

struct Camera {
    Vec3 target;
    double distance = 10.0;
    double yaw = -0.7853981634;   // radians around world Z
    double pitch = 0.5235987756;  // radians above the XY plane
    double fov = 0.6108652382;    // 35 degrees
    double orthoHeight = 10.0;
    bool ortho = true;

    Vec3 eye() const;
    Vec3 forward() const;
    Vec3 right() const;
    Vec3 up() const;

    // Frames the box for the given viewport aspect (width / height).
    void fit(const BBox& box, double aspect, double margin = 1.08);
};

struct RenderStyle {
    std::uint32_t backgroundTop = 0xFF2E3A46;
    std::uint32_t backgroundBottom = 0xFF10151A;
    bool transparentBackground = false;
    std::uint32_t faceColor = 0xFFB9C4CC;
    std::uint32_t edgeColor = 0xFF20282F;
    bool drawEdges = true;
    bool drawFaces = true;
    double edgeWidth = 1.1;  // output pixels
    int supersample = 2;     // 1..4
};

struct Framebuffer {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;  // BGRA, alpha premultiplied

    void resize(int w, int h) {
        width = w;
        height = h;
        pixels.assign(static_cast<std::size_t>(w) * h, 0);
    }
};

// Renders into an image of exactly width x height pixels.
void renderMesh(const Mesh& mesh, const Camera& camera, const RenderStyle& style, int width,
                int height, Framebuffer* out);

// Projects a world point to pixel coordinates of a width x height viewport.
// Returns false when the point is behind the camera.
bool projectPoint(const Camera& camera, int width, int height, const Vec3& world, double* sx,
                  double* sy);

}  // namespace stp
