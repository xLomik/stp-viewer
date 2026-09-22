// Visor 3D de archivos STEP. La ventana principal solo hospeda la vista 3D
// compartida con el panel de vista previa del Explorador.
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>

#include <string>

#include "scene_view.h"

namespace {

stp::SceneView* g_view = nullptr;
std::wstring g_currentFile;

std::wstring fileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

void openFile(HWND frame, const std::wstring& path) {
    if (!g_view) return;
    g_currentFile = path;
    SetWindowTextW(frame, (fileNameOf(path) + L" - stp-viewer").c_str());
    g_view->loadFile(path);
    g_view->focus();
}

void promptOpen(HWND frame) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = frame;
    dialog.lpstrFilter =
        L"Modelos 3D y planos\0*.stp;*.step;*.igs;*.iges;*.dxf;*.stl;*.obj;*.ply;*.dwg;*.prt;"
        L"*.sldprt;*.sldasm;*.ipt;*.iam;*.catpart\0"
        L"STEP (*.stp;*.step)\0*.stp;*.step\0"
        L"IGES (*.igs;*.iges)\0*.igs;*.iges\0"
        L"DXF (*.dxf)\0*.dxf\0"
        L"Mallas (*.stl;*.obj;*.ply)\0*.stl;*.obj;*.ply\0"
        L"Todos (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&dialog)) openFile(frame, path);
}

LRESULT CALLBACK frameProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_SIZE:
            if (g_view && g_view->hwnd()) {
                RECT client;
                GetClientRect(hwnd, &client);
                g_view->setRect(client);
            }
            return 0;

        case WM_SETFOCUS:
            if (g_view) g_view->focus();
            return 0;

        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wparam);
            wchar_t path[MAX_PATH] = {};
            if (DragQueryFileW(drop, 0, path, MAX_PATH)) openFile(hwnd, path);
            DragFinish(drop);
            SetForegroundWindow(hwnd);
            return 0;
        }

        case WM_KEYDOWN:
            if (wparam == 'O' && GetKeyState(VK_CONTROL) < 0) {
                promptOpen(hwnd);
                return 0;
            }
            if (wparam == VK_ESCAPE) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR commandLine, int showCmd) {
    SetProcessDPIAware();

    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    cls.style = CS_HREDRAW | CS_VREDRAW;
    cls.lpfnWndProc = &frameProc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    cls.hIconSm = cls.hIcon;
    cls.lpszClassName = L"StpViewerFrame";
    if (!RegisterClassExW(&cls)) return 1;

    HWND frame = CreateWindowExW(WS_EX_ACCEPTFILES, cls.lpszClassName, L"stp-viewer",
                                 WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 760,
                                 nullptr, nullptr, instance, nullptr);
    if (!frame) return 1;

    RECT client;
    GetClientRect(frame, &client);

    stp::SceneView view;
    g_view = &view;
    view.setBudget(120000);
    if (!view.create(instance, frame, client)) return 1;

    ShowWindow(frame, showCmd);
    UpdateWindow(frame);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(commandLine, &argc);
    if (argv) {
        if (argc >= 1 && argv[0] && argv[0][0]) openFile(frame, argv[0]);
        LocalFree(argv);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.hwnd == view.hwnd()) {
            // Ctrl+O y Escape los atiende el marco aunque el foco este en la vista.
            if ((msg.wParam == 'O' && GetKeyState(VK_CONTROL) < 0) || msg.wParam == VK_ESCAPE) {
                SendMessageW(frame, WM_KEYDOWN, msg.wParam, msg.lParam);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_view = nullptr;
    return 0;
}
