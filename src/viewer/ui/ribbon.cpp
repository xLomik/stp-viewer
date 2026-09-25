#include "ribbon.h"

#include <windowsx.h>

#include <algorithm>

#include "popup_menu.h"
#include "theme.h"

namespace stp {
namespace ui {
namespace {

constexpr wchar_t kClass[] = L"StpViewerRibbon";
constexpr UINT_PTR kAnimTimer = 1;
constexpr UINT_PTR kTipTimer = 2;
constexpr UINT_PTR kPeekTimer = 3;
const wchar_t* const kTabNames[] = {L"Archivo", L"Inicio", L"Medir", L"Marcar", L"Vista"};

int tool(int index) { return kCmdTool + index; }  // indice de Tool

}  // namespace

bool Ribbon::create(HINSTANCE instance, HWND parent, StateProvider state) {
    m_parent = parent;
    m_state = std::move(state);
    using K = Kind;
    m_tabs = {
        {{L"Archivo", {{kCmdOpen, K::Button, false}, {kCmdRecent, K::Button, true}}},
         {L"Marcas", {{kCmdSaveMarks, K::Button, false}}},
         {L"Exportar", {{kCmdExportPdf, K::Button, false}, {kCmdExportPng, K::Button, false}}},
         {L"Ayuda", {{kCmdAbout, K::Button, false}}}},
        {{L"Navegar", {{kCmdNavigate, K::Button, false}, {kCmdFit, K::Button, false}}},
         {L"Vistas", {{kCmdViews, K::Button, true}, {kCmdPlan2d, K::Button, false}}}},
        {{L"Medir", {{tool(1), K::Button, false}, {tool(2), K::Button, false}, {tool(3), K::Button, false},
                     {tool(4), K::Button, false}}},
         {L"Enganche", {{kCmdSnapToggle, K::Button, false}, {kCmdSnapMode + 0, K::Check, false},
                        {kCmdSnapMode + 1, K::Check, false}, {kCmdSnapMode + 2, K::Check, false},
                        {kCmdSnapMode + 3, K::Check, false}, {kCmdSnapMode + 4, K::Check, false},
                        {kCmdSnapMode + 5, K::Check, false}, {kCmdSnapMode + 6, K::Check, false},
                        {kCmdSnapMode + 7, K::Check, false}, {kCmdSnapMode + 8, K::Check, false}}},
         {L"Restricciones", {{kCmdOrtho, K::Button, false}, {kCmdPolar, K::Button, false}, {kCmdPolarMenu, K::Small, true}}}},
        {{L"Dibujar", {{tool(5), K::Button, false}, {tool(6), K::Button, false}, {tool(7), K::Button, false},
                       {tool(8), K::Button, false}, {tool(9), K::Button, false}, {tool(10), K::Button, false},
                       {tool(11), K::Button, false}}},
         {L"Color", {{kCmdColorMenu, K::Button, true}}},
         {L"Editar", {{kCmdUndo, K::Button, false}, {kCmdRedo, K::Button, false}, {kCmdDelete, K::Button, false}}}},
        {{L"Estilo", {{kCmdShaded, K::Button, false}, {kCmdWireframe, K::Button, false}, {kCmdEdges, K::Button, false},
                      {kCmdPerspective, K::Button, false}}},
         {L"Mostrar", {{kCmdPanel, K::Button, false}, {kCmdStatusBar, K::Button, false}, {kCmdViewCube, K::Button, false}}}},
    };

    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    cls.style = CS_DBLCLKS;
    cls.lpfnWndProc = &Ribbon::proc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.lpszClassName = kClass;
    RegisterClassExW(&cls);
    m_hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 100, height(), parent,
                             nullptr, instance, this);
    if (!m_hwnd) return false;
    m_tooltip.create(instance, m_hwnd);
    return true;
}

int Ribbon::height() const { return tabHeight() + (m_collapsed ? 0 : bodyHeight()) + 1; }

void Ribbon::setDpi(int dpi) {
    m_dpi = dpi;
    layout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Ribbon::setTab(int tab) {
    if (tab < 0 || tab >= static_cast<int>(m_tabs.size())) return;
    m_tab = tab;
    m_hot = -1;
    layout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Ribbon::setCollapsed(bool collapsed) {
    m_collapsed = collapsed;
    m_peek = false;
    if (m_hwnd) KillTimer(m_hwnd, kPeekTimer);
    layout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Ribbon::refresh() {
    layout();  // los textos pueden cambiar de ancho ("Paso 45")
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

std::wstring Ribbon::labelOf(int command, const CommandState& state) const {
    if (!state.label.empty()) return state.label;
    const CommandInfo* info = commandInfo(command);
    return info ? info->label : L"";
}

// Coloca los grupos de la pestana activa segun el ancho disponible.
void Ribbon::layout() {
    m_hots.clear();
    m_groupRects.clear();
    m_tabRects.clear();
    if (!m_hwnd) return;
    RECT client;
    GetClientRect(m_hwnd, &client);
    HDC dc = GetDC(m_hwnd);
    Gdiplus::Graphics g(dc);
    const auto tabFont = font(10, m_dpi);
    const auto f = font(9, m_dpi);
    const auto small = font(8, m_dpi);

    int x = scaled(8);
    for (const wchar_t* name : kTabNames) {
        const int w = static_cast<int>(measureText(g, name, *tabFont).Width) + scaled(24);
        m_tabRects.push_back(RECT{x, 0, x + w, tabHeight()});
        x += w;
    }

    const std::vector<Group>& list = groups();
    const int pad = scaled(6);
    const int largeMin = scaled(52), largeMax = scaled(78);
    const int row = scaled(24);
    auto textWidth = [&](int command) {
        CommandState state = m_state ? m_state(command) : CommandState();
        return static_cast<int>(measureText(g, labelOf(command, state), *f).Width);
    };
    auto largeWidth = [&](const Item& item) {
        // Texto en dos renglones como mucho: el ancho se limita.
        return std::max(largeMin, std::min(largeMax, textWidth(item.command) + scaled(14)));
    };
    auto smallWidth = [&](const Item& item) {
        // Casilla (12) + glifo (16) con sus separaciones, o icono (16), mas el texto.
        const int lead = item.kind == Kind::Check ? scaled(4 + 12 + 5 + 16 + 5 + 12) : scaled(4 + 16 + 6 + 12);
        return lead + textWidth(item.command) + (item.dropdown ? scaled(14) : 0);
    };
    // Columnas de 3 renglones para una lista de items chicos.
    auto columnsWidth = [&](const std::vector<const Item*>& items) {
        int total = 0;
        for (std::size_t i = 0; i < items.size(); i += 3) {
            int w = 0;
            for (std::size_t k = i; k < items.size() && k < i + 3; ++k) w = std::max(w, smallWidth(*items[k]));
            total += w;
        }
        return total;
    };

    std::vector<RibbonGroupSpec> specs;
    for (const Group& group : list) {
        std::vector<const Item*> smallAlways, all;
        int largeSum = 0;
        for (const Item& item : group.items) {
            all.push_back(&item);
            if (item.kind == Kind::Button) largeSum += largeWidth(item);
            else smallAlways.push_back(&item);
        }
        const int label = static_cast<int>(measureText(g, group.name, *small).Width) + 2 * pad;
        RibbonGroupSpec spec;
        spec.large = std::max(label, 2 * pad + largeSum + columnsWidth(smallAlways));
        spec.small = std::max(label, 2 * pad + columnsWidth(all));
        spec.small = std::min(spec.small, spec.large);
        spec.collapsed = std::max({label, scaled(60), static_cast<int>(measureText(g, group.name, *f).Width) + scaled(22)});
        specs.push_back(spec);
    }
    const int available = client.right - scaled(8);
    m_sizes = layoutRibbon(specs, available, 1, client.right < scaled(640));

    const int top = tabHeight();
    const int labelHeight = scaled(20);
    const int bodyBottom = top + bodyHeight() - labelHeight;
    x = scaled(4);
    for (std::size_t gi = 0; gi < list.size(); ++gi) {
        const Group& group = list[gi];
        const GroupSize size = m_sizes[gi];
        const int width = size == GroupSize::Large ? specs[gi].large
                          : size == GroupSize::Small ? specs[gi].small : specs[gi].collapsed;
        m_groupRects.push_back(RECT{x, top, x + width, top + bodyHeight()});
        int cx = x + pad;
        if (size == GroupSize::Collapsed) {
            m_hots.push_back({RECT{x + pad, top + scaled(4), x + width - pad, bodyBottom}, 0, static_cast<int>(gi), true,
                              Kind::Button, true});
        } else {
            std::vector<const Item*> smalls;
            for (const Item& item : group.items) {
                if (size == GroupSize::Large && item.kind == Kind::Button) {
                    const int w = largeWidth(item);
                    m_hots.push_back({RECT{cx, top + scaled(4), cx + w, bodyBottom}, item.command, -1, item.dropdown,
                                      item.kind, true});
                    cx += w;
                } else {
                    smalls.push_back(&item);
                }
            }
            for (std::size_t i = 0; i < smalls.size(); i += 3) {
                int w = 0;
                for (std::size_t k = i; k < smalls.size() && k < i + 3; ++k) w = std::max(w, smallWidth(*smalls[k]));
                for (std::size_t k = i; k < smalls.size() && k < i + 3; ++k) {
                    const int y = top + scaled(5) + static_cast<int>(k - i) * row;
                    m_hots.push_back({RECT{cx, y, cx + w, y + row}, smalls[k]->command, -1, smalls[k]->dropdown,
                                      smalls[k]->kind, false});
                }
                cx += w;
            }
        }
        x += width + 1;
    }
    ReleaseDC(m_hwnd, dc);
}

void Ribbon::paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(m_hwnd, &ps);
    RECT client;
    GetClientRect(m_hwnd, &client);
    paintBuffered(dc, client, [&](Gdiplus::Graphics& g) {
        const float w = static_cast<float>(client.right);
        Gdiplus::SolidBrush back(color(kBackground));
        g.FillRectangle(&back, 0.0f, 0.0f, w, static_cast<float>(tabHeight()));
        const bool body = !m_collapsed || m_peek;
        if (body) {
            Gdiplus::SolidBrush panel(color(kPanel));
            g.FillRectangle(&panel, 0.0f, static_cast<float>(tabHeight()), w, static_cast<float>(bodyHeight()));
        }
        Gdiplus::Pen border(color(kBorder), 1.0f);
        const float bottom = static_cast<float>(client.bottom) - 0.5f;
        g.DrawLine(&border, 0.0f, bottom, w, bottom);

        // Pestanas.
        const auto tabFont = font(10, m_dpi);
        for (std::size_t i = 0; i < m_tabRects.size(); ++i) {
            const RECT& r = m_tabRects[i];
            const bool active = static_cast<int>(i) == m_tab && body;
            const bool hot = static_cast<int>(i) == m_hotTab;
            const Gdiplus::RectF box(static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right - r.left),
                                     static_cast<float>(r.bottom - r.top));
            if (active) {
                Gdiplus::SolidBrush sel(color(kPanel));
                g.FillRectangle(&sel, box.X, box.Y + scaled(4), box.Width, box.Height - scaled(4));
                Gdiplus::SolidBrush accent(color(kAccent));
                g.FillRectangle(&accent, box.X + scaled(10), box.Y + box.Height - scaled(3), box.Width - scaled(20),
                                static_cast<float>(scaled(2)));
            }
            drawText(g, kTabNames[i], *tabFont, box, active || hot ? kText : kTextDim, 1);
        }
        if (!body) return;

        // Grupos.
        const auto f = font(9, m_dpi);
        const auto small = font(8, m_dpi);
        const std::vector<Group>& list = groups();
        const float labelHeight = static_cast<float>(scaled(20));
        for (std::size_t gi = 0; gi < m_groupRects.size() && gi < list.size(); ++gi) {
            const RECT& r = m_groupRects[gi];
            const Gdiplus::RectF label(static_cast<float>(r.left), static_cast<float>(r.bottom) - labelHeight,
                                       static_cast<float>(r.right - r.left), labelHeight - scaled(3));
            drawText(g, list[gi].name, *small, label, kTextDim, 1);
            const float sx = static_cast<float>(r.right) + 0.5f;
            g.DrawLine(&border, sx, static_cast<float>(r.top + scaled(8)), sx, static_cast<float>(r.bottom - scaled(8)));
        }

        const double fade = m_hotSince ? std::min(1.0, (GetTickCount64() - m_hotSince) / 120.0) : 1.0;
        for (std::size_t i = 0; i < m_hots.size(); ++i) {
            const Hot& h = m_hots[i];
            const Gdiplus::RectF box(static_cast<float>(h.rect.left), static_cast<float>(h.rect.top),
                                     static_cast<float>(h.rect.right - h.rect.left), static_cast<float>(h.rect.bottom - h.rect.top));
            const int command = h.group >= 0 ? list[static_cast<std::size_t>(h.group)].items.front().command : h.command;
            const CommandState state = m_state ? m_state(command) : CommandState();
            const bool hot = static_cast<int>(i) == m_hot && state.enabled;
            const bool pressed = static_cast<int>(i) == m_pressed;
            const bool checked = state.checked && h.group < 0;
            if (checked && h.kind != Kind::Check) {
                fillRound(g, box, 4, blend(kPanel, kAccent, 0.28));
                strokeRound(g, Gdiplus::RectF(box.X + 0.5f, box.Y + 0.5f, box.Width - 1, box.Height - 1), 4,
                            blend(kPanel, kAccent, 0.7), 1.0f);
            }
            if (pressed) fillRound(g, box, 4, kPressed);
            else if (hot) fillRound(g, box, 4, blend(checked ? blend(kPanel, kAccent, 0.28) : kPanel, kHover, fade));
            Gdiplus::ARGB ink = state.enabled ? kText : blend(kPanel, kTextDim, 0.55);
            const CommandInfo* info = commandInfo(command);
            const Icon icon = info ? info->icon : Icon::None;
            const std::wstring text = h.group >= 0 ? std::wstring(list[static_cast<std::size_t>(h.group)].name)
                                                   : labelOf(command, state);
            if (h.large) {
                const float iconSize = static_cast<float>(scaled(28));
                const Gdiplus::RectF iconBox(box.X + (box.Width - iconSize) / 2, box.Y + scaled(6), iconSize, iconSize);
                const Gdiplus::ARGB iconInk = checked && state.enabled ? blend(kText, kAccent, 0.35) : ink;
                if (state.swatch) {
                    Gdiplus::SolidBrush sw{Gdiplus::Color(state.swatch)};
                    g.FillEllipse(&sw, iconBox.X + scaled(3), iconBox.Y + scaled(3), iconSize - scaled(6), iconSize - scaled(6));
                    strokeRound(g, Gdiplus::RectF(iconBox.X + scaled(3), iconBox.Y + scaled(3), iconSize - scaled(6), iconSize - scaled(6)),
                                (iconSize - scaled(6)) / 2, kBorder, 1.0f);
                } else {
                    drawIcon(g, icon, iconBox, iconInk);
                }
                Gdiplus::StringFormat format;
                format.SetAlignment(Gdiplus::StringAlignmentCenter);
                format.SetTrimming(Gdiplus::StringTrimmingEllipsisWord);
                Gdiplus::SolidBrush brush{Gdiplus::Color(ink)};
                const Gdiplus::RectF textBox(box.X + 2, iconBox.Y + iconSize + scaled(4), box.Width - 4,
                                             box.Y + box.Height - (iconBox.Y + iconSize + scaled(4)));
                g.DrawString(text.c_str(), -1, f.get(), textBox, &format, &brush);
                if (h.dropdown) {
                    const float c = static_cast<float>(scaled(9));
                    drawIcon(g, Icon::ChevronDown, Gdiplus::RectF(box.X + box.Width - c - 2, iconBox.Y + iconSize - c, c, c), kTextDim);
                }
            } else {
                const float iconSize = static_cast<float>(scaled(16));
                const Gdiplus::RectF iconBox(box.X + scaled(4), box.Y + (box.Height - iconSize) / 2, iconSize, iconSize);
                if (h.kind == Kind::Check) {
                    // Casilla y glifo del modo.
                    const float c = static_cast<float>(scaled(12));
                    const Gdiplus::RectF check(iconBox.X, box.Y + (box.Height - c) / 2, c, c);
                    if (state.checked) {
                        fillRound(g, check, 2, kAccent);
                        drawIcon(g, Icon::Check, Gdiplus::RectF(check.X + 1, check.Y + 1, c - 2, c - 2), 0xFFFFFFFF);
                    } else {
                        strokeRound(g, check, 2, kTextDim, 1.0f);
                    }
                    drawIcon(g, icon, Gdiplus::RectF(check.X + c + scaled(5), iconBox.Y, iconSize, iconSize),
                             state.checked ? kSnapGreen : kTextDim);
                    drawText(g, text, *f, Gdiplus::RectF(check.X + c + scaled(5) + iconSize + scaled(5), box.Y,
                                                         box.Width, box.Height), ink);
                } else {
                    drawIcon(g, icon, iconBox, ink);
                    drawText(g, text, *f, Gdiplus::RectF(iconBox.X + iconSize + scaled(6), box.Y,
                                                         box.Width - iconSize - scaled(10), box.Height), ink);
                    if (h.dropdown) {
                        const float c = static_cast<float>(scaled(9));
                        drawIcon(g, Icon::ChevronDown, Gdiplus::RectF(box.X + box.Width - c - scaled(4), box.Y + (box.Height - c) / 2, c, c),
                                 kTextDim);
                    }
                }
            }
        }
    });
    EndPaint(m_hwnd, &ps);
}

int Ribbon::tabAt(int x, int y) const {
    for (std::size_t i = 0; i < m_tabRects.size(); ++i) {
        const RECT& r = m_tabRects[i];
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return static_cast<int>(i);
    }
    return -1;
}

int Ribbon::hotAt(int x, int y) const {
    if (m_collapsed && !m_peek) return -1;
    for (std::size_t i = 0; i < m_hots.size(); ++i) {
        const RECT& r = m_hots[i].rect;
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return static_cast<int>(i);
    }
    return -1;
}

void Ribbon::setPeek(bool peek) {
    if (!m_collapsed || peek == m_peek) return;
    m_peek = peek;
    RECT rect;
    GetWindowRect(m_hwnd, &rect);
    MapWindowPoints(nullptr, m_parent, reinterpret_cast<POINT*>(&rect), 2);
    const int h = peek ? tabHeight() + bodyHeight() + 1 : height();
    SetWindowPos(m_hwnd, HWND_TOP, rect.left, rect.top, rect.right - rect.left, h, SWP_NOACTIVATE);
    if (peek) SetTimer(m_hwnd, kPeekTimer, 200, nullptr);
    else KillTimer(m_hwnd, kPeekTimer);
    layout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Ribbon::press(int index) {
    const Hot h = m_hots[static_cast<std::size_t>(index)];
    POINT anchor = {h.rect.left, h.rect.bottom};
    ClientToScreen(m_hwnd, &anchor);
    m_anchor = anchor;
    // El tooltip no debe aparecer encima del menu que se abre.
    KillTimer(m_hwnd, kTipTimer);
    m_tooltip.hide();
    m_hot = -1;
    int command = h.command;
    if (h.group >= 0) {
        // Grupo plegado: sus comandos en un menu.
        std::vector<MenuItem> items;
        for (const Item& item : groups()[static_cast<std::size_t>(h.group)].items) {
            const CommandState state = m_state ? m_state(item.command) : CommandState();
            const CommandInfo* info = commandInfo(item.command);
            MenuItem entry;
            entry.id = item.command;
            entry.text = labelOf(item.command, state);
            entry.shortcut = info && info->shortcut ? info->shortcut : L"";
            entry.icon = info ? info->icon : Icon::None;
            entry.checked = state.checked;
            entry.enabled = state.enabled;
            entry.swatch = state.swatch;
            items.push_back(entry);
        }
        command = showPopupMenu(m_hwnd, anchor, items, m_dpi);
        if (!command) return;
    }
    SendMessageW(m_parent, WM_COMMAND, MAKEWPARAM(command, 0), reinterpret_cast<LPARAM>(m_hwnd));
    // Los comandos con menu (Recientes, Vistas, Color, paso polar) dejan la cinta abierta
    // mientras el padre muestra el menu; el resto la cierra si estaba abierta encima.
    const bool menu = command == kCmdRecent || command == kCmdViews || command == kCmdColorMenu || command == kCmdPolarMenu;
    if (m_peek && !menu) setPeek(false);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

LRESULT Ribbon::handle(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_SIZE:
            m_tooltip.hide();
            layout();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_PAINT:
            paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            if (!m_tracking) {
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, m_hwnd, 0};
                m_tracking = TrackMouseEvent(&track) != FALSE;
            }
            const int x = GET_X_LPARAM(lparam), y = GET_Y_LPARAM(lparam);
            const int hot = hotAt(x, y), tab = tabAt(x, y);
            if (hot != m_hot || tab != m_hotTab) {
                m_hot = hot;
                m_hotTab = tab;
                m_hotSince = GetTickCount64();
                m_tooltip.hide();
                KillTimer(m_hwnd, kTipTimer);
                if (hot >= 0) SetTimer(m_hwnd, kTipTimer, 500, nullptr);
                SetTimer(m_hwnd, kAnimTimer, 15, nullptr);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            m_tracking = false;
            m_hot = m_hotTab = -1;
            m_pressed = -1;
            m_tooltip.hide();
            KillTimer(m_hwnd, kTipTimer);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_TIMER:
            if (wparam == kAnimTimer) {
                if (GetTickCount64() - m_hotSince > 140) KillTimer(m_hwnd, kAnimTimer);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            } else if (wparam == kTipTimer) {
                KillTimer(m_hwnd, kTipTimer);
                if (m_hot >= 0) {
                    const Hot& h = m_hots[static_cast<std::size_t>(m_hot)];
                    POINT anchor = {h.rect.left, h.rect.bottom + scaled(6)};
                    ClientToScreen(m_hwnd, &anchor);
                    if (h.group >= 0) {
                        m_tooltip.show(groups()[static_cast<std::size_t>(h.group)].name, L"", L"Clic para ver los comandos del grupo.",
                                       anchor, m_dpi);
                    } else if (const CommandInfo* info = commandInfo(h.command)) {
                        m_tooltip.show(*info, anchor, m_dpi);
                    }
                }
            } else if (wparam == kPeekTimer) {
                // La cinta abierta sobre la vista se cierra cuando el raton se va.
                POINT p;
                GetCursorPos(&p);
                RECT r;
                GetWindowRect(m_hwnd, &r);
                if (!PtInRect(&r, p) && GetCapture() == nullptr) setPeek(false);
            }
            return 0;
        case WM_LBUTTONDOWN: {
            const int x = GET_X_LPARAM(lparam), y = GET_Y_LPARAM(lparam);
            const int tab = tabAt(x, y);
            if (tab >= 0) {
                if (m_collapsed) {
                    const bool same = tab == m_tab && m_peek;
                    setTab(tab);
                    setPeek(!same);
                } else {
                    setTab(tab);
                }
                SendMessageW(m_parent, WM_COMMAND, MAKEWPARAM(kCmdNone, 1), reinterpret_cast<LPARAM>(m_hwnd));
                return 0;
            }
            const int hot = hotAt(x, y);
            if (hot >= 0) {
                const CommandState state = m_state ? m_state(m_hots[static_cast<std::size_t>(hot)].command) : CommandState();
                if (!state.enabled && m_hots[static_cast<std::size_t>(hot)].group < 0) return 0;
                m_pressed = hot;
                SetCapture(m_hwnd);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (GetCapture() == m_hwnd) ReleaseCapture();
            const int hot = hotAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            const int pressed = m_pressed;
            m_pressed = -1;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            if (hot >= 0 && hot == pressed) press(hot);
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            if (tabAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)) >= 0) {
                setCollapsed(!m_collapsed);
                SendMessageW(m_parent, WM_COMMAND, MAKEWPARAM(kCmdRibbonToggle, 1), reinterpret_cast<LPARAM>(m_hwnd));
                return 0;
            }
            return handle(WM_LBUTTONDOWN, wparam, lparam);
        }
        default:
            break;
    }
    return DefWindowProcW(m_hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK Ribbon::proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* self = static_cast<Ribbon*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }
    auto* self = reinterpret_cast<Ribbon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->handle(msg, wparam, lparam) : DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace ui
}  // namespace stp
