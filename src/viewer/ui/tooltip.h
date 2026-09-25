// Tooltip enriquecido con el tema: titulo, atajo a la derecha y una linea de ayuda.
#pragma once

#include <windows.h>

#include <string>

#include "commands.h"

namespace stp {
namespace ui {

class Tooltip {
public:
    ~Tooltip();
    bool create(HINSTANCE instance, HWND owner);
    // anchor: esquina de arriba a la izquierda deseada, en pantalla. Se corre para
    // quedar dentro del monitor.
    void show(const CommandInfo& info, POINT anchor, int dpi);
    void show(const std::wstring& title, const std::wstring& shortcut, const std::wstring& help, POINT anchor, int dpi);
    void hide();
    bool visible() const { return m_hwnd && IsWindowVisible(m_hwnd); }

private:
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void paint();

    HWND m_hwnd = nullptr;
    int m_dpi = 96;
    std::wstring m_title, m_shortcut, m_help;
};

}  // namespace ui
}  // namespace stp
