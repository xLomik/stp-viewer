// Lista de archivos recientes: el mas nuevo arriba, sin repetidos, hasta 10.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace stp {
namespace ui {

// Rutas de Windows: sin distinguir mayusculas y con '/' igual a '\\'.
bool samePath(const std::wstring& a, const std::wstring& b);

class RecentFiles {
public:
    static constexpr std::size_t kLimit = 10;
    void add(const std::wstring& path);
    bool remove(const std::wstring& path);
    const std::vector<std::wstring>& items() const { return m_items; }
    // Carga una lista guardada: ignora vacias y repetidas, conserva el orden.
    void setItems(const std::vector<std::wstring>& items);

private:
    std::vector<std::wstring> m_items;
};

}  // namespace ui
}  // namespace stp
