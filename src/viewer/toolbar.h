// Barra vertical de herramientas del visor: iconos dibujados por codigo, sin
// archivos de imagen, con tooltip y atajo.
#pragma once

#include <windows.h>

#include <cstdint>

#include "markup_tools.h"

namespace stp {

constexpr int kCommandTool = 1000;  // + static_cast<int>(Tool)
constexpr int kCommandColor = 1100;
constexpr int kCommandList = 1101;
constexpr int kCommandSave = 1102;
constexpr int kCommandExport = 1103;

class Toolbar {
public:
    bool create(HINSTANCE instance, HWND parent);
    HWND hwnd() const { return m_hwnd; }
    int width() const;
    void setState(Tool tool, std::uint32_t color);

private:
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void paint();
    int buttonAt(int y) const;
    int buttonTop(int index) const;
    int buttonSize() const;

    HWND m_hwnd = nullptr;
    HWND m_tips = nullptr;
    int m_dpi = 96;
    Tool m_tool = Tool::Navigate;
    std::uint32_t m_color = kMarkRed;
    int m_hot = -1;
};

}  // namespace stp
