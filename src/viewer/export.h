// Guardar imagenes con GDI+: PNG para compartir una vista, JPEG para el PDF.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace stp {

bool savePng(HBITMAP bitmap, const std::wstring& path);
bool encodeJpeg(HBITMAP bitmap, ULONG quality, std::vector<std::uint8_t>* out);

}  // namespace stp
