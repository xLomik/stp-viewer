// Menu emergente con el tema grafito. Bloquea hasta que se elige algo o se cierra.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "icons.h"

namespace stp {
namespace ui {

struct MenuItem {
    int id = 0;
    std::wstring text;
    std::wstring shortcut;
    Icon icon = Icon::None;
    bool checked = false;
    bool enabled = true;
    bool separator = false;
    bool dim = false;               // p. ej. un reciente que ya no existe
    std::uint32_t swatch = 0;       // != 0: muestra de color en lugar del icono
};

// Devuelve el id elegido o 0. screen: esquina de arriba a la izquierda del menu.
int showPopupMenu(HWND owner, POINT screen, const std::vector<MenuItem>& items, int dpi);

}  // namespace ui
}  // namespace stp
