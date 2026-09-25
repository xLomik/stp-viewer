// Barra de estado: coordenadas del cursor, OSNAP / ORTO / POLAR, 2D/3D, unidades,
// zoom y mensajes. Clic alterna; clic derecho abre el menu de modos o angulos.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "../../engine/measure.h"
#include "tooltip.h"

namespace stp {
namespace ui {

class StatusBar {
public:
    struct State {
        std::wstring coords;  // "X 170.000   Y 35.000 mm" o vacio
        bool snap = true, ortho = false, polar = false;
        unsigned snapModes = kSnapDefaultModes;
        double polarStep = 45;
        bool hasModel = false;
        bool plan2d = false;
        std::wstring units;
        int zoomPercent = 0;
        std::wstring message;
        bool operator==(const State& o) const {
            return coords == o.coords && snap == o.snap && ortho == o.ortho && polar == o.polar &&
                   snapModes == o.snapModes && polarStep == o.polarStep && hasModel == o.hasModel &&
                   plan2d == o.plan2d && units == o.units && zoomPercent == o.zoomPercent && message == o.message;
        }
    };

    bool create(HINSTANCE instance, HWND parent);  // WM_COMMAND al padre
    HWND hwnd() const { return m_hwnd; }
    int height() const { return MulDiv(26, m_dpi, 96); }
    void setDpi(int dpi);
    void setState(const State& state);

private:
    enum Pill { kSnap, kOrtho, kPolar, kPills };
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT handle(UINT msg, WPARAM wparam, LPARAM lparam);
    void paint();
    int pillAt(int x, int y) const;
    int scaled(int v) const { return MulDiv(v, m_dpi, 96); }

    HWND m_hwnd = nullptr;
    HWND m_parent = nullptr;
    Tooltip m_tooltip;
    int m_dpi = 96;
    State m_state;
    RECT m_pills[kPills] = {};
    int m_hot = -1;
    int m_pressed = -1;
    bool m_tracking = false;
};

}  // namespace ui
}  // namespace stp
