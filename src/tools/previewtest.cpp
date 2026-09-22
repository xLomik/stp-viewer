// Aloja el manejador de vista previa igual que lo hace el Explorador, para
// poder probarlo sin registrar nada en el sistema.
//
//     previewtest StepShellExt.dll pieza.stp
#include <windows.h>
#include <objbase.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <cstdio>
#include <string>

namespace {

const CLSID kPreviewClsid = {
    0xB718893F, 0xE5EC, 0x4CD8, {0xBF, 0x74, 0xD0, 0x2B, 0xC8, 0x13, 0x08, 0xC3}};
const IID kIID_IPreviewHandler = {
    0x8895B1C6, 0xB41F, 0x4C1C, {0xA5, 0x62, 0x0D, 0x56, 0x42, 0x50, 0x83, 0x6F}};
const IID kIID_IPreviewHandlerVisuals = {
    0x196BF9A5, 0xB346, 0x4EF0, {0xAA, 0x1E, 0x5D, 0xCD, 0xB7, 0x67, 0x68, 0xB1}};
const IID kIID_IInitializeWithStream = {
    0xB824B49D, 0x22AC, 0x4161, {0xAC, 0x8A, 0x99, 0x16, 0xE8, 0xFA, 0x3F, 0x7F}};

typedef HRESULT(STDAPICALLTYPE* GetClassObjectFn)(REFCLSID, REFIID, void**);

IPreviewHandler* g_handler = nullptr;

std::wstring widen(const char* text) {
    const int size = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, -1, &out[0], size);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

LRESULT CALLBACK hostProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_SIZE:
            if (g_handler) {
                RECT client;
                GetClientRect(hwnd, &client);
                g_handler->SetRect(&client);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("uso: previewtest <dll> <archivo.stp>\n");
        return 2;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HMODULE dll = LoadLibraryW(widen(argv[1]).c_str());
    if (!dll) {
        std::printf("error: no se pudo cargar la DLL (%lu)\n", GetLastError());
        return 1;
    }
    auto getClassObject =
        reinterpret_cast<GetClassObjectFn>(
            reinterpret_cast<void*>(GetProcAddress(dll, "DllGetClassObject")));
    if (!getClassObject) {
        std::printf("error: falta DllGetClassObject\n");
        return 1;
    }

    IClassFactory* factory = nullptr;
    HRESULT hr = getClassObject(kPreviewClsid, IID_IClassFactory,
                                reinterpret_cast<void**>(&factory));
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
        std::printf("error: no se pudo abrir el archivo 0x%08lX\n",
                    static_cast<unsigned long>(hr));
        return 1;
    }
    hr = init->Initialize(stream, STGM_READ);
    stream->Release();
    if (FAILED(hr)) {
        std::printf("error: Initialize 0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }

    hr = init->QueryInterface(kIID_IPreviewHandler, reinterpret_cast<void**>(&g_handler));
    init->Release();
    if (FAILED(hr) || !g_handler) {
        std::printf("error: sin IPreviewHandler 0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }

    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = &hostProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.lpszClassName = L"StpPreviewTestHost";
    RegisterClassExW(&cls);

    HWND host = CreateWindowExW(0, cls.lpszClassName, L"Prueba del panel de vista previa",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 820, 640,
                                nullptr, nullptr, cls.hInstance, nullptr);
    ShowWindow(host, SW_SHOW);

    RECT client;
    GetClientRect(host, &client);

    IPreviewHandlerVisuals* visuals = nullptr;
    if (SUCCEEDED(g_handler->QueryInterface(kIID_IPreviewHandlerVisuals,
                                            reinterpret_cast<void**>(&visuals)))) {
        visuals->SetBackgroundColor(RGB(255, 255, 255));
        visuals->SetTextColor(RGB(20, 24, 28));
        visuals->Release();
    }

    hr = g_handler->SetWindow(host, &client);
    std::printf("SetWindow 0x%08lX\n", static_cast<unsigned long>(hr));
    hr = g_handler->SetRect(&client);
    std::printf("SetRect 0x%08lX\n", static_cast<unsigned long>(hr));
    hr = g_handler->DoPreview();
    std::printf("DoPreview 0x%08lX\n", static_cast<unsigned long>(hr));
    if (FAILED(hr)) return 1;
    g_handler->SetFocus();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_handler->Unload();
    g_handler->Release();
    CoUninitialize();
    return 0;
}
