// Que archivos de una carpeta conviene pasar por la cache de miniaturas y en que
// orden. Sin Windows: se prueba en Linux.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace stp {
namespace ui {

// Extensiones con miniatura de stp-viewer, en minusculas y con punto.
const std::vector<std::wstring>& cadExtensions();
bool isCadFile(const std::wstring& name);

struct WarmupFile {
    std::wstring name;
    std::uint64_t size = 0;
    std::int64_t modified = 0;  // cualquier escala creciente (FILETIME)
    bool folder = false;
};

// Archivos CAD no vacios, los mas recientes primero, como mucho limit.
std::vector<std::wstring> warmupOrder(std::vector<WarmupFile> files, std::size_t limit = 2000);

}  // namespace ui
}  // namespace stp
