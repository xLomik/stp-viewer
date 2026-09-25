// Distribucion de la cinta: que tamano toma cada grupo segun el ancho disponible.
// Sin dependencias de Windows: se prueba en Linux.
#pragma once

#include <vector>

namespace stp {
namespace ui {

enum class GroupSize { Large, Small, Collapsed };

// Anchos en pixeles ya escalados por DPI de cada forma del grupo.
struct RibbonGroupSpec {
    int large = 0;      // botones grandes con texto debajo
    int small = 0;      // iconos pequenos con texto al lado, en columnas
    int collapsed = 0;  // un solo boton que despliega el grupo
};

// Reduce de derecha a izquierda: primero cada grupo a Small (del ultimo al
// primero), despues a Collapsed. narrow (ventana < 640 px): nunca Large.
std::vector<GroupSize> layoutRibbon(const std::vector<RibbonGroupSpec>& groups, int available, int gap,
                                    bool narrow);
int ribbonWidth(const std::vector<RibbonGroupSpec>& groups, const std::vector<GroupSize>& sizes, int gap);

}  // namespace ui
}  // namespace stp
