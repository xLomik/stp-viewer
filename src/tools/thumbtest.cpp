// Ejercita el manejador de miniaturas por la misma ruta COM que usa el
// Explorador de Windows y guarda el resultado como BMP.
//
//     thumbtest StepThumbnail.dll pieza.stp salida.bmp 256
#include <windows.h>
#include <objbase.h>
#include <shlwapi.h>
#include <thumbcache.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

const CLSID kClsid = {0x90D4532D, 0xA5D0, 0x49A7, {0xB1, 0x15, 0x80, 0x0A, 0xD0, 0x04, 0x26, 0x93}};
const IID kIID_IThumbnailProvider = {
    0xE357FCCD, 0xA995, 0x4576, {0xB0, 0x1F, 0x23, 0x46, 0x30, 0x15, 0x4E, 0x96}};
const IID kIID_IInitializeWithStream = {
    0xB824B49D, 0x22AC, 0x4161, {0xAC, 0x8A, 0x99, 0x16, 0xE8, 0xFA, 0x3F, 0x7F}};

typedef HRESULT(STDAPICALLTYPE* GetClassObjectFn)(REFCLSID, REFIID, void**);

std::wstring widen(const char* text) {
    const int size = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, -1, &out[0], size);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

bool saveBitmap(HBITMAP bitmap, const char* path) {
    BITMAP info = {};
    if (!GetObject(bitmap, sizeof(info), &info)) return false;

    const int width = info.bmWidth;
    const int height = std::abs(info.bmHeight);
    std::string pixels(static_cast<size_t>(width) * height * 4, '\0');

    BITMAPINFO request = {};
    request.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    request.bmiHeader.biWidth = width;
    request.bmiHeader.biHeight = -height;
    request.bmiHeader.biPlanes = 1;
    request.bmiHeader.biBitCount = 32;
    request.bmiHeader.biCompression = BI_RGB;

    HDC dc = GetDC(nullptr);
    const int copied = GetDIBits(dc, bitmap, 0, height, &pixels[0], &request, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (copied == 0) return false;

    FILE* file = fopen(path, "wb");
    if (!file) return false;
    const unsigned dataSize = static_cast<unsigned>(pixels.size());
    const unsigned offset = 14 + 40;
    auto u16 = [&](unsigned v) { fputc(v & 0xFF, file); fputc((v >> 8) & 0xFF, file); };
    auto u32 = [&](unsigned v) {
        for (int i = 0; i < 4; ++i) fputc((v >> (8 * i)) & 0xFF, file);
    };
    fputc('B', file);
    fputc('M', file);
    u32(offset + dataSize);
    u16(0);
    u16(0);
    u32(offset);
    u32(40);
    u32(static_cast<unsigned>(width));
    u32(static_cast<unsigned>(height));
    u16(1);
    u16(32);
    u32(0);
    u32(dataSize);
    u32(2835);
    u32(2835);
    u32(0);
    u32(0);
    for (int y = height - 1; y >= 0; --y) {
        fwrite(pixels.data() + static_cast<size_t>(y) * width * 4, 4, width, file);
    }
    fclose(file);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("uso: thumbtest <dll> <archivo.stp> <salida.bmp> [tamano]\n");
        return 2;
    }
    const UINT size = argc > 4 ? static_cast<UINT>(std::atoi(argv[4])) : 256;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HMODULE dll = LoadLibraryW(widen(argv[1]).c_str());
    if (!dll) {
        std::printf("error: no se pudo cargar la DLL (%lu)\n", GetLastError());
        return 1;
    }
    auto getClassObject =
        reinterpret_cast<GetClassObjectFn>(GetProcAddress(dll, "DllGetClassObject"));
    if (!getClassObject) {
        std::printf("error: falta DllGetClassObject\n");
        return 1;
    }

    IClassFactory* factory = nullptr;
    HRESULT hr = getClassObject(kClsid, IID_IClassFactory, reinterpret_cast<void**>(&factory));
    if (FAILED(hr)) {
        std::printf("error: DllGetClassObject 0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }

    IInitializeWithStream* init = nullptr;
    hr = factory->CreateInstance(nullptr, kIID_IInitializeWithStream,
                                 reinterpret_cast<void**>(&init));
    factory->Release();
    if (FAILED(hr)) {
        std::printf("error: CreateInstance 0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }

    IStream* stream = nullptr;
    hr = SHCreateStreamOnFileW(widen(argv[2]).c_str(), STGM_READ, &stream);
    if (FAILED(hr)) {
        std::printf("error: no se pudo abrir %s (0x%08lX)\n", argv[2],
                    static_cast<unsigned long>(hr));
        return 1;
    }
    hr = init->Initialize(stream, STGM_READ);
    stream->Release();
    if (FAILED(hr)) {
        std::printf("error: Initialize 0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }

    IThumbnailProvider* provider = nullptr;
    hr = init->QueryInterface(kIID_IThumbnailProvider, reinterpret_cast<void**>(&provider));
    init->Release();
    if (FAILED(hr)) {
        std::printf("error: sin IThumbnailProvider 0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }

    HBITMAP bitmap = nullptr;
    WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
    hr = provider->GetThumbnail(size, &bitmap, &alpha);
    provider->Release();
    if (FAILED(hr) || !bitmap) {
        std::printf("error: GetThumbnail 0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }

    if (!saveBitmap(bitmap, argv[3])) {
        std::printf("error: no se pudo guardar %s\n", argv[3]);
        return 1;
    }
    DeleteObject(bitmap);
    std::printf("miniatura %ux%u alpha=%d -> %s\n", size, size, static_cast<int>(alpha), argv[3]);
    CoUninitialize();
    return 0;
}
