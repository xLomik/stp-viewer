#include "icons.h"

#include <cmath>

#include "../markup_tools.h"
#include "theme.h"

namespace stp {
namespace ui {

Icon iconForSnap(SnapKind kind) {
    switch (kind) {
        case SnapKind::Endpoint: return Icon::SnapEndpoint;
        case SnapKind::Midpoint: return Icon::SnapMidpoint;
        case SnapKind::Center: return Icon::SnapCenter;
        case SnapKind::Quadrant: return Icon::SnapQuadrant;
        case SnapKind::Intersection: return Icon::SnapIntersection;
        case SnapKind::Extension: return Icon::SnapExtension;
        case SnapKind::Perpendicular: return Icon::SnapPerpendicular;
        case SnapKind::Tangent: return Icon::SnapTangent;
        case SnapKind::OnEdge: return Icon::SnapNearest;
        default: return Icon::None;
    }
}

SnapKind snapKindForIcon(Icon icon) {
    switch (icon) {
        case Icon::SnapEndpoint: return SnapKind::Endpoint;
        case Icon::SnapMidpoint: return SnapKind::Midpoint;
        case Icon::SnapCenter: return SnapKind::Center;
        case Icon::SnapQuadrant: return SnapKind::Quadrant;
        case Icon::SnapIntersection: return SnapKind::Intersection;
        case Icon::SnapExtension: return SnapKind::Extension;
        case Icon::SnapPerpendicular: return SnapKind::Perpendicular;
        case Icon::SnapTangent: return SnapKind::Tangent;
        case Icon::SnapNearest: return SnapKind::OnEdge;
        default: return SnapKind::None;
    }
}

namespace {

using Gdiplus::PointF;
using Gdiplus::REAL;

// Lienzo de 16 x 16 unidades escalado a la caja del icono.
struct Canvas {
    Gdiplus::Graphics& g;
    Gdiplus::ARGB color;
    float x0, y0, k;

    PointF p(float x, float y) const { return PointF(x0 + x * k, y0 + y * k); }
    void style(Gdiplus::Pen* out) const {
        out->SetStartCap(Gdiplus::LineCapRound);
        out->SetEndCap(Gdiplus::LineCapRound);
        out->SetLineJoin(Gdiplus::LineJoinRound);
    }
    void line(float ax, float ay, float bx, float by, float width = 1.5f) const {
        Gdiplus::Pen q(Gdiplus::Color(color), (width) * k);
        style(&q);
        g.DrawLine(&q, p(ax, ay), p(bx, by));
    }
    void dashed(float ax, float ay, float bx, float by) const {
        Gdiplus::Pen q(Gdiplus::Color(color), (1.2f) * k);
        style(&q);
        q.SetDashStyle(Gdiplus::DashStyleDash);
        g.DrawLine(&q, p(ax, ay), p(bx, by));
    }
    void poly(std::initializer_list<float> xy, bool closed, float width = 1.5f) const {
        std::vector<PointF> pts;
        for (auto it = xy.begin(); it != xy.end(); it += 2) pts.push_back(p(*it, *(it + 1)));
        Gdiplus::Pen q(Gdiplus::Color(color), (width) * k);
        style(&q);
        if (closed) g.DrawPolygon(&q, pts.data(), static_cast<INT>(pts.size()));
        else g.DrawLines(&q, pts.data(), static_cast<INT>(pts.size()));
    }
    void fillPoly(std::initializer_list<float> xy, BYTE alpha) const {
        std::vector<PointF> pts;
        for (auto it = xy.begin(); it != xy.end(); it += 2) pts.push_back(p(*it, *(it + 1)));
        Gdiplus::SolidBrush b(ui::color(color, alpha));
        g.FillPolygon(&b, pts.data(), static_cast<INT>(pts.size()));
    }
    void rect(float x, float y, float w, float h, float width = 1.5f) const {
        Gdiplus::Pen q(Gdiplus::Color(color), (width) * k);
        style(&q);
        g.DrawRectangle(&q, x0 + x * k, y0 + y * k, w * k, h * k);
    }
    void fillRect(float x, float y, float w, float h, BYTE alpha = 255) const {
        Gdiplus::SolidBrush b(ui::color(color, alpha));
        g.FillRectangle(&b, x0 + x * k, y0 + y * k, w * k, h * k);
    }
    void circle(float cx, float cy, float r, float width = 1.5f) const {
        Gdiplus::Pen q(Gdiplus::Color(color), (width) * k);
        style(&q);
        g.DrawEllipse(&q, x0 + (cx - r) * k, y0 + (cy - r) * k, 2 * r * k, 2 * r * k);
    }
    void fillCircle(float cx, float cy, float r, BYTE alpha = 255) const {
        Gdiplus::SolidBrush b(ui::color(color, alpha));
        g.FillEllipse(&b, x0 + (cx - r) * k, y0 + (cy - r) * k, 2 * r * k, 2 * r * k);
    }
    void ellipse(float cx, float cy, float rx, float ry, float width = 1.5f) const {
        Gdiplus::Pen q(Gdiplus::Color(color), (width) * k);
        style(&q);
        g.DrawEllipse(&q, x0 + (cx - rx) * k, y0 + (cy - ry) * k, 2 * rx * k, 2 * ry * k);
    }
    void arc(float cx, float cy, float r, float start, float sweep, float width = 1.5f) const {
        Gdiplus::Pen q(Gdiplus::Color(color), (width) * k);
        style(&q);
        g.DrawArc(&q, x0 + (cx - r) * k, y0 + (cy - r) * k, 2 * r * k, 2 * r * k, start, sweep);
    }
    void text(const wchar_t* s, float x, float y, float w, float h, float size) const {
        const std::unique_ptr<Gdiplus::Font> f = fontPixels(size * k, Gdiplus::FontStyleBold);
        drawText(g, s, *f, Gdiplus::RectF(x0 + x * k, y0 + y * k, w * k, h * k), color, 1);
    }
};

void sheet(const Canvas& c) { c.poly({3, 1.5f, 10, 1.5f, 13, 4.5f, 13, 14.5f, 3, 14.5f}, true); c.poly({10, 1.5f, 10, 4.5f, 13, 4.5f}, false); }
void cube(const Canvas& c) {
    c.poly({8, 1.5f, 14, 4.5f, 14, 11.5f, 8, 14.5f, 2, 11.5f, 2, 4.5f}, true);
    c.poly({2, 4.5f, 8, 7.5f, 14, 4.5f}, false);
    c.line(8, 7.5f, 8, 14.5f);
}
void folder(const Canvas& c) { c.poly({1.5f, 3.5f, 6, 3.5f, 7.5f, 5, 14.5f, 5, 14.5f, 13.5f, 1.5f, 13.5f}, true); }

}  // namespace

void drawIcon(Gdiplus::Graphics& g, Icon icon, const Gdiplus::RectF& box, Gdiplus::ARGB color) {
    const float side = std::min(box.Width, box.Height);
    const Canvas c{g, color, box.X + (box.Width - side) / 2, box.Y + (box.Height - side) / 2, side / 16.0f};
    const SnapKind snap = snapKindForIcon(icon);
    if (snap != SnapKind::None) {
        drawSnapGlyph(&g, snap, c.x0 + 8 * c.k, c.y0 + 8 * c.k, 5.5f * c.k, color, 1.5f * c.k);
        return;
    }
    switch (icon) {
        case Icon::Open:
            folder(c);
            c.line(8, 11.5f, 8, 7);
            c.poly({6, 9, 8, 7, 10, 9}, false);
            break;
        case Icon::Folder: folder(c); break;
        case Icon::Recent:
            c.circle(8, 8, 6);
            c.poly({8, 4.5f, 8, 8, 10.5f, 9.5f}, false);
            break;
        case Icon::Save:
            c.poly({2, 2, 12, 2, 14, 4, 14, 14, 2, 14}, true);
            c.rect(5, 2, 6, 4);
            c.rect(4.5f, 9, 7, 5);
            break;
        case Icon::ExportPdf: sheet(c); c.text(L"PDF", 1, 7, 14, 6, 4.6f); break;
        case Icon::ExportPng: sheet(c); c.text(L"PNG", 1, 7, 14, 6, 4.6f); break;
        case Icon::File: sheet(c); break;
        case Icon::About:
            c.circle(8, 8, 6.5f);
            c.line(8, 7.5f, 8, 11.5f);
            c.fillCircle(8, 5, 0.9f);
            break;
        case Icon::Navigate:
            c.fillPoly({4, 2, 4, 13, 7, 10, 9.5f, 14.5f, 11.5f, 13.5f, 9, 9, 13, 9}, 60);
            c.poly({4, 2, 4, 13, 7, 10, 9.5f, 14.5f, 11.5f, 13.5f, 9, 9, 13, 9}, true, 1.3f);
            break;
        case Icon::Fit:
            c.poly({2, 5.5f, 2, 2, 5.5f, 2}, false);
            c.poly({10.5f, 2, 14, 2, 14, 5.5f}, false);
            c.poly({14, 10.5f, 14, 14, 10.5f, 14}, false);
            c.poly({5.5f, 14, 2, 14, 2, 10.5f}, false);
            c.rect(5.5f, 5.5f, 5, 5, 1.2f);
            break;
        case Icon::Views: cube(c); break;
        case Icon::View3d:
            c.fillPoly({8, 1.5f, 14, 4.5f, 8, 7.5f, 2, 4.5f}, 110);
            cube(c);
            break;
        case Icon::Plan2d:
            c.rect(2, 3, 12, 10);
            c.line(4, 10.5f, 12, 10.5f, 1.1f);
            c.line(4, 9.5f, 4, 11.5f, 1.1f);
            c.line(12, 9.5f, 12, 11.5f, 1.1f);
            c.circle(8, 6.5f, 2, 1.2f);
            break;
        case Icon::Distance:
            c.line(2, 4, 2, 12);
            c.line(14, 4, 14, 12);
            c.line(3, 8, 13, 8);
            c.poly({5, 6, 3, 8, 5, 10}, false);
            c.poly({11, 6, 13, 8, 11, 10}, false);
            break;
        case Icon::Radius:
            c.circle(8, 8, 6.5f);
            c.fillCircle(8, 8, 1.1f);
            c.line(8, 8, 12.6f, 3.4f);
            break;
        case Icon::Angle:
            c.poly({14, 13, 2, 13, 11, 3}, false);
            c.arc(2, 13, 7, -48, 48);
            break;
        case Icon::Area:
            c.fillPoly({2, 12, 5, 3, 13, 2, 14, 10, 8, 14}, 90);
            c.poly({2, 12, 5, 3, 13, 2, 14, 10, 8, 14}, true);
            break;
        case Icon::Snap:
            c.rect(3, 3, 10, 10);
            c.fillCircle(8, 8, 1.6f);
            break;
        case Icon::Ortho:
            c.poly({3, 2.5f, 3, 13, 13.5f, 13}, false);
            c.poly({1.5f, 4.5f, 3, 2.5f, 4.5f, 4.5f}, false);
            c.poly({11.5f, 11.5f, 13.5f, 13, 11.5f, 14.5f}, false);
            break;
        case Icon::Polar:
            c.line(2.5f, 13.5f, 14, 13.5f);
            c.line(2.5f, 13.5f, 12, 4);
            c.arc(2.5f, 13.5f, 7, -45, 45, 1.2f);
            c.fillCircle(2.5f, 13.5f, 1.1f);
            break;
        case Icon::Highlight:
            c.fillRect(1.5f, 11.5f, 13, 3, 110);
            c.poly({5, 11, 12, 4, 14, 6, 7, 13}, true);
            c.line(10, 6, 12, 8, 1.2f);
            break;
        case Icon::Underline:
            c.poly({4.5f, 2, 4.5f, 8, 6, 10.5f, 10, 10.5f, 11.5f, 8, 11.5f, 2}, false);
            c.line(3, 14, 13, 14);
            break;
        case Icon::Note:
            c.poly({2, 2.5f, 14, 2.5f, 14, 11, 7, 11, 4, 14, 4, 11, 2, 11}, true);
            c.line(5, 5.5f, 11, 5.5f, 1.1f);
            c.line(5, 8, 9.5f, 8, 1.1f);
            break;
        case Icon::Rectangle: c.rect(2, 3.5f, 12, 9); break;
        case Icon::Ellipse: c.ellipse(8, 8, 6.5f, 4.5f); break;
        case Icon::Cloud:
            c.arc(5, 9, 3, 90, 180);
            c.arc(7.5f, 5.5f, 3, 180, 150);
            c.arc(11, 7, 3, 250, 170);
            c.arc(9.5f, 11, 2.5f, 0, 180);
            c.arc(6, 11.5f, 2, 40, 110);
            break;
        case Icon::Pen:
            c.poly({3, 13, 3.5f, 10, 11, 2.5f, 13.5f, 5, 6, 12.5f}, true);
            c.line(9.5f, 4, 12, 6.5f, 1.2f);
            break;
        case Icon::Color: c.fillCircle(8, 8, 5.5f); break;
        case Icon::Undo:
            c.arc(8.5f, 9, 5, 200, 250);
            c.poly({2, 4.5f, 3.8f, 7.5f, 7, 6.2f}, false);
            break;
        case Icon::Redo:
            c.arc(7.5f, 9, 5, 90, 250);
            c.poly({14, 4.5f, 12.2f, 7.5f, 9, 6.2f}, false);
            break;
        case Icon::Delete:
            c.line(2.5f, 4, 13.5f, 4);
            c.poly({6, 4, 6, 2, 10, 2, 10, 4}, false);
            c.poly({3.5f, 4, 4.5f, 14, 11.5f, 14, 12.5f, 4}, false);
            c.line(6.5f, 6.5f, 6.8f, 11.5f, 1.1f);
            c.line(9.5f, 6.5f, 9.2f, 11.5f, 1.1f);
            break;
        case Icon::Shaded:
            c.fillCircle(8, 8, 6, 90);
            c.fillCircle(6.5f, 6.5f, 2.5f, 120);
            c.circle(8, 8, 6);
            break;
        case Icon::Wireframe:
            c.circle(8, 8, 6);
            c.ellipse(8, 8, 2.8f, 6, 1.1f);
            c.ellipse(8, 8, 6, 2.4f, 1.1f);
            break;
        case Icon::Edges:
            cube(c);
            c.line(2, 4.5f, 8, 1.5f, 2.3f);
            c.line(8, 7.5f, 14, 4.5f, 2.3f);
            break;
        case Icon::Perspective:
            c.poly({4.5f, 3, 11.5f, 3, 14.5f, 13, 1.5f, 13}, true);
            c.line(8, 3, 8, 13, 1.1f);
            c.line(3, 8, 13, 8, 1.1f);
            break;
        case Icon::Panel:
            c.rect(1.5f, 2.5f, 13, 11);
            c.fillRect(10, 2.5f, 4.5f, 11, 110);
            break;
        case Icon::StatusBar:
            c.rect(1.5f, 2.5f, 13, 11);
            c.fillRect(1.5f, 10.5f, 13, 3, 110);
            break;
        case Icon::ViewCube:
            c.fillPoly({8, 1.5f, 14, 4.5f, 8, 7.5f, 2, 4.5f}, 130);
            cube(c);
            break;
        case Icon::Home:
            c.poly({2, 8, 8, 2.5f, 14, 8}, false);
            c.poly({4, 6.5f, 4, 13.5f, 12, 13.5f, 12, 6.5f}, false);
            c.rect(6.8f, 9.5f, 2.4f, 4, 1.2f);
            break;
        case Icon::Minimize: c.line(3, 8, 13, 8, 1.1f); break;
        case Icon::Maximize: c.rect(3, 3, 10, 10, 1.1f); break;
        case Icon::Restore:
            c.rect(3, 5, 8, 8, 1.1f);
            c.poly({5, 5, 5, 3, 13, 3, 13, 11, 11, 11}, false, 1.1f);
            break;
        case Icon::Close:
            c.line(3, 3, 13, 13, 1.1f);
            c.line(3, 13, 13, 3, 1.1f);
            break;
        case Icon::ChevronDown: c.poly({4, 6, 8, 10, 12, 6}, false); break;
        case Icon::ChevronRight: c.poly({6, 4, 10, 8, 6, 12}, false); break;
        case Icon::Check: c.poly({3, 8.5f, 6.5f, 12, 13, 4}, false, 2.0f); break;
        case Icon::Marks:
            c.fillCircle(3, 4, 1.2f);
            c.fillCircle(3, 8, 1.2f);
            c.fillCircle(3, 12, 1.2f);
            c.line(6, 4, 14, 4);
            c.line(6, 8, 14, 8);
            c.line(6, 12, 12, 12);
            break;
        case Icon::Properties:
            c.line(2, 4, 14, 4);
            c.line(2, 8, 14, 8);
            c.line(2, 12, 14, 12);
            c.fillCircle(10, 4, 1.8f);
            c.fillCircle(5, 8, 1.8f);
            c.fillCircle(11, 12, 1.8f);
            break;
        default:
            break;
    }
}

}  // namespace ui
}  // namespace stp
