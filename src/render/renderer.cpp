#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

namespace stp {
namespace {

// Vertice ya proyectado a pixeles, con su color resuelto. Sombrear por vertice
// en vez de por pixel quita el grueso del trabajo del bucle interno, que es lo
// que se nota en equipos lentos.
struct ScreenVertex {
    float x = 0, y = 0, z = 0;
    float r = 0, g = 0, b = 0;
    bool visible = false;
};

struct CameraVertex {
    Vec3 view;
    Vec3 normal;
};

inline std::uint8_t channel(float v) {
    return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

inline std::uint32_t packBgra(float b, float g, float r, float a) {
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

        const double halfH = cam.orthoHeight * 0.5;
        m_orthoScaleX = halfH * m_aspect > 0 ? m_raster.w * 0.5 / (halfH * m_aspect) : 1.0;
        m_orthoScaleY = halfH > 0 ? m_raster.h * 0.5 / halfH : 1.0;
        m_perspScaleX = m_raster.w * 0.5 / (m_tanHalf * m_aspect);
        m_perspScaleY = m_raster.h * 0.5 / m_tanHalf;
        m_centerX = m_raster.w * 0.5;
        m_centerY = m_raster.h * 0.5;

        m_faceR = ((style.faceColor >> 16) & 0xFF) / 255.0f;
        m_faceG = ((style.faceColor >> 8) & 0xFF) / 255.0f;
        m_faceB = (style.faceColor & 0xFF) / 255.0f;
    }

    CameraVertex toCamera(const Vec3& p, const Vec3& n) const {
        const Vec3 d = p - m_eye;
        CameraVertex v;
        v.view = Vec3(dot(d, m_right), dot(d, m_up), dot(d, m_fwd));
        v.normal = Vec3(dot(n, m_right), dot(n, m_up), dot(n, m_fwd));
        return v;
    }

    ScreenVertex project(const CameraVertex& v, bool shaded) const {
        ScreenVertex s;
        if (m_cam.ortho) {
            s.x = static_cast<float>(m_centerX + v.view.x * m_orthoScaleX);
            s.y = static_cast<float>(m_centerY - v.view.y * m_orthoScaleY);
            s.z = static_cast<float>(v.view.z);
            s.visible = true;
        } else {
            if (v.view.z <= m_near) return s;
            const double inv = 1.0 / v.view.z;
            s.x = static_cast<float>(m_centerX + v.view.x * inv * m_perspScaleX);
            s.y = static_cast<float>(m_centerY - v.view.y * inv * m_perspScaleY);
            s.z = static_cast<float>(v.view.z);
            s.visible = true;
        }
        if (shaded) shade(v.normal, &s.r, &s.g, &s.b);
        return s;
    }

    void shade(const Vec3& nCam, float* r, float* g, float* b) const {
        const Vec3 n = normalize(nCam);
        // Las luces viven en espacio de camara: la pieza siempre queda iluminada
        // desde el punto de vista del observador.
        static const Vec3 key = normalize(Vec3(-0.35, 0.45, -1.0));
        static const Vec3 fill = normalize(Vec3(0.6, -0.25, -0.6));
        const float kd = static_cast<float>(std::fabs(dot(n, key)));
        const float kf = static_cast<float>(std::fabs(dot(n, fill)));
        const float facing = static_cast<float>(std::fabs(n.z));
        const float rim = (1.0f - std::min(1.0f, facing));
        const float rim3 = rim * rim * rim;

        const float base = 0.20f + 0.68f * kd + 0.22f * kf;
        const float kd2 = kd * kd;
        const float kd8 = kd2 * kd2 * kd2 * kd2;
        const float spec = kd8 * kd8 * kd8 * kd8 * kd8 * 0.45f;  // kd^40 aprox.

        *r = (m_faceR * base + spec + rim3 * 0.10f) * 255.0f;
        *g = (m_faceG * base + spec + rim3 * 0.11f) * 255.0f;
        *b = (m_faceB * base + spec + rim3 * 0.13f) * 255.0f;
    }

    void drawBackground() {
        if (m_style.transparentBackground) return;
        for (int y = 0; y < m_raster.h; ++y) {
            const float t = m_raster.h > 1 ? static_cast<float>(y) / (m_raster.h - 1) : 0.0f;
            const std::uint32_t a = m_style.backgroundTop;
            const std::uint32_t b = m_style.backgroundBottom;
            const float r = ((a >> 16) & 0xFF) * (1 - t) + ((b >> 16) & 0xFF) * t;
            const float g = ((a >> 8) & 0xFF) * (1 - t) + ((b >> 8) & 0xFF) * t;
            const float bl = (a & 0xFF) * (1 - t) + (b & 0xFF) * t;
            const std::uint32_t c = packBgra(bl, g, r, 255);
            std::fill(m_raster.color.begin() + static_cast<std::size_t>(y) * m_raster.w,
                      m_raster.color.begin() + static_cast<std::size_t>(y + 1) * m_raster.w, c);
        }
    }

    // Rasteriza dentro de la banda [bandY0, bandY1). Cada hilo escribe solo en
    // su banda, asi que no hay que sincronizar nada.
    void triangle(const ScreenVertex& a, const ScreenVertex& b, const ScreenVertex& c, int bandY0,
                  int bandY1) {
        const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        if (!(std::fabs(area) > 1e-7f)) return;

        int minX = static_cast<int>(std::floor(std::min({a.x, b.x, c.x})));
        int maxX = static_cast<int>(std::ceil(std::max({a.x, b.x, c.x})));
        int minY = static_cast<int>(std::floor(std::min({a.y, b.y, c.y})));
        int maxY = static_cast<int>(std::ceil(std::max({a.y, b.y, c.y})));
        minX = std::max(0, minX);
        minY = std::max(bandY0, minY);
        maxX = std::min(m_raster.w - 1, maxX);
        maxY = std::min(bandY1 - 1, maxY);
        if (minX > maxX || minY > maxY) return;

        const float invArea = 1.0f / area;
        // Funciones de borde incrementales: sin divisiones dentro del bucle.
        const float w0dx = (a.y - b.y) * invArea;
        const float w0dy = (b.x - a.x) * invArea;
        const float w1dx = (c.y - a.y) * invArea;
        const float w1dy = (a.x - c.x) * invArea;

        const float px0 = minX + 0.5f;
        const float py0 = minY + 0.5f;
        float w0Row = ((b.x - a.x) * (py0 - a.y) - (px0 - a.x) * (b.y - a.y)) * invArea;
        float w1Row = ((px0 - a.x) * (c.y - a.y) - (c.x - a.x) * (py0 - a.y)) * invArea;

        for (int y = minY; y <= maxY; ++y) {
            float w0 = w0Row;
            float w1 = w1Row;
            std::size_t idx = static_cast<std::size_t>(y) * m_raster.w + minX;
            for (int x = minX; x <= maxX; ++x, ++idx, w0 += w0dx, w1 += w1dx) {
                const float w2 = 1.0f - w0 - w1;
                if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f) continue;
                const float la = w2, lb = w1, lc = w0;

                const float z = a.z * la + b.z * lb + c.z * lc;
                if (z >= m_raster.depth[idx]) continue;

                m_raster.depth[idx] = z;
                m_raster.color[idx] = packBgra(a.b * la + b.b * lb + c.b * lc,
                                               a.g * la + b.g * lb + c.g * lc,
                                               a.r * la + b.r * lb + c.r * lc, 255.0f);
            }
            w0Row += w0dy;
            w1Row += w1dy;
        }
    }

    void line(const ScreenVertex& a, const ScreenVertex& b, float cr, float cg, float cb,
              int bandY0, int bandY1) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const int steps = static_cast<int>(std::ceil(std::max(std::fabs(dx), std::fabs(dy))));
        if (steps <= 0 || steps > 8 * (m_raster.w + m_raster.h)) return;

        const int half = std::max(0, (m_lineWidth - 1) / 2);
        const float invSteps = 1.0f / steps;
        for (int i = 0; i <= steps; ++i) {
            const float t = i * invSteps;
            const int cx = static_cast<int>(a.x + dx * t);
            const int cy = static_cast<int>(a.y + dy * t);
            const float z = a.z + (b.z - a.z) * t;
            for (int oy = -half; oy <= half; ++oy) {
                const int yi = cy + oy;
                if (yi < bandY0 || yi >= bandY1) continue;
                for (int ox = -half; ox <= half; ++ox) {
                    const int xi = cx + ox;
                    if (xi < 0 || xi >= m_raster.w) continue;
                    const std::size_t idx = static_cast<std::size_t>(yi) * m_raster.w + xi;
                    // Las aristas se acercan un pelo a la camara para ganar el
                    // test de profundidad contra la cara que las contiene.
                    if (z > m_raster.depth[idx] * 1.0008f + m_bias) continue;
                    m_raster.color[idx] = packBgra(cb, cg, cr, 255.0f);
                    m_raster.depth[idx] = std::min(m_raster.depth[idx], z);
                }
            }
        }
    }

    // Recorta contra el plano cercano y dibuja; solo hace falta en perspectiva.
    void triangleClipped(const CameraVertex& v0, const CameraVertex& v1, const CameraVertex& v2,
                         int bandY0, int bandY1) {
        CameraVertex poly[4];
        int count = 0;
        const CameraVertex src[3] = {v0, v1, v2};
        for (int i = 0; i < 3 && count < 4; ++i) {
            const CameraVertex& cur = src[i];
            const CameraVertex& nxt = src[(i + 1) % 3];
            const bool curIn = cur.view.z > m_near;
            const bool nxtIn = nxt.view.z > m_near;
            if (curIn) poly[count++] = cur;
            if (curIn != nxtIn && count < 4) {
                const double t = (m_near - cur.view.z) / (nxt.view.z - cur.view.z);
                CameraVertex mid;
                mid.view = cur.view + (nxt.view - cur.view) * t;
                mid.normal = cur.normal + (nxt.normal - cur.normal) * t;
                poly[count++] = mid;
            }
        }
        if (count < 3) return;
        const ScreenVertex first = project(poly[0], true);
        for (int i = 2; i < count; ++i) {
            triangle(first, project(poly[i - 1], true), project(poly[i], true), bandY0, bandY1);
        }
    }

    void run(const Mesh& mesh, int threadCount) {
        drawBackground();
        m_bias = static_cast<float>(mesh.bounds.diagonal() * 1e-4);

        const std::size_t vertexCount = mesh.positions.size();
        std::vector<ScreenVertex> screen;
        std::vector<CameraVertex> camera;
        const bool needClip = !m_cam.ortho;

        if (m_style.drawFaces && vertexCount > 0) {
            screen.resize(vertexCount);
            if (needClip) camera.resize(vertexCount);
            // Cada vertice se transforma y se sombrea una sola vez, aunque lo
            // compartan muchos triangulos.
            parallelFor(vertexCount, threadCount, [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i) {
                    const CameraVertex cv = toCamera(mesh.positions[i], mesh.normals[i]);
                    if (needClip) camera[i] = cv;
                    screen[i] = project(cv, true);
                }
            });
        }

        std::vector<ScreenVertex> edgeScreen;
        std::vector<CameraVertex> edgeCamera;
        if (m_style.drawEdges && !mesh.edgeLines.empty()) {
            edgeScreen.resize(mesh.edgeLines.size());
            if (needClip) edgeCamera.resize(mesh.edgeLines.size());
            parallelFor(mesh.edgeLines.size(), threadCount,
                        [&](std::size_t begin, std::size_t end) {
                            for (std::size_t i = begin; i < end; ++i) {
                                const CameraVertex cv = toCamera(mesh.edgeLines[i], Vec3(0, 0, 1));
                                if (needClip) edgeCamera[i] = cv;
                                edgeScreen[i] = project(cv, false);
                            }
                        });
        }

        const float edgeR = static_cast<float>((m_style.edgeColor >> 16) & 0xFF);
        const float edgeG = static_cast<float>((m_style.edgeColor >> 8) & 0xFF);
        const float edgeB = static_cast<float>(m_style.edgeColor & 0xFF);

        // El reparto es por bandas de pixeles: cada hilo es dueno de las suyas.
        const int bands = std::max(1, threadCount);
        auto renderBand = [&](int band) {
            const int y0 = static_cast<int>(static_cast<long long>(m_raster.h) * band / bands);
            const int y1 = static_cast<int>(static_cast<long long>(m_raster.h) * (band + 1) / bands);
            if (y0 >= y1) return;

            if (m_style.drawFaces) {
                for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    const std::uint32_t i0 = mesh.indices[i];
                    const std::uint32_t i1 = mesh.indices[i + 1];
                    const std::uint32_t i2 = mesh.indices[i + 2];
                    const ScreenVertex& a = screen[i0];
                    const ScreenVertex& b = screen[i1];
                    const ScreenVertex& c = screen[i2];
                    if (a.visible && b.visible && c.visible) {
                        const float minY = std::min({a.y, b.y, c.y});
                        const float maxY = std::max({a.y, b.y, c.y});
                        if (maxY < y0 || minY > y1) continue;
                        const float minX = std::min({a.x, b.x, c.x});
                        const float maxX = std::max({a.x, b.x, c.x});
                        if (maxX < 0 || minX > m_raster.w) continue;
                        triangle(a, b, c, y0, y1);
                    } else if (needClip) {
                        triangleClipped(camera[i0], camera[i1], camera[i2], y0, y1);
                    }
                }
            }
            if (m_style.drawEdges) {
                for (std::size_t i = 0; i + 1 < edgeScreen.size(); i += 2) {
                    const ScreenVertex& a = edgeScreen[i];
                    const ScreenVertex& b = edgeScreen[i + 1];
                    if (a.visible && b.visible) {
                        if (std::max(a.y, b.y) < y0 - m_lineWidth ||
                            std::min(a.y, b.y) > y1 + m_lineWidth) {
                            continue;
                        }
                        line(a, b, edgeR, edgeG, edgeB, y0, y1);
                    } else if (needClip) {
                        CameraVertex ca = edgeCamera[i];
                        CameraVertex cb = edgeCamera[i + 1];
                        if (!clipSegment(&ca, &cb)) continue;
                        line(project(ca, false), project(cb, false), edgeR, edgeG, edgeB, y0, y1);
                    }
                }
            }
        };

        if (bands == 1) {
            renderBand(0);
        } else {
            std::vector<std::thread> workers;
            workers.reserve(bands - 1);
            for (int band = 1; band < bands; ++band) {
                workers.emplace_back([&renderBand, band]() { renderBand(band); });
            }
            renderBand(0);
            for (std::thread& worker : workers) worker.join();
        }
    }

    bool clipSegment(CameraVertex* a, CameraVertex* b) const {
        if (m_cam.ortho) return true;
        const double za = a->view.z;
        const double zb = b->view.z;
        if (za <= m_near && zb <= m_near) return false;
        if (za < m_near) {
            const double t = (m_near - za) / (zb - za);
            a->view = a->view + (b->view - a->view) * t;
        } else if (zb < m_near) {
            const double t = (m_near - zb) / (za - zb);
            b->view = b->view + (a->view - b->view) * t;
        }
        return true;
    }

    const Raster& raster() const { return m_raster; }

private:
    template <typename Fn>
    static void parallelFor(std::size_t count, int threadCount, Fn body) {
        const int workers = std::max(1, threadCount);
        if (workers == 1 || count < 4096) {
            body(0, count);
            return;
        }
        std::vector<std::thread> pool;
        pool.reserve(workers - 1);
        for (int i = 1; i < workers; ++i) {
            const std::size_t begin = count * i / workers;
            const std::size_t end = count * (i + 1) / workers;
            pool.emplace_back([&body, begin, end]() { body(begin, end); });
        }
        body(0, count / workers);
        for (std::thread& worker : pool) worker.join();
    }

    const Camera& m_cam;
    const RenderStyle& m_style;
    Raster m_raster;
    double m_aspect = 1.0;
    double m_tanHalf = 0.3;
    double m_near = 1e-4;
    double m_orthoScaleX = 1.0, m_orthoScaleY = 1.0;
    double m_perspScaleX = 1.0, m_perspScaleY = 1.0;
    double m_centerX = 0.0, m_centerY = 0.0;
    float m_bias = 0.0f;
    float m_faceR = 0.7f, m_faceG = 0.75f, m_faceB = 0.8f;
    int m_lineWidth = 1;
    Vec3 m_eye, m_right, m_up, m_fwd;
};

int resolveThreads(int requested) {
    if (requested > 0) return std::min(requested, 16);
    const unsigned hardware = std::thread::hardware_concurrency();
    if (hardware == 0) return 2;
    return static_cast<int>(std::min(hardware, 8u));
}

}  // namespace

Vec3 Camera::forward() const {
    const Vec3 toTarget = target - eye();
    return normalize(toTarget);
}

Vec3 Camera::eye() const {
    if (planView) return target + planNormal * distance;
    const double cp = std::cos(pitch);
    const Vec3 dir(cp * std::cos(yaw), cp * std::sin(yaw), std::sin(pitch));
    return target + dir * distance;
}

Vec3 Camera::right() const {
    if (planView) return planRight;
    const Vec3 f = forward();
    Vec3 worldUp(0, 0, 1);
    if (std::fabs(dot(f, worldUp)) > 0.999) worldUp = Vec3(0, 1, 0);
    return normalize(cross(f, worldUp));
}

Vec3 Camera::up() const {
    if (planView) return planUp;
    return normalize(cross(right(), forward()));
}

void Camera::fitPlanar(const PlanarInfo& info, double aspect, double margin) {
    planView = true;
    ortho = true;
    planNormal = info.normal;
    planRight = info.u;
    planUp = info.v;
    target = info.center;
    if (aspect <= 0) aspect = 1.0;
    const double extent = std::max(1e-6, std::max(info.height, info.width / aspect));
    orthoHeight = extent * margin;
    distance = std::max(info.width, info.height) + 1.0;
}

void Camera::zoomAt(double factor, double sx, double sy, int width, int height) {
    if (width <= 0 || height <= 0 || factor <= 0) return;
    const double aspect = static_cast<double>(width) / height;
    // Desplazamiento del cursor respecto al centro, en unidades del modelo.
    const double ox = (sx / width - 0.5) * orthoHeight * aspect;
    const double oy = (0.5 - sy / height) * orthoHeight;
    target = target + right() * (ox * (1.0 - factor)) + up() * (oy * (1.0 - factor));
    orthoHeight = std::max(1e-9, orthoHeight * factor);
    distance = std::max(1e-6, distance * factor);
}

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
    const int threads = resolveThreads(style.threads);

    Renderer renderer(camera, style, rw, rh, ss);
    renderer.run(mesh, threads);

    out->resize(width, height);
    const Raster& src = renderer.raster();
    const bool transparent = style.transparentBackground;

    if (ss == 1) {
        if (!transparent) {
            std::copy(src.color.begin(), src.color.end(), out->pixels.begin());
            return;
        }
        for (std::size_t i = 0; i < out->pixels.size(); ++i) {
            out->pixels[i] = src.color[i];
        }
        return;
    }

    const float inv = 1.0f / (static_cast<float>(ss) * ss);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float r = 0, g = 0, b = 0, a = 0;
            for (int sy = 0; sy < ss; ++sy) {
                const std::size_t row = static_cast<std::size_t>(y * ss + sy) * rw + x * ss;
                for (int sx = 0; sx < ss; ++sx) {
                    const std::uint32_t c = src.color[row + sx];
                    const float ca = ((c >> 24) & 0xFF) * (1.0f / 255.0f);
                    r += ((c >> 16) & 0xFF) * ca;
                    g += ((c >> 8) & 0xFF) * ca;
                    b += (c & 0xFF) * ca;
                    a += ca;
                }
            }
            const float alpha = transparent ? a * inv : 1.0f;
            // BGRA con alfa premultiplicado, que es lo que espera WTSAT_ARGB.
            out->pixels[static_cast<std::size_t>(y) * width + x] =
                packBgra(b * inv, g * inv, r * inv, alpha * 255.0f);
        }
    }
}

}  // namespace stp
