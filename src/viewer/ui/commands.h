// Comandos del visor: identificador, icono, texto, atajo y ayuda. La cinta, los
// menus, la barra de estado y los tooltips salen de esta tabla.
#pragma once

#include <cstdint>
#include <string>

#include "icons.h"

namespace stp {
namespace ui {

enum Command : int {
    kCmdNone = 0,
    kCmdOpen = 100, kCmdRecent, kCmdSaveMarks, kCmdExportPdf, kCmdExportPng, kCmdAbout,
    kCmdNavigate = 200, kCmdFit, kCmdViews, kCmdPlan2d,
    kCmdViewFront, kCmdViewBack, kCmdViewLeft, kCmdViewRight, kCmdViewTop, kCmdViewBottom, kCmdViewIso,
    kCmdTool = 300,  // + static_cast<int>(Tool)
    kCmdSnapToggle = 400,
    kCmdSnapMode = 401,  // + indice del bit (0..8)
    kCmdOrtho = 420, kCmdPolar, kCmdPolarMenu,
    kCmdPolarStep = 430,  // + grados (15, 30, 45, 90)
    kCmdColor = 500, kCmdColorMenu, kCmdUndo, kCmdRedo, kCmdDelete,
    kCmdColorPick = 510,  // + indice de la paleta de marcas
    kCmdShaded = 600, kCmdWireframe, kCmdEdges, kCmdPerspective, kCmdPanel, kCmdStatusBar, kCmdViewCube,
    kCmdRibbonToggle = 700,
    kCmdRecentFirst = 800,  // + indice en la lista de recientes
};

struct CommandInfo {
    int id;
    Icon icon;
    const wchar_t* label;     // texto del boton
    const wchar_t* shortcut;  // "Ctrl+O", "F8"... o nullptr
    const wchar_t* help;      // una linea para el tooltip
};

const CommandInfo* commandInfo(int id);  // nullptr si no existe

// Estado que la cinta, los menus y la barra de estado consultan al pintar.
struct CommandState {
    bool enabled = true;
    bool checked = false;
    std::wstring label;          // texto que reemplaza al de la tabla ("Paso 45\u00b0")
    std::uint32_t swatch = 0;    // != 0: el boton muestra este color (ARGB)
};

}  // namespace ui
}  // namespace stp
