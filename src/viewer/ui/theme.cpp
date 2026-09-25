#include "theme.h"

#include <algorithm>

#include "../image_view.h"

namespace stp {
namespace ui {

std::unique_ptr<Gdiplus::Font> fontPixels(double pixels, INT style) {
    ensureGdiplus();
    // GDI+ no sustituye una fuente que falta (GDI si).
    auto f = std::make_unique<Gdiplus::Font>(L"Segoe UI", static_cast<Gdiplus::REAL>(pixels), style, Gdiplus::UnitPixel);
    if (f->GetLastStatus() == Gdiplus::Ok && f->IsAvailable()) return f;
    return std::make_unique<Gdiplus::Font>(Gdiplus::FontFamily::GenericSansSerif(), static_cast<Gdiplus::REAL>(pixels),
                                           style, Gdiplus::UnitPixel);
}

std::unique_ptr<Gdiplus::Font> font(double points, int dpi, INT style) {
    return fontPixels(points * dpi / 72.0, style);
}

namespace {
void roundPath(Gdiplus::GraphicsPath* path, const Gdiplus::RectF& r, float radius) {
    const float d = std::min({2 * radius, r.Width, r.Height});
    if (d <= 0.5f) {
        path->AddRectangle(r);
        return;
    }
    path->AddArc(r.X, r.Y, d, d, 180, 90);
    path->AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path->AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path->AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path->CloseFigure();
}
}  // namespace

void fillRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, Gdiplus::ARGB c) {
    Gdiplus::GraphicsPath path;
    roundPath(&path, r, radius);
    Gdiplus::SolidBrush brush{Gdiplus::Color(c)};
    g.FillPath(&brush, &path);
}

void strokeRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, Gdiplus::ARGB c, float width) {
    Gdiplus::GraphicsPath path;
    roundPath(&path, r, radius);
    Gdiplus::Pen pen(Gdiplus::Color(c), width);
    g.DrawPath(&pen, &path);
}

void drawText(Gdiplus::Graphics& g, const std::wstring& text, const Gdiplus::Font& f, const Gdiplus::RectF& r,
              Gdiplus::ARGB c, int align) {
    Gdiplus::StringFormat format;
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetAlignment(align == 1 ? Gdiplus::StringAlignmentCenter
                                   : align == 2 ? Gdiplus::StringAlignmentFar : Gdiplus::StringAlignmentNear);
    Gdiplus::SolidBrush brush{Gdiplus::Color(c)};
    g.DrawString(text.c_str(), -1, &f, r, &format, &brush);
}

Gdiplus::SizeF measureText(Gdiplus::Graphics& g, const std::wstring& text, const Gdiplus::Font& f) {
    Gdiplus::RectF box;
    g.MeasureString(text.c_str(), -1, &f, Gdiplus::PointF(0, 0), &box);
    return Gdiplus::SizeF(box.Width, box.Height);
}

Gdiplus::ARGB blend(Gdiplus::ARGB a, Gdiplus::ARGB b, double t) {
    t = std::max(0.0, std::min(1.0, t));
    Gdiplus::ARGB out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const double ca = (a >> shift) & 255, cb = (b >> shift) & 255;
        out |= static_cast<Gdiplus::ARGB>(ca + (cb - ca) * t + 0.5) << shift;
    }
    return out;
}

void paintBuffered(HDC dc, const RECT& rect, const std::function<void(Gdiplus::Graphics&)>& draw) {
    const int w = std::max(1L, rect.right - rect.left), h = std::max(1L, rect.bottom - rect.top);
    HDC memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ old = SelectObject(memory, bitmap);
    {
        ensureGdiplus();
        Gdiplus::Graphics g(memory);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.TranslateTransform(static_cast<Gdiplus::REAL>(-rect.left), static_cast<Gdiplus::REAL>(-rect.top));
        draw(g);
    }
    BitBlt(dc, rect.left, rect.top, w, h, memory, 0, 0, SRCCOPY);
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

std::wstring widen(const char* utf8) {
    if (!utf8 || !*utf8) return std::wstring();
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(1, size)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &out[0], size);
    out.resize(static_cast<std::size_t>(std::max(0, size - 1)));
    return out;
}

}  // namespace ui
}  // namespace stp
