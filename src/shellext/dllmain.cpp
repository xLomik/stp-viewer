#include <windows.h>
#include <shlobj.h>

#include <new>
#include <string>
#include <vector>

#include "shellext.h"

HMODULE g_shellExtModule = nullptr;

namespace {

long g_lockCount = 0;

// Interfaces de shell que mandan sobre cada asociacion.
const wchar_t* kThumbnailProviderIid = L"{E357FCCD-A995-4576-B01F-234630154E96}";
const wchar_t* kPreviewHandlerIid = L"{8895B1C6-B41F-4C1C-A562-0D564250836F}";
// Sustituto estandar de Windows que hospeda los manejadores de vista previa.
const wchar_t* kPrevHostAppId = L"{534A1E02-D58F-44f0-B58B-36CBED287C7C}";

const wchar_t* kThumbnailClsid = L"{90D4532D-A5D0-49A7-B115-800AD0042693}";
const wchar_t* kPreviewClsid = L"{B718893F-E5EC-4CD8-BF74-D02BC81308C3}";
const wchar_t* kThumbnailName = L"stp-viewer STEP thumbnail provider";
const wchar_t* kPreviewName = L"stp-viewer STEP preview handler";
const wchar_t* kExtensions[] = {L".stp", L".step", L".stpz"};
const wchar_t* kPreviewHandlersKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\PreviewHandlers";

class ClassFactory : public IClassFactory {
public:
    explicit ClassFactory(bool preview) : m_preview(preview) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    IFACEMETHODIMP_(ULONG) Release() override {
        const LONG left = InterlockedDecrement(&m_refs);
        if (left == 0) delete this;
        return left;
    }
    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        return m_preview ? CreateStepPreviewHandler(riid, ppv)
                         : CreateStepThumbnailProvider(riid, ppv);
    }
    IFACEMETHODIMP LockServer(BOOL lock) override {
        if (lock) InterlockedIncrement(&g_lockCount);
        else InterlockedDecrement(&g_lockCount);
        return S_OK;
    }

private:
    LONG m_refs = 1;
    bool m_preview;
};

LONG setValue(HKEY root, const std::wstring& subkey, const wchar_t* name,
              const std::wstring& value) {
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                  KEY_WRITE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) return result;
    result = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return result;
}

std::wstring classesRoot() { return L"Software\\Classes\\"; }

std::wstring readString(HKEY root, const std::wstring& subkey, const wchar_t* name) {
    wchar_t buffer[512] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    if (RegGetValueW(root, subkey.c_str(), name, RRF_RT_REG_SZ, &type, buffer, &size) !=
        ERROR_SUCCESS) {
        return std::wstring();
    }
    return std::wstring(buffer);
}

// Cuando un CAD ya reclamo la extension, su ProgID manda sobre la clave de la
// extension, asi que hay que colgar las mismas asociaciones ahi tambien.
std::vector<std::wstring> progIdsFor(const wchar_t* ext) {
    std::vector<std::wstring> ids;
    auto push = [&ids](const std::wstring& id) {
        if (id.empty() || id[0] == L'{') return;
        for (const std::wstring& existing : ids) {
            if (_wcsicmp(existing.c_str(), id.c_str()) == 0) return;
        }
        ids.push_back(id);
    };

    push(readString(HKEY_CLASSES_ROOT, ext, nullptr));
    push(readString(HKEY_CURRENT_USER, classesRoot() + ext, nullptr));
    push(readString(HKEY_CURRENT_USER,
                    std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\"
                                 L"FileExts\\") +
                        ext + L"\\UserChoice",
                    L"ProgId"));
    return ids;
}

}  // namespace

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    bool preview = false;
    if (clsid == CLSID_StepPreviewHandler) preview = true;
    else if (clsid != CLSID_StepThumbnailProvider) return CLASS_E_CLASSNOTAVAILABLE;

    ClassFactory* factory = new (std::nothrow) ClassFactory(preview);
    if (!factory) return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow() {
    return (g_dllRefCount == 0 && g_lockCount == 0) ? S_OK : S_FALSE;
}

HRESULT RegisterShellExtensions(HMODULE module, bool perUser) {
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(module, path, MAX_PATH)) return HRESULT_FROM_WIN32(GetLastError());

    const HKEY root = perUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    const std::wstring base = classesRoot();

    struct Server {
        const wchar_t* clsid;
        const wchar_t* name;
        bool surrogate;
    };
    const Server servers[] = {{kThumbnailClsid, kThumbnailName, false},
                              {kPreviewClsid, kPreviewName, true}};

    for (const Server& server : servers) {
        const std::wstring clsidKey = base + L"CLSID\\" + server.clsid;
        LONG result = setValue(root, clsidKey, nullptr, server.name);
        if (result != ERROR_SUCCESS) return HRESULT_FROM_WIN32(result);
        result = setValue(root, clsidKey + L"\\InprocServer32", nullptr, path);
        if (result != ERROR_SUCCESS) return HRESULT_FROM_WIN32(result);
        setValue(root, clsidKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
        if (server.surrogate) {
            // El panel de vista previa corre fuera del Explorador, en prevhost.exe.
            setValue(root, clsidKey, L"AppID", kPrevHostAppId);
            setValue(root, clsidKey, L"DisplayName", server.name);
        }
    }

    for (const wchar_t* ext : kExtensions) {
        // Las dos grafias importan: la clave de la extension y la que consulta
        // el Explorador cuando la extension ya tiene ProgID de algun CAD.
        setValue(root, base + ext + L"\\ShellEx\\" + kThumbnailProviderIid, nullptr,
                 kThumbnailClsid);
        setValue(root, base + L"SystemFileAssociations\\" + ext + L"\\ShellEx\\" +
                           kThumbnailProviderIid,
                 nullptr, kThumbnailClsid);
        setValue(root, base + ext + L"\\ShellEx\\" + kPreviewHandlerIid, nullptr, kPreviewClsid);
        setValue(root, base + L"SystemFileAssociations\\" + ext + L"\\ShellEx\\" +
                           kPreviewHandlerIid,
                 nullptr, kPreviewClsid);
        // Sin PerceivedType el panel no ofrece vista previa para extensiones
        // que ningun programa reclama.
        setValue(root, base + ext, L"PerceivedType", L"document");

        for (const std::wstring& progId : progIdsFor(ext)) {
            setValue(root, base + progId + L"\\ShellEx\\" + kThumbnailProviderIid, nullptr,
                     kThumbnailClsid);
            setValue(root, base + progId + L"\\ShellEx\\" + kPreviewHandlerIid, nullptr,
                     kPreviewClsid);
        }
    }

    // Windows solo acepta manejadores listados aqui. La lista de maquina pide
    // permisos de administrador; la del usuario se escribe siempre.
    setValue(root, kPreviewHandlersKey, kPreviewClsid, kPreviewName);
    if (perUser) setValue(HKEY_LOCAL_MACHINE, kPreviewHandlersKey, kPreviewClsid, kPreviewName);

    if (!perUser) {
        const std::wstring approved =
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved";
        setValue(HKEY_LOCAL_MACHINE, approved, kThumbnailClsid, kThumbnailName);
        setValue(HKEY_LOCAL_MACHINE, approved, kPreviewClsid, kPreviewName);
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

HRESULT UnregisterShellExtensions(bool perUser) {
    const HKEY root = perUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    const std::wstring base = classesRoot();

    RegDeleteTreeW(root, (base + L"CLSID\\" + kThumbnailClsid).c_str());
    RegDeleteTreeW(root, (base + L"CLSID\\" + kPreviewClsid).c_str());

    for (const wchar_t* ext : kExtensions) {
        RegDeleteTreeW(root, (base + ext + L"\\ShellEx\\" + kThumbnailProviderIid).c_str());
        RegDeleteTreeW(root, (base + ext + L"\\ShellEx\\" + kPreviewHandlerIid).c_str());
        RegDeleteTreeW(root, (base + L"SystemFileAssociations\\" + ext + L"\\ShellEx\\" +
                              kThumbnailProviderIid)
                                 .c_str());
        RegDeleteTreeW(root, (base + L"SystemFileAssociations\\" + ext + L"\\ShellEx\\" +
                              kPreviewHandlerIid)
                                 .c_str());
        for (const std::wstring& progId : progIdsFor(ext)) {
            RegDeleteTreeW(root, (base + progId + L"\\ShellEx\\" + kThumbnailProviderIid).c_str());
            RegDeleteTreeW(root, (base + progId + L"\\ShellEx\\" + kPreviewHandlerIid).c_str());
        }
    }

    for (HKEY hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(hive, kPreviewHandlersKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
            RegDeleteValueW(key, kPreviewClsid);
            RegCloseKey(key);
        }
    }
    if (!perUser) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell "
                          L"Extensions\\Approved",
                          0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
            RegDeleteValueW(key, kThumbnailClsid);
            RegDeleteValueW(key, kPreviewClsid);
            RegCloseKey(key);
        }
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

// regsvr32 sin argumentos instala para el usuario actual, sin elevacion;
// regsvr32 /i:machine instala para todos.
STDAPI DllRegisterServer() { return RegisterShellExtensions(g_shellExtModule, true); }

STDAPI DllUnregisterServer() {
    UnregisterShellExtensions(true);
    return S_OK;
}

STDAPI DllInstall(BOOL install, LPCWSTR command) {
    const bool machine = command && (_wcsicmp(command, L"machine") == 0);
    if (install) return RegisterShellExtensions(g_shellExtModule, !machine);
    return UnregisterShellExtensions(!machine);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_shellExtModule = instance;
    }
    return TRUE;
}
