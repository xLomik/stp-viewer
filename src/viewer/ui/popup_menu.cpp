#include "popup_menu.h"

#include <windowsx.h>

#include <algorithm>

#include "theme.h"

namespace stp {
namespace ui {
namespace {

constexpr wchar_t kClass[] = L"StpViewerMenu";

struct MenuState {
    const std::vector<MenuItem>* items = nullptr;
    int dpi = 96;
    int hot = -1;
    int chosen = 0;
    bool done = false;
    int rowHeight = 0;
    int separatorHeight = 0;
};

int rowTop(const MenuState& s, int index) {
    int y = scale(4, s.dpi);
    for (int i = 0; i < index; ++i) y += (*s.items)[static_cast<std::size_t>(i)].separator ? s.separatorHeight : s.rowHeight;
    return y;
}

int rowAt(const MenuState& s, int y) {
    int top = scale(4, s.dpi);
    for (std::size_t i = 0; i < s.items->size(); ++i) {
        const int h = (*s.items)[i].separator ? s.separatorHeight : s.rowHeight;
        if (y >= top && y < top + h) return (*s.items)[i].separator ? -1 : static_cast<int>(i);
        top += h;
    }
    return -1;
}

bool selectable(const MenuState& s, int i) {
    if (i < 0 || i >= static_cast<int>(s.items->size())) return false;
    const MenuItem& item = (*s.items)[static_cast<std::size_t>(i)];
    return !item.separator && item.enabled;
}

void paintMenu(HWND hwnd, MenuState& s) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    paintBuffered(dc, client, [&](Gdiplus::Graphics& g) {
        const float w = static_cast<float>(client.right), h = static_cast<float>(client.bottom);
        Gdiplus::SolidBrush back(color(kPanel));
        g.FillRectangle(&back, 0.0f, 0.0f, w, h);
        Gdiplus::Pen border(color(kBorder), 1.0f);
        g.DrawRectangle(&border, 0.0f, 0.0f, w - 1, h - 1);
        const auto f = font(9, s.dpi);
        const float pad = static_cast<float>(scale(8, s.dpi));
        const float icon = static_cast<float>(scale(16, s.dpi));
        for (std::size_t i = 0; i < s.items->size(); ++i) {
            const MenuItem& item = (*s.items)[i];
            const float top = static_cast<float>(rowTop(s, static_cast<int>(i)));
            if (item.separator) {
                Gdiplus::Pen line(color(kBorder), 1.0f);
                const float y = top + s.separatorHeight / 2.0f;
                g.DrawLine(&line, pad, y, w - pad, y);
                continue;
            }
            const float rh = static_cast<float>(s.rowHeight);
            if (static_cast<int>(i) == s.hot && item.enabled) fillRound(g, Gdiplus::RectF(3, top, w - 6, rh), 3, kHover);
            const Gdiplus::ARGB ink = !item.enabled || item.dim ? kTextDim : kText;
            const Gdiplus::RectF iconBox(pad, top + (rh - icon) / 2, icon, icon);
            if (item.swatch) {
                Gdiplus::SolidBrush sw{Gdiplus::Color(item.swatch)};
                g.FillEllipse(&sw, iconBox.X + 2, iconBox.Y + 2, icon - 4, icon - 4);
                if (item.checked) strokeRound(g, Gdiplus::RectF(iconBox.X, iconBox.Y, icon, icon), icon / 2, kText, 1.5f);
            } else if (item.checked) {
                if (item.icon != Icon::None) fillRound(g, Gdiplus::RectF(iconBox.X - 3, iconBox.Y - 3, icon + 6, icon + 6), 3, kPressed);
                drawIcon(g, item.icon != Icon::None ? item.icon : Icon::Check, iconBox, item.icon != Icon::None ? kAccent : kText);
            } else if (item.icon != Icon::None) {
                drawIcon(g, item.icon, iconBox, ink);
            }
            const float textLeft = pad + icon + pad;
            drawText(g, item.text, *f, Gdiplus::RectF(textLeft, top, w - textLeft - pad, rh), ink);
            if (!item.shortcut.empty()) drawText(g, item.shortcut, *f, Gdiplus::RectF(textLeft, top, w - textLeft - pad, rh), kTextDim, 2);
        }
    });
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK menuProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
    }
    auto* s = reinterpret_cast<MenuState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_PAINT:
            if (s) {
                paintMenu(hwnd, *s);
                return 0;
            }
            break;
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

int showPopupMenu(HWND owner, POINT screen, const std::vector<MenuItem>& items, int dpi) {
    if (items.empty()) return 0;
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = &menuProc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.lpszClassName = kClass;
    cls.style = CS_DROPSHADOW;
    RegisterClassExW(&cls);

    MenuState s;
    s.items = &items;
    s.dpi = dpi;
    s.rowHeight = scale(28, dpi);
    s.separatorHeight = scale(9, dpi);

    // Ancho: el texto mas largo mas el atajo mas largo.
    int textWidth = 0, shortcutWidth = 0;
    {
        HDC dc = GetDC(owner);
        Gdiplus::Graphics g(dc);
        const auto f = font(9, dpi);
        for (const MenuItem& item : items) {
            textWidth = std::max(textWidth, static_cast<int>(measureText(g, item.text, *f).Width));
            if (!item.shortcut.empty()) shortcutWidth = std::max(shortcutWidth, static_cast<int>(measureText(g, item.shortcut, *f).Width));
        }
        ReleaseDC(owner, dc);
    }
    const int width = std::max(scale(180, dpi), scale(8 + 16 + 8, dpi) + textWidth + (shortcutWidth ? scale(24, dpi) + shortcutWidth : 0) + scale(12, dpi));
    const int height = rowTop(s, static_cast<int>(items.size())) + scale(4, dpi);
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint(screen, MONITOR_DEFAULTTONEAREST), &monitor);
    const int x = std::max<int>(monitor.rcWork.left, std::min<int>(screen.x, monitor.rcWork.right - width));
    const int y = screen.y + height > monitor.rcWork.bottom ? std::max<int>(monitor.rcWork.top, screen.y - height) : screen.y;

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kClass, L"", WS_POPUP, x, y, width,
                                height, owner, nullptr, instance, &s);
    if (!hwnd) return 0;
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    SetCapture(hwnd);

    auto move = [&](int step) {
        const int n = static_cast<int>(items.size());
        int i = s.hot;
        for (int k = 0; k < n; ++k) {
            i = (i + step + n) % n;
            if (selectable(s, i)) break;
        }
        s.hot = i;
        InvalidateRect(hwnd, nullptr, FALSE);
    };

    MSG msg;
    while (!s.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        switch (msg.message) {
            case WM_MOUSEMOVE:
                if (msg.hwnd == hwnd) {
                    const int hot = rowAt(s, GET_Y_LPARAM(msg.lParam));
                    if (hot != s.hot) {
                        s.hot = hot;
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    continue;
                }
                break;
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_NCLBUTTONDOWN: {
                POINT p = {GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam)};
                if (msg.hwnd == hwnd && p.x >= 0 && p.y >= 0 && p.x < width && p.y < height) continue;
                s.done = true;  // clic fuera: se cierra sin elegir
                continue;
            }
            case WM_LBUTTONUP:
                if (msg.hwnd == hwnd) {
                    const int i = rowAt(s, GET_Y_LPARAM(msg.lParam));
                    if (selectable(s, i)) {
                        s.chosen = items[static_cast<std::size_t>(i)].id;
                        s.done = true;
                    }
                    continue;
                }
                break;
            case WM_KEYDOWN:
                if (msg.wParam == VK_ESCAPE) s.done = true;
                else if (msg.wParam == VK_DOWN) move(1);
                else if (msg.wParam == VK_UP) move(-1);
                else if (msg.wParam == VK_RETURN && selectable(s, s.hot)) {
                    s.chosen = items[static_cast<std::size_t>(s.hot)].id;
                    s.done = true;
                }
                continue;
            case WM_CHAR:
            case WM_SYSKEYDOWN:
                continue;
            default:
                break;
        }
        if (GetCapture() != hwnd && !s.done) SetCapture(hwnd);
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (msg.message == WM_QUIT) PostQuitMessage(static_cast<int>(msg.wParam));
    ReleaseCapture();
    DestroyWindow(hwnd);
    return s.chosen;
}

}  // namespace ui
}  // namespace stp
