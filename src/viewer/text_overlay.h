// Textos de los planos (TEXT, MTEXT, cotas) dibujados con GDI encima del
// render. Lo usan la vista interactiva y la miniatura.
#pragma once

#include <windows.h>

#include "../engine/mesh.h"
#include "../render/renderer.h"

namespace stp {

// Dibuja cada texto proyectado con la camara. Los que quedan con menos de
// minPixels de alto no se dibujan: serian una mancha ilegible.
void drawMeshTexts(HDC dc, const Mesh& mesh, const Camera& camera, int width, int height,
                   COLORREF color, double minPixels = 3.0);

}  // namespace stp
