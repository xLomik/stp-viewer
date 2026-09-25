// Configuracion del visor en HKCU\Software\stp-viewer. Si el registro no se puede
// leer o escribir se usan los valores por defecto sin avisar.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "markup_tools.h"

namespace stp {

struct ViewerSettings {
    SnapSettings snap;
    bool ribbonCollapsed = false;
    int ribbonTab = 2;  // Medir
    bool panelVisible = true;
    int panelWidth = 280;  // a 96 DPI
    bool statusVisible = true;
    bool cubeVisible = true;
    RECT window = {0, 0, 0, 0};  // vacio = posicion por defecto
    bool maximized = false;
    std::vector<std::wstring> recent;
};

ViewerSettings loadSettings();
void saveSettings(const ViewerSettings& settings);
// Solo el enganche: lo lee el panel del Explorador (que nunca escribe).
SnapSettings loadSnapSettings();

}  // namespace stp
