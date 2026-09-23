#include "text_overlay.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace stp {
namespace {

// La fuente se crea una sola vez a un tamano de referencia y cada texto se
// coloca con una transformacion afin: giro, escala, espejo y ancho sin crear
// fuentes nuevas.
constexpr double kCapUnits = 64.0;       // altura de mayuscula en unidades de la fuente
constexpr double kCapToEm = 1.0 / 0.716;  // Arial: la mayuscula mide 0,716 em
constexpr std::size_t kMaxTexts = 20000;

std::wstring widen(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(0, size)), L'\0');
    if (size > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], size);
    }
    return out;
}

std::vector<std::wstring> splitLines(const std::wstring& text) {
    std::vector<std::wstring> lines(1);
    for (const wchar_t c : text) {
        if (c == L'\n') lines.emplace_back();
        else if (c != L'\r') lines.back().push_back(c);
    }
    return lines;
}

}  // namespace

void drawMeshTexts(HDC dc, const Mesh& mesh, const Camera& camera, int width, int height,
                   COLORREF color, double minPixels) {
    if (mesh.texts.empty() || width <= 0 || height <= 0) return;

    HFONT font = CreateFontW(-static_cast<int>(std::lround(kCapUnits * kCapToEm)), 0, 0, 0,
                             FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             VARIABLE_PITCH | FF_SWISS, L"Arial");
    if (!font) return;

    const int oldMode = SetGraphicsMode(dc, GM_ADVANCED);
    HGDIOBJ oldFont = SelectObject(dc, font);
    const int oldBk = SetBkMode(dc, TRANSPARENT);
    const COLORREF oldColor = SetTextColor(dc, color);
    const UINT oldAlign = SetTextAlign(dc, TA_LEFT | TA_BASELINE | TA_NOUPDATECP);

    TEXTMETRICW metrics = {};
    GetTextMetricsW(dc, &metrics);
    const double descent = metrics.tmDescent;
    const double lineSpacing = kTextLineSpacing * kCapUnits;

    std::size_t drawn = 0;
    for (const MeshText& text : mesh.texts) {
        if (drawn >= kMaxTexts) break;
        double ax, ay, dx, dy, ux, uy;
        if (!projectPoint(camera, width, height, text.position, &ax, &ay) ||
            !projectPoint(camera, width, height, text.position + text.direction * text.height, &dx,
                          &dy) ||
            !projectPoint(camera, width, height, text.position + text.up * text.height, &ux, &uy)) {
            continue;
        }
        // Pixeles por unidad de fuente a lo largo de la linea base y hacia arriba.
        double bx = (dx - ax) / kCapUnits, by = (dy - ay) / kCapUnits;
        const double upx = (ux - ax) / kCapUnits, upy = (uy - ay) / kCapUnits;
        const double capPixels = std::hypot(upx, upy) * kCapUnits;
        if (capPixels < minPixels || !std::isfinite(capPixels)) continue;

        const std::vector<std::wstring> lines = splitLines(widen(text.text));
        std::vector<double> widths(lines.size(), 0.0);
        double longest = 0.0;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            SIZE extent = {};
            if (!lines[i].empty()) {
                GetTextExtentPoint32W(dc, lines[i].c_str(), static_cast<int>(lines[i].size()), &extent);
            }
            widths[i] = extent.cx;
            longest = std::max(longest, widths[i]);
        }
        if (longest <= 0) continue;

        double stretch = text.widthFactor;
        if (text.fitWidth > 0) stretch = (text.fitWidth / text.height * kCapUnits) / longest;
        bx *= stretch;
        by *= stretch;

        // Descarte rapido de lo que cae fuera de la ventana.
        const double reach = longest * std::hypot(bx, by) +
                             static_cast<double>(lines.size()) * lineSpacing * std::hypot(upx, upy);
        if (ax + reach < 0 || ax - reach > width || ay + reach < 0 || ay - reach > height) continue;

        // Espacio de la fuente: x a lo largo de la linea base, y hacia abajo.
        XFORM xf;
        xf.eM11 = static_cast<FLOAT>(bx);
        xf.eM12 = static_cast<FLOAT>(by);
        xf.eM21 = static_cast<FLOAT>(-upx);
        xf.eM22 = static_cast<FLOAT>(-upy);
        xf.eDx = static_cast<FLOAT>(ax);
        xf.eDy = static_cast<FLOAT>(ay);
        if (!SetWorldTransform(dc, &xf)) continue;

        const double block = kCapUnits + static_cast<double>(lines.size() - 1) * lineSpacing;
        double firstBaseline = 0.0;  // y hacia abajo, respecto al punto de alineacion
        switch (text.valign) {
            case 1: firstBaseline = -descent - static_cast<double>(lines.size() - 1) * lineSpacing; break;
            case 2: firstBaseline = kCapUnits - block * 0.5; break;
            case 3: firstBaseline = kCapUnits; break;
            default: firstBaseline = 0.0; break;
        }
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].empty()) continue;
            const double x = -widths[i] * 0.5 * text.halign;
            const double y = firstBaseline + static_cast<double>(i) * lineSpacing;
            TextOutW(dc, static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)),
                     lines[i].c_str(), static_cast<int>(lines[i].size()));
        }
        // Las medidas del texto siguiente tienen que salir en unidades de la fuente.
        ModifyWorldTransform(dc, nullptr, MWT_IDENTITY);
        ++drawn;
    }

    ModifyWorldTransform(dc, nullptr, MWT_IDENTITY);
    SetTextAlign(dc, oldAlign);
    SetTextColor(dc, oldColor);
    SetBkMode(dc, oldBk);
    SelectObject(dc, oldFont);
    SetGraphicsMode(dc, oldMode);
    DeleteObject(font);
}

}  // namespace stp
