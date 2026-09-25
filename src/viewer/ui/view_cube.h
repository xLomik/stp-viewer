// Cubo de vistas (arriba a la derecha de la vista 3D) y brujula de los planos 2D.
#pragma once

#include <windows.h>
#include <gdiplus.h>

#include "../../engine/planar.h"
#include "../../render/renderer.h"
#include "../../ui/view_cube_math.h"

namespace stp {
namespace ui {

class ViewCube {
public:
    enum class Hit { None, Region, Home };

    // Cuadrado que ocupa arriba a la derecha de una vista de width x height.
    RECT bounds(int width, int height, int dpi, bool compact) const;
    // plan: plano 2D de frente (se dibuja la brujula en lugar del cubo).
    void draw(Gdiplus::Graphics& g, const Camera& camera, bool plan, const RECT& bounds, int dpi, bool light) const;
    Hit hitTest(const Camera& camera, bool plan, const RECT& bounds, POINT p, CubeRegion* region) const;
    // Devuelve true si cambio (hay que repintar).
    bool setHover(Hit hit, const CubeRegion& region);

private:
    Hit m_hover = Hit::None;
    CubeRegion m_region;
};

}  // namespace ui
}  // namespace stp
