#include "markup_tools.h"

#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "image_view.h"

namespace stp {
namespace {

constexpr UINT_PTR kMessageTimer = 7;
constexpr ULONGLONG kMessageMs = 4000;

std::wstring widen(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(0, size)), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], size);
    return out;
}

Gdiplus::Color gdiColor(std::uint32_t argb, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, static_cast<BYTE>((argb >> 16) & 0xFF), static_cast<BYTE>((argb >> 8) & 0xFF),
                          static_cast<BYTE>(argb & 0xFF));
}

bool toScreen(const Camera& camera, int width, int height, const Vec3& p, Gdiplus::PointF* out) {
    double x, y;
    if (!projectPoint(camera, width, height, p, &x, &y)) return false;
    *out = Gdiplus::PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y));
    return true;
}

// GDI+ no sustituye una fuente que falta (GDI si): sin Segoe UI se usa la
// sans-serif generica del sistema.
std::unique_ptr<Gdiplus::Font> uiFont(double size, INT style) {
    auto font = std::make_unique<Gdiplus::Font>(L"Segoe UI", static_cast<Gdiplus::REAL>(size), style, Gdiplus::UnitPixel);
    if (font->GetLastStatus() == Gdiplus::Ok && font->IsAvailable()) return font;
    return std::make_unique<Gdiplus::Font>(Gdiplus::FontFamily::GenericSansSerif(), static_cast<Gdiplus::REAL>(size),
                                           style, Gdiplus::UnitPixel);
}

}  // namespace

MarkupTools::MarkupTools(MarkupHost* host, bool editing) : m_host(host), m_editing(editing) {
    ensureGdiplus();
}

bool MarkupTools::isMeasureTool() const {
    return m_tool == Tool::Distance || m_tool == Tool::Radius || m_tool == Tool::Angle || m_tool == Tool::Area;
}

void MarkupTools::setTool(Tool tool) {
    // En el panel solo se mide.
    if (!m_editing && tool != Tool::Navigate && tool != Tool::Distance && tool != Tool::Radius &&
        tool != Tool::Angle && tool != Tool::Area) {
        return;
    }
    m_tool = tool;
    m_pending.clear();
    m_hover = SnapResult();
    m_host->markupRedraw();
    m_host->markupChanged();
}

void MarkupTools::setDocument(const MarkupDocument& doc) {
    m_doc = doc;
    m_values.clear();
    m_pending.clear();
    m_dirty = false;
    m_host->markupRedraw();
    m_host->markupChanged();
}

void MarkupTools::clear() { setDocument(MarkupDocument()); }

void MarkupTools::showMessage(const std::wstring& text) {
    m_message = text;
    m_messageUntil = GetTickCount64() + kMessageMs;
    SetTimer(m_host->markupWindow(), kMessageTimer, static_cast<UINT>(kMessageMs + 50), nullptr);
    m_host->markupRedraw();
}

std::wstring MarkupTools::hint() const {
    if (!m_message.empty() && GetTickCount64() < m_messageUntil) return m_message;
    const std::wstring tail = L"   (Shift: sin enganche, Esc: terminar)";
    switch (m_tool) {
        case Tool::Distance:
            return (m_pending.empty() ? L"Distancia: clic en el primer punto" : L"Distancia: clic en el segundo punto") + tail;
        case Tool::Radius: return L"Radio: clic sobre un circulo, un arco o un agujero" + tail;
        case Tool::Angle:
            return (m_pending.empty() ? L"Angulo: clic en tres puntos (vertice al medio) o en dos lineas"
                                      : L"Angulo: siguiente punto o segunda linea") + tail;
        case Tool::Area: return L"Area: clic dentro de un contorno cerrado o sobre una cara" + tail;
        default: return std::wstring();
    }
}

SnapResult MarkupTools::snapAt(int x, int y) const {
    SnapOptions options;
    options.snapping = GetKeyState(VK_SHIFT) >= 0;
    options.plane = m_host->markupPlane();
    options.radiusPixels = 8.0 * m_host->markupDpi() / 96.0;
    return m_host->markupPick().snap(m_host->markupMesh(), m_host->markupCamera(), m_host->markupWidth(),
                                     m_host->markupHeight(), x, y, options);
}

void MarkupTools::addMark(Mark mark) {
    mark.id = m_doc.newId();
    m_doc.marks.push_back(mark);
    m_dirty = true;
    m_host->markupRedraw();
    m_host->markupChanged();
}

void MarkupTools::clickMeasure(const SnapResult& snap) {
    if (snap.kind == SnapKind::None) {
        showMessage(L"Aqui no hay nada que medir");
        return;
    }
    Mark mark;
    mark.color = kMeasureColor;
    switch (m_tool) {
        case Tool::Distance:
            m_pending.push_back(snap);
            if (m_pending.size() < 2) break;
            mark.kind = MarkKind::Distance;
            mark.points = {m_pending[0].point, m_pending[1].point};
            m_pending.clear();
            addMark(mark);
            break;
        case Tool::Radius:
        case Tool::Area: {
            mark.kind = m_tool == Tool::Radius ? MarkKind::Radius : MarkKind::Area;
            mark.points = {snap.point};
            const Value value = evaluate(mark);
            if (!value.ok) {
                showMessage(value.error);
                break;
            }
            addMark(mark);
            break;
        }
        case Tool::Angle: {
            const Mesh& mesh = m_host->markupMesh();
            auto straight = [&](const SnapResult& s) {
                return s.kind == SnapKind::OnEdge && s.segment >= 0 &&
                       (static_cast<std::size_t>(s.segment) >= mesh.edgeCurve.size() || !mesh.edgeCurve[s.segment]);
            };
            if (m_pending.size() == 1 && straight(m_pending[0]) && straight(snap) &&
                snap.segment != m_pending[0].segment) {
                // Dos lineas: A (a0, a1, clic) y B (b0, b1, clic).
                const std::size_t a = static_cast<std::size_t>(m_pending[0].segment) * 2;
                const std::size_t b = static_cast<std::size_t>(snap.segment) * 2;
                mark.kind = MarkKind::Angle;
                mark.points = {mesh.edgeLines[a], mesh.edgeLines[a + 1], m_pending[0].point,
                               mesh.edgeLines[b], mesh.edgeLines[b + 1], snap.point};
                m_pending.clear();
                addMark(mark);
                break;
            }
            m_pending.push_back(snap);
            if (m_pending.size() < 3) break;
            mark.kind = MarkKind::Angle;
            mark.points = {m_pending[0].point, m_pending[1].point, m_pending[2].point};
            m_pending.clear();
            addMark(mark);
            break;
        }
        default:
            break;
    }
    m_host->markupRedraw();
}

bool MarkupTools::handle(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_TIMER:
            if (wparam != kMessageTimer) return false;
            KillTimer(m_host->markupWindow(), kMessageTimer);
            m_message.clear();
            m_host->markupRedraw();
            return true;

        case WM_SETCURSOR:
            if (m_tool == Tool::Navigate || LOWORD(lparam) != HTCLIENT) return false;
            // Sin nada que medir bajo el cursor: "no disponible".
            SetCursor(LoadCursor(nullptr, isMeasureTool() && m_mouse.x >= 0 && m_hover.kind == SnapKind::None
                                              ? IDC_NO : IDC_CROSS));
            return true;

        case WM_MOUSEMOVE:
            m_mouse = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (isMeasureTool()) {
                m_hover = snapAt(m_mouse.x, m_mouse.y);
                m_host->markupRedraw();
            }
            return false;  // la vista sigue moviendo con el boton derecho

        case WM_LBUTTONDOWN:
            if (!isMeasureTool()) return false;
            SetFocus(m_host->markupWindow());
            clickMeasure(snapAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)));
            return true;

        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            return m_tool != Tool::Navigate;

        case WM_KEYDOWN: {
            if (wparam == 'M') {
                setTool(Tool::Distance);
                return true;
            }
            if (isMeasureTool() && wparam >= '1' && wparam <= '4') {
                const Tool tools[] = {Tool::Distance, Tool::Radius, Tool::Angle, Tool::Area};
                setTool(tools[wparam - '1']);
                return true;
            }
            if (wparam == VK_ESCAPE) {
                if (!m_pending.empty()) {
                    m_pending.clear();
                    m_host->markupRedraw();
                    return true;
                }
                if (m_tool != Tool::Navigate) {
                    setTool(Tool::Navigate);
                    return true;
                }
                return false;
            }
            if (wparam == VK_DELETE && !m_doc.marks.empty()) {
                // Hasta que exista la seleccion (Task 8): borra la ultima marca.
                m_values.erase(m_doc.marks.back().id);
                m_doc.marks.pop_back();
                m_dirty = true;
                m_host->markupRedraw();
                m_host->markupChanged();
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

const MarkupTools::Value& MarkupTools::valueOf(const Mark& mark) const {
    auto it = m_values.find(mark.id);
    if (it == m_values.end()) it = m_values.emplace(mark.id, evaluate(mark)).first;
    return it->second;
}

MarkupTools::Value MarkupTools::evaluate(const Mark& mark) const {
    Value v;
    const Mesh& mesh = m_host->markupMesh();
    const PlanarInfo* plane = m_host->markupPlane();
    const LengthUnit unit = mesh.units;
    const double tolerance = std::max(1e-9, mesh.bounds.diagonal() * 1e-6);
    switch (mark.kind) {
        case MarkKind::Distance: {
            if (mark.points.size() < 2) break;
            const DistanceResult d = measureDistance(mark.points[0], mark.points[1], plane);
            v.ok = true;
            v.label = widen(formatLength(d.total, unit));
            v.detail = L"ΔX " + widen(formatNumber(d.delta.x, 3)) + L"   ΔY " + widen(formatNumber(d.delta.y, 3));
            if (!plane) v.detail += L"   ΔZ " + widen(formatNumber(d.delta.z, 3));
            break;
        }
        case MarkKind::Radius: {
            if (mark.points.empty()) break;
            const int triangle = m_host->markupPick().triangleAt(mesh, mark.points[0], tolerance);
            // Tolerancia de 8 px a la escala actual: lo que el usuario considero "sobre el circulo".
            const double reach = 8.0 * worldPerPixel(m_host->markupCamera(), m_host->markupHeight(), 1.0);
            const RadiusResult r = measureRadius(mesh, mark.points[0], triangle, std::max(reach, tolerance));
            if (!r.ok) {
                v.error = widen(r.error);
                break;
            }
            v.ok = true;
            v.center = r.center;
            v.label = L"R " + widen(formatLength(r.radius, unit));
            v.detail = L"Ø " + widen(formatLength(2 * r.radius, unit));
            break;
        }
        case MarkKind::Angle: {
            double degrees = 0.0;
            if (mark.points.size() == 3) {
                degrees = angleAt(mark.points[0], mark.points[1], mark.points[2]);
            } else if (mark.points.size() == 6) {
                degrees = angleBetweenLines(mark.points[0], mark.points[1], mark.points[2], mark.points[3],
                                            mark.points[4], mark.points[5]);
            } else {
                break;
            }
            v.ok = true;
            v.label = widen(formatAngle(degrees));
            break;
        }
        case MarkKind::Area: {
            if (mark.points.empty()) break;
            const int triangle = m_host->markupPick().triangleAt(mesh, mark.points[0], tolerance);
            v.area = measureArea(mesh, mark.points[0], triangle, plane);
            if (!v.area.ok) {
                v.error = widen(v.area.error);
                break;
            }
            v.ok = true;
            v.label = L"A " + widen(formatArea(v.area.area, unit));
            v.detail = L"P " + widen(formatLength(v.area.perimeter, unit));
            break;
        }
        default:
            break;
    }
    return v;
}

std::wstring MarkupTools::describe(const Mark& mark) const {
    const Value& v = isMeasurement(mark.kind) ? valueOf(mark) : Value();
    switch (mark.kind) {
        case MarkKind::Distance: return L"Distancia  " + v.label;
        case MarkKind::Radius: return L"Radio  " + v.label + L"  " + v.detail;
        case MarkKind::Angle: return L"Angulo  " + v.label;
        case MarkKind::Area: return L"Area  " + v.label + L"  " + v.detail;
        case MarkKind::Note: return L"Nota: " + widen(mark.text);
        case MarkKind::Highlight: return L"Resaltado";
        case MarkKind::Underline: return L"Subrayado";
        case MarkKind::Pen: return L"Trazo";
        case MarkKind::Rectangle: return L"Rectangulo";
        case MarkKind::Ellipse: return L"Elipse";
        case MarkKind::Cloud: return L"Nube de revision";
    }
    return std::wstring();
}

void MarkupTools::drawLabel(void* graphics, double x, double y, const std::wstring& first,
                            const std::wstring& second, std::uint32_t color, double scale) const {
    Gdiplus::Graphics& g = *static_cast<Gdiplus::Graphics*>(graphics);
    const double dpi = m_host->markupDpi() / 96.0;
    const std::unique_ptr<Gdiplus::Font> bold = uiFont(12.0 * dpi * scale, Gdiplus::FontStyleBold);
    const std::unique_ptr<Gdiplus::Font> regular = uiFont(10.5 * dpi * scale, Gdiplus::FontStyleRegular);
    Gdiplus::Font& font = *bold;
    Gdiplus::Font& small = *regular;
    Gdiplus::RectF a, b;
    g.MeasureString(first.c_str(), -1, &font, Gdiplus::PointF(0, 0), &a);
    if (!second.empty()) g.MeasureString(second.c_str(), -1, &small, Gdiplus::PointF(0, 0), &b);
    const Gdiplus::REAL pad = static_cast<Gdiplus::REAL>(4.0 * dpi * scale);
    const Gdiplus::REAL w = std::max(a.Width, b.Width) + 2 * pad;
    const Gdiplus::REAL h = a.Height + b.Height + 2 * pad;
    const Gdiplus::REAL left = static_cast<Gdiplus::REAL>(x) - w / 2;
    const Gdiplus::REAL top = static_cast<Gdiplus::REAL>(y) - h - pad;
    Gdiplus::SolidBrush fill(Gdiplus::Color(235, 255, 255, 255));
    Gdiplus::Pen border(gdiColor(color), static_cast<Gdiplus::REAL>(1.5 * scale));
    g.FillRectangle(&fill, left, top, w, h);
    g.DrawRectangle(&border, left, top, w, h);
    Gdiplus::SolidBrush ink(gdiColor(kMarkBlack));
    g.DrawString(first.c_str(), -1, &font, Gdiplus::PointF(left + pad, top + pad), &ink);
    if (!second.empty()) g.DrawString(second.c_str(), -1, &small, Gdiplus::PointF(left + pad, top + pad + a.Height), &ink);
}

void MarkupTools::drawMeasure(void* graphics, const Mark& mark, const Camera& camera, int width, int height,
                              double scale) const {
    Gdiplus::Graphics& g = *static_cast<Gdiplus::Graphics*>(graphics);
    const Value& v = valueOf(mark);
    const double dpi = m_host->markupDpi() / 96.0;
    Gdiplus::Pen pen(gdiColor(mark.color), static_cast<Gdiplus::REAL>(2.0 * dpi * scale));
    Gdiplus::PointF p[6];
    for (std::size_t i = 0; i < mark.points.size() && i < 6; ++i) {
        if (!toScreen(camera, width, height, mark.points[i], &p[i])) return;
    }
    switch (mark.kind) {
        case MarkKind::Distance: {
            g.DrawLine(&pen, p[0], p[1]);
            // Marcas de extremo perpendiculares, como en una cota.
            const double dx = p[1].X - p[0].X, dy = p[1].Y - p[0].Y;
            const double len = std::max(1e-6, std::hypot(dx, dy));
            const Gdiplus::REAL nx = static_cast<Gdiplus::REAL>(-dy / len * 6 * dpi * scale);
            const Gdiplus::REAL ny = static_cast<Gdiplus::REAL>(dx / len * 6 * dpi * scale);
            for (int i = 0; i < 2; ++i) g.DrawLine(&pen, p[i].X - nx, p[i].Y - ny, p[i].X + nx, p[i].Y + ny);
            drawLabel(graphics, (p[0].X + p[1].X) / 2, (p[0].Y + p[1].Y) / 2, v.label, v.detail, mark.color, scale);
            break;
        }
        case MarkKind::Radius: {
            if (!v.ok) break;
            Gdiplus::PointF c;
            if (!toScreen(camera, width, height, v.center, &c)) break;
            Gdiplus::AdjustableArrowCap arrow(4, 4, TRUE);
            pen.SetCustomEndCap(&arrow);
            g.DrawLine(&pen, c, p[0]);
            const Gdiplus::REAL s = static_cast<Gdiplus::REAL>(4 * dpi * scale);
            Gdiplus::Pen thin(gdiColor(mark.color), static_cast<Gdiplus::REAL>(1.5 * scale));
            g.DrawLine(&thin, c.X - s, c.Y, c.X + s, c.Y);
            g.DrawLine(&thin, c.X, c.Y - s, c.X, c.Y + s);
            drawLabel(graphics, p[0].X, p[0].Y, v.label, v.detail, mark.color, scale);
            break;
        }
        case MarkKind::Angle: {
            Gdiplus::PointF vertex, a, b;
            if (mark.points.size() == 3) {
                vertex = p[1];
                a = p[0];
                b = p[2];
            } else {
                // Dos rectas: del clic en cada una hasta el punto donde se cruzan.
                a = p[2];
                b = p[5];
                vertex = a;
                const Vec3 u = normalize(mark.points[1] - mark.points[0]);
                const Vec3 w = normalize(mark.points[4] - mark.points[3]);
                const Vec3 d = mark.points[0] - mark.points[3];
                const double B = dot(u, w), D = dot(u, d), E = dot(w, d), den = 1 - B * B;
                if (den > 1e-18) {
                    const double s = (B * E - D) / den;
                    toScreen(camera, width, height, mark.points[0] + u * s, &vertex);
                }
            }
            Gdiplus::Pen dashed(gdiColor(mark.color), static_cast<Gdiplus::REAL>(1.5 * dpi * scale));
            dashed.SetDashStyle(Gdiplus::DashStyleDash);
            g.DrawLine(&dashed, vertex, a);
            g.DrawLine(&dashed, vertex, b);
            const double a0 = std::atan2(a.Y - vertex.Y, a.X - vertex.X) * 180 / kPi;
            double sweep = std::atan2(b.Y - vertex.Y, b.X - vertex.X) * 180 / kPi - a0;
            while (sweep > 180) sweep -= 360;
            while (sweep < -180) sweep += 360;
            const Gdiplus::REAL r = static_cast<Gdiplus::REAL>(26 * dpi * scale);
            g.DrawArc(&pen, vertex.X - r, vertex.Y - r, 2 * r, 2 * r, static_cast<Gdiplus::REAL>(a0),
                      static_cast<Gdiplus::REAL>(sweep));
            const double mid = (a0 + sweep / 2) * kPi / 180;
            drawLabel(graphics, vertex.X + std::cos(mid) * r * 1.8, vertex.Y + std::sin(mid) * r * 1.8 + 12 * dpi * scale,
                      v.label, v.detail, mark.color, scale);
            break;
        }
        case MarkKind::Area: {
            if (!v.ok) break;
            Gdiplus::SolidBrush fill(gdiColor(mark.color, 70));
            const Mesh& mesh = m_host->markupMesh();
            if (v.area.contour >= 0) {
                Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);
                std::vector<int> rings = {v.area.contour};
                rings.insert(rings.end(), v.area.holes.begin(), v.area.holes.end());
                for (const int ring : rings) {
                    std::vector<Gdiplus::PointF> poly;
                    for (const Vec3& q : contourOutline(mesh.features.contours[static_cast<std::size_t>(ring)])) {
                        Gdiplus::PointF s;
                        if (toScreen(camera, width, height, q, &s)) poly.push_back(s);
                    }
                    if (poly.size() >= 3) path.AddPolygon(poly.data(), static_cast<INT>(poly.size()));
                }
                g.FillPath(&fill, &path);
            } else {
                for (const std::uint32_t t : v.area.triangles) {
                    Gdiplus::PointF tri[3];
                    bool ok = true;
                    for (int k = 0; k < 3; ++k) ok = ok && toScreen(camera, width, height, mesh.positions[mesh.indices[3 * t + k]], &tri[k]);
                    if (ok) g.FillPolygon(&fill, tri, 3);
                }
            }
            drawLabel(graphics, p[0].X, p[0].Y, v.label, v.detail, mark.color, scale);
            break;
        }
        default:
            break;
    }
}

void MarkupTools::drawSnap(void* graphics, double scale) const {
    if (!isMeasureTool() || m_hover.kind == SnapKind::None) return;
    Gdiplus::Graphics& g = *static_cast<Gdiplus::Graphics*>(graphics);
    Gdiplus::PointF c;
    if (!toScreen(m_host->markupCamera(), m_host->markupWidth(), m_host->markupHeight(), m_hover.point, &c)) return;
    const double dpi = m_host->markupDpi() / 96.0;
    const Gdiplus::REAL s = static_cast<Gdiplus::REAL>(5 * dpi * scale);
    Gdiplus::Pen pen(Gdiplus::Color(255, 0, 200, 83), static_cast<Gdiplus::REAL>(2 * dpi * scale));
    switch (m_hover.kind) {
        case SnapKind::Endpoint: g.DrawRectangle(&pen, c.X - s, c.Y - s, 2 * s, 2 * s); break;
        case SnapKind::Center:
            g.DrawEllipse(&pen, c.X - s, c.Y - s, 2 * s, 2 * s);
            g.DrawLine(&pen, c.X - s, c.Y, c.X + s, c.Y);
            g.DrawLine(&pen, c.X, c.Y - s, c.X, c.Y + s);
            break;
        case SnapKind::Midpoint: {
            const Gdiplus::PointF tri[3] = {{c.X, c.Y - s}, {c.X + s, c.Y + s}, {c.X - s, c.Y + s}};
            g.DrawPolygon(&pen, tri, 3);
            break;
        }
        case SnapKind::OnEdge:
            g.DrawLine(&pen, c.X - s, c.Y - s, c.X + s, c.Y + s);
            g.DrawLine(&pen, c.X - s, c.Y + s, c.X + s, c.Y - s);
            break;
        default: {
            Gdiplus::SolidBrush dot(Gdiplus::Color(255, 0, 200, 83));
            g.FillEllipse(&dot, c.X - s / 2, c.Y - s / 2, s, s);
            break;
        }
    }
}

void MarkupTools::draw(HDC dc, const Camera& camera, int width, int height, double scale, bool interactive) const {
    if (!ensureGdiplus()) return;
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    for (const Mark& mark : m_doc.marks) {
        if (!markVisible(mark, m_doc, camera)) continue;
        if (isMeasurement(mark.kind)) drawMeasure(&g, mark, camera, width, height, scale);
    }
    if (!interactive) return;

    // Medida en curso: linea de goma hasta el cursor.
    if (!m_pending.empty() && m_hover.kind != SnapKind::None) {
        Gdiplus::PointF a, b;
        if (toScreen(camera, width, height, m_pending.back().point, &a) &&
            toScreen(camera, width, height, m_hover.point, &b)) {
            Gdiplus::Pen rubber(gdiColor(kMeasureColor), 1.5f);
            rubber.SetDashStyle(Gdiplus::DashStyleDash);
            g.DrawLine(&rubber, a, b);
            if (m_tool == Tool::Distance) {
                const DistanceResult d = measureDistance(m_pending.back().point, m_hover.point, m_host->markupPlane());
                drawLabel(&g, (a.X + b.X) / 2, (a.Y + b.Y) / 2,
                          widen(formatLength(d.total, m_host->markupMesh().units)), L"", kMeasureColor, scale);
            }
        }
    }
    drawSnap(&g, scale);
}

}  // namespace stp
