// Panel de vista previa del Explorador: aloja la misma vista 3D interactiva
// que el visor, dentro de la ventana que entrega prevhost.exe.
#include <windows.h>
#include <shobjidl.h>
#include <shlwapi.h>

#include <new>
#include <string>

#include "../viewer/scene_view.h"
#include "../viewer/settings.h"
#include "shellext.h"

namespace {

// mingw-w64 declara las interfaces pero no trae sus IID en libuuid.
const IID kIID_IPreviewHandler = {
    0x8895B1C6, 0xB41F, 0x4C1C, {0xA5, 0x62, 0x0D, 0x56, 0x42, 0x50, 0x83, 0x6F}};
const IID kIID_IPreviewHandlerVisuals = {
    0x196BF9A5, 0xB346, 0x4EF0, {0xAA, 0x1E, 0x5D, 0xCD, 0xB7, 0x67, 0x68, 0xB1}};
const IID kIID_IObjectWithSite = {
    0xFC4801A3, 0x2BA9, 0x11CF, {0xA2, 0x29, 0x00, 0xAA, 0x00, 0x3D, 0x73, 0x52}};
const IID kIID_IOleWindow = {
    0x00000114, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const IID kIID_IInitializeWithStream = {
    0xB824B49D, 0x22AC, 0x4161, {0xAC, 0x8A, 0x99, 0x16, 0xE8, 0xFA, 0x3F, 0x7F}};
const IID kIID_IPreviewHandlerFrame = {
    0xFEC87AAF, 0x35F9, 0x447A, {0xAD, 0xB7, 0x20, 0x23, 0x44, 0x91, 0x40, 0x1A}};

class StepPreviewHandler : public IPreviewHandler,
                           public IInitializeWithStream,
                           public IObjectWithSite,
                           public IOleWindow,
                           public IPreviewHandlerVisuals {
public:
    StepPreviewHandler() { InterlockedIncrement(&g_dllRefCount); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == kIID_IPreviewHandler) {
            *ppv = static_cast<IPreviewHandler*>(this);
        } else if (riid == kIID_IInitializeWithStream) {
            *ppv = static_cast<IInitializeWithStream*>(this);
        } else if (riid == kIID_IObjectWithSite) {
            *ppv = static_cast<IObjectWithSite*>(this);
        } else if (riid == kIID_IOleWindow) {
            *ppv = static_cast<IOleWindow*>(this);
        } else if (riid == kIID_IPreviewHandlerVisuals) {
            *ppv = static_cast<IPreviewHandlerVisuals*>(this);
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
        if (SUCCEEDED(stream->Stat(&stat, 0))) {
            if (stat.cbSize.QuadPart > 512ull * 1024 * 1024) {
                CoTaskMemFree(stat.pwcsName);
                return E_FAIL;
            }
            if (stat.pwcsName) {
                m_title = stat.pwcsName;
                CoTaskMemFree(stat.pwcsName);
            }
        }

        std::string buffer;
        char chunk[64 * 1024];
        for (;;) {
            ULONG got = 0;
            const HRESULT hr = stream->Read(chunk, sizeof(chunk), &got);
            if (FAILED(hr)) return hr;
            if (got == 0) break;
            buffer.append(chunk, got);
            if (buffer.size() > 512ull * 1024 * 1024) return E_FAIL;
        }
        if (buffer.empty()) return E_FAIL;
        m_data.swap(buffer);
        return S_OK;
    }

    // IPreviewHandler
    IFACEMETHODIMP SetWindow(HWND parent, const RECT* rect) override {
        m_parent = parent;
        if (rect) m_rect = *rect;
        if (m_view.hwnd()) {
            SetParent(m_view.hwnd(), m_parent);
            m_view.setRect(m_rect);
        }
        return S_OK;
    }
    IFACEMETHODIMP SetRect(const RECT* rect) override {
        if (!rect) return E_INVALIDARG;
        m_rect = *rect;
        if (m_view.hwnd()) m_view.setRect(m_rect);
        return S_OK;
    }
    IFACEMETHODIMP DoPreview() override {
        if (m_view.hwnd()) return S_OK;
        if (!m_parent || m_data.empty()) return E_FAIL;

        RECT local = {0, 0, m_rect.right - m_rect.left, m_rect.bottom - m_rect.top};
        local.left = m_rect.left;
        local.top = m_rect.top;
        local.right = m_rect.right;
        local.bottom = m_rect.bottom;
        if (!m_view.create(g_shellExtModule, m_parent, local)) return E_FAIL;

        m_view.setCompact(true);
        m_view.enableTools(false);
        // Los modos de enganche del visor, solo de lectura: el panel nunca escribe.
        if (m_view.tools()) m_view.tools()->setSnapSettings(stp::loadSnapSettings());
        // prevhost.exe es un proceso aparte, pero el panel no puede quedarse
        // cargando para siempre con un archivo enorme.
        m_view.setBudget(20000);
        if (m_hasVisuals) m_view.setHostColors(m_background, m_textColor);
        m_view.loadMemory(std::move(m_data), m_title);
        m_data.clear();
        return S_OK;
    }
    IFACEMETHODIMP Unload() override {
        m_view.destroy();
        m_data.clear();
        m_title.clear();
        return S_OK;
    }
    IFACEMETHODIMP SetFocus() override {
        if (!m_view.hwnd()) return S_FALSE;
        m_view.focus();
        return S_OK;
    }
    IFACEMETHODIMP QueryFocus(HWND* result) override {
        if (!result) return E_POINTER;
        *result = ::GetFocus();
        return *result ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
    IFACEMETHODIMP TranslateAccelerator(MSG* msg) override {
        // El anfitrion necesita ver Tab, Alt+flechas y demas atajos del panel.
        if (!m_site) return S_FALSE;
        IPreviewHandlerFrame* frame = nullptr;
        if (FAILED(m_site->QueryInterface(kIID_IPreviewHandlerFrame,
                                          reinterpret_cast<void**>(&frame)))) {
            return S_FALSE;
        }
        const HRESULT hr = frame->TranslateAccelerator(msg);
        frame->Release();
        return hr;
    }

    // IObjectWithSite
    IFACEMETHODIMP SetSite(IUnknown* site) override {
        if (m_site) {
            m_site->Release();
            m_site = nullptr;
        }
        m_site = site;
        if (m_site) m_site->AddRef();
        return S_OK;
    }
    IFACEMETHODIMP GetSite(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (!m_site) return E_FAIL;
        return m_site->QueryInterface(riid, ppv);
    }

    // IOleWindow
    IFACEMETHODIMP GetWindow(HWND* result) override {
        if (!result) return E_POINTER;
        *result = m_view.hwnd() ? m_view.hwnd() : m_parent;
        return *result ? S_OK : E_FAIL;
    }
    IFACEMETHODIMP ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }

    // IPreviewHandlerVisuals
    IFACEMETHODIMP SetBackgroundColor(COLORREF color) override {
        m_background = color;
        m_hasVisuals = true;
        if (m_view.hwnd()) m_view.setHostColors(m_background, m_textColor);
        return S_OK;
    }
    IFACEMETHODIMP SetFont(const LOGFONTW*) override { return S_OK; }
    IFACEMETHODIMP SetTextColor(COLORREF color) override {
        m_textColor = color;
        m_hasVisuals = true;
        if (m_view.hwnd()) m_view.setHostColors(m_background, m_textColor);
        return S_OK;
    }

private:
    ~StepPreviewHandler() {
        m_view.destroy();
        if (m_site) m_site->Release();
        InterlockedDecrement(&g_dllRefCount);
    }

    LONG m_refs = 1;
    IUnknown* m_site = nullptr;
    HWND m_parent = nullptr;
    RECT m_rect = {0, 0, 0, 0};
    std::string m_data;
    std::wstring m_title;
    stp::SceneView m_view;
    COLORREF m_background = RGB(32, 40, 48);
    COLORREF m_textColor = RGB(232, 236, 240);
    bool m_hasVisuals = false;
};

}  // namespace

const CLSID CLSID_StepPreviewHandler = {
    0xB718893F, 0xE5EC, 0x4CD8, {0xBF, 0x74, 0xD0, 0x2B, 0xC8, 0x13, 0x08, 0xC3}};

HRESULT CreateStepPreviewHandler(REFIID riid, void** ppv) {
    auto* handler = new (std::nothrow) StepPreviewHandler();
    if (!handler) return E_OUTOFMEMORY;
    const HRESULT hr = handler->QueryInterface(riid, ppv);
    handler->Release();
    return hr;
}
