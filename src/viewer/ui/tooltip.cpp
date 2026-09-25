#include "tooltip.h"

#include <algorithm>

#include "theme.h"

namespace stp {
namespace ui {
namespace {
constexpr wchar_t kClass[] = L"StpViewerTooltip";
constexpr int kWidth = 280;  // ancho maximo a 96 DPI
}  // namespace

Tooltip::~Tooltip() { destroy(); }

void Tooltip::destroy() {
    if (m_hwnd) {
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);  // la ventana no vuelve a tocar este objeto
        DestroyWindow(m_hwnd);
    }
    m_hwnd = nullptr;
}

bool Tooltip::create(HINSTANCE instance, HWND owner) {
    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = &Tooltip::proc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.lpszClassName = kClass;
    cls.style = CS_DROPSHADOW;
    RegisterClassExW(&cls);
    destroy();  // una sola ventana por objeto aunque se cree de nuevo
    m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, kClass, L"",
                             WS_POPUP, 0, 0, 10, 10, owner, nullptr, instance, this);
    return m_hwnd != nullptr;
}

void Tooltip::show(const CommandInfo& info, POINT anchor, int dpi) {
    show(info.label, info.shortcut ? info.shortcut : L"", info.help ? info.help : L"", anchor, dpi);
}

void Tooltip::show(const std::wstring& title, const std::wstring& shortcut, const std::wstring& help, POINT anchor,
                   int dpi) {
    if (!m_hwnd) return;
    m_title = title;
    m_shortcut = shortcut;
    m_help = help;
    m_dpi = dpi;
    // Medida: titulo y atajo en una linea, ayuda partida en renglones.
    HDC dc = GetDC(m_hwnd);
    Gdiplus::SizeF titleSize, shortcutSize;
    Gdiplus::RectF helpBox;
    {
        Gdiplus::Graphics g(dc);
        const auto bold = font(9, dpi, Gdiplus::FontStyleBold);
        const auto regular = font(9, dpi);
        titleSize = measureText(g, m_title, *bold);
        shortcutSize = m_shortcut.empty() ? Gdiplus::SizeF(0, 0) : measureText(g, m_shortcut, *regular);
        const Gdiplus::RectF layout(0, 0, static_cast<Gdiplus::REAL>(scale(kWidth - 20, dpi)), 1000);
        if (!m_help.empty()) g.MeasureString(m_help.c_str(), -1, regular.get(), layout, &helpBox);
    }
    ReleaseDC(m_hwnd, dc);
    const int pad = scale(10, dpi);
    const int gap = scale(16, dpi);
    const int width = std::max<int>(static_cast<int>(titleSize.Width + (m_shortcut.empty() ? 0 : gap + shortcutSize.Width)),
                                    static_cast<int>(helpBox.Width)) + 2 * pad + 2;
    const int height = static_cast<int>(titleSize.Height + (m_help.empty() ? 0 : helpBox.Height + scale(4, dpi))) +
                       2 * pad - scale(2, dpi);
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    int x = std::min<int>(anchor.x, monitor.rcWork.right - width);
    int y = anchor.y;
    if (y + height > monitor.rcWork.bottom) y = anchor.y - height - scale(40, dpi);
    x = std::max<int>(x, monitor.rcWork.left);
    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Tooltip::hide() {
    if (m_hwnd && IsWindowVisible(m_hwnd)) ShowWindow(m_hwnd, SW_HIDE);
}

void Tooltip::paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(m_hwnd, &ps);
    RECT client;
    GetClientRect(m_hwnd, &client);
    paintBuffered(dc, client, [&](Gdiplus::Graphics& g) {
        const float w = static_cast<float>(client.right), h = static_cast<float>(client.bottom);
        Gdiplus::SolidBrush back(color(kPanel));
        g.FillRectangle(&back, 0.0f, 0.0f, w, h);
        Gdiplus::Pen border(color(kBorder), 1.0f);
        g.DrawRectangle(&border, 0.0f, 0.0f, w - 1, h - 1);
        const float pad = static_cast<float>(scale(10, m_dpi));
        const auto bold = font(9, m_dpi, Gdiplus::FontStyleBold);
        const auto regular = font(9, m_dpi);
        const float line = bold->GetHeight(&g);
        drawText(g, m_title, *bold, Gdiplus::RectF(pad, pad - 2, w - 2 * pad, line), kText);
        if (!m_shortcut.empty()) drawText(g, m_shortcut, *regular, Gdiplus::RectF(pad, pad - 2, w - 2 * pad, line), kTextDim, 2);
        if (!m_help.empty()) {
            Gdiplus::SolidBrush ink(color(kTextDim));
            const Gdiplus::RectF box(pad, pad - 2 + line + scale(4, m_dpi), w - 2 * pad + 2, h);
            g.DrawString(m_help.c_str(), -1, regular.get(), box, nullptr, &ink);
        }
    });
    EndPaint(m_hwnd, &ps);
}

LRESULT CALLBACK Tooltip::proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<Tooltip*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_PAINT:
            if (self) {
                self->paint();
                return 0;
            }
            break;
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_NCDESTROY:
            if (self) self->m_hwnd = nullptr;
            break;
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace ui
}  // namespace stp
