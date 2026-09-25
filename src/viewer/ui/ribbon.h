// Cinta con pestanas (Archivo, Inicio, Medir, Marcar, Vista): botones grandes con
// icono y texto, grupos rotulados, reduccion por ancho y tooltips.
#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "../../ui/ribbon_layout.h"
#include "commands.h"
#include "tooltip.h"

namespace stp {
namespace ui {

class Ribbon {
public:
    using StateProvider = std::function<CommandState(int command)>;

    // Los comandos llegan al padre como WM_COMMAND(id). Los de menu (Recientes,
    // Vistas, Color, paso polar) tambien: el padre abre el menu en popupAnchor().
    bool create(HINSTANCE instance, HWND parent, StateProvider state);
    HWND hwnd() const { return m_hwnd; }
    int height() const;  // alto que ocupa en el marco (plegada: solo pestanas)
    void setDpi(int dpi);
    void setTab(int tab);  // 0 Archivo, 1 Inicio, 2 Medir, 3 Marcar, 4 Vista
    int tab() const { return m_tab; }
    void setCollapsed(bool collapsed);
    bool collapsed() const { return m_collapsed; }
    void refresh();  // vuelve a pedir el estado de los botones y repinta
    POINT popupAnchor() const { return m_anchor; }  // pantalla, debajo del ultimo boton pulsado

private:
    enum class Kind { Button, Check, Small };
    struct Item {
        int command;
        Kind kind;
        bool dropdown;
    };
    struct Group {
        const wchar_t* name;
        std::vector<Item> items;
    };
    struct Hot {
        RECT rect;
        int command;       // 0 en un grupo plegado
        int group;         // grupo plegado (despliega su menu) o -1
        bool dropdown;
        Kind kind;
        bool large;
    };

    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT handle(UINT msg, WPARAM wparam, LPARAM lparam);
    void layout();
    void paint();
    int tabAt(int x, int y) const;
    int hotAt(int x, int y) const;
    void press(int index);
    void setPeek(bool peek);
    std::wstring labelOf(int command, const CommandState& state) const;
    const std::vector<Group>& groups() const { return m_tabs[static_cast<std::size_t>(m_tab)]; }
    int tabHeight() const { return scaled(30); }
    int bodyHeight() const { return scaled(96); }
    int scaled(int v) const { return MulDiv(v, m_dpi, 96); }

    HWND m_hwnd = nullptr;
    HWND m_parent = nullptr;
    StateProvider m_state;
    Tooltip m_tooltip;
    int m_dpi = 96;
    int m_tab = 2;
    bool m_collapsed = false;
    bool m_peek = false;  // plegada pero abierta encima de la vista
    std::vector<std::vector<Group>> m_tabs;
    std::vector<GroupSize> m_sizes;
    std::vector<RECT> m_groupRects;
    std::vector<Hot> m_hots;
    std::vector<RECT> m_tabRects;
    int m_hot = -1;
    int m_hotTab = -1;
    int m_pressed = -1;
    ULONGLONG m_hotSince = 0;
    bool m_tracking = false;
    POINT m_anchor = {0, 0};
};

}  // namespace ui
}  // namespace stp
