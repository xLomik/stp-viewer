#include "start_page.h"

#include <windowsx.h>

#include <algorithm>

#include "commands.h"
#include "theme.h"

namespace stp {
namespace ui {
namespace {
constexpr wchar_t kClass[] = L"StpViewerStartPage";
}  // namespace

bool StartPage::create(HINSTANCE instance, HWND parent) {
    m_parent = parent;
    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = &StartPage::proc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.lpszClassName = kClass;
    RegisterClassExW(&cls);
    m_hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 100, 100, parent, nullptr, instance, this);
    return m_hwnd != nullptr;
}

void StartPage::setDpi(int dpi) {
    m_dpi = dpi;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void StartPage::setRecent(const std::vector<std::wstring>& items) {
    m_recent = items;
    m_missing.clear();
    for (const std::wstring& path : items) m_missing.push_back(GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void StartPage::paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(m_hwnd, &ps);
    RECT client;
    GetClientRect(m_hwnd, &client);
    m_rows.clear();
    paintBuffered(dc, client, [&](Gdiplus::Graphics& g) {
        const float w = static_cast<float>(client.right), h = static_cast<float>(client.bottom);
        Gdiplus::SolidBrush back(color(kBackground));
        g.FillRectangle(&back, 0.0f, 0.0f, w, h);

        const float column = std::min(w - scaled(48), static_cast<float>(scaled(520)));
        const float left = (w - column) / 2;
        const float rows = static_cast<float>(std::max<std::size_t>(m_recent.size(), 1));
        const float contentHeight = scaled(64 + 16 + 34 + 24 + 22 + 30 + 36 + 34 + 28) + rows * scaled(44);
        float y = std::max(static_cast<float>(scaled(24)), (h - contentHeight) / 2);

        // Logo: el cubo de la aplicacion.
        const float logo = static_cast<float>(scaled(64));
        fillRound(g, Gdiplus::RectF(left, y, logo, logo), static_cast<float>(scaled(14)), kPanel);
        drawIcon(g, Icon::View3d, Gdiplus::RectF(left + scaled(12), y + scaled(12), logo - scaled(24), logo - scaled(24)), kAccent);
        const auto title = font(20, m_dpi, Gdiplus::FontStyleBold);
        const auto subtitle = font(10, m_dpi);
        drawText(g, L"Visor STP", *title, Gdiplus::RectF(left + logo + scaled(18), y + scaled(2), column, static_cast<float>(scaled(38))), kText);
        drawText(g, L"STEP  ·  IGES  ·  DXF  ·  STL  ·  OBJ  ·  PLY", *subtitle,
                 Gdiplus::RectF(left + logo + scaled(18), y + scaled(38), column, static_cast<float>(scaled(22))), kTextDim);
        y += logo + scaled(34);

        // Boton principal.
        const auto button = font(10, m_dpi, Gdiplus::FontStyleBold);
        const float bw = static_cast<float>(scaled(170)), bh = static_cast<float>(scaled(38));
        m_open = RECT{static_cast<LONG>(left), static_cast<LONG>(y), static_cast<LONG>(left + bw), static_cast<LONG>(y + bh)};
        fillRound(g, Gdiplus::RectF(left, y, bw, bh), 5, m_hot == -1 ? blend(kAccent, 0xFFFFFFFF, 0.12) : kAccent);
        const float ic = static_cast<float>(scaled(18));
        drawIcon(g, Icon::Open, Gdiplus::RectF(left + scaled(16), y + (bh - ic) / 2, ic, ic), 0xFFFFFFFF);
        drawText(g, L"Abrir archivo", *button, Gdiplus::RectF(left + scaled(42), y, bw - scaled(48), bh), 0xFFFFFFFF);
        const auto small = font(9, m_dpi);
        drawText(g, L"Ctrl+O", *small, Gdiplus::RectF(left + bw + scaled(12), y, static_cast<float>(scaled(120)), bh), kTextDim);
        y += bh + scaled(36);

        // Recientes.
        const auto section = font(9, m_dpi, Gdiplus::FontStyleBold);
        drawText(g, L"RECIENTES", *section, Gdiplus::RectF(left, y, column, static_cast<float>(scaled(22))), kTextDim);
        y += scaled(28);
        Gdiplus::Pen line(color(kBorder), 1.0f);
        g.DrawLine(&line, left, y - scaled(4), left + column, y - scaled(4));
        if (m_recent.empty()) {
            drawText(g, L"Todavía no hay archivos recientes.", *small,
                     Gdiplus::RectF(left, y, column, static_cast<float>(scaled(36))), kTextDim);
            y += scaled(44);
        }
        const auto name = font(10, m_dpi);
        for (std::size_t i = 0; i < m_recent.size(); ++i) {
            const std::wstring& path = m_recent[i];
            const bool missing = i < m_missing.size() && m_missing[i];
            const float rh = static_cast<float>(scaled(44));
            const Gdiplus::RectF row(left - scaled(8), y, column + scaled(16), rh);
            m_rows.push_back(RECT{static_cast<LONG>(row.X), static_cast<LONG>(row.Y), static_cast<LONG>(row.X + row.Width),
                                  static_cast<LONG>(row.Y + row.Height)});
            if (m_hot == static_cast<int>(i)) fillRound(g, row, 4, kHover);
            const std::size_t slash = path.find_last_of(L"\\/");
            const std::wstring file = slash == std::wstring::npos ? path : path.substr(slash + 1);
            const std::wstring folder = slash == std::wstring::npos ? L"" : path.substr(0, slash);
            drawIcon(g, Icon::File, Gdiplus::RectF(left, y + (rh - ic) / 2, ic, ic), missing ? kBorder : kTextDim);
            const float tx = left + ic + scaled(12);
            drawText(g, file, *name, Gdiplus::RectF(tx, y + scaled(3), column - ic - scaled(12), rh / 2), missing ? kTextDim : kText);
            drawText(g, missing ? folder + L"  (no se encuentra)" : folder, *small,
                     Gdiplus::RectF(tx, y + rh / 2 - scaled(1), column - ic - scaled(12), rh / 2 - scaled(4)), kTextDim);
            y += rh;
        }
        y += scaled(22);
        drawText(g, L"o arrastra un archivo a esta ventana", *small, Gdiplus::RectF(left, y, column, static_cast<float>(scaled(24))),
                 kTextDim);
    });
    EndPaint(m_hwnd, &ps);
}

LRESULT StartPage::handle(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_PAINT: paint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: InvalidateRect(m_hwnd, nullptr, FALSE); return 0;
        case WM_MOUSEMOVE: {
            if (!m_tracking) {
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, m_hwnd, 0};
                m_tracking = TrackMouseEvent(&track) != FALSE;
            }
            const POINT p = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            int hot = PtInRect(&m_open, p) ? -1 : -2;
            for (std::size_t i = 0; i < m_rows.size(); ++i) {
                if (PtInRect(&m_rows[i], p)) hot = static_cast<int>(i);
            }
            if (hot != m_hot) {
                m_hot = hot;
                SetCursor(LoadCursor(nullptr, hot >= -1 ? IDC_HAND : IDC_ARROW));
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lparam) == HTCLIENT && m_hot >= -1) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        case WM_MOUSELEAVE:
            m_tracking = false;
            m_hot = -2;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
            m_pressed = m_hot;
            return 0;
        case WM_LBUTTONUP:
            if (m_pressed != m_hot) return 0;  // la pulsacion empezo en otro lado
            m_pressed = -2;
            if (m_hot == -1) PostMessageW(m_parent, WM_COMMAND, MAKEWPARAM(kCmdOpen, 0), 0);
            else if (m_hot >= 0) PostMessageW(m_parent, WM_COMMAND, MAKEWPARAM(kCmdRecentFirst + m_hot, 0), 0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(m_hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK StartPage::proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<StartPage*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }
    auto* self = reinterpret_cast<StartPage*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->handle(msg, wparam, lparam) : DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace ui
}  // namespace stp
