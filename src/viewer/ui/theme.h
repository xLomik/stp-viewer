// Tema grafito de la interfaz: paleta, fuentes y primitivas de dibujo con GDI+.
#pragma once

#include <windows.h>
#include <gdiplus.h>

#include <functional>
#include <memory>
#include <string>

namespace stp {
namespace ui {

// Paleta (ARGB).
constexpr Gdiplus::ARGB kBackground = 0xFF1F2329;
constexpr Gdiplus::ARGB kPanel = 0xFF262B32;
constexpr Gdiplus::ARGB kBorder = 0xFF363C45;
constexpr Gdiplus::ARGB kText = 0xFFE6E9ED;
constexpr Gdiplus::ARGB kTextDim = 0xFF9AA3AD;
constexpr Gdiplus::ARGB kAccent = 0xFF2F80ED;
constexpr Gdiplus::ARGB kHover = 0xFF2D333B;
constexpr Gdiplus::ARGB kPressed = 0xFF343B45;
constexpr Gdiplus::ARGB kSnapGreen = 0xFF00C853;
constexpr Gdiplus::ARGB kCloseRed = 0xFFC42B1C;

inline COLORREF gdi(Gdiplus::ARGB c) {
    return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255);
}
inline Gdiplus::Color color(Gdiplus::ARGB c, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, static_cast<BYTE>((c >> 16) & 255), static_cast<BYTE>((c >> 8) & 255),
                          static_cast<BYTE>(c & 255));
}
inline int scale(int value, int dpi) { return MulDiv(value, dpi, 96); }

// Segoe UI en puntos a ese DPI; sin Segoe UI (wine), la sans-serif generica.
std::unique_ptr<Gdiplus::Font> font(double points, int dpi, INT style = Gdiplus::FontStyleRegular);
std::unique_ptr<Gdiplus::Font> fontPixels(double pixels, INT style = Gdiplus::FontStyleRegular);

void fillRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, Gdiplus::ARGB color);
void strokeRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, Gdiplus::ARGB color, float width);
// Una sola linea, recortada con "..." si no cabe; align 0 izquierda, 1 centro, 2 derecha.
void drawText(Gdiplus::Graphics& g, const std::wstring& text, const Gdiplus::Font& f, const Gdiplus::RectF& r,
              Gdiplus::ARGB color, int align = 0);
Gdiplus::SizeF measureText(Gdiplus::Graphics& g, const std::wstring& text, const Gdiplus::Font& f);
// Mezcla lineal (t de 0 a 1): transiciones de hover.
Gdiplus::ARGB blend(Gdiplus::ARGB a, Gdiplus::ARGB b, double t);
// Pinta rect con doble buffer: bitmap en memoria, Graphics con antialias y copia.
void paintBuffered(HDC dc, const RECT& rect, const std::function<void(Gdiplus::Graphics&)>& draw);
std::wstring widen(const char* utf8);

}  // namespace ui
}  // namespace stp
