// Decodifica la imagen de vista previa que llevan dentro los formatos
// propietarios (.dwg, .sldprt, .prt...) usando GDI+, que ya viene en Windows.
#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

namespace stp {

// Devuelve un HBITMAP de 32 bits (o nullptr). El llamante lo libera.
HBITMAP decodePreviewImage(const std::vector<std::uint8_t>& bytes, int* width, int* height);

// Copia la imagen escalada y centrada dentro de un mapa de bits cuadrado de
// lado size, con fondo transparente: justo lo que espera el Explorador.
HBITMAP fitPreviewToSquare(HBITMAP source, int sourceWidth, int sourceHeight, int size);

// Arranca GDI+ una vez por proceso. Devuelve false si no esta disponible.
bool ensureGdiplus();

}  // namespace stp
