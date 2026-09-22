#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace stp {
namespace {

struct ViewVertex {
    Vec3 view;    // camera space: x right, y up, z forward (positive in front)
    Vec3 normal;  // camera space
};

struct Projected {
    double x = 0, y = 0, z = 0;
    Vec3 normal;
};

inline std::uint8_t channel(double v) {
    return static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, v)));
}

inline std::uint32_t packBgra(double b, double g, double r, double a) {
    return (static_cast<std::uint32_t>(channel(a)) << 24) |
           (static_cast<std::uint32_t>(channel(r)) << 16) |
           (static_cast<std::uint32_t>(channel(g)) << 8) | static_cast<std::uint32_t>(channel(b));
}

struct Raster {
    int w = 0, h = 0;
    std::vector<std::uint32_t> color;
    std::vector<float> depth;

    void init(int width, int height, std::uint32_t clear) {
        w = width;
        h = height;
        color.assign(static_cast<std::size_t>(w) * h, clear);
        depth.assign(static_cast<std::size_t>(w) * h, std::numeric_limits<float>::max());
    }
};

class Renderer {
public:
    Renderer(const Camera& cam, const RenderStyle& style, int w, int h, int supersample)
        : m_cam(cam), m_style(style) {
        m_lineWidth = std::max(1, static_cast<int>(std::lround(style.edgeWidth * supersample)));
        m_raster.init(w, h, 0);
        m_aspect = h > 0 ? static_cast<double>(w) / h : 1.0;
        m_eye = cam.eye();
        m_right = cam.right();
        m_up = cam.up();
        m_fwd = cam.forward();
        m_tanHalf = std::tan(cam.fov * 0.5);
        m_near = std::max(1e-7, cam.distance * 1e-4);
    }

    ViewVertex toView(const Vec3& p, const Vec3& n) const {
        const Vec3 d = p - m_eye;
        ViewVertex v;
        v.view = Vec3(dot(d, m_right), dot(d, m_up), dot(d, m_fwd));
        v.normal = Vec3(dot(n, m_right), dot(n, m_up), dot(n, m_fwd));
        return v;
    }

    bool project(const ViewVertex& v, Projected* out) const {
        if (m_cam.ortho) {
            const double halfH = m_cam.orthoHeight * 0.5;
            const double halfW = halfH * m_aspect;
            out->x = (v.view.x / halfW * 0.5 + 0.5) * m_raster.w;
            out->y = (0.5 - v.view.y / halfH * 0.5) * m_raster.h;
            out->z = v.view.z;
        } else {
            if (v.view.z <= m_near) return false;
            const double ndcX = (v.view.x / v.view.z) / (m_tanHalf * m_aspect);
            const double ndcY = (v.view.y / v.view.z) / m_tanHalf;
            out->x = (ndcX * 0.5 + 0.5) * m_raster.w;
            out->y = (0.5 - ndcY * 0.5) * m_raster.h;
            out->z = v.view.z;
        }
        out->normal = v.normal;
        return true;
    }

    void drawBackground() {
        if (m_style.transparentBackground) return;
        for (int y = 0; y < m_raster.h; ++y) {
            const double t = m_raster.h > 1 ? static_cast<double>(y) / (m_raster.h - 1) : 0.0;
            const std::uint32_t a = m_style.backgroundTop;
            const std::uint32_t b = m_style.backgroundBottom;
            const double r = ((a >> 16) & 0xFF) * (1 - t) + ((b >> 16) & 0xFF) * t;
            const double g = ((a >> 8) & 0xFF) * (1 - t) + ((b >> 8) & 0xFF) * t;
            const double bl = (a & 0xFF) * (1 - t) + (b & 0xFF) * t;
            const std::uint32_t c = packBgra(bl, g, r, 255);
            std::fill(m_raster.color.begin() + static_cast<std::size_t>(y) * m_raster.w,
                      m_raster.color.begin() + static_cast<std::size_t>(y + 1) * m_raster.w, c);
        }
    }

    void shade(const Vec3& nCam, double* r, double* g, double* b) const {
        const Vec3 n = normalize(nCam);
        // Lights live in camera space so the model is always lit from the viewer.
        static const Vec3 key = normalize(Vec3(-0.35, 0.45, -1.0));
        static const Vec3 fill = normalize(Vec3(0.6, -0.25, -0.6));
        const double kd = std::fabs(dot(n, key));
        const double kf = std::fabs(dot(n, fill));
        const double rim = std::pow(1.0 - std::min(1.0, std::fabs(n.z)), 3.0);

        const double base = 0.20 + 0.68 * kd + 0.22 * kf;
        const double spec = std::pow(std::max(0.0, kd), 42.0) * 0.45;

        const double cr = ((m_style.faceColor >> 16) & 0xFF) / 255.0;
        const double cg = ((m_style.faceColor >> 8) & 0xFF) / 255.0;
        const double cb = (m_style.faceColor & 0xFF) / 255.0;
        *r = (cr * base + spec + rim * 0.10) * 255.0;
        *g = (cg * base + spec + rim * 0.11) * 255.0;
        *b = (cb * base + spec + rim * 0.13) * 255.0;
    }

    void triangle(const Projected& a, const Projected& b, const Projected& c) {
        const double area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        if (std::fabs(area) < 1e-12) return;

        int minX = static_cast<int>(std::floor(std::min({a.x, b.x, c.x})));
        int maxX = static_cast<int>(std::ceil(std::max({a.x, b.x, c.x})));
        int minY = static_cast<int>(std::floor(std::min({a.y, b.y, c.y})));
        int maxY = static_cast<int>(std::ceil(std::max({a.y, b.y, c.y})));
        minX = std::max(0, minX);
        minY = std::max(0, minY);
        maxX = std::min(m_raster.w - 1, maxX);
        maxY = std::min(m_raster.h - 1, maxY);
        if (minX > maxX || minY > maxY) return;

        const double invArea = 1.0 / area;
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const double px = x + 0.5;
                const double py = y + 0.5;
                double w0 = ((b.x - a.x) * (py - a.y) - (px - a.x) * (b.y - a.y)) * invArea;
                double w1 = ((px - a.x) * (c.y - a.y) - (c.x - a.x) * (py - a.y)) * invArea;
                const double w2 = 1.0 - w0 - w1;
                if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
                const double la = w2, lb = w1, lc = w0;

                const double z = a.z * la + b.z * lb + c.z * lc;
                const std::size_t idx = static_cast<std::size_t>(y) * m_raster.w + x;
                if (z >= m_raster.depth[idx]) continue;

                const Vec3 n = a.normal * la + b.normal * lb + c.normal * lc;
                double r, g, bl;
                shade(n, &r, &g, &bl);
                m_raster.depth[idx] = static_cast<float>(z);
                m_raster.color[idx] = packBgra(bl, g, r, 255);
            }
        }
    }

    void line(const Projected& a, const Projected& b, std::uint32_t rgb) {
        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const int steps = static_cast<int>(std::ceil(std::max(std::fabs(dx), std::fabs(dy))));
        if (steps <= 0) return;
        if (steps > 8 * (m_raster.w + m_raster.h)) return;

        const double cr = (rgb >> 16) & 0xFF;
        const double cg = (rgb >> 8) & 0xFF;
        const double cb = rgb & 0xFF;

        const int half = std::max(0, (m_lineWidth - 1) / 2);
        for (int i = 0; i <= steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            const double x = a.x + dx * t;
            const double y = a.y + dy * t;
            const double z = a.z + (b.z - a.z) * t;
            const int cx = static_cast<int>(x);
            const int cy = static_cast<int>(y);
            for (int oy = -half; oy <= half; ++oy) {
                for (int ox = -half; ox <= half; ++ox) {
                    const int xi = cx + ox;
                    const int yi = cy + oy;
                    if (xi < 0 || yi < 0 || xi >= m_raster.w || yi >= m_raster.h) continue;
                    const std::size_t idx = static_cast<std::size_t>(yi) * m_raster.w + xi;
                    // Pull edges slightly towards the camera so they win the depth test.
                    if (z > m_raster.depth[idx] * 1.0008 + m_bias) continue;
                    m_raster.color[idx] = packBgra(cb, cg, cr, 255);
                    m_raster.depth[idx] =
                        static_cast<float>(std::min<double>(m_raster.depth[idx], z));
                }
            }
        }
    }

    // Clips a segment to the near plane in camera space (perspective only).
    bool clipSegment(ViewVertex* a, ViewVertex* b) const {
        if (m_cam.ortho) return true;
        double za = a->view.z;
        double zb = b->view.z;
        if (za <= m_near && zb <= m_near) return false;
        if (za < m_near) {
            const double t = (m_near - za) / (zb - za);
            a->view = a->view + (b->view - a->view) * t;
            a->normal = a->normal + (b->normal - a->normal) * t;
        } else if (zb < m_near) {
            const double t = (m_near - zb) / (za - zb);
            b->view = b->view + (a->view - b->view) * t;
            b->normal = b->normal + (a->normal - b->normal) * t;
        }
        return true;
    }

    void drawTriangleClipped(ViewVertex v0, ViewVertex v1, ViewVertex v2) {
        if (!m_cam.ortho) {
            ViewVertex in[3];
            int inCount = 0;
            ViewVertex out[3];
            int outCount = 0;
            const ViewVertex src[3] = {v0, v1, v2};
            for (int i = 0; i < 3; ++i) {
                if (src[i].view.z > m_near) in[inCount++] = src[i];
                else out[outCount++] = src[i];
            }
            if (inCount == 0) return;
            if (inCount < 3) {
                // Rebuild the visible polygon by clipping each crossing edge.
                std::vector<ViewVertex> poly;
                for (int i = 0; i < 3; ++i) {
                    const ViewVertex& cur = src[i];
                    const ViewVertex& nxt = src[(i + 1) % 3];
                    const bool curIn = cur.view.z > m_near;
                    const bool nxtIn = nxt.view.z > m_near;
                    if (curIn) poly.push_back(cur);
                    if (curIn != nxtIn) {
                        const double t = (m_near - cur.view.z) / (nxt.view.z - cur.view.z);
                        ViewVertex mid;
                        mid.view = cur.view + (nxt.view - cur.view) * t;
                        mid.normal = cur.normal + (nxt.normal - cur.normal) * t;
                        poly.push_back(mid);
                    }
                }
                for (std::size_t i = 2; i < poly.size(); ++i) {
                    Projected pa, pb, pc;
                    if (project(poly[0], &pa) && project(poly[i - 1], &pb) &&
                        project(poly[i], &pc)) {
                        triangle(pa, pb, pc);
                    }
                }
                return;
            }
            (void)out;
            (void)outCount;
        }
        Projected pa, pb, pc;
        if (project(v0, &pa) && project(v1, &pb) && project(v2, &pc)) triangle(pa, pb, pc);
    }

    void run(const Mesh& mesh) {
        drawBackground();
        m_bias = mesh.bounds.diagonal() * 1e-4;

        if (m_style.drawFaces) {
            for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const std::uint32_t i0 = mesh.indices[i];
                const std::uint32_t i1 = mesh.indices[i + 1];
                const std::uint32_t i2 = mesh.indices[i + 2];
                drawTriangleClipped(toView(mesh.positions[i0], mesh.normals[i0]),
                                    toView(mesh.positions[i1], mesh.normals[i1]),
                                    toView(mesh.positions[i2], mesh.normals[i2]));
            }
        }
        if (m_style.drawEdges) {
            const std::uint32_t rgb = m_style.edgeColor & 0x00FFFFFF;
            for (std::size_t i = 0; i + 1 < mesh.edgeLines.size(); i += 2) {
                ViewVertex a = toView(mesh.edgeLines[i], Vec3(0, 0, 1));
                ViewVertex b = toView(mesh.edgeLines[i + 1], Vec3(0, 0, 1));
                if (!clipSegment(&a, &b)) continue;
                Projected pa, pb;
                if (project(a, &pa) && project(b, &pb)) line(pa, pb, rgb);
            }
        }
    }

    const Raster& raster() const { return m_raster; }

private:
    const Camera& m_cam;
    const RenderStyle& m_style;
    Raster m_raster;
    double m_aspect = 1.0;
    double m_tanHalf = 0.3;
    double m_near = 1e-4;
    double m_bias = 0.0;
    int m_lineWidth = 1;
    Vec3 m_eye, m_right, m_up, m_fwd;
};

}  // namespace

Vec3 Camera::forward() const {
    const Vec3 toTarget = target - eye();
    return normalize(toTarget);
}

Vec3 Camera::eye() const {
    const double cp = std::cos(pitch);
    const Vec3 dir(cp * std::cos(yaw), cp * std::sin(yaw), std::sin(pitch));
    return target + dir * distance;
}

Vec3 Camera::right() const {
    const Vec3 f = forward();
    Vec3 worldUp(0, 0, 1);
    if (std::fabs(dot(f, worldUp)) > 0.999) worldUp = Vec3(0, 1, 0);
    return normalize(cross(f, worldUp));
}

Vec3 Camera::up() const { return normalize(cross(right(), forward())); }

void Camera::fit(const BBox& box, double aspect, double margin) {
    if (!box.valid()) return;
    target = box.center();
    const double radius = std::max(1e-6, box.diagonal() * 0.5);

    const Vec3 r = right();
    const Vec3 u = up();
    const Vec3 f = forward();
    const Vec3 c = box.center();

    double maxX = 0, maxY = 0, maxZ = 0;
    for (int i = 0; i < 8; ++i) {
        const Vec3 corner((i & 1) ? box.hi.x : box.lo.x, (i & 2) ? box.hi.y : box.lo.y,
                          (i & 4) ? box.hi.z : box.lo.z);
        const Vec3 d = corner - c;
        maxX = std::max(maxX, std::fabs(dot(d, r)));
        maxY = std::max(maxY, std::fabs(dot(d, u)));
        maxZ = std::max(maxZ, std::fabs(dot(d, f)));
    }
    if (aspect <= 0) aspect = 1.0;

    const double halfH = std::max(maxY, maxX / aspect) * margin;
    orthoHeight = std::max(1e-6, halfH * 2.0);

    const double tanHalf = std::tan(fov * 0.5);
    distance = halfH / std::max(1e-6, tanHalf) + maxZ + radius * 0.1;
    if (!(distance > 0)) distance = radius * 3.0;
}

bool projectPoint(const Camera& camera, int width, int height, const Vec3& world, double* sx,
                  double* sy) {
    const Vec3 eye = camera.eye();
    const Vec3 r = camera.right();
    const Vec3 u = camera.up();
    const Vec3 f = camera.forward();
    const Vec3 d = world - eye;
    const Vec3 view(dot(d, r), dot(d, u), dot(d, f));
    const double aspect = height > 0 ? static_cast<double>(width) / height : 1.0;

    if (camera.ortho) {
        const double halfH = camera.orthoHeight * 0.5;
        const double halfW = halfH * aspect;
        *sx = (view.x / halfW * 0.5 + 0.5) * width;
        *sy = (0.5 - view.y / halfH * 0.5) * height;
        return true;
    }
    const double near = std::max(1e-7, camera.distance * 1e-4);
    if (view.z <= near) return false;
    const double tanHalf = std::tan(camera.fov * 0.5);
    *sx = ((view.x / view.z) / (tanHalf * aspect) * 0.5 + 0.5) * width;
    *sy = (0.5 - (view.y / view.z) / tanHalf * 0.5) * height;
    return true;
}

void renderMesh(const Mesh& mesh, const Camera& camera, const RenderStyle& style, int width,
                int height, Framebuffer* out) {
    const int ss = std::max(1, std::min(4, style.supersample));
    const int rw = width * ss;
    const int rh = height * ss;

    Renderer renderer(camera, style, rw, rh, ss);
    renderer.run(mesh);

    out->resize(width, height);
    const Raster& src = renderer.raster();
    const bool transparent = style.transparentBackground;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double r = 0, g = 0, b = 0, a = 0;
            for (int sy = 0; sy < ss; ++sy) {
                for (int sx = 0; sx < ss; ++sx) {
                    const std::size_t idx =
                        static_cast<std::size_t>(y * ss + sy) * rw + (x * ss + sx);
                    const std::uint32_t c = src.color[idx];
                    const double ca = ((c >> 24) & 0xFF) / 255.0;
                    r += ((c >> 16) & 0xFF) * ca;
                    g += ((c >> 8) & 0xFF) * ca;
                    b += (c & 0xFF) * ca;
                    a += ca;
                }
            }
            const double n = static_cast<double>(ss) * ss;
            const double alpha = transparent ? a / n : 1.0;
            // Premultiplied BGRA, which is what Windows expects for WTSAT_ARGB.
            out->pixels[static_cast<std::size_t>(y) * width + x] =
                packBgra(b / n, g / n, r / n, alpha * 255.0);
        }
    }
}

}  // namespace stp
