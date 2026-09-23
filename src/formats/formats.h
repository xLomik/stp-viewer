// Lectores de los formatos 3D y CAD que se pueden abrir sin SDK del fabricante.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../engine/mesh.h"
#include "../engine/planar.h"
#include "../engine/step_model.h"

namespace stp {

enum class Format {
    Unknown,
    Step,   // .stp .step  (ISO 10303-21)
    Iges,   // .igs .iges
    Dxf,    // .dxf
    Stl,    // .stl  (ASCII y binario)
    Obj,    // .obj
    Ply,    // .ply
    Preview  // formato propietario del que solo se puede sacar su vista previa
};

// Deduce el formato por extension y, si no basta, por el contenido.
Format detectFormat(const std::string& extensionLowercase, const char* data, std::size_t length);

// Carga cualquier formato reconocido. extensionLowercase incluye el punto
// (".stp"); puede ir vacia y entonces se deduce del contenido.
bool loadModel(const char* data, std::size_t length, const std::string& extensionLowercase,
               Mesh* mesh, std::string* error, LoadStats* stats = nullptr,
               double quality = 0.0015, int budgetMs = 0);

bool loadStl(const char* data, std::size_t length, Mesh* mesh, std::string* error);
bool loadObj(const char* data, std::size_t length, Mesh* mesh, std::string* error);
bool loadPly(const char* data, std::size_t length, Mesh* mesh, std::string* error);
bool loadDxf(const char* data, std::size_t length, Mesh* mesh, std::string* error,
             LoadStats* stats, int budgetMs);
bool loadIges(const char* data, std::size_t length, Mesh* mesh, std::string* error,
              LoadStats* stats, double quality, int budgetMs);

// Formatos propietarios (.dwg, .prt, .sldprt, .ipt...): no se interpreta la
// geometria, pero casi todos llevan dentro una imagen de vista previa que el
// propio CAD guardo al grabar. Devuelve los bytes de esa imagen.
bool extractEmbeddedPreview(const char* data, std::size_t length,
                            std::vector<std::uint8_t>* image);

// Normales por cara para mallas que no las traen.
void computeFlatNormals(Mesh* mesh);

}  // namespace stp
