#include "export.h"

#include <objidl.h>
#include <gdiplus.h>

#include <memory>

#include "image_view.h"

namespace stp {
namespace {

bool encoderFor(const wchar_t* mime, CLSID* clsid) {
    UINT count = 0, size = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &size) != Gdiplus::Ok || size == 0) return false;
    std::unique_ptr<BYTE[]> buffer(new BYTE[size]);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.get());
    if (Gdiplus::GetImageEncoders(count, size, codecs) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(codecs[i].MimeType, mime) == 0) {
            *clsid = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

}  // namespace

bool savePng(HBITMAP bitmap, const std::wstring& path) {
    CLSID png;
    if (!ensureGdiplus() || !encoderFor(L"image/png", &png)) return false;
    Gdiplus::Bitmap image(bitmap, nullptr);
    return image.Save(path.c_str(), &png, nullptr) == Gdiplus::Ok;
}

bool encodeJpeg(HBITMAP bitmap, ULONG quality, std::vector<std::uint8_t>* out) {
    CLSID jpeg;
    if (!ensureGdiplus() || !encoderFor(L"image/jpeg", &jpeg)) return false;
    Gdiplus::Bitmap image(bitmap, nullptr);
    Gdiplus::EncoderParameters params;
    params.Count = 1;
    params.Parameter[0].Guid = Gdiplus::EncoderQuality;
    params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Value = &quality;
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK) return false;
    bool ok = image.Save(stream, &jpeg, &params) == Gdiplus::Ok;
    if (ok) {
        HGLOBAL memory = nullptr;
        ok = GetHGlobalFromStream(stream, &memory) == S_OK;
        STATSTG stat = {};
        ok = ok && stream->Stat(&stat, STATFLAG_NONAME) == S_OK;
        const void* data = ok ? GlobalLock(memory) : nullptr;
        if (data) {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            out->assign(bytes, bytes + stat.cbSize.QuadPart);
            GlobalUnlock(memory);
        } else {
            ok = false;
        }
    }
    stream->Release();
    return ok;
}

}  // namespace stp
