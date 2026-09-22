#include "image_view.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>

namespace stp {
namespace {

// GDI+ se inicializa una vez por proceso y se deja vivo: tanto el anfitrion de
// miniaturas como el visor piden imagenes varias veces.
class GdiPlusSession {
public:
    static bool ensure() {
        static GdiPlusSession session;
        return session.m_ok;
    }

private:
    GdiPlusSession() {
        Gdiplus::GdiplusStartupInput input;
        m_ok = Gdiplus::GdiplusStartup(&m_token, &input, nullptr) == Gdiplus::Ok;
    }
    ~GdiPlusSession() {
        if (m_ok) Gdiplus::GdiplusShutdown(m_token);
    }

    ULONG_PTR m_token = 0;
    bool m_ok = false;
};

}  // namespace

HBITMAP decodePreviewImage(const std::vector<std::uint8_t>& bytes, int* width, int* height) {
    if (bytes.empty() || !GdiPlusSession::ensure()) return nullptr;

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!memory) return nullptr;
    void* target = GlobalLock(memory);
    if (!target) {
        GlobalFree(memory);
        return nullptr;
    }
    memcpy(target, bytes.data(), bytes.size());
    GlobalUnlock(memory);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(memory, TRUE, &stream) != S_OK) {
        GlobalFree(memory);
        return nullptr;
    }

    HBITMAP bitmap = nullptr;
    Gdiplus::Bitmap* image = Gdiplus::Bitmap::FromStream(stream);
    if (image && image->GetLastStatus() == Gdiplus::Ok) {
        if (width) *width = static_cast<int>(image->GetWidth());
        if (height) *height = static_cast<int>(image->GetHeight());
        image->GetHBITMAP(Gdiplus::Color(255, 255, 255), &bitmap);
    }
    delete image;
    stream->Release();
    return bitmap;
}

HBITMAP fitPreviewToSquare(HBITMAP source, int sourceWidth, int sourceHeight, int size) {
    if (!source || sourceWidth <= 0 || sourceHeight <= 0 || size <= 0) return nullptr;

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;  // de arriba abajo
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP target = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!target || !bits) return nullptr;
    memset(bits, 0, static_cast<std::size_t>(size) * size * 4);

    const double scale = std::min(static_cast<double>(size) / sourceWidth,
                                  static_cast<double>(size) / sourceHeight);
    const int drawWidth = std::max(1, static_cast<int>(sourceWidth * scale));
    const int drawHeight = std::max(1, static_cast<int>(sourceHeight * scale));
    const int offsetX = (size - drawWidth) / 2;
    const int offsetY = (size - drawHeight) / 2;

    HDC screen = GetDC(nullptr);
    HDC targetDc = CreateCompatibleDC(screen);
    HDC sourceDc = CreateCompatibleDC(screen);
    HGDIOBJ oldTarget = SelectObject(targetDc, target);
    HGDIOBJ oldSource = SelectObject(sourceDc, source);

    SetStretchBltMode(targetDc, HALFTONE);
    SetBrushOrgEx(targetDc, 0, 0, nullptr);
    StretchBlt(targetDc, offsetX, offsetY, drawWidth, drawHeight, sourceDc, 0, 0, sourceWidth,
               sourceHeight, SRCCOPY);

    SelectObject(targetDc, oldTarget);
    SelectObject(sourceDc, oldSource);
    DeleteDC(targetDc);
    DeleteDC(sourceDc);
    ReleaseDC(nullptr, screen);

    // StretchBlt no escribe el canal alfa: se marca opaco lo dibujado.
    std::uint32_t* pixels = static_cast<std::uint32_t*>(bits);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool inside = x >= offsetX && x < offsetX + drawWidth && y >= offsetY &&
                                y < offsetY + drawHeight;
            std::uint32_t& pixel = pixels[static_cast<std::size_t>(y) * size + x];
            pixel = inside ? (pixel | 0xFF000000u) : 0u;
        }
    }
    return target;
}

}  // namespace stp
