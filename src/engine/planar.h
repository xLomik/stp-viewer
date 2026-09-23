// Detecta si un modelo es un plano 2D (todo en un mismo plano, aunque este
// girado) y da un sistema de ejes para mirarlo de frente.
#pragma once

#include "geom.h"
#include "mesh.h"

namespace stp {

struct PlanarInfo {
    bool planar = false;
    // Ejes de pantalla: u a la derecha, v hacia arriba, normal hacia el
    // observador (u x v = normal). Planos horizontales se ven desde arriba con
    // Y hacia arriba; planos verticales, con Z hacia arriba.
    Vec3 normal{0, 0, 1};
    Vec3 u{1, 0, 0};
    Vec3 v{0, 1, 0};
    Vec3 center;          // centro del rectangulo que ocupa el dibujo en el plano
    double width = 0.0;   // extension a lo largo de u
    double height = 0.0;  // extension a lo largo de v
};

// Considera vertices, aristas y el rectangulo de cada texto. Es 2D si ningun punto se
// separa del plano ajustado mas de un 0,1 % del tamano del modelo.
PlanarInfo detectPlanar(const Mesh& mesh);

}  // namespace stp
