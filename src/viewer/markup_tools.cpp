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

constexpr UINT kNoteCommit = WM_APP + 21;
constexpr std::size_t kUndoDepth = 100;

std::string narrow(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(0, size)), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], size, nullptr, nullptr);
    }
    return out;
}

// Douglas-Peucker sobre el trazo en pantalla.
void simplify(const std::vector<POINT>& in, double tolerance, std::vector<POINT>* out) {
    if (in.size() < 3) {
        *out = in;
        return;
    }
    std::vector<char> keep(in.size(), 0);
    keep.front() = keep.back() = 1;
    std::vector<std::pair<std::size_t, std::size_t>> stack = {{0, in.size() - 1}};
    while (!stack.empty()) {
        const auto range = stack.back();
        stack.pop_back();
        const double ax = in[range.first].x, ay = in[range.first].y;
        const double bx = in[range.second].x, by = in[range.second].y;
        const double len = std::max(1e-9, std::hypot(bx - ax, by - ay));
        double worst = 0;
        std::size_t at = 0;
        for (std::size_t i = range.first + 1; i < range.second; ++i) {
            const double d = std::fabs((bx - ax) * (ay - in[i].y) - (ax - in[i].x) * (by - ay)) / len;
            if (d > worst) {
                worst = d;
                at = i;
            }
        }
        if (worst > tolerance) {
            keep[at] = 1;
            stack.push_back({range.first, at});
            stack.push_back({at, range.second});
        }
    }
    out->clear();
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (keep[i]) out->push_back(in[i]);
    }
}

double segmentDistance(double px, double py, double ax, double ay, double bx, double by) {
    const double dx = bx - ax, dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0;
    t = std::max(0.0, std::min(1.0, t));
    return std::hypot(px - (ax + dx * t), py - (ay + dy * t));
}

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
    m_stroke.clear();
    m_sketching = false;
    m_noteAnchored = false;
    m_hover = SnapResult();
    m_host->markupRedraw();
    m_host->markupChanged();
}



void MarkupTools::clear() { setDocument(MarkupDocument()); }

void MarkupTools::showMessage(const std::wstring& text, unsigned milliseconds) {
    m_message = text;
    m_messageUntil = GetTickCount64() + milliseconds;
    SetTimer(m_host->markupWindow(), kMessageTimer, milliseconds + 50, nullptr);
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
        case Tool::Highlight: return L"Resaltador: arrastrar   (Q: color, Esc: terminar)";
        case Tool::Underline: return L"Subrayado: arrastrar   (Shift: recto, Q: color, Esc: terminar)";
        case Tool::Pen: return L"Lapiz: arrastrar   (Q: color, Esc: terminar)";
        case Tool::Rectangle: return L"Rectangulo: arrastrar de esquina a esquina   (Q: color, Esc: terminar)";
        case Tool::Ellipse: return L"Elipse: arrastrar de esquina a esquina   (Q: color, Esc: terminar)";
        case Tool::Cloud: return L"Nube de revision: arrastrar de esquina a esquina   (Q: color, Esc: terminar)";
        case Tool::Note:
            return (m_noteAnchored ? L"Nota: clic donde va el texto" : L"Nota: clic en el punto a senalar") +
                   std::wstring(L"   (Esc: terminar)");
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

void MarkupTools::pushUndo() {
    m_undo.push_back(m_doc);
    if (m_undo.size() > kUndoDepth) m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void MarkupTools::addMark(Mark mark) {
    pushUndo();
    mark.id = m_doc.newId();
    m_doc.marks.push_back(mark);
    m_dirty = true;
    m_host->markupRedraw();
    m_host->markupChanged();
}

void MarkupTools::setDocument(const MarkupDocument& doc) {
    closeNoteEditor(false);
    m_doc = doc;
    m_values.clear();
    m_pending.clear();
    m_undo.clear();
    m_redo.clear();
    m_selected = -1;
    m_dirty = false;
    m_host->markupRedraw();
    m_host->markupChanged();
}

void MarkupTools::undo() {
    if (m_undo.empty()) return;
    m_redo.push_back(m_doc);
    m_doc = m_undo.back();
    m_undo.pop_back();
    m_values.clear();
    m_selected = -1;
    m_dirty = true;
    m_host->markupRedraw();
    m_host->markupChanged();
}

void MarkupTools::redo() {
    if (m_redo.empty()) return;
    m_undo.push_back(m_doc);
    m_doc = m_redo.back();
    m_redo.pop_back();
    m_values.clear();
    m_selected = -1;
    m_dirty = true;
    m_host->markupRedraw();
    m_host->markupChanged();
}

void MarkupTools::select(int id) {
    m_selected = id;
    m_host->markupRedraw();
}

void MarkupTools::deleteSelected() {
    const auto it = std::find_if(m_doc.marks.begin(), m_doc.marks.end(),
                                 [&](const Mark& m) { return m.id == m_selected; });
    if (it == m_doc.marks.end()) return;
    pushUndo();
    m_values.erase(it->id);
    m_doc.marks.erase(it);
    m_selected = -1;
    m_dirty = true;
    m_host->markupRedraw();
    m_host->markupChanged();
}

void MarkupTools::cycleColor() {
    if (m_tool == Tool::Highlight) {
        m_highlight = m_highlight == kHighlightYellow ? kHighlightGreen
                      : m_highlight == kHighlightGreen ? kHighlightPink : kHighlightYellow;
    } else {
        const std::uint32_t colors[] = {kMarkRed, kMarkYellow, kMarkGreen, kMarkBlue, kMarkBlack};
        std::size_t i = 0;
        while (i < 5 && colors[i] != m_color) ++i;
        m_color = colors[(i + 1) % 5];
    }
    m_host->markupChanged();
}

int MarkupTools::hiddenCount(const Camera& camera) const {
    int count = 0;
    for (const Mark& mark : m_doc.marks) {
        if (!markVisible(mark, m_doc, camera)) ++count;
    }
    return count;
}

bool MarkupTools::isSketchTool() const {
    return m_tool == Tool::Highlight || m_tool == Tool::Underline || m_tool == Tool::Pen ||
           m_tool == Tool::Rectangle || m_tool == Tool::Ellipse || m_tool == Tool::Cloud;
}

// Punto de pantalla llevado al plano del dibujo (2D) o al plano perpendicular a
// la vista que pasa por `through` (3D).
Vec3 MarkupTools::unproject(int x, int y, const Vec3& through) const {
    const Camera& camera = m_host->markupCamera();
    const Ray ray = pixelRay(camera, m_host->markupWidth(), m_host->markupHeight(), x, y);
    Vec3 normal = camera.forward(), origin = through;
    if (const PlanarInfo* plane = m_host->markupPlane()) {
        normal = plane->normal;
        origin = plane->center;
    }
    const double den = dot(ray.dir, normal);
    if (std::fabs(den) < 1e-12) return origin;
    return ray.origin + ray.dir * (dot(origin - ray.origin, normal) / den);
}

// En 2D los trazos se ven siempre; en 3D pertenecen a la vista en que se dibujan.
int MarkupTools::viewForSketch() {
    if (m_host->markupPlane()) return 0;
    const Camera& camera = m_host->markupCamera();
    for (const MarkupView& view : m_doc.views) {
        if (sameView(view.camera, camera)) return view.id;
    }
    MarkupView view;
    view.id = m_doc.newId();
    view.name = "Vista " + std::to_string(m_doc.views.size() + 1);
    view.camera = camera;
    m_doc.views.push_back(view);
    return view.id;
}

void MarkupTools::finishSketch() {
    m_sketching = false;
    if (m_stroke.empty()) return;
    long minX = m_stroke[0].x, maxX = minX, minY = m_stroke[0].y, maxY = minY;
    for (const POINT& p : m_stroke) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    if (maxX - minX < 3 && maxY - minY < 3) {  // un clic suelto no es un trazo
        m_stroke.clear();
        m_host->markupRedraw();
        return;
    }
    std::vector<POINT> points;
    if (m_tool == Tool::Pen || m_tool == Tool::Highlight) simplify(m_stroke, 0.5, &points);
    else points = {m_stroke.front(), m_stroke.back()};

    pushUndo();
    Mark mark;
    mark.kind = m_tool == Tool::Highlight ? MarkKind::Highlight
                : m_tool == Tool::Underline ? MarkKind::Underline
                : m_tool == Tool::Pen ? MarkKind::Pen
                : m_tool == Tool::Rectangle ? MarkKind::Rectangle
                : m_tool == Tool::Ellipse ? MarkKind::Ellipse : MarkKind::Cloud;
    mark.color = color();
    mark.view = viewForSketch();
    const Vec3 depth = m_host->markupCamera().target;
    for (const POINT& p : points) mark.points.push_back(unproject(p.x, p.y, depth));
    mark.id = m_doc.newId();
    m_doc.marks.push_back(mark);
    m_stroke.clear();
    m_dirty = true;
    m_host->markupRedraw();
    m_host->markupChanged();
}

LRESULT CALLBACK MarkupTools::editProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<MarkupTools*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);
    if (msg == WM_KEYDOWN && wparam == VK_RETURN && GetKeyState(VK_SHIFT) >= 0) {
        PostMessageW(self->m_host->markupWindow(), kNoteCommit, 1, 0);
        return 0;
    }
    if (msg == WM_KEYDOWN && wparam == VK_ESCAPE) {
        PostMessageW(self->m_host->markupWindow(), kNoteCommit, 0, 0);
        return 0;
    }
    if (msg == WM_KILLFOCUS) PostMessageW(self->m_host->markupWindow(), kNoteCommit, 1, 0);
    return CallWindowProcW(self->m_editDefault, hwnd, msg, wparam, lparam);
}

void MarkupTools::openNoteEditor(int markId) {
    closeNoteEditor(true);
    const auto it = std::find_if(m_doc.marks.begin(), m_doc.marks.end(), [&](const Mark& m) { return m.id == markId; });
    if (it == m_doc.marks.end() || it->points.size() < 2) return;
    double x, y;
    if (!projectPoint(m_host->markupCamera(), m_host->markupWidth(), m_host->markupHeight(), it->points[1], &x, &y)) return;
    const int dpi = m_host->markupDpi();
    m_edit = CreateWindowExW(0, L"EDIT", widen(it->text).c_str(),
                             WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                             static_cast<int>(x), static_cast<int>(y), MulDiv(260, dpi, 96), MulDiv(64, dpi, 96),
                             m_host->markupWindow(), nullptr, nullptr, nullptr);
    if (!m_edit) return;
    SendMessageW(m_edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SetWindowLongPtrW(m_edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    m_editDefault = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(m_edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&MarkupTools::editProc)));
    m_editMark = markId;
    SendMessageW(m_edit, EM_SETSEL, 0, -1);
    SetFocus(m_edit);
}

void MarkupTools::closeNoteEditor(bool commit) {
    if (!m_edit) return;
    HWND edit = m_edit;
    m_edit = nullptr;  // antes de destruir: WM_KILLFOCUS no debe volver a entrar
    const int length = GetWindowTextLengthW(edit);
    std::wstring text(static_cast<std::size_t>(length), L'\0');
    if (length > 0) GetWindowTextW(edit, &text[0], length + 1);
    DestroyWindow(edit);

    const auto it = std::find_if(m_doc.marks.begin(), m_doc.marks.end(), [&](const Mark& m) { return m.id == m_editMark; });
    m_editMark = -1;
    if (it != m_doc.marks.end()) {
        std::string utf8 = narrow(text);
        utf8.erase(std::remove(utf8.begin(), utf8.end(), '\r'), utf8.end());
        if (commit && !utf8.empty()) {
            // Una nota nueva ya guardo su paso de deshacer al crearse; editar una
            // existente guarda uno nuevo.
            if (utf8 != it->text && !it->text.empty()) pushUndo();
            it->text = utf8;
            m_dirty = true;
        } else if (it->text.empty()) {
            m_doc.marks.erase(it);  // nota nueva sin texto: se descarta
        }
    }
    SetFocus(m_host->markupWindow());
    m_host->markupRedraw();
    m_host->markupChanged();
}

bool MarkupTools::screenBounds(const Mark& mark, const Camera& camera, int width, int height, RECT* out) const {
    bool any = false;
    for (const Vec3& p : mark.points) {
        double x, y;
        if (!projectPoint(camera, width, height, p, &x, &y)) continue;
        const LONG ix = static_cast<LONG>(x), iy = static_cast<LONG>(y);
        if (!any) *out = {ix, iy, ix, iy};
        out->left = std::min(out->left, ix); out->right = std::max(out->right, ix);
        out->top = std::min(out->top, iy); out->bottom = std::max(out->bottom, iy);
        any = true;
    }
    return any;
}

int MarkupTools::hitTest(int x, int y) const {
    const Camera& camera = m_host->markupCamera();
    const int w = m_host->markupWidth(), h = m_host->markupHeight();
    const double reach = 6.0 * m_host->markupDpi() / 96.0;
    for (auto it = m_doc.marks.rbegin(); it != m_doc.marks.rend(); ++it) {
        const Mark& mark = *it;
        if (!markVisible(mark, m_doc, camera)) continue;
        std::vector<Gdiplus::PointF> p;
        for (const Vec3& q : mark.points) {
            Gdiplus::PointF s;
            if (toScreen(camera, w, h, q, &s)) p.push_back(s);
        }
        if (p.empty()) continue;
        double best = 1e300;
        if (mark.kind == MarkKind::Rectangle || mark.kind == MarkKind::Ellipse || mark.kind == MarkKind::Cloud) {
            if (p.size() < 2) continue;
            const double l = std::min(p[0].X, p[1].X), r = std::max(p[0].X, p[1].X);
            const double t = std::min(p[0].Y, p[1].Y), b = std::max(p[0].Y, p[1].Y);
            best = std::min({segmentDistance(x, y, l, t, r, t), segmentDistance(x, y, r, t, r, b),
                             segmentDistance(x, y, r, b, l, b), segmentDistance(x, y, l, b, l, t)});
        } else {
            for (std::size_t i = 0; i + 1 < p.size(); ++i) {
                best = std::min(best, segmentDistance(x, y, p[i].X, p[i].Y, p[i + 1].X, p[i + 1].Y));
            }
            // Etiqueta de medidas y caja de notas: un recuadro generoso alrededor del ultimo punto.
            const Gdiplus::PointF& label = mark.kind == MarkKind::Note ? p[1 % p.size()] : p[0];
            if (mark.kind == MarkKind::Note || isMeasurement(mark.kind)) {
                if (std::fabs(x - label.X) < 90 * reach / 6 && std::fabs(y - label.Y) < 24 * reach / 6) best = 0;
            }
            if (p.size() == 1) best = std::min(best, static_cast<double>(std::hypot(x - p[0].X, y - p[0].Y)));
        }
        const double width = mark.kind == MarkKind::Highlight ? 7.0 * reach / 6 : reach;
        if (best <= width) return mark.id;
    }
    return -1;
}

void MarkupTools::drawSketch(void* graphics, const Mark& mark, const Camera& camera, int width, int height,
                             double scale) const {
    Gdiplus::Graphics& g = *static_cast<Gdiplus::Graphics*>(graphics);
    const double dpi = m_host->markupDpi() / 96.0;
    std::vector<Gdiplus::PointF> p;
    for (const Vec3& q : mark.points) {
        Gdiplus::PointF s;
        if (!toScreen(camera, width, height, q, &s)) return;
        p.push_back(s);
    }
    if (p.size() < 2) return;
    const bool highlight = mark.kind == MarkKind::Highlight;
    const double widthPx = highlight ? 14 : (mark.kind == MarkKind::Underline ? 3 : 2);
    Gdiplus::Pen pen(gdiColor(mark.color, highlight ? 102 : 255), static_cast<Gdiplus::REAL>(widthPx * dpi * scale));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    const Gdiplus::REAL l = std::min(p[0].X, p[1].X), r = std::max(p[0].X, p[1].X);
    const Gdiplus::REAL t = std::min(p[0].Y, p[1].Y), b = std::max(p[0].Y, p[1].Y);
    switch (mark.kind) {
        case MarkKind::Highlight:
        case MarkKind::Pen:
        case MarkKind::Underline:
            g.DrawLines(&pen, p.data(), static_cast<INT>(p.size()));
            break;
        case MarkKind::Rectangle: g.DrawRectangle(&pen, l, t, r - l, b - t); break;
        case MarkKind::Ellipse: g.DrawEllipse(&pen, l, t, r - l, b - t); break;
        case MarkKind::Cloud: {
            // Arcos hacia afuera a lo largo de los cuatro lados.
            const Gdiplus::REAL bump = static_cast<Gdiplus::REAL>(10 * dpi * scale);
            Gdiplus::GraphicsPath path;
            auto side = [&](Gdiplus::REAL length, auto center, Gdiplus::REAL start) {
                const int n = std::max(1, static_cast<int>(length / bump));
                const Gdiplus::REAL step = length / n;
                for (int i = 0; i < n; ++i) {
                    const Gdiplus::PointF c = center((i + 0.5f) * step);
                    path.AddArc(c.X - step / 2, c.Y - step / 2, step, step, start, 180.0f);
                }
            };
            side(r - l, [&](Gdiplus::REAL d) { return Gdiplus::PointF(l + d, t); }, 180.0f);
            side(b - t, [&](Gdiplus::REAL d) { return Gdiplus::PointF(r, t + d); }, 270.0f);
            side(r - l, [&](Gdiplus::REAL d) { return Gdiplus::PointF(r - d, b); }, 0.0f);
            side(b - t, [&](Gdiplus::REAL d) { return Gdiplus::PointF(l, b - d); }, 90.0f);
            g.DrawPath(&pen, &path);
            break;
        }
        default:
            break;
    }
}

void MarkupTools::drawNote(void* graphics, const Mark& mark, const Camera& camera, int width, int height,
                           double scale) const {
    Gdiplus::Graphics& g = *static_cast<Gdiplus::Graphics*>(graphics);
    if (mark.points.size() < 2 || mark.id == m_editMark) return;  // mientras se edita la tapa el cuadro de texto
    Gdiplus::PointF anchor, label;
    if (!toScreen(camera, width, height, mark.points[0], &anchor) ||
        !toScreen(camera, width, height, mark.points[1], &label)) {
        return;
    }
    const double dpi = m_host->markupDpi() / 96.0;
    Gdiplus::Pen pen(gdiColor(mark.color), static_cast<Gdiplus::REAL>(2 * dpi * scale));
    Gdiplus::AdjustableArrowCap arrow(4, 4, TRUE);
    pen.SetCustomEndCap(&arrow);
    g.DrawLine(&pen, label, anchor);

    const std::unique_ptr<Gdiplus::Font> regular = uiFont(12 * dpi * scale, Gdiplus::FontStyleRegular);
    Gdiplus::Font& font = *regular;
    const std::wstring text = widen(mark.text);
    const Gdiplus::RectF layout(0, 0, static_cast<Gdiplus::REAL>(260 * dpi * scale), 10000);
    Gdiplus::RectF box;
    g.MeasureString(text.c_str(), -1, &font, layout, &box);
    const Gdiplus::REAL pad = static_cast<Gdiplus::REAL>(5 * dpi * scale);
    const Gdiplus::RectF frame(label.X, label.Y, box.Width + 2 * pad, box.Height + 2 * pad);
    Gdiplus::SolidBrush fill(Gdiplus::Color(240, 255, 255, 255));
    Gdiplus::Pen border(gdiColor(mark.color), static_cast<Gdiplus::REAL>(1.5 * dpi * scale));
    g.FillRectangle(&fill, frame);
    g.DrawRectangle(&border, frame.X, frame.Y, frame.Width, frame.Height);
    Gdiplus::SolidBrush ink(gdiColor(kMarkBlack));
    // Mismo ancho de maquetado que al medir: asi los renglones se parten igual.
    const Gdiplus::RectF inner(frame.X + pad, frame.Y + pad, layout.Width, box.Height + font.GetHeight(&g));
    g.DrawString(text.c_str(), -1, &font, inner, nullptr, &ink);
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
    const int x = GET_X_LPARAM(lparam), y = GET_Y_LPARAM(lparam);
    switch (msg) {
        case kNoteCommit:
            closeNoteEditor(wparam != 0);
            return true;

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
            m_mouse = {x, y};
            if (isMeasureTool() || (m_tool == Tool::Note && !m_noteAnchored)) {
                m_hover = snapAt(x, y);
                m_host->markupRedraw();
            } else if (m_tool == Tool::Note) {
                m_host->markupRedraw();  // linea de goma de la nota
            }
            if (m_sketching) {
                POINT p = {x, y};
                if (m_tool == Tool::Underline && GetKeyState(VK_SHIFT) < 0) {
                    // Shift: horizontal o vertical, lo que este mas cerca.
                    if (std::abs(p.x - m_stroke[0].x) >= std::abs(p.y - m_stroke[0].y)) p.y = m_stroke[0].y;
                    else p.x = m_stroke[0].x;
                }
                if (m_tool == Tool::Pen || m_tool == Tool::Highlight) m_stroke.push_back(p);
                else if (m_stroke.size() == 1) m_stroke.push_back(p);
                else m_stroke[1] = p;
                m_host->markupRedraw();
                return true;
            }
            return false;

        case WM_LBUTTONDOWN:
            SetFocus(m_host->markupWindow());
            m_downAt = {x, y};
            if (isMeasureTool()) {
                clickMeasure(snapAt(x, y));
                return true;
            }
            if (isSketchTool()) {
                SetCapture(m_host->markupWindow());
                m_sketching = true;
                m_stroke = {POINT{x, y}};
                return true;
            }
            if (m_tool == Tool::Note) {
                if (!m_noteAnchored) {
                    m_noteAnchor = snapAt(x, y);
                    if (m_noteAnchor.kind == SnapKind::None) {
                        m_noteAnchor.point = unproject(x, y, m_host->markupCamera().target);
                    }
                    m_noteAnchored = true;
                } else {
                    pushUndo();
                    Mark note;
                    note.kind = MarkKind::Note;
                    note.color = m_color;
                    note.points = {m_noteAnchor.point, unproject(x, y, m_noteAnchor.point)};
                    note.id = m_doc.newId();
                    m_doc.marks.push_back(note);
                    m_noteAnchored = false;
                    m_dirty = true;
                    openNoteEditor(note.id);
                }
                m_host->markupRedraw();
                return true;
            }
            return false;  // Navegar: la vista gira o mueve

        case WM_LBUTTONUP:
            if (m_sketching) {
                ReleaseCapture();
                finishSketch();
                return true;
            }
            if (m_tool == Tool::Navigate && m_editing && std::abs(x - m_downAt.x) < 3 && std::abs(y - m_downAt.y) < 3) {
                select(hitTest(x, y));
                m_host->markupChanged();
            }
            return m_tool != Tool::Navigate;

        case WM_LBUTTONDBLCLK:
            if (m_tool == Tool::Navigate && m_editing) {
                const int id = hitTest(x, y);
                const auto it = std::find_if(m_doc.marks.begin(), m_doc.marks.end(), [&](const Mark& m) { return m.id == id; });
                if (it != m_doc.marks.end() && it->kind == MarkKind::Note) {
                    openNoteEditor(id);
                    return true;
                }
                return false;
            }
            return m_tool != Tool::Navigate;

        case WM_KEYDOWN: {
            const bool control = GetKeyState(VK_CONTROL) < 0;
            if (control && wparam == 'Z') { undo(); return true; }
            if (control && wparam == 'Y') { redo(); return true; }
            if (control) return false;
            if (wparam == 'M') { setTool(Tool::Distance); return true; }
            if (isMeasureTool() && wparam >= '1' && wparam <= '4') {
                const Tool tools[] = {Tool::Distance, Tool::Radius, Tool::Angle, Tool::Area};
                setTool(tools[wparam - '1']);
                return true;
            }
            if (m_editing) {
                switch (wparam) {
                    case 'H': setTool(Tool::Highlight); return true;
                    case 'U': setTool(Tool::Underline); return true;
                    case 'N': setTool(Tool::Note); return true;
                    case 'R': setTool(Tool::Rectangle); return true;
                    case 'E': setTool(Tool::Ellipse); return true;
                    case 'C': setTool(Tool::Cloud); return true;
                    case 'L': setTool(Tool::Pen); return true;
                    case 'Q': cycleColor(); return true;
                    default: break;
                }
            }
            if (wparam == VK_ESCAPE) {
                if (m_sketching || !m_pending.empty() || m_noteAnchored) {
                    if (m_sketching) ReleaseCapture();
                    m_sketching = false;
                    m_stroke.clear();
                    m_pending.clear();
                    m_noteAnchored = false;
                    m_host->markupRedraw();
                    return true;
                }
                if (m_tool != Tool::Navigate) { setTool(Tool::Navigate); return true; }
                if (m_selected >= 0) { select(-1); return true; }
                return false;
            }
            if (wparam == VK_DELETE) {
                if (m_selected >= 0) { deleteSelected(); return true; }
                if (!m_editing && !m_doc.marks.empty()) {  // panel: borra la ultima medida
                    pushUndo();
                    m_values.erase(m_doc.marks.back().id);
                    m_doc.marks.pop_back();
                    m_host->markupRedraw();
                    return true;
                }
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
            v.detail = L"\u0394X " + widen(formatNumber(d.delta.x, 3)) + L"   \u0394Y " + widen(formatNumber(d.delta.y, 3));
            if (!plane) v.detail += L"   \u0394Z " + widen(formatNumber(d.delta.z, 3));
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
            v.detail = L"\u00D8 " + widen(formatLength(2 * r.radius, unit));
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
    if (m_hover.kind == SnapKind::None) return;
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
    const double dpi = m_host->markupDpi() / 96.0;

    for (const Mark& mark : m_doc.marks) {
        if (!markVisible(mark, m_doc, camera)) continue;
        if (isMeasurement(mark.kind)) drawMeasure(&g, mark, camera, width, height, scale);
        else if (mark.kind == MarkKind::Note) drawNote(&g, mark, camera, width, height, scale);
        else drawSketch(&g, mark, camera, width, height, scale);
        if (interactive && mark.id == m_selected) {
            RECT box;
            if (screenBounds(mark, camera, width, height, &box)) {
                Gdiplus::Pen dashed(Gdiplus::Color(255, 47, 111, 219), static_cast<Gdiplus::REAL>(1.5 * dpi));
                dashed.SetDashStyle(Gdiplus::DashStyleDash);
                const Gdiplus::REAL pad = static_cast<Gdiplus::REAL>(8 * dpi);
                g.DrawRectangle(&dashed, box.left - pad, box.top - pad, box.right - box.left + 2 * pad,
                                box.bottom - box.top + 2 * pad);
            }
        }
    }
    if (!interactive) return;

    const int hidden = hiddenCount(camera);
    if (hidden > 0) {
        drawLabel(&g, width / 2.0, 64 * dpi,
                  std::to_wstring(hidden) + (hidden == 1 ? L" marca en otra vista" : L" marcas en otras vistas") +
                      L" \u2014 F2 para verlas",
                  L"", kMarkBlue, 1.0);
    }

    // Trazo o forma en curso, dibujado como si ya existiera.
    if (m_sketching && m_stroke.size() >= 2) {
        Mark preview;
        preview.kind = m_tool == Tool::Highlight ? MarkKind::Highlight
                       : m_tool == Tool::Underline ? MarkKind::Underline
                       : m_tool == Tool::Pen ? MarkKind::Pen
                       : m_tool == Tool::Rectangle ? MarkKind::Rectangle
                       : m_tool == Tool::Ellipse ? MarkKind::Ellipse : MarkKind::Cloud;
        preview.color = color();
        const Vec3 depth = camera.target;
        for (const POINT& p : m_stroke) preview.points.push_back(unproject(p.x, p.y, depth));
        drawSketch(&g, preview, camera, width, height, scale);
    }

    // Nota a medio hacer: flecha de goma desde el ancla hasta el cursor.
    if (m_tool == Tool::Note && m_noteAnchored) {
        Gdiplus::PointF a;
        if (toScreen(camera, width, height, m_noteAnchor.point, &a)) {
            Gdiplus::Pen rubber(gdiColor(m_color), static_cast<Gdiplus::REAL>(1.5 * dpi));
            rubber.SetDashStyle(Gdiplus::DashStyleDash);
            g.DrawLine(&rubber, a, Gdiplus::PointF(static_cast<Gdiplus::REAL>(m_mouse.x), static_cast<Gdiplus::REAL>(m_mouse.y)));
        }
    }

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
    if (isMeasureTool() || (m_tool == Tool::Note && !m_noteAnchored)) drawSnap(&g, scale);
}

}  // namespace stp
