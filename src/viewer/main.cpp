// Visor 3D de archivos STEP. La ventana principal solo hospeda la vista 3D
// compartida con el panel de vista previa del Explorador.
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>

#include <string>
#include <vector>

#include "scene_view.h"
#include "toolbar.h"

namespace {

stp::SceneView* g_view = nullptr;
std::wstring g_currentFile;

std::wstring fileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

int GetDpiForWindowSafe(HWND hwnd) {
    HDC dc = GetDC(hwnd);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(hwnd, dc);
    return dpi;
}

stp::Toolbar g_toolbar;
HWND g_list = nullptr;
bool g_listVisible = false;
std::vector<int> g_listCodes;

void layout(HWND frame) {
    RECT client;
    GetClientRect(frame, &client);
    const int bar = g_toolbar.hwnd() ? g_toolbar.width() : 0;
    const int list = g_listVisible ? MulDiv(280, GetDpiForWindowSafe(frame), 96) : 0;
    if (g_toolbar.hwnd()) MoveWindow(g_toolbar.hwnd(), 0, 0, bar, client.bottom, TRUE);
    if (g_list) {
        ShowWindow(g_list, g_listVisible ? SW_SHOW : SW_HIDE);
        MoveWindow(g_list, client.right - list, 0, list, client.bottom, TRUE);
    }
    if (g_view && g_view->hwnd()) g_view->setRect(RECT{bar, 0, client.right - list, client.bottom});
}

void refreshList() {
    if (!g_list || !g_view || !g_view->tools()) return;
    SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    g_listCodes.clear();
    for (const auto& entry : g_view->tools()->listEntries()) {
        SendMessageW(g_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.second.c_str()));
        g_listCodes.push_back(entry.first);
    }
    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, nullptr, TRUE);
}

void openFile(HWND frame, const std::wstring& path) {
    if (!g_view) return;
    g_currentFile = path;
    SetWindowTextW(frame, (fileNameOf(path) + L" - stp-viewer").c_str());
    std::wstring problem;
    if (!g_view->saveMarks(&problem)) MessageBoxW(frame, problem.c_str(), L"stp-viewer", MB_ICONWARNING);
    g_view->loadFile(path);
    g_view->focus();
}

void updateTitle(HWND frame) {
    if (!g_view || g_currentFile.empty()) return;
    const bool dirty = g_view->tools() && g_view->tools()->dirty();
    SetWindowTextW(frame, (fileNameOf(g_currentFile) + (dirty ? L" *" : L"") + L" - stp-viewer").c_str());
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
            layout(hwnd);
            return 0;

        case WM_COMMAND: {
            const int command = LOWORD(wparam);
            if (reinterpret_cast<HWND>(lparam) == g_list && HIWORD(wparam) == LBN_SELCHANGE) {
                const LRESULT index = SendMessageW(g_list, LB_GETCURSEL, 0, 0);
                if (index >= 0 && static_cast<std::size_t>(index) < g_listCodes.size() && g_view && g_view->tools()) {
                    g_view->tools()->focusEntry(g_listCodes[static_cast<std::size_t>(index)]);
                    g_view->focus();
                }
                return 0;
            }
            if (!g_view || !g_view->tools()) return 0;
            if (command >= stp::kCommandTool && command <= stp::kCommandTool + static_cast<int>(stp::Tool::Pen)) {
                g_view->tools()->setTool(static_cast<stp::Tool>(command - stp::kCommandTool));
            } else if (command == stp::kCommandColor) {
                g_view->tools()->cycleColor();
            } else if (command == stp::kCommandList) {
                g_listVisible = !g_listVisible;
                layout(hwnd);
            } else if (command == stp::kCommandSave) {
                SendMessageW(hwnd, WM_KEYDOWN, 'S', 0);  // mismo camino que Ctrl+S
            } else if (command == stp::kCommandExport) {
                SendMessageW(hwnd, WM_KEYDOWN, 'E', 0);  // Task 11
            }
            g_view->focus();
            return 0;
        }

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
            if (wparam == VK_F2) {
                g_listVisible = !g_listVisible;
                layout(hwnd);
                return 0;
            }
            if (wparam == 'S' && (GetKeyState(VK_CONTROL) < 0 || lparam == 0) && g_view && g_view->tools()) {
                std::wstring problem;
                const bool saved = g_view->saveMarks(&problem);
                g_view->tools()->showMessage(saved ? L"Marcas guardadas" : problem);
                updateTitle(hwnd);
                return 0;
            }
            if (wparam == 'O' && GetKeyState(VK_CONTROL) < 0) {
                promptOpen(hwnd);
                return 0;
            }
            if (wparam == VK_ESCAPE) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            break;

        case WM_CLOSE: {
            std::wstring problem;
            if (g_view && !g_view->saveMarks(&problem)) {
                const std::wstring question = problem + L"\n\n\u00BFCerrar de todos modos?";
                if (MessageBoxW(hwnd, question.c_str(), L"stp-viewer", MB_YESNO | MB_ICONWARNING) != IDYES) return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        }

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
    view.enableTools(true);
    g_toolbar.create(instance, frame);
    g_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                             WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0, 10, 10, frame,
                             nullptr, instance, nullptr);
    SendMessageW(g_list, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    layout(frame);
    view.setMarkupListener([frame]() {
        updateTitle(frame);
        if (g_view && g_view->tools()) g_toolbar.setState(g_view->tools()->tool(), g_view->tools()->color());
        refreshList();
    });
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
            if ((msg.wParam == 'O' && GetKeyState(VK_CONTROL) < 0) ||
                (msg.wParam == 'S' && GetKeyState(VK_CONTROL) < 0) ||
                msg.wParam == VK_F2 ||
                (msg.wParam == VK_ESCAPE && !view.toolActive())) {
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
