// Panel lateral del visor: arbol de marcas por vista arriba y propiedades de la
// marca elegida (o del modelo) abajo, con divisor y borde izquierdo ajustables.
#pragma once

#include <windows.h>

#include <set>
#include <string>
#include <vector>

namespace stp {
class SceneView;
namespace ui {

// El panel avisa al padre con este mensaje cuando el usuario cambia su ancho.
constexpr UINT kPanelResized = WM_APP + 41;

class SidePanel {
public:
    bool create(HINSTANCE instance, HWND parent);
    HWND hwnd() const { return m_hwnd; }
    void setDpi(int dpi);
    void attach(SceneView* view) { m_view = view; }
    void refresh();
    int preferredWidth() const { return m_width; }  // a 96 DPI
    void setPreferredWidth(int width);
    // Confirma el texto de nota que se esta escribiendo (antes de guardar o abrir otro).
    void commitEdit() {
        if (m_edit && GetFocus() == m_edit) commitNote();
    }

private:
    struct Row {
        int code;        // id de marca, -id de vista, 0 = nodo "Modelo"
        int depth;
        std::wstring text;
        int kind;        // MarkKind o -1 para un nodo
        int view;        // vista del nodo
        bool open;
    };
    struct Prop {
        std::wstring name, value;
    };

    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK editProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT handle(UINT msg, WPARAM wparam, LPARAM lparam);
    void rebuild();
    void paint();
    void layoutEdit();
    void commitNote();
    int rowHeight() const { return scaled(24); }
    int headerHeight() const { return scaled(30); }
    int splitY() const;
    int scaled(int v) const { return MulDiv(v, m_dpi, 96); }
    RECT listRect() const;
    RECT propsRect() const;

    HWND m_hwnd = nullptr;
    HWND m_parent = nullptr;
    HWND m_edit = nullptr;
    WNDPROC m_editDefault = nullptr;
    HFONT m_editFont = nullptr;
    HBRUSH m_editBrush = nullptr;
    SceneView* m_view = nullptr;
    int m_dpi = 96;
    int m_width = 280;
    double m_split = 0.5;
    int m_scroll = 0;
    int m_hot = -1;
    int m_editingNote = -1;
    enum class Drag { None, Width, Split } m_drag = Drag::None;
    std::vector<Row> m_rows;
    std::set<int> m_closed;  // vistas plegadas
    std::vector<Prop> m_props;
    std::vector<RECT> m_swatches;
    bool m_tracking = false;
};

}  // namespace ui
}  // namespace stp
