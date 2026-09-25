// Cubo de vistas: que cara, arista o esquina esta bajo el cursor y a que
// orientacion de camara lleva. Sin dependencias de Windows.
#pragma once

#include "../render/renderer.h"

namespace stp {
namespace ui {

// Cada componente es -1, 0 o 1: una no nula = cara, dos = arista, tres = esquina.
struct CubeRegion {
    int x = 0, y = 0, z = 0;
    bool valid() const { return x != 0 || y != 0 || z != 0; }
    bool operator==(const CubeRegion& o) const { return x == o.x && y == o.y && z == o.z; }
};

// (dx, dy): pixel medido desde el centro del cubo, y hacia abajo. halfSize: mitad
// de la arista en pixeles. El cubo se ve con la misma orientacion que la camara.
CubeRegion cubeRegionAt(const Camera& camera, double dx, double dy, double halfSize);
// Orientacion (yaw, pitch) de una camara que mira la region desde afuera; el
// pitch se limita como la orbita (89 grados).
void cubeOrientation(const CubeRegion& region, double* yaw, double* pitch);
// "Frente" (-Y), "Atr\u00e1s" (+Y), "Derecha" (+X), "Izquierda" (-X),
// "Superior" (+Z), "Inferior" (-Z), en UTF-8; nullptr si no es una cara.
const char* cubeFaceName(int x, int y, int z);
// Giro suave (t de 0 a 1) por el camino corto del yaw.
void interpolateOrientation(double yaw0, double pitch0, double yaw1, double pitch1, double t, double* yaw,
                            double* pitch);

}  // namespace ui
}  // namespace stp
