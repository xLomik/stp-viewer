#include "view_cube.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "icons.h"
#include "theme.h"

namespace stp {
namespace ui {
namespace {

constexpr double kBand = 0.6;  // igual que cubeRegionAt: franja de aristas y esquinas

struct Geometry {
    float cx, cy, h;  // centro y media arista en pixeles
    Vec3 right, up, forward;
    Gdiplus::PointF project(const Vec3& v) const {
        return Gdiplus::PointF(cx + h * static_cast<float>(dot(v, right)), cy - h * static_cast<float>(dot(v, up)));
    }
    Gdiplus::PointF direction(const Vec3& v) const {
        return Gdiplus::PointF(h * static_cast<float>(dot(v, right)), -h * static_cast<float>(dot(v, up)));
    }
};

Geometry geometry(const Camera& camera, const RECT& b) {
    Geometry geo;
    const float size = static_cast<float>(b.right - b.left);
    geo.cx = b.left + size / 2;
    geo.cy = b.top + size * 0.46f;
    geo.h = size * 0.25f;
    geo.right = camera.right();
    geo.up = camera.up();
    geo.forward = camera.forward();
    return geo;
}

Vec3 axisVector(int axis, double sign) {
    return Vec3(axis == 0 ? sign : 0, axis == 1 ? sign : 0, axis == 2 ? sign : 0);
}

// Ejes de lectura del texto en cada cara: x hacia la derecha, y hacia abajo.
void faceTextAxes(int axis, int sign, Vec3* x, Vec3* y) {
    if (axis == 2) {
        *x = Vec3(1, 0, 0);
        *y = Vec3(0, sign > 0 ? -1 : 1, 0);
        return;
    }
    *y = Vec3(0, 0, -1);
    if (axis == 0) *x = Vec3(0, sign, 0);
    else *x = Vec3(-sign, 0, 0);
}

double component(const CubeRegion& r, int axis) { return axis == 0 ? r.x : (axis == 1 ? r.y : r.z); }

// Tramo [a, b] del eje de una cara que ocupa la parte c (-1, 0 o 1) de la region.
void span(int c, double* a, double* b) {
    if (c == 0) {
        *a = -kBand;
        *b = kBand;
    } else if (c > 0) {
        *a = kBand;
        *b = 1;
    } else {
        *a = -1;
        *b = -kBand;
    }
}

}  // namespace

RECT ViewCube::bounds(int width, int height, int dpi, bool compact) const {
    const int size = MulDiv(compact ? 84 : 120, dpi, 96);
    const int margin = MulDiv(compact ? 6 : 10, dpi, 96);
    (void)height;
    return RECT{width - margin - size, margin, width - margin, margin + size};
}

void ViewCube::draw(Gdiplus::Graphics& g, const Camera& camera, bool plan, const RECT& b, int dpi, bool light) const {
    const Geometry geo = geometry(camera, b);
    const Gdiplus::ARGB ink = light ? 0xFF3A4652 : kText;
    const Gdiplus::ARGB dim = light ? 0xFF6B7885 : kTextDim;

    // Boton Inicio arriba a la izquierda.
    const float home = static_cast<float>(MulDiv(18, dpi, 96));
    const Gdiplus::RectF homeBox(static_cast<float>(b.left), static_cast<float>(b.top), home, home);
    if (m_hover == Hit::Home) fillRound(g, Gdiplus::RectF(homeBox.X - 2, homeBox.Y - 2, home + 4, home + 4), 3, light ? 0x30000000 : kHover);
    drawIcon(g, Icon::Home, homeBox, m_hover == Hit::Home ? kAccent : dim);

    if (plan) {
        // Brujula: circulo y flecha hacia el +Y del mundo proyectado en el dibujo.
        const float r = geo.h * 1.25f;
        Gdiplus::Pen ring(color(dim, 160), 1.2f);
        g.DrawEllipse(&ring, geo.cx - r, geo.cy - r, 2 * r, 2 * r);
        const Gdiplus::PointF d = geo.direction(Vec3(0, 1, 0));
        const float len = std::hypot(d.X, d.Y);
        if (len > 1e-3f) {
            const float ux = d.X / len, uy = d.Y / len;
            const Gdiplus::PointF tip(geo.cx + ux * r * 0.8f, geo.cy + uy * r * 0.8f);
            const Gdiplus::PointF left(geo.cx - uy * r * 0.22f, geo.cy + ux * r * 0.22f);
            const Gdiplus::PointF rightP(geo.cx + uy * r * 0.22f, geo.cy - ux * r * 0.22f);
            const Gdiplus::PointF north[3] = {tip, left, rightP};
            Gdiplus::SolidBrush accent(color(kAccent));
            g.FillPolygon(&accent, north, 3);
            const Gdiplus::PointF tail(geo.cx - ux * r * 0.6f, geo.cy - uy * r * 0.6f);
            const Gdiplus::PointF south[3] = {tail, left, rightP};
            Gdiplus::SolidBrush grey(color(dim, 200));
            g.FillPolygon(&grey, south, 3);
            const auto f = font(8, dpi, Gdiplus::FontStyleBold);
            const float lx = geo.cx + ux * (r + static_cast<float>(MulDiv(10, dpi, 96)));
            const float ly = geo.cy + uy * (r + static_cast<float>(MulDiv(10, dpi, 96)));
            drawText(g, L"N", *f, Gdiplus::RectF(lx - 10, ly - 10, 20, 20), ink, 1);
        }
        return;
    }

    // Anillo de la brujula bajo el cubo.
    {
        std::vector<Gdiplus::PointF> ringPoints;
        for (int k = 0; k <= 48; ++k) {
            const double a = 2 * kPi * k / 48;
            ringPoints.push_back(geo.project(Vec3(1.55 * std::cos(a), 1.55 * std::sin(a), -1.0)));
        }
        Gdiplus::Pen ring(color(dim, 120), 1.2f);
        g.DrawLines(&ring, ringPoints.data(), static_cast<INT>(ringPoints.size()));
        const Gdiplus::PointF n = geo.project(Vec3(0, 1.85, -1.0));
        const auto f = font(7, dpi, Gdiplus::FontStyleBold);
        drawText(g, L"N", *f, Gdiplus::RectF(n.X - 10, n.Y - 8, 20, 16), dim, 1);
    }

    const auto label = fontPixels(geo.h * 0.36f, Gdiplus::FontStyleBold);
    for (int axis = 0; axis < 3; ++axis) {
        for (int sign = -1; sign <= 1; sign += 2) {
            const Vec3 normal = axisVector(axis, sign);
            const double facing = -dot(normal, geo.forward);
            if (facing <= 1e-3) continue;
            const int b1 = (axis + 1) % 3, b2 = (axis + 2) % 3;
            auto corner = [&](double s1, double s2) {
                Vec3 v = normal;
                v = v + axisVector(b1, s1) + axisVector(b2, s2);
                return geo.project(v);
            };
            const Gdiplus::PointF quad[4] = {corner(-1, -1), corner(1, -1), corner(1, 1), corner(-1, 1)};
            // Sombreado suave segun cuanto mira a la camara.
            const Gdiplus::ARGB base = light ? 0xFFE9EDF1 : blend(0xFF2A3039, 0xFF3A424D, facing);
            Gdiplus::SolidBrush fill{Gdiplus::Color(base)};
            g.FillPolygon(&fill, quad, 4);

            // Region bajo el cursor sobre esta cara.
            if (m_hover == Hit::Region && static_cast<int>(component(m_region, axis)) == sign) {
                double a1, e1, a2, e2;
                span(static_cast<int>(component(m_region, b1)), &a1, &e1);
                span(static_cast<int>(component(m_region, b2)), &a2, &e2);
                const Gdiplus::PointF cell[4] = {corner(a1, a2), corner(e1, a2), corner(e1, e2), corner(a1, e2)};
                Gdiplus::SolidBrush hot(color(kAccent, 190));
                g.FillPolygon(&hot, cell, 4);
            }
            Gdiplus::Pen edge(color(light ? 0xFF9AA6B2 : 0xFF4A535F), 1.0f);
            g.DrawPolygon(&edge, quad, 4);

            // Nombre de la cara, deformado con la cara (si no esta muy de canto).
            Vec3 tx, ty;
            faceTextAxes(axis, sign, &tx, &ty);
            const Gdiplus::PointF ux = geo.direction(tx), uy = geo.direction(ty);
            const float area = std::fabs(ux.X * uy.Y - ux.Y * uy.X) / (geo.h * geo.h);
            const char* name = cubeFaceName(normal.x > 0.5 ? 1 : normal.x < -0.5 ? -1 : 0,
                                            normal.y > 0.5 ? 1 : normal.y < -0.5 ? -1 : 0,
                                            normal.z > 0.5 ? 1 : normal.z < -0.5 ? -1 : 0);
            if (area < 0.3f || !name) continue;
            const Gdiplus::PointF c = geo.project(normal);
            Gdiplus::Matrix old;
            g.GetTransform(&old);
            Gdiplus::Matrix m(ux.X / geo.h, ux.Y / geo.h, uy.X / geo.h, uy.Y / geo.h, c.X, c.Y);
            g.SetTransform(&m);
            const std::wstring text = widen(name);
            const float box = geo.h * 0.95f;
            drawText(g, text, *label, Gdiplus::RectF(-box, -geo.h * 0.3f, 2 * box, geo.h * 0.6f), ink, 1);
            g.SetTransform(&old);
        }
    }
}

ViewCube::Hit ViewCube::hitTest(const Camera& camera, bool plan, const RECT& b, POINT p, CubeRegion* region) const {
    if (p.x < b.left || p.x >= b.right || p.y < b.top || p.y >= b.bottom) return Hit::None;
    const int home = (b.right - b.left) * 18 / 120 + 4;
    if (p.x < b.left + home && p.y < b.top + home) return Hit::Home;
    if (plan) return Hit::None;
    const Geometry geo = geometry(camera, b);
    const CubeRegion r = cubeRegionAt(camera, p.x - geo.cx, p.y - geo.cy, geo.h);
    if (!r.valid()) return Hit::None;
    if (region) *region = r;
    return Hit::Region;
}

bool ViewCube::setHover(Hit hit, const CubeRegion& region) {
    if (hit == m_hover && (hit != Hit::Region || region == m_region)) return false;
    m_hover = hit;
    m_region = region;
    return true;
}

}  // namespace ui
}  // namespace stp
