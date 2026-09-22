// Punto unico de entrada: elige el lector segun la extension o el contenido.
#include <algorithm>
#include <cstring>

#include "formats.h"

namespace stp {
namespace {

bool contains(const char* data, std::size_t length, const char* needle) {
    const std::size_t needleLength = std::strlen(needle);
    if (length < needleLength) return false;
    const std::size_t limit = std::min<std::size_t>(length, 4096);
    for (std::size_t i = 0; i + needleLength <= limit; ++i) {
        if (std::memcmp(data + i, needle, needleLength) == 0) return true;
    }
    return false;
}

bool extensionIs(const std::string& extension, std::initializer_list<const char*> options) {
    for (const char* option : options) {
        if (extension == option) return true;
    }
    return false;
}

}  // namespace

Format detectFormat(const std::string& extension, const char* data, std::size_t length) {
    if (extensionIs(extension, {".stp", ".step", ".p21"})) return Format::Step;
    if (extensionIs(extension, {".igs", ".iges"})) return Format::Iges;
    if (extensionIs(extension, {".dxf"})) return Format::Dxf;
    if (extensionIs(extension, {".stl"})) return Format::Stl;
    if (extensionIs(extension, {".obj"})) return Format::Obj;
    if (extensionIs(extension, {".ply"})) return Format::Ply;
    if (extensionIs(extension, {".dwg", ".prt", ".sldprt", ".sldasm", ".ipt", ".iam", ".catpart",
                                ".catproduct", ".asm", ".par", ".psm", ".x_t", ".x_b", ".sat",
                                ".3dm", ".f3d"})) {
        return Format::Preview;
    }

    if (!data || length < 16) return Format::Unknown;
    if (contains(data, length, "ISO-10303-21")) return Format::Step;
    if (length > 80 && data[72] == 'S') return Format::Iges;  // primer registro de la seccion S
    if (contains(data, length, "SECTION") && contains(data, length, "HEADER")) return Format::Dxf;
    if (contains(data, length, "solid") && contains(data, length, "facet")) return Format::Stl;
    if (std::memcmp(data, "ply", 3) == 0) return Format::Ply;
    if (contains(data, length, "AC10") || contains(data, length, "AC24")) return Format::Preview;
    return Format::Unknown;
}

bool loadModel(const char* data, std::size_t length, const std::string& extension, Mesh* mesh,
               std::string* error, LoadStats* stats, double quality, int budgetMs) {
    if (!data || length == 0) {
        if (error) *error = "archivo vacio";
        return false;
    }

    switch (detectFormat(extension, data, length)) {
        case Format::Step:
            return loadStepMemory(data, length, mesh, error, stats, quality, budgetMs);
        case Format::Iges:
            return loadIges(data, length, mesh, error, stats, quality, budgetMs);
        case Format::Dxf:
            return loadDxf(data, length, mesh, error, stats, budgetMs);
        case Format::Stl:
            return loadStl(data, length, mesh, error);
        case Format::Obj:
            return loadObj(data, length, mesh, error);
        case Format::Ply:
            return loadPly(data, length, mesh, error);
        case Format::Preview:
            if (error) *error = "formato propietario: solo se puede mostrar su vista previa";
            return false;
        case Format::Unknown:
        default:
            if (error) *error = "formato no reconocido";
            return false;
    }
}

}  // namespace stp
