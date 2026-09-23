// Visor 3D de archivos STEP. La ventana principal solo hospeda la vista 3D
// compartida con el panel de vista previa del Explorador.
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <lmcons.h>

#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#include "scene_view.h"
#include "toolbar.h"
#include "export.h"
#include "../export/pdf_writer.h"

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

std::string utf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], size, nullptr, nullptr);
    return out;
}

bool writeBytes(const std::wstring& path, const std::string& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                    written == bytes.size();
    CloseHandle(file);
    return ok;
}

std::string footerLine() {
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t user[UNLEN + 1] = {};
    DWORD length = UNLEN + 1;
    GetUserNameW(user, &length);
    char date[32];
    std::snprintf(date, sizeof(date), "%04u-%02u-%02u %02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute);
    return utf8(fileNameOf(g_currentFile)) + "  \xE2\x80\x94  " + date + "  \xE2\x80\x94  " + utf8(user);
}

// Pagina con una vista: imagen de 1540x1000 px en 770x500 pt, titulo arriba y pie.
bool addViewPage(stp::PdfWriter* pdf, const stp::Camera& camera, const std::string& title, int pageNumber) {
    const int pw = 1540, ph = 1000;
    HBITMAP bitmap = g_view->renderSnapshot(camera, pw, ph, 2.0);
    if (!bitmap) return false;
    std::vector<std::uint8_t> jpeg;
    const bool ok = stp::encodeJpeg(bitmap, 90, &jpeg);
    DeleteObject(bitmap);
    if (!ok) return false;
    pdf->addPage(jpeg, pw, ph, 36, 52, 770, 500,
                 {{36, 566, 14, title, true}, {36, 24, 8, footerLine() + "  \xE2\x80\x94  p. " + std::to_string(pageNumber), false}});
    return true;
}

void exportMarkup(HWND frame) {
    if (!g_view || !g_view->tools() || g_currentFile.empty()) return;
    std::wstring base = g_currentFile;
    const std::size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos) base = base.substr(0, dot);
    wchar_t path[MAX_PATH] = {};
    wcsncpy(path, (fileNameOf(base) + L"-revision.pdf").c_str(), MAX_PATH - 1);
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = frame;
    dialog.lpstrFilter = L"PDF con todas las vistas (*.pdf)\0*.pdf\0Imagen de esta vista (*.png)\0*.png\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"pdf";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetSaveFileNameW(&dialog)) return;

    std::wstring target = path;
    const bool png = dialog.nFilterIndex == 2 || (target.size() > 4 && _wcsicmp(target.c_str() + target.size() - 4, L".png") == 0);
    bool ok = false;
    if (png) {
        if (_wcsicmp(target.c_str() + std::max<std::size_t>(target.size(), 4) - 4, L".png") != 0) target += L".png";
        RECT client;
        GetClientRect(g_view->hwnd(), &client);
        HBITMAP bitmap = g_view->renderSnapshot(g_view->camera(), 2 * client.right, 2 * client.bottom, 2.0);
        ok = bitmap && stp::savePng(bitmap, target);
        if (bitmap) DeleteObject(bitmap);
    } else {
        stp::PdfWriter pdf;
        const double aspect = 1540.0 / 1000.0;
        int page = 1;
        ok = addViewPage(&pdf, g_view->overviewCamera(aspect), "Vista general", page++);
        const stp::MarkupDocument& doc = g_view->tools()->document();
        for (const stp::MarkupView& view : doc.views) {
            stp::Camera camera = view.camera;
            ok = ok && addViewPage(&pdf, camera, view.name, page++);
        }
        // Tabla de medidas y notas, 34 renglones por pagina.
        std::vector<std::string> rows;
        int n = 1;
        for (const stp::Mark& mark : doc.marks) {
            if (!stp::isMeasurement(mark.kind) && mark.kind != stp::MarkKind::Note) continue;
            std::string row = std::to_string(n++) + ".  " + utf8(g_view->tools()->describe(mark));
            for (char& c : row) if (c == '\n') c = ' ';
            rows.push_back(row);
        }
        for (std::size_t start = 0; start < rows.size(); start += 34) {
            std::vector<stp::PdfText> texts = {{36, 566, 14, "Medidas y notas", true}};
            for (std::size_t i = start; i < rows.size() && i < start + 34; ++i) {
                texts.push_back({36, 536 - 14.5 * static_cast<double>(i - start), 10, rows[i], false});
            }
            texts.push_back({36, 24, 8, footerLine() + "  \xE2\x80\x94  p. " + std::to_string(page++), false});
            pdf.addPage({}, 0, 0, 0, 0, 0, 0, texts);
        }
        ok = ok && writeBytes(target, pdf.finish());
    }
    g_view->tools()->showMessage(ok ? L"Exportado: " + fileNameOf(target) : L"No se pudo exportar");
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
            if (wparam == 'E' && (GetKeyState(VK_CONTROL) < 0 || lparam == 0)) {
                exportMarkup(hwnd);
                return 0;
            }
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
                (msg.wParam == 'E' && GetKeyState(VK_CONTROL) < 0) ||
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
