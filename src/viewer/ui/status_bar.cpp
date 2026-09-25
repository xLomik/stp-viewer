#include "status_bar.h"

#include <windowsx.h>

#include <algorithm>

#include "commands.h"
#include "popup_menu.h"
#include "theme.h"

namespace stp {
namespace ui {
namespace {
constexpr wchar_t kClass[] = L"StpViewerStatusBar";
const wchar_t* const kPillText[] = {L"OSNAP", L"ORTO", L"POLAR"};
const int kPillCommand[] = {kCmdSnapToggle, kCmdOrtho, kCmdPolar};
}  // namespace

bool StatusBar::create(HINSTANCE instance, HWND parent) {
    m_parent = parent;
    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = &StatusBar::proc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.lpszClassName = kClass;
    RegisterClassExW(&cls);
    m_hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 100, height(), parent, nullptr,
                             instance, this);
    if (m_hwnd) m_tooltip.create(instance, m_hwnd);
    return m_hwnd != nullptr;
}

void StatusBar::setDpi(int dpi) {
    m_dpi = dpi;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void StatusBar::setState(const State& state) {
    if (state == m_state) return;
    m_state = state;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void StatusBar::paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(m_hwnd, &ps);
    RECT client;
    GetClientRect(m_hwnd, &client);
    paintBuffered(dc, client, [&](Gdiplus::Graphics& g) {
        const float w = static_cast<float>(client.right), h = static_cast<float>(client.bottom);
        Gdiplus::SolidBrush back(color(kPanel));
        g.FillRectangle(&back, 0.0f, 0.0f, w, h);
        Gdiplus::Pen border(color(kBorder), 1.0f);
        g.DrawLine(&border, 0.0f, 0.5f, w, 0.5f);
        const auto f = font(9, m_dpi);
        const auto mono = font(9, m_dpi);
        const auto bold = font(8, m_dpi, Gdiplus::FontStyleBold);
        const float pad = static_cast<float>(scaled(10));

        // Coordenadas con ancho fijo: no bailan al mover el raton.
        // En ventanas angostas las coordenadas ceden espacio.
        const float coordsWidth = std::min(measureText(g, L"X -00000.000   Y -00000.000   Z -00000.000 mm", *mono).Width, w * 0.28f);
        drawText(g, m_state.coords, *mono, Gdiplus::RectF(pad, 1, coordsWidth, h - 1), kTextDim);
        float x = pad + coordsWidth + pad;
        g.DrawLine(&border, x - pad / 2, static_cast<float>(scaled(6)), x - pad / 2, h - scaled(6));

        const bool on[kPills] = {m_state.snap, m_state.ortho, m_state.polar};
        for (int i = 0; i < kPills; ++i) {
            const float pw = measureText(g, kPillText[i], *bold).Width + scaled(16);
            const Gdiplus::RectF pill(x, static_cast<float>(scaled(4)), pw, h - scaled(8));
            m_pills[i] = RECT{static_cast<LONG>(pill.X), 0, static_cast<LONG>(pill.X + pill.Width), client.bottom};
            if (on[i]) fillRound(g, pill, pill.Height / 2, i == m_hot ? blend(kAccent, 0xFFFFFFFF, 0.12) : kAccent);
            else if (i == m_hot) fillRound(g, pill, pill.Height / 2, kHover);
            drawText(g, kPillText[i], *bold, pill, on[i] ? 0xFFFFFFFF : kTextDim, 1);
            x += pw + scaled(4);
        }

        // A la derecha: zoom, unidades, 2D/3D; el mensaje ocupa lo que sobra.
        float right = w - pad;
        auto rightText = [&](const std::wstring& text) {
            if (text.empty()) return;
            const float tw = measureText(g, text, *f).Width;
            if (right - tw < x + pad) return;  // no cabe: se omite
            drawText(g, text, *f, Gdiplus::RectF(right - tw, 1, tw + 2, h - 1), kTextDim, 2);
            right -= tw + pad;
            g.DrawLine(&border, right + pad / 2, static_cast<float>(scaled(6)), right + pad / 2, h - scaled(6));
        };
        if (m_state.hasModel) {
            if (m_state.zoomPercent > 0) rightText(L"Zoom " + std::to_wstring(m_state.zoomPercent) + L" %");
            rightText(m_state.units);
            rightText(m_state.plan2d ? L"2D" : L"3D");
        }
        drawText(g, m_state.message, *f, Gdiplus::RectF(x + pad, 1, std::max(0.0f, right - x - pad), h - 1), kText);
    });
    EndPaint(m_hwnd, &ps);
}

int StatusBar::pillAt(int x, int y) const {
    for (int i = 0; i < kPills; ++i) {
        if (x >= m_pills[i].left && x < m_pills[i].right && y >= 0) return i;
    }
    return -1;
}

LRESULT StatusBar::handle(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_PAINT: paint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: InvalidateRect(m_hwnd, nullptr, FALSE); return 0;
        case WM_MOUSEMOVE: {
            if (!m_tracking) {
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, m_hwnd, 0};
                m_tracking = TrackMouseEvent(&track) != FALSE;
            }
            const int hot = pillAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            if (hot != m_hot) {
                m_hot = hot;
                InvalidateRect(m_hwnd, nullptr, FALSE);
                m_tooltip.hide();
                if (hot >= 0) {
                    POINT anchor = {m_pills[hot].left, -scaled(64)};
                    ClientToScreen(m_hwnd, &anchor);
                    const CommandInfo* info = commandInfo(kPillCommand[hot]);
                    if (info) {
                        m_tooltip.show(info->label, info->shortcut ? info->shortcut : L"",
                                       std::wstring(info->help) + L" Clic derecho: opciones.", anchor, m_dpi);
                    }
                }
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            m_tracking = false;
            m_hot = -1;
            m_tooltip.hide();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            const int pill = pillAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            if (pill >= 0) SendMessageW(m_parent, WM_COMMAND, MAKEWPARAM(kPillCommand[pill], 0), reinterpret_cast<LPARAM>(m_hwnd));
            return 0;
        }
        case WM_RBUTTONUP: {
            const int pill = pillAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            if (pill < 0) return 0;
            m_tooltip.hide();
            std::vector<MenuItem> items;
            auto add = [&](int command, bool checked) {
                const CommandInfo* info = commandInfo(command);
                MenuItem item;
                item.id = command;
                item.text = info ? info->label : L"";
                item.shortcut = info && info->shortcut ? info->shortcut : L"";
                item.icon = info ? info->icon : Icon::None;
                item.checked = checked;
                items.push_back(item);
            };
            if (pill == kSnap) {
                add(kCmdSnapToggle, m_state.snap);
                items.push_back(MenuItem{0, L"", L"", Icon::None, false, true, true});
                for (int bit = 0; bit < 9; ++bit) add(kCmdSnapMode + bit, (m_state.snapModes >> bit) & 1u);
            } else if (pill == kOrtho) {
                add(kCmdOrtho, m_state.ortho);
            } else {
                add(kCmdPolar, m_state.polar);
                items.push_back(MenuItem{0, L"", L"", Icon::None, false, true, true});
                for (int step : {15, 30, 45, 90}) add(kCmdPolarStep + step, static_cast<int>(m_state.polarStep) == step);
            }
            POINT anchor = {m_pills[pill].left, 0};
            ClientToScreen(m_hwnd, &anchor);
            anchor.y -= static_cast<LONG>(items.size()) * scaled(28);
            const int chosen = showPopupMenu(m_hwnd, anchor, items, m_dpi);
            if (chosen) SendMessageW(m_parent, WM_COMMAND, MAKEWPARAM(chosen, 0), reinterpret_cast<LPARAM>(m_hwnd));
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(m_hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK StatusBar::proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<StatusBar*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }
    auto* self = reinterpret_cast<StatusBar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->handle(msg, wparam, lparam) : DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace ui
}  // namespace stp
