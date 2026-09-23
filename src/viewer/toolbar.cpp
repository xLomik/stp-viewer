#include "toolbar.h"

#include <windowsx.h>
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cmath>

#include "image_view.h"

namespace stp {
namespace {

struct Button {
    int command;      // 0 = separador
    const wchar_t* tip;
};

const Button kButtons[] = {
    {kCommandTool + static_cast<int>(Tool::Navigate), L"Navegar (Esc)"},
    {0, nullptr},
    {kCommandTool + static_cast<int>(Tool::Distance), L"Medir distancia (M, 1)"},
    {kCommandTool + static_cast<int>(Tool::Radius), L"Medir radio y diámetro (M, 2)"},
    {kCommandTool + static_cast<int>(Tool::Angle), L"Medir ángulo (M, 3)"},
    {kCommandTool + static_cast<int>(Tool::Area), L"Medir área y perímetro (M, 4)"},
    {0, nullptr},
    {kCommandTool + static_cast<int>(Tool::Highlight), L"Resaltador (H)"},
    {kCommandTool + static_cast<int>(Tool::Underline), L"Subrayado (U)"},
    {kCommandTool + static_cast<int>(Tool::Note), L"Nota con flecha (N)"},
    {kCommandTool + static_cast<int>(Tool::Rectangle), L"Rectángulo (R)"},
    {kCommandTool + static_cast<int>(Tool::Ellipse), L"Elipse (E)"},
    {kCommandTool + static_cast<int>(Tool::Cloud), L"Nube de revisión (C)"},
    {kCommandTool + static_cast<int>(Tool::Pen), L"Lápiz (L)"},
    {kCommandColor, L"Color (Q)"},
    {0, nullptr},
    {kCommandList, L"Lista de marcas (F2)"},
    {kCommandSave, L"Guardar marcas (Ctrl+S)"},
    {kCommandExport, L"Exportar PNG o PDF (Ctrl+E)"},
};
constexpr int kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);
const wchar_t* kClass = L"StpToolbar";

Gdiplus::Color argb(std::uint32_t c, BYTE a = 255) {
    return Gdiplus::Color(a, static_cast<BYTE>(c >> 16), static_cast<BYTE>(c >> 8), static_cast<BYTE>(c));
}

// Iconos de 24x24 unidades, escalados a la caja.
void drawIcon(Gdiplus::Graphics& g, int command, const Gdiplus::RectF& box, std::uint32_t color) {
    const Gdiplus::REAL s = box.Width / 24.0f;
    auto P = [&](float x, float y) { return Gdiplus::PointF(box.X + x * s, box.Y + y * s); };
    Gdiplus::Pen ink(Gdiplus::Color(255, 40, 48, 56), 1.8f * s);
    ink.SetStartCap(Gdiplus::LineCapRound);
    ink.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::SolidBrush solid(Gdiplus::Color(255, 40, 48, 56));
    const int tool = command - kCommandTool;
    if (command == kCommandColor) {
        Gdiplus::SolidBrush swatch(argb(color));
        g.FillEllipse(&swatch, box.X + 5 * s, box.Y + 5 * s, 14 * s, 14 * s);
        g.DrawEllipse(&ink, box.X + 5 * s, box.Y + 5 * s, 14 * s, 14 * s);
        return;
    }
    switch (command) {
        case kCommandList:
            for (int i = 0; i < 3; ++i) g.DrawLine(&ink, P(5, 7 + 5.0f * i), P(19, 7 + 5.0f * i));
            return;
        case kCommandSave:
            g.DrawRectangle(&ink, box.X + 5 * s, box.Y + 5 * s, 14 * s, 14 * s);
            g.DrawRectangle(&ink, box.X + 8 * s, box.Y + 5 * s, 8 * s, 5 * s);
            return;
        case kCommandExport:
            g.DrawLine(&ink, P(12, 16), P(12, 5));
            g.DrawLine(&ink, P(8, 9), P(12, 5));
            g.DrawLine(&ink, P(16, 9), P(12, 5));
            g.DrawLine(&ink, P(5, 14), P(5, 19));
            g.DrawLine(&ink, P(5, 19), P(19, 19));
            g.DrawLine(&ink, P(19, 19), P(19, 14));
            return;
        default:
            break;
    }
    switch (static_cast<Tool>(tool)) {
        case Tool::Navigate: {
            const Gdiplus::PointF arrow[] = {P(7, 4), P(7, 19), P(11, 15), P(14, 21), P(16, 20), P(13, 14), P(18, 14)};
            g.FillPolygon(&solid, arrow, 7);
            break;
        }
        case Tool::Distance:
            g.DrawLine(&ink, P(4, 12), P(20, 12));
            g.DrawLine(&ink, P(4, 8), P(4, 16));
            g.DrawLine(&ink, P(20, 8), P(20, 16));
            break;
        case Tool::Radius:
            g.DrawEllipse(&ink, box.X + 4 * s, box.Y + 4 * s, 16 * s, 16 * s);
            g.DrawLine(&ink, P(12, 12), P(18, 7));
            break;
        case Tool::Angle:
            g.DrawLine(&ink, P(5, 19), P(20, 19));
            g.DrawLine(&ink, P(5, 19), P(16, 6));
            g.DrawArc(&ink, box.X - 3 * s, box.Y + 11 * s, 16 * s, 16 * s, -50.0f, 50.0f);
            break;
        case Tool::Area: {
            Gdiplus::SolidBrush fill(Gdiplus::Color(120, 255, 140, 26));
            g.FillRectangle(&fill, box.X + 5 * s, box.Y + 5 * s, 14 * s, 14 * s);
            g.DrawRectangle(&ink, box.X + 5 * s, box.Y + 5 * s, 14 * s, 14 * s);
            break;
        }
        case Tool::Highlight: {
            Gdiplus::Pen marker(argb(kHighlightYellow, 170), 6 * s);
            marker.SetStartCap(Gdiplus::LineCapRound);
            marker.SetEndCap(Gdiplus::LineCapRound);
            g.DrawLine(&marker, P(5, 15), P(19, 9));
            break;
        }
        case Tool::Underline: {
            // Sin Segoe UI (wine) GDI+ no sustituye la fuente: se usa la sans-serif generica.
            Gdiplus::Font segoe(L"Segoe UI", 13 * s, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::Font generic(Gdiplus::FontFamily::GenericSansSerif(), 13 * s, Gdiplus::FontStyleBold,
                                  Gdiplus::UnitPixel);
            g.DrawString(L"U", 1, segoe.IsAvailable() ? &segoe : &generic, P(7, 1), &solid);
            Gdiplus::Pen red(argb(kMarkRed), 2 * s);
            g.DrawLine(&red, P(5, 20), P(19, 20));
            break;
        }
        case Tool::Note:
            g.DrawRectangle(&ink, box.X + 9 * s, box.Y + 4 * s, 11 * s, 8 * s);
            g.DrawLine(&ink, P(9, 12), P(4, 20));
            break;
        case Tool::Rectangle: g.DrawRectangle(&ink, box.X + 4 * s, box.Y + 6 * s, 16 * s, 12 * s); break;
        case Tool::Ellipse: g.DrawEllipse(&ink, box.X + 4 * s, box.Y + 6 * s, 16 * s, 12 * s); break;
        case Tool::Cloud:
            for (int i = 0; i < 3; ++i) g.DrawArc(&ink, box.X + (4 + 5.5f * i) * s, box.Y + 8 * s, 6 * s, 6 * s, 180.0f, 180.0f);
            for (int i = 0; i < 3; ++i) g.DrawArc(&ink, box.X + (4 + 5.5f * i) * s, box.Y + 12 * s, 6 * s, 6 * s, 0.0f, 180.0f);
            break;
        case Tool::Pen: {
            const Gdiplus::PointF wave[] = {P(4, 16), P(8, 9), P(12, 16), P(16, 9), P(20, 14)};
            g.DrawCurve(&ink, wave, 5);
            break;
        }
    }
}

}  // namespace

bool Toolbar::create(HINSTANCE instance, HWND parent) {
    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    if (!GetClassInfoExW(instance, kClass, &cls)) {
        cls.lpfnWndProc = &Toolbar::proc;
        cls.hInstance = instance;
        cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        cls.lpszClassName = kClass;
        RegisterClassExW(&cls);
    }
    m_hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent, nullptr, instance, this);
    if (!m_hwnd) return false;
    HDC dc = GetDC(m_hwnd);
    m_dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(m_hwnd, dc);

    INITCOMMONCONTROLSEX init = {sizeof(init), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&init);
    m_tips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT,
                             CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, m_hwnd, nullptr, instance, nullptr);
    for (int i = 0; i < kButtonCount; ++i) {
        if (!kButtons[i].command) continue;
        TOOLINFOW info = {};
        info.cbSize = TTTOOLINFOW_V2_SIZE;
        info.uFlags = TTF_SUBCLASS;
        info.hwnd = m_hwnd;
        info.uId = static_cast<UINT_PTR>(i);
        info.rect = {0, buttonTop(i), width(), buttonTop(i) + buttonSize()};
        info.lpszText = const_cast<wchar_t*>(kButtons[i].tip);
        SendMessageW(m_tips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    }
    return true;
}

int Toolbar::buttonSize() const { return MulDiv(36, m_dpi, 96); }
int Toolbar::width() const { return MulDiv(44, m_dpi, 96); }

int Toolbar::buttonTop(int index) const {
    int y = MulDiv(6, m_dpi, 96);
    for (int i = 0; i < index; ++i) y += kButtons[i].command ? buttonSize() : MulDiv(10, m_dpi, 96);
    return y;
}

int Toolbar::buttonAt(int y) const {
    for (int i = 0; i < kButtonCount; ++i) {
        if (kButtons[i].command && y >= buttonTop(i) && y < buttonTop(i) + buttonSize()) return i;
    }
    return -1;
}

void Toolbar::setState(Tool tool, std::uint32_t color) {
    m_tool = tool;
    m_color = color;
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void Toolbar::paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(m_hwnd, &ps);
    if (ensureGdiplus()) {
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const Gdiplus::REAL pad = static_cast<Gdiplus::REAL>(MulDiv(6, m_dpi, 96));
        const Gdiplus::REAL left = (width() - buttonSize()) / 2.0f;
        for (int i = 0; i < kButtonCount; ++i) {
            const Gdiplus::REAL top = static_cast<Gdiplus::REAL>(buttonTop(i));
            if (!kButtons[i].command) {
                Gdiplus::Pen line(Gdiplus::Color(255, 190, 196, 204), 1.0f);
                const Gdiplus::REAL y = top + MulDiv(5, m_dpi, 96);
                g.DrawLine(&line, left, y, left + buttonSize(), y);
                continue;
            }
            const bool pressed = kButtons[i].command == kCommandTool + static_cast<int>(m_tool);
            if (pressed || i == m_hot) {
                Gdiplus::SolidBrush back(pressed ? Gdiplus::Color(255, 205, 222, 247) : Gdiplus::Color(255, 229, 233, 238));
                g.FillRectangle(&back, left, top, static_cast<Gdiplus::REAL>(buttonSize()), static_cast<Gdiplus::REAL>(buttonSize()));
            }
            const Gdiplus::RectF box(left + pad, top + pad, buttonSize() - 2 * pad, buttonSize() - 2 * pad);
            drawIcon(g, kButtons[i].command, box, m_color);
        }
    }
    EndPaint(m_hwnd, &ps);
}

LRESULT CALLBACK Toolbar::proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<Toolbar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg) {
        case WM_PAINT:
            self->paint();
            return 0;
        case WM_MOUSEMOVE: {
            const int hot = self->buttonAt(GET_Y_LPARAM(lparam));
            if (hot != self->m_hot) {
                self->m_hot = hot;
                InvalidateRect(hwnd, nullptr, TRUE);
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&track);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            self->m_hot = -1;
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_LBUTTONDOWN: {
            const int index = self->buttonAt(GET_Y_LPARAM(lparam));
            if (index >= 0) PostMessageW(GetParent(hwnd), WM_COMMAND, static_cast<WPARAM>(kButtons[index].command), 0);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

}  // namespace stp
