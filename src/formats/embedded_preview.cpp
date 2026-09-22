// Formatos propietarios (.dwg, .prt, .sldprt, .ipt, .catpart...): su geometria
// esta en binarios cerrados sin especificacion publica, pero casi todos guardan
// dentro la imagen de vista previa que el CAD genero al grabar. Eso es lo que
// se saca aqui.
#include <cstring>

#include "formats.h"

namespace stp {
namespace {

constexpr std::size_t kScanLimit = 24u * 1024 * 1024;
constexpr std::size_t kMinImageBytes = 1024;

std::uint32_t readUint32LE(const char* p) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(p[0])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(p[1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(p[2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(p[3])) << 24);
}

bool takePng(const char* data, std::size_t length, std::size_t start,
             std::vector<std::uint8_t>* out) {
    static const unsigned char signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (start + 8 > length || std::memcmp(data + start, signature, 8) != 0) return false;

    // Recorrer los bloques hasta IEND para saber donde acaba.
    std::size_t cursor = start + 8;
    while (cursor + 8 <= length) {
        const std::uint32_t chunkSize =
            (static_cast<unsigned char>(data[cursor]) << 24) |
            (static_cast<unsigned char>(data[cursor + 1]) << 16) |
            (static_cast<unsigned char>(data[cursor + 2]) << 8) |
            static_cast<unsigned char>(data[cursor + 3]);
        const bool isEnd = std::memcmp(data + cursor + 4, "IEND", 4) == 0;
        cursor += 12 + static_cast<std::size_t>(chunkSize);
        if (cursor > length) return false;
        if (isEnd) {
            out->assign(data + start, data + cursor);
            return out->size() >= kMinImageBytes;
        }
    }
    return false;
}

bool takeJpeg(const char* data, std::size_t length, std::size_t start,
              std::vector<std::uint8_t>* out) {
    if (start + 4 > length) return false;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(data);
    if (!(p[start] == 0xFF && p[start + 1] == 0xD8 && p[start + 2] == 0xFF)) return false;

    for (std::size_t i = start + 2; i + 1 < length; ++i) {
        if (p[i] == 0xFF && p[i + 1] == 0xD9) {
            out->assign(data + start, data + i + 2);
            return out->size() >= kMinImageBytes;
        }
    }
    return false;
}

bool takeBmp(const char* data, std::size_t length, std::size_t start,
             std::vector<std::uint8_t>* out) {
    if (start + 54 > length) return false;
    if (data[start] != 'B' || data[start + 1] != 'M') return false;
    const std::uint32_t size = readUint32LE(data + start + 2);
    if (size < kMinImageBytes || start + size > length) return false;
    const std::uint32_t headerSize = readUint32LE(data + start + 14);
    if (headerSize != 12 && headerSize != 40 && headerSize != 108 && headerSize != 124) {
        return false;
    }
    out->assign(data + start, data + start + size);
    return true;
}

// Los DWG guardan la vista previa sin cabecera BMP: una BITMAPINFOHEADER suelta
// a la que hay que anteponer el encabezado de archivo para que se pueda leer.
bool takeDwgPreview(const char* data, std::size_t length, std::size_t start,
                    std::vector<std::uint8_t>* out) {
    if (start + 40 > length) return false;
    const std::uint32_t headerSize = readUint32LE(data + start);
    if (headerSize != 40) return false;
    const std::int32_t width = static_cast<std::int32_t>(readUint32LE(data + start + 4));
    const std::int32_t height = static_cast<std::int32_t>(readUint32LE(data + start + 8));
    const std::uint32_t planesAndBits = readUint32LE(data + start + 12);
    const std::uint16_t planes = static_cast<std::uint16_t>(planesAndBits & 0xFFFF);
    const std::uint16_t bits = static_cast<std::uint16_t>(planesAndBits >> 16);
    if (planes != 1 || width <= 8 || height == 0 || std::abs(width) > 8192) return false;
    if (bits != 1 && bits != 4 && bits != 8 && bits != 24 && bits != 32) return false;

    const std::uint32_t declared = readUint32LE(data + start + 20);
    std::uint32_t palette = readUint32LE(data + start + 32) * 4;
    if (bits <= 8 && palette == 0) palette = (1u << bits) * 4;
    const std::int32_t absHeight = height < 0 ? -height : height;
    const std::uint32_t rowBytes = ((static_cast<std::uint32_t>(width) * bits + 31) / 32) * 4;
    const std::uint32_t pixels = declared > 0 ? declared : rowBytes * absHeight;
    const std::size_t total = 40 + palette + pixels;
    if (pixels < kMinImageBytes || start + total > length) return false;

    out->clear();
    out->reserve(14 + total);
    const std::uint32_t fileSize = static_cast<std::uint32_t>(14 + total);
    const std::uint32_t offset = 14 + 40 + palette;
    auto push16 = [&](std::uint16_t v) {
        out->push_back(static_cast<std::uint8_t>(v & 0xFF));
        out->push_back(static_cast<std::uint8_t>(v >> 8));
    };
    auto push32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) out->push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    };
    out->push_back('B');
    out->push_back('M');
    push32(fileSize);
    push16(0);
    push16(0);
    push32(offset);
    out->insert(out->end(), reinterpret_cast<const std::uint8_t*>(data + start),
                reinterpret_cast<const std::uint8_t*>(data + start + total));
    return true;
}

}  // namespace

bool extractEmbeddedPreview(const char* data, std::size_t length,
                            std::vector<std::uint8_t>* image) {
    if (!data || length < 64 || !image) return false;
    const std::size_t limit = std::min(length, kScanLimit);

    std::vector<std::uint8_t> best;
    std::vector<std::uint8_t> candidate;

    for (std::size_t i = 0; i + 8 < limit; ++i) {
        const unsigned char c = static_cast<unsigned char>(data[i]);
        bool found = false;
        if (c == 0x89 && data[i + 1] == 'P') {
            found = takePng(data, length, i, &candidate);
        } else if (c == 0xFF && static_cast<unsigned char>(data[i + 1]) == 0xD8) {
            found = takeJpeg(data, length, i, &candidate);
        } else if (c == 'B' && data[i + 1] == 'M') {
            found = takeBmp(data, length, i, &candidate);
        } else if (c == 0x28 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 0) {
            found = takeDwgPreview(data, length, i, &candidate);
        }
        if (!found) continue;

        // Se queda la mayor: las miniaturas pequenas de icono no sirven.
        if (candidate.size() > best.size()) best.swap(candidate);
        i += candidate.size() > 0 ? candidate.size() - 1 : 0;
        candidate.clear();
    }

    if (best.size() < kMinImageBytes) return false;
    image->swap(best);
    return true;
}

}  // namespace stp
