#include <windows.h>
#include <shlobj.h>

#include <new>
#include <string>

#include "provider.h"

namespace {

HMODULE g_module = nullptr;
long g_lockCount = 0;

// Shell interface that owns the "thumbnail of a file type" association.
const wchar_t* kThumbnailProviderIid = L"{E357FCCD-A995-4576-B01F-234630154E96}";
const wchar_t* kClsidText = L"{90D4532D-A5D0-49A7-B115-800AD0042693}";
const wchar_t* kFriendlyName = L"stp-viewer STEP thumbnail provider";
const wchar_t* kExtensions[] = {L".stp", L".step", L".stpz"};

class ClassFactory : public IClassFactory {
public:
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
        return CreateStepThumbnailProvider(riid, ppv);
    }
    IFACEMETHODIMP LockServer(BOOL lock) override {
        if (lock) InterlockedIncrement(&g_lockCount);
        else InterlockedDecrement(&g_lockCount);
        return S_OK;
    }

private:
    LONG m_refs = 1;
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

void deleteTree(HKEY root, const std::wstring& subkey) {
    RegDeleteTreeW(root, subkey.c_str());
}

std::wstring classesRoot() { return L"Software\\Classes\\"; }

}  // namespace

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (clsid != CLSID_StepThumbnailProvider) return CLASS_E_CLASSNOTAVAILABLE;

    ClassFactory* factory = new (std::nothrow) ClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow() {
    return (g_dllRefCount == 0 && g_lockCount == 0) ? S_OK : S_FALSE;
}

HRESULT RegisterThumbnailProvider(HMODULE module, bool perUser) {
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(module, path, MAX_PATH)) return HRESULT_FROM_WIN32(GetLastError());

    const HKEY root = perUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    const std::wstring base = classesRoot();
    const std::wstring clsidKey = base + L"CLSID\\" + kClsidText;

    LONG result = setValue(root, clsidKey, nullptr, kFriendlyName);
    if (result != ERROR_SUCCESS) return HRESULT_FROM_WIN32(result);
    result = setValue(root, clsidKey + L"\\InprocServer32", nullptr, path);
    if (result != ERROR_SUCCESS) return HRESULT_FROM_WIN32(result);
    result = setValue(root, clsidKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
    if (result != ERROR_SUCCESS) return HRESULT_FROM_WIN32(result);

    for (const wchar_t* ext : kExtensions) {
        // Both spellings matter: the plain extension key and the one Explorer
        // consults when the extension already has a ProgID from a CAD package.
        setValue(root, base + ext + L"\\ShellEx\\" + kThumbnailProviderIid, nullptr, kClsidText);
        setValue(root, base + L"SystemFileAssociations\\" + ext + L"\\ShellEx\\" +
                           kThumbnailProviderIid,
                 nullptr, kClsidText);
    }

    if (!perUser) {
        setValue(HKEY_LOCAL_MACHINE,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
                 kClsidText, kFriendlyName);
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

HRESULT UnregisterThumbnailProvider(bool perUser) {
    const HKEY root = perUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    const std::wstring base = classesRoot();

    deleteTree(root, base + L"CLSID\\" + kClsidText);
    for (const wchar_t* ext : kExtensions) {
        deleteTree(root, base + ext + L"\\ShellEx\\" + kThumbnailProviderIid);
        deleteTree(root,
                   base + L"SystemFileAssociations\\" + ext + L"\\ShellEx\\" + kThumbnailProviderIid);
    }
    if (!perUser) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell "
                          L"Extensions\\Approved",
                          0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
            RegDeleteValueW(key, kClsidText);
            RegCloseKey(key);
        }
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

// regsvr32 without arguments registers for the current user, which needs no
// elevation; regsvr32 /i:machine installs for everyone.
STDAPI DllRegisterServer() { return RegisterThumbnailProvider(g_module, true); }

STDAPI DllUnregisterServer() {
    UnregisterThumbnailProvider(true);
    return S_OK;
}

STDAPI DllInstall(BOOL install, LPCWSTR command) {
    const bool machine = command && (_wcsicmp(command, L"machine") == 0);
    if (install) return RegisterThumbnailProvider(g_module, !machine);
    return UnregisterThumbnailProvider(!machine);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
