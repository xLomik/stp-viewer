// Pantalla de inicio (sin archivo abierto): abrir, recientes y soltar un archivo.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace stp {
namespace ui {

class StartPage {
public:
    // Envia al padre WM_COMMAND(kCmdOpen) o WM_COMMAND(kCmdRecentFirst + i).
    bool create(HINSTANCE instance, HWND parent);
    HWND hwnd() const { return m_hwnd; }
    void setDpi(int dpi);
    void setRecent(const std::vector<std::wstring>& items);

private:
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT handle(UINT msg, WPARAM wparam, LPARAM lparam);
    void paint();
    int scaled(int v) const { return MulDiv(v, m_dpi, 96); }

    HWND m_hwnd = nullptr;
    HWND m_parent = nullptr;
    int m_dpi = 96;
    std::vector<std::wstring> m_recent;
    std::vector<bool> m_missing;  // se mira al cargar la lista, no en cada repintado
    RECT m_open = {};
    std::vector<RECT> m_rows;
    int m_hot = -2;  // -1 = boton Abrir, >= 0 = reciente
    int m_pressed = -2;
    bool m_tracking = false;
};

}  // namespace ui
}  // namespace stp
