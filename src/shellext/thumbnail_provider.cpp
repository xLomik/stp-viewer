#include "shellext.h"

#include <shlwapi.h>

#include <new>
#include <cctype>
#include <string>
#include <vector>

#include "../formats/formats.h"
#include "../viewer/image_view.h"
#include "../render/renderer.h"

const CLSID CLSID_StepThumbnailProvider = {
    0x90D4532D, 0xA5D0, 0x49A7, {0xB1, 0x15, 0x80, 0x0A, 0xD0, 0x04, 0x26, 0x93}};

long g_dllRefCount = 0;

namespace {

// mingw-w64 declares these shell interfaces but does not ship their IIDs in
// libuuid, so they are spelled out here.
const IID kIID_IThumbnailProvider = {
    0xE357FCCD, 0xA995, 0x4576, {0xB0, 0x1F, 0x23, 0x46, 0x30, 0x15, 0x4E, 0x96}};
const IID kIID_IInitializeWithStream = {
    0xB824B49D, 0x22AC, 0x4161, {0xAC, 0x8A, 0x99, 0x16, 0xE8, 0xFA, 0x3F, 0x7F}};

// El Explorador espera la miniatura de forma sincrona: un archivo enorme no
// puede tardar minutos. Los archivos grandes se mallan mas grueso y, pasado el
// plazo, se dibuja lo que haya.
constexpr size_t kMaxThumbnailBytes = 64u * 1024 * 1024;
constexpr int kThumbnailBudgetMs = 6000;

double qualityForSize(size_t bytes) {
    if (bytes > 40u * 1024 * 1024) return 0.006;
    if (bytes > 8u * 1024 * 1024) return 0.004;
    return 0.0025;
}

class StepThumbnailProvider : public IInitializeWithStream, public IThumbnailProvider {
public:
    StepThumbnailProvider() { InterlockedIncrement(&g_dllRefCount); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == kIID_IInitializeWithStream) {
            *ppv = static_cast<IInitializeWithStream*>(this);
        } else if (riid == kIID_IThumbnailProvider) {
            *ppv = static_cast<IThumbnailProvider*>(this);
        } else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    IFACEMETHODIMP_(ULONG) Release() override {
        const LONG left = InterlockedDecrement(&m_refs);
        if (left == 0) delete this;
        return left;
    }

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* stream, DWORD) override {
        if (!stream) return E_INVALIDARG;
        if (!m_data.empty()) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);

        STATSTG stat = {};
        ULONGLONG size = 0;
        if (SUCCEEDED(stream->Stat(&stat, 0))) {
            size = stat.cbSize.QuadPart;
            if (stat.pwcsName) {
                // La extension elige el lector; el Explorador la da en el nombre.
                const std::wstring name = stat.pwcsName;
                CoTaskMemFree(stat.pwcsName);
                const std::size_t dot = name.find_last_of(L'.');
                if (dot != std::wstring::npos) {
                    for (std::size_t i = dot; i < name.size(); ++i) {
                        m_extension.push_back(
                            static_cast<char>(std::tolower(static_cast<int>(name[i]))));
                    }
                }
            }
        }
        if (size > kMaxThumbnailBytes) return E_FAIL;  // el Explorador usa el icono

        std::string buffer;
        if (size > 0) buffer.reserve(static_cast<size_t>(size));

        char chunk[64 * 1024];
        for (;;) {
            ULONG got = 0;
            const HRESULT hr = stream->Read(chunk, sizeof(chunk), &got);
            if (FAILED(hr)) return hr;
            if (got == 0) break;
            buffer.append(chunk, got);
            if (buffer.size() > kMaxThumbnailBytes) return E_FAIL;
        }
        if (buffer.empty()) return E_FAIL;
        m_data.swap(buffer);
        return S_OK;
    }

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override {
        if (!phbmp || !pdwAlpha) return E_POINTER;
        *phbmp = nullptr;
        *pdwAlpha = WTSAT_ARGB;
        if (m_data.empty()) return E_UNEXPECTED;
        if (cx < 16) cx = 16;
        if (cx > 1024) cx = 1024;

        return RenderStepThumbnail(m_data.data(), m_data.size(), cx, phbmp, m_extension);
    }

private:
    ~StepThumbnailProvider() { InterlockedDecrement(&g_dllRefCount); }

    LONG m_refs = 1;
    std::string m_data;
    std::string m_extension;
};

}  // namespace

HRESULT RenderStepThumbnail(const char* data, size_t length, UINT size, HBITMAP* out,
                            const std::string& extension) {
    stp::Mesh mesh;
    std::string error;
    if (!stp::loadModel(data, length, extension, &mesh, &error, nullptr, qualityForSize(length),
                        kThumbnailBudgetMs)) {
        // Formatos cerrados (.dwg, .sldprt, .prt...): se usa la imagen que el
        // propio CAD dejo dentro del archivo.
        std::vector<std::uint8_t> image;
        if (!stp::extractEmbeddedPreview(data, length, &image)) return E_FAIL;

        int width = 0, height = 0;
        HBITMAP decoded = stp::decodePreviewImage(image, &width, &height);
        if (!decoded) return E_FAIL;
        HBITMAP fitted = stp::fitPreviewToSquare(decoded, width, height, static_cast<int>(size));
        DeleteObject(decoded);
        if (!fitted) return E_FAIL;
        *out = fitted;
        return S_OK;
    }
    if (mesh.empty() || !mesh.bounds.valid()) return E_FAIL;

    stp::Camera camera;
    camera.ortho = true;
    camera.fit(mesh.bounds, 1.0);

    stp::RenderStyle style;
    style.transparentBackground = true;
    style.supersample = size >= 256 ? 2 : 3;
    style.edgeWidth = size >= 96 ? 1.1 : 0.9;

    stp::Framebuffer frame;
    stp::renderMesh(mesh, camera, style, static_cast<int>(size), static_cast<int>(size), &frame);

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(size);
    info.bmiHeader.biHeight = -static_cast<LONG>(size);  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) return E_OUTOFMEMORY;

    memcpy(bits, frame.pixels.data(), frame.pixels.size() * sizeof(std::uint32_t));
    *out = bitmap;
    return S_OK;
}

HRESULT CreateStepThumbnailProvider(REFIID riid, void** ppv) {
    StepThumbnailProvider* provider = new (std::nothrow) StepThumbnailProvider();
    if (!provider) return E_OUTOFMEMORY;
    const HRESULT hr = provider->QueryInterface(riid, ppv);
    provider->Release();
    return hr;
}
