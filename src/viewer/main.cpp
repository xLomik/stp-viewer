// Visor STP: marco con barra de titulo propia, cinta, vista 3D/2D y barra de
// estado. La vista es la misma que usa el panel de vista previa del Explorador.
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <lmcons.h>

#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#include "../export/pdf_writer.h"
#include "../ui/recent_files.h"
#include "export.h"
#include "scene_view.h"
#include "settings.h"
#include "ui/commands.h"
#include "ui/popup_menu.h"
#include "ui/ribbon.h"
#include "ui/side_panel.h"
#include "ui/start_page.h"
#include "ui/status_bar.h"
#include "ui/theme.h"

namespace {

using namespace stp::ui;

stp::SceneView* g_view = nullptr;
std::wstring g_currentFile;

std::wstring fileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
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

// --- Marco -----------------------------------------------------------------------

struct Frame {
    HWND hwnd = nullptr;
    HINSTANCE instance = nullptr;
    int dpi = 96;
    stp::SceneView view;
    Ribbon ribbon;
    StatusBar status;
    SidePanel panel;
    stp::ui::StartPage start;
    RecentFiles recent;
    bool statusVisible = true;
    bool panelVisible = true;
    bool panelFloatOpen = false;  // ventana angosta: panel flotante abierto con F2
    bool lastLoading = false;
    bool lastModel = false;
    bool cubeVisible = true;
    int hotButton = -1;  // 0 minimizar, 1 maximizar, 2 cerrar
    int pressedButton = -1;
    bool trackingMouse = false;

    int titleHeight() const { return scale(32, dpi); }
    bool narrow() const {
        RECT client;
        GetClientRect(hwnd, &client);
        return client.right < scale(640, dpi);
    }
    bool panelShown() const { return !g_currentFile.empty() && (narrow() ? panelFloatOpen : panelVisible); }
    int buttonWidth() const { return scale(46, dpi); }
    RECT buttonRect(int index) const {
        RECT client;
        GetClientRect(hwnd, &client);
        const int right = client.right - (2 - index) * buttonWidth();
        return RECT{right - buttonWidth(), 0, right, titleHeight()};
    }
    int buttonAt(POINT p) const {
        for (int i = 0; i < 3; ++i) {
            const RECT r = buttonRect(i);
            if (PtInRect(&r, p)) return i;
        }
        return -1;
    }

    void layout();
    void execute(int command);
    CommandState state(int command) const;
    void refreshStatus();
    void refreshAll() {
        ribbon.refresh();
        refreshStatus();
        updateTitle();
    }
    void openFile(const std::wstring& path);
    void promptOpen();
    void saveMarks();
    void exportPdf();
    void exportPng();
    void updateTitle();
    void paintTitle(HDC dc);
    void popup(const std::vector<MenuItem>& items);
    void saveAll();
    void setDpi(int value);
};

Frame* g_frame = nullptr;

void Frame::layout() {
    RECT client;
    GetClientRect(hwnd, &client);
    const int top = titleHeight();
    const int ribbonHeight = ribbon.height();
    const int statusHeight = statusVisible ? status.height() : 0;
    MoveWindow(ribbon.hwnd(), 0, top, client.right, ribbonHeight, TRUE);
    ShowWindow(status.hwnd(), statusVisible ? SW_SHOWNA : SW_HIDE);
    if (statusVisible) MoveWindow(status.hwnd(), 0, client.bottom - statusHeight, client.right, statusHeight, TRUE);
    const RECT content = {0, top + ribbonHeight, client.right, client.bottom - statusHeight};
    const bool file = !g_currentFile.empty();
    ShowWindow(start.hwnd(), file ? SW_HIDE : SW_SHOWNA);
    if (!file) MoveWindow(start.hwnd(), content.left, content.top, content.right - content.left, content.bottom - content.top, TRUE);
    if (view.hwnd()) ShowWindow(view.hwnd(), file ? SW_SHOWNA : SW_HIDE);
    const bool showPanel = panelShown();
    const int panelWidth = std::min(scale(panel.preferredWidth(), dpi), static_cast<int>(content.right) / 2 + scale(60, dpi));
    RECT viewRect = content;
    if (showPanel && !narrow()) viewRect.right -= panelWidth;
    if (view.hwnd()) view.setRect(viewRect);
    ShowWindow(panel.hwnd(), showPanel ? SW_SHOWNA : SW_HIDE);
    if (showPanel) {
        // Angosta: el panel flota encima del borde derecho de la vista.
        SetWindowPos(panel.hwnd(), HWND_TOP, content.right - panelWidth, content.top, panelWidth, content.bottom - content.top,
                     SWP_NOACTIVATE);
    }
    RECT strip = {0, 0, client.right, top};
    InvalidateRect(hwnd, &strip, FALSE);
}

CommandState Frame::state(int command) const {
    CommandState s;
    stp::SceneView& v = const_cast<stp::SceneView&>(view);
    stp::MarkupTools* tools = v.tools();
    const bool model = v.hasModel() && tools;
    const bool faces = model && !v.mesh().indices.empty() && !v.plan2d();
    if (command >= kCmdTool && command <= kCmdTool + static_cast<int>(stp::Tool::Pen)) {
        s.enabled = model;
        s.checked = model && static_cast<int>(tools->tool()) == command - kCmdTool;
        return s;
    }
    if (command >= kCmdSnapMode && command < kCmdSnapMode + 9) {
        s.enabled = tools != nullptr;
        s.checked = tools && ((tools->snapSettings().modes >> (command - kCmdSnapMode)) & 1u);
        return s;
    }
    switch (command) {
        case kCmdSaveMarks:
        case kCmdExportPdf:
        case kCmdExportPng:
        case kCmdFit:
        case kCmdViews:
            s.enabled = model;
            break;
        case kCmdRecent:
            s.enabled = !recent.items().empty();
            break;
        case kCmdNavigate:
            s.enabled = model;
            s.checked = model && tools->tool() == stp::Tool::Navigate;
            break;
        case kCmdPlan2d:
            s.enabled = v.canPlan2d();
            s.checked = v.plan2d();
            break;
        case kCmdSnapToggle:
            s.checked = tools && tools->snapSettings().enabled;
            break;
        case kCmdOrtho:
            s.checked = tools && tools->snapSettings().constraint.kind == stp::ConstraintKind::Ortho;
            break;
        case kCmdPolar:
            s.checked = tools && tools->snapSettings().constraint.kind == stp::ConstraintKind::Polar;
            break;
        case kCmdPolarMenu:
            s.label = L"Paso " + std::to_wstring(tools ? static_cast<int>(tools->snapSettings().constraint.polarStepDegrees) : 45) +
                      L"°";
            break;
        case kCmdColorMenu:
        case kCmdColor:
            s.enabled = model;
            s.swatch = tools ? tools->color() : stp::kMarkRed;
            break;
        case kCmdUndo: s.enabled = model && tools->canUndo(); break;
        case kCmdRedo: s.enabled = model && tools->canRedo(); break;
        case kCmdDelete: s.enabled = model && tools->selected() >= 0; break;
        case kCmdShaded:
            s.enabled = faces;
            s.checked = faces && v.shaded();
            break;
        case kCmdWireframe:
            s.enabled = faces;
            s.checked = faces && !v.shaded();
            break;
        case kCmdEdges:
            s.enabled = faces;
            s.checked = faces && v.edges();
            break;
        case kCmdPerspective:
            s.enabled = model && !v.plan2d();
            s.checked = v.perspective();
            break;
        case kCmdPanel: s.checked = panelShown(); break;
        case kCmdStatusBar: s.checked = statusVisible; break;
        case kCmdViewCube: s.checked = cubeVisible; break;
        default: break;
    }
    return s;
}

void Frame::popup(const std::vector<MenuItem>& items) {
    const int chosen = showPopupMenu(hwnd, ribbon.popupAnchor(), items, dpi);
    if (chosen) execute(chosen);
}

MenuItem menuItem(int command, const CommandState& state) {
    const CommandInfo* info = commandInfo(command);
    MenuItem item;
    item.id = command;
    item.text = state.label.empty() && info ? info->label : state.label;
    item.shortcut = info && info->shortcut ? info->shortcut : L"";
    item.icon = info ? info->icon : Icon::None;
    item.checked = state.checked;
    item.enabled = state.enabled;
    return item;
}

void Frame::execute(int command) {
    stp::MarkupTools* tools = view.tools();
    auto snapChange = [&](auto change) {
        if (!tools) return;
        stp::SnapSettings settings = tools->snapSettings();
        change(settings);
        tools->setSnapSettings(settings);
    };
    if (command >= kCmdTool && command <= kCmdTool + static_cast<int>(stp::Tool::Pen)) {
        if (tools && view.hasModel()) tools->setTool(static_cast<stp::Tool>(command - kCmdTool));
    } else if (command >= kCmdSnapMode && command < kCmdSnapMode + 9) {
        snapChange([&](stp::SnapSettings& s) { s.modes ^= 1u << (command - kCmdSnapMode); });
    } else if (command == kCmdPolarStep + 15 || command == kCmdPolarStep + 30 || command == kCmdPolarStep + 45 ||
               command == kCmdPolarStep + 90) {
        snapChange([&](stp::SnapSettings& s) {
            s.constraint.polarStepDegrees = command - kCmdPolarStep;
            s.constraint.kind = stp::ConstraintKind::Polar;
        });
    } else if (command >= kCmdRecentFirst && command < kCmdRecentFirst + static_cast<int>(RecentFiles::kLimit)) {
        const std::size_t index = static_cast<std::size_t>(command - kCmdRecentFirst);
        if (index < recent.items().size()) {
            const std::wstring path = recent.items()[index];
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
                const std::wstring question = L"No se encuentra\n" + path + L"\n\n¿Quitarlo de la lista de recientes?";
                if (MessageBoxW(hwnd, question.c_str(), L"Visor STP", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    recent.remove(path);
                    start.setRecent(recent.items());
                }
            } else {
                openFile(path);
            }
        }
    } else if (command >= kCmdColorPick && command < kCmdColorPick + 8) {
        static const std::uint32_t marks[] = {stp::kMarkRed, stp::kMarkYellow, stp::kMarkGreen, stp::kMarkBlue, stp::kMarkBlack};
        static const std::uint32_t highlights[] = {stp::kHighlightYellow, stp::kHighlightGreen, stp::kHighlightPink};
        const int i = command - kCmdColorPick;
        if (tools) {
            if (tools->tool() == stp::Tool::Highlight && i < 3) tools->setColor(highlights[i]);
            else if (tools->tool() != stp::Tool::Highlight && i < 5) tools->setColor(marks[i]);
        }
    } else {
        switch (command) {
            case kCmdNone: break;
            case kCmdOpen: promptOpen(); break;
            case kCmdRecent: {
                std::vector<MenuItem> items;
                for (std::size_t i = 0; i < recent.items().size(); ++i) {
                    MenuItem item;
                    item.id = kCmdRecentFirst + static_cast<int>(i);
                    item.text = fileNameOf(recent.items()[i]);
                    item.icon = Icon::File;
                    item.dim = GetFileAttributesW(recent.items()[i].c_str()) == INVALID_FILE_ATTRIBUTES;
                    items.push_back(item);
                }
                popup(items);
                break;
            }
            case kCmdSaveMarks: saveMarks(); break;
            case kCmdExportPdf: exportPdf(); break;
            case kCmdExportPng: exportPng(); break;
            case kCmdAbout:
                MessageBoxW(hwnd,
                            L"Visor STP\n\nModelos STEP, IGES, STL, OBJ, PLY y planos DXF.\n"
                            L"Medidas con enganche a objetos (OSNAP), Orto y Polar, marcas de revisión "
                            L"y exportación a PDF y PNG.",
                            L"Acerca de Visor STP", MB_OK | MB_ICONINFORMATION);
                break;
            case kCmdNavigate:
                if (tools) tools->setTool(stp::Tool::Navigate);
                break;
            case kCmdFit: view.fit(); break;
            case kCmdViews: {
                std::vector<MenuItem> items;
                for (int c = kCmdViewFront; c <= kCmdViewIso; ++c) items.push_back(menuItem(c, CommandState()));
                popup(items);
                break;
            }
            case kCmdViewFront: case kCmdViewBack: case kCmdViewLeft: case kCmdViewRight:
            case kCmdViewTop: case kCmdViewBottom: case kCmdViewIso:
                view.standardView(command - kCmdViewFront);
                break;
            case kCmdPlan2d: view.togglePlan2d(); break;
            case kCmdSnapToggle: if (tools) tools->toggleSnap(); break;
            case kCmdOrtho: if (tools) tools->toggleOrtho(); break;
            case kCmdPolar: if (tools) tools->togglePolar(); break;
            case kCmdPolarMenu: {
                std::vector<MenuItem> items;
                const int step = tools ? static_cast<int>(tools->snapSettings().constraint.polarStepDegrees) : 45;
                for (int s : {15, 30, 45, 90}) {
                    CommandState st;
                    st.checked = s == step;
                    items.push_back(menuItem(kCmdPolarStep + s, st));
                }
                popup(items);
                break;
            }
            case kCmdColor: if (tools) tools->cycleColor(); break;
            case kCmdColorMenu: {
                if (!tools) break;
                const bool highlight = tools->tool() == stp::Tool::Highlight;
                static const std::uint32_t marks[] = {stp::kMarkRed, stp::kMarkYellow, stp::kMarkGreen, stp::kMarkBlue, stp::kMarkBlack};
                static const wchar_t* const markNames[] = {L"Rojo", L"Amarillo", L"Verde", L"Azul", L"Negro"};
                static const std::uint32_t highlights[] = {stp::kHighlightYellow, stp::kHighlightGreen, stp::kHighlightPink};
                static const wchar_t* const highlightNames[] = {L"Amarillo", L"Verde", L"Rosa"};
                std::vector<MenuItem> items;
                const int count = highlight ? 3 : 5;
                for (int i = 0; i < count; ++i) {
                    MenuItem item;
                    item.id = kCmdColorPick + i;
                    item.text = highlight ? highlightNames[i] : markNames[i];
                    item.swatch = highlight ? highlights[i] : marks[i];
                    item.checked = tools->color() == item.swatch;
                    items.push_back(item);
                }
                popup(items);
                break;
            }
            case kCmdUndo: if (tools) tools->undo(); break;
            case kCmdRedo: if (tools) tools->redo(); break;
            case kCmdDelete: if (tools) tools->deleteSelected(); break;
            case kCmdShaded: view.setShaded(true); break;
            case kCmdWireframe: view.setShaded(false); break;
            case kCmdEdges: view.setEdges(!view.edges()); break;
            case kCmdPerspective: view.setPerspective(!view.perspective()); break;
            case kCmdPanel:
                if (narrow()) panelFloatOpen = !panelFloatOpen;
                else panelVisible = !panelVisible;
                layout();
                panel.refresh();
                break;
            case kCmdStatusBar: statusVisible = !statusVisible; layout(); break;
            case kCmdViewCube:
                cubeVisible = !cubeVisible;
                view.setCubeVisible(cubeVisible);
                break;
            case kCmdRibbonToggle: layout(); break;
            default: break;
        }
    }
    refreshAll();
}

void Frame::refreshStatus() {
    // Termino una carga: el panel y la cinta muestran el modelo nuevo.
    if (view.loading() != lastLoading || view.hasModel() != lastModel) {
        lastLoading = view.loading();
        lastModel = view.hasModel();
        panel.refresh();
        ribbon.refresh();
    }
    StatusBar::State s;
    stp::MarkupTools* tools = view.tools();
    if (tools) {
        const stp::SnapSettings& snap = tools->snapSettings();
        s.snap = snap.enabled;
        s.ortho = snap.constraint.kind == stp::ConstraintKind::Ortho;
        s.polar = snap.constraint.kind == stp::ConstraintKind::Polar;
        s.snapModes = snap.modes;
        s.polarStep = snap.constraint.polarStepDegrees;
    }
    s.hasModel = view.hasModel();
    s.plan2d = view.plan2d();
    const char* suffix = stp::unitSuffix(view.mesh().units);
    const std::wstring units = widen(suffix);
    s.units = units.empty() ? L"Sin unidades" : units;
    s.zoomPercent = view.zoomPercent();
    stp::Vec3 p;
    if (s.hasModel && tools && tools->cursorPoint(&p)) {
        wchar_t text[160];
        const bool flat = view.plan2d() && std::fabs(view.planar().normal.z) > 0.999;
        if (flat) swprintf(text, 160, L"X %.3f   Y %.3f %ls", p.x, p.y, units.c_str());
        else swprintf(text, 160, L"X %.3f   Y %.3f   Z %.3f %ls", p.x, p.y, p.z, units.c_str());
        s.coords = text;
    }
    if (view.loading() || !view.message().empty()) {
        s.message = view.message();
    } else {
        s.message = view.statusText();
        if (view.truncated() && (!tools || tools->tool() == stp::Tool::Navigate)) {
            s.message = L"Modelo muy grande: se muestra solo una parte   ·   " + s.message;
        }
    }
    status.setState(s);
}

void Frame::updateTitle() {
    std::wstring title = L"Visor STP";
    if (!g_currentFile.empty()) {
        const bool dirty = view.tools() && view.tools()->dirty();
        title = fileNameOf(g_currentFile) + (dirty ? L" *" : L"") + L" — Visor STP";
    }
    wchar_t current[512] = {};
    GetWindowTextW(hwnd, current, 512);
    if (title != current) {
        SetWindowTextW(hwnd, title.c_str());
        RECT strip = {0, 0, 10000, titleHeight()};
        InvalidateRect(hwnd, &strip, FALSE);
    }
}

void Frame::paintTitle(HDC dc) {
    RECT client;
    GetClientRect(hwnd, &client);
    const RECT strip = {0, 0, client.right, titleHeight()};
    paintBuffered(dc, strip, [&](Gdiplus::Graphics& g) {
        const float h = static_cast<float>(titleHeight());
        Gdiplus::SolidBrush back(color(kBackground));
        g.FillRectangle(&back, 0.0f, 0.0f, static_cast<float>(client.right), h);
        // Icono de la aplicacion.
        HICON icon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, scale(16, dpi), scale(16, dpi), 0));
        if (icon) {
            HDC hdc = g.GetHDC();
            DrawIconEx(hdc, scale(12, dpi), (titleHeight() - scale(16, dpi)) / 2, icon, scale(16, dpi), scale(16, dpi), 0, nullptr,
                       DI_NORMAL);
            g.ReleaseHDC(hdc);
            DestroyIcon(icon);
        }
        wchar_t title[512] = {};
        GetWindowTextW(hwnd, title, 512);
        const auto f = font(9, dpi);
        const float left = static_cast<float>(scale(38, dpi));
        const float right = static_cast<float>(buttonRect(0).left - scale(8, dpi));
        drawText(g, title, *f, Gdiplus::RectF(left, 0, std::max(0.0f, right - left), h),
                 GetActiveWindow() == hwnd ? kText : kTextDim);
        const bool maximized = IsZoomed(hwnd) != FALSE;
        const Icon icons[3] = {Icon::Minimize, maximized ? Icon::Restore : Icon::Maximize, Icon::Close};
        for (int i = 0; i < 3; ++i) {
            const RECT r = buttonRect(i);
            const Gdiplus::RectF box(static_cast<float>(r.left), 0, static_cast<float>(r.right - r.left), h);
            if (i == hotButton) {
                Gdiplus::SolidBrush hot(color(i == 2 ? kCloseRed : kHover));
                g.FillRectangle(&hot, box);
            }
            const float s = static_cast<float>(scale(14, dpi));
            drawIcon(g, icons[i], Gdiplus::RectF(box.X + (box.Width - s) / 2, (h - s) / 2, s, s),
                     i == hotButton && i == 2 ? 0xFFFFFFFF : kText);
        }
    });
}

void Frame::saveAll() {
    stp::ViewerSettings s;
    if (view.tools()) s.snap = view.tools()->snapSettings();
    s.ribbonCollapsed = ribbon.collapsed();
    s.ribbonTab = ribbon.tab();
    s.panelVisible = panelVisible;
    s.panelWidth = panel.preferredWidth();
    s.statusVisible = statusVisible;
    s.cubeVisible = cubeVisible;
    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(hwnd, &placement)) {
        s.window = placement.rcNormalPosition;
        s.maximized = placement.showCmd == SW_SHOWMAXIMIZED;
    }
    s.recent = recent.items();
    stp::saveSettings(s);
}

void Frame::setDpi(int value) {
    dpi = value;
    ribbon.setDpi(dpi);
    status.setDpi(dpi);
    panel.setDpi(dpi);
    start.setDpi(dpi);
    view.setDpi(dpi);
    layout();
    InvalidateRect(hwnd, nullptr, FALSE);
}

void Frame::openFile(const std::wstring& given) {
    // Ruta completa: la de la linea de comandos puede ser relativa y los recientes
    // tienen que servir desde cualquier carpeta.
    wchar_t full[MAX_PATH] = {};
    const DWORD length = GetFullPathNameW(given.c_str(), MAX_PATH, full, nullptr);
    const std::wstring path = length > 0 && length < MAX_PATH ? std::wstring(full) : given;
    panel.commitEdit();
    std::wstring problem;
    if (!view.saveMarks(&problem)) {
        const std::wstring question = problem + L"\n\nSi abres otro archivo, esas marcas se pierden. ¿Abrir de todos modos?";
        if (MessageBoxW(hwnd, question.c_str(), L"Visor STP", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    }
    g_currentFile = path;
    recent.add(path);
    start.setRecent(recent.items());
    saveAll();  // los recientes quedan guardados aunque el visor se cierre mal
    view.loadFile(path);
    layout();
    view.focus();
    panel.refresh();
    refreshAll();
}

void Frame::promptOpen() {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
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
    if (GetOpenFileNameW(&dialog)) openFile(path);
}

void Frame::saveMarks() {
    panel.commitEdit();
    if (!view.tools() || !view.hasModel()) return;
    std::wstring problem;
    const bool saved = view.saveMarks(&problem);
    view.tools()->showMessage(saved ? L"Marcas guardadas" : problem);
    updateTitle();
}

// Dialogo de guardar con un solo tipo. Devuelve la ruta o "" si se cancela.
std::wstring askSavePath(HWND owner, const std::wstring& suggested, const wchar_t* filter, const wchar_t* extension) {
    wchar_t path[MAX_PATH] = {};
    wcsncpy(path, suggested.c_str(), MAX_PATH - 1);
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = extension;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetSaveFileNameW(&dialog) ? std::wstring(path) : std::wstring();
}

std::wstring baseName() {
    std::wstring base = fileNameOf(g_currentFile);
    const std::size_t dot = base.find_last_of(L'.');
    return dot == std::wstring::npos ? base : base.substr(0, dot);
}

void Frame::exportPng() {
    panel.commitEdit();
    if (!view.tools() || !view.hasModel()) return;
    view.tools()->commitPendingEdit();
    const std::wstring target = askSavePath(hwnd, baseName() + L".png", L"Imagen PNG (*.png)\0*.png\0", L"png");
    if (target.empty()) return;
    RECT client;
    GetClientRect(view.hwnd(), &client);
    HBITMAP bitmap = view.renderSnapshot(view.camera(), 2 * client.right, 2 * client.bottom, 2.0);
    const bool ok = bitmap && stp::savePng(bitmap, target);
    if (bitmap) DeleteObject(bitmap);
    view.tools()->showMessage(ok ? L"Exportado: " + fileNameOf(target) : L"No se pudo exportar");
}

void Frame::exportPdf() {
    panel.commitEdit();
    if (!view.tools() || !view.hasModel()) return;
    view.tools()->commitPendingEdit();
    const std::wstring target = askSavePath(hwnd, baseName() + L"-revision.pdf", L"PDF con todas las vistas (*.pdf)\0*.pdf\0", L"pdf");
    if (target.empty()) return;
    stp::PdfWriter pdf;
    const double aspect = 1540.0 / 1000.0;
    int page = 1;
    bool ok = addViewPage(&pdf, view.overviewCamera(aspect), "Vista general", page++);
    const stp::MarkupDocument& doc = view.tools()->document();
    for (const stp::MarkupView& v : doc.views) ok = ok && addViewPage(&pdf, v.camera, v.name, page++);
    // Tabla de medidas y notas, 34 renglones por pagina.
    std::vector<std::string> rows;
    int n = 1;
    for (const stp::Mark& mark : doc.marks) {
        if (!stp::isMeasurement(mark.kind) && mark.kind != stp::MarkKind::Note) continue;
        std::string row = std::to_string(n++) + ".  " + utf8(view.tools()->describe(mark));
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
    view.tools()->showMessage(ok ? L"Exportado: " + fileNameOf(target) : L"No se pudo exportar");
}

int windowDpi(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static const auto fn = reinterpret_cast<GetDpiForWindowFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    if (fn) {
        const UINT dpi = fn(hwnd);
        if (dpi) return static_cast<int>(dpi);
    }
    HDC dc = GetDC(hwnd);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(hwnd, dc);
    return dpi;
}

// Sombra y bordes del sistema con un marco sin barra de titulo estandar (Windows 10+).
void extendFrame(HWND hwnd) {
    struct Margins {
        int left, right, top, bottom;
    };
    using ExtendFn = HRESULT(WINAPI*)(HWND, const Margins*);
    static HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    const auto extend = reinterpret_cast<ExtendFn>(reinterpret_cast<void*>(GetProcAddress(dwm, "DwmExtendFrameIntoClientArea")));
    const Margins margins = {0, 0, 1, 0};
    if (extend) extend(hwnd, &margins);
}

LRESULT CALLBACK frameProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Frame* f = g_frame;
    switch (msg) {
        case WM_NCCALCSIZE:
            if (wparam && f) {
                // Todo es area cliente: la barra de titulo la dibuja el visor. Maximizada,
                // Windows corre la ventana el ancho del borde fuera del monitor.
                if (IsZoomed(hwnd)) {
                    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
                    const int border = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                    params->rgrc[0].left += border;
                    params->rgrc[0].right -= border;
                    params->rgrc[0].top += border;
                    params->rgrc[0].bottom -= border;
                }
                return 0;
            }
            break;

        case WM_NCACTIVATE:
            if (f) {
                RECT strip = {0, 0, 10000, f->titleHeight()};
                InvalidateRect(hwnd, &strip, FALSE);
            }
            return DefWindowProcW(hwnd, msg, wparam, -1);

        case WM_NCHITTEST: {
            if (!f) break;
            POINT p = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &p);
            RECT client;
            GetClientRect(hwnd, &client);
            if (!IsZoomed(hwnd)) {
                const int edge = scale(6, f->dpi);
                const bool left = p.x < edge, right = p.x >= client.right - edge;
                const bool top = p.y < edge, bottom = p.y >= client.bottom - edge;
                if (top && left) return HTTOPLEFT;
                if (top && right) return HTTOPRIGHT;
                if (bottom && left) return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left) return HTLEFT;
                if (right) return HTRIGHT;
                if (top) return HTTOP;
                if (bottom) return HTBOTTOM;
            }
            if (p.y < f->titleHeight()) {
                if (f->buttonAt(p) >= 0) return HTCLIENT;
                if (p.x < scale(36, f->dpi)) return HTSYSMENU;
                return HTCAPTION;
            }
            return HTCLIENT;
        }

        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            const int dpi = f ? f->dpi : 96;
            info->ptMinTrackSize.x = scale(520, dpi);
            info->ptMinTrackSize.y = scale(400, dpi);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            if (f) f->paintTitle(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEMOVE:
            if (f) {
                if (!f->trackingMouse) {
                    TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
                    f->trackingMouse = TrackMouseEvent(&track) != FALSE;
                }
                const int hot = f->buttonAt(POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
                if (hot != f->hotButton) {
                    f->hotButton = hot;
                    RECT strip = {0, 0, 10000, f->titleHeight()};
                    InvalidateRect(hwnd, &strip, FALSE);
                }
            }
            return 0;
        case WM_MOUSELEAVE:
            if (f) {
                f->trackingMouse = false;
                f->hotButton = -1;
                RECT strip = {0, 0, 10000, f->titleHeight()};
                InvalidateRect(hwnd, &strip, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (f) f->pressedButton = f->buttonAt(POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
            return 0;
        case WM_LBUTTONUP:
            if (f) {
                // Solo si la pulsacion empezo en el mismo boton (no una que viene de un dialogo).
                const int button = f->buttonAt(POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
                const int pressed = f->pressedButton;
                f->pressedButton = -1;
                switch (button == pressed ? button : -1) {
                    case 0: ShowWindow(hwnd, SW_MINIMIZE); break;
                    case 1: ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE); break;
                    case 2: PostMessageW(hwnd, WM_CLOSE, 0, 0); break;
                    default: break;
                }
            }
            return 0;
        case WM_NCRBUTTONUP:
            if (wparam == HTCAPTION || wparam == HTSYSMENU) {
                HMENU menu = GetSystemMenu(hwnd, FALSE);
                const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, GET_X_LPARAM(lparam),
                                                  GET_Y_LPARAM(lparam), 0, hwnd, nullptr);
                if (chosen) PostMessageW(hwnd, WM_SYSCOMMAND, static_cast<WPARAM>(chosen), 0);
                return 0;
            }
            break;

        case WM_SIZE:
            if (f) f->layout();
            return 0;

        case kPanelResized:
            if (f) f->layout();
            return 0;

        case WM_DPICHANGED:
            if (f) {
                const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                             suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
                f->setDpi(HIWORD(wparam));
            }
            return 0;

        case WM_ACTIVATE:
            if (f) {
                RECT strip = {0, 0, 10000, f->titleHeight()};
                InvalidateRect(hwnd, &strip, FALSE);
            }
            break;

        case WM_COMMAND:
            if (f) {
                f->execute(LOWORD(wparam));
                if (LOWORD(wparam) != kCmdOpen) f->view.focus();
            }
            return 0;

        case WM_SETFOCUS:
            if (f) f->view.focus();
            return 0;

        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wparam);
            wchar_t path[MAX_PATH] = {};
            if (DragQueryFileW(drop, 0, path, MAX_PATH) && f) f->openFile(path);
            DragFinish(drop);
            SetForegroundWindow(hwnd);
            return 0;
        }

        case WM_CLOSE: {
            if (f) f->panel.commitEdit();
            std::wstring problem;
            if (f && !f->view.saveMarks(&problem)) {
                const std::wstring question = problem + L"\n\n¿Cerrar de todos modos?";
                if (MessageBoxW(hwnd, question.c_str(), L"Visor STP", MB_YESNO | MB_ICONWARNING) != IDYES) return 0;
            }
            if (f) f->saveAll();
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

// Atajos del marco que llegan con el foco en la vista.
int shortcutCommand(const MSG& msg) {
    if (msg.message != WM_KEYDOWN) return 0;
    const bool control = GetKeyState(VK_CONTROL) < 0;
    if (control && msg.wParam == 'O') return kCmdOpen;
    if (control && msg.wParam == 'S') return kCmdSaveMarks;
    if (control && msg.wParam == 'E') return kCmdExportPdf;
    if (msg.wParam == VK_F2) return kCmdPanel;
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR commandLine, int showCmd) {
    SetProcessDPIAware();

    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    cls.style = CS_DBLCLKS;
    cls.lpfnWndProc = &frameProc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    cls.hIconSm = cls.hIcon;
    cls.hbrBackground = CreateSolidBrush(gdi(kBackground));
    cls.lpszClassName = L"StpViewerFrame";
    if (!RegisterClassExW(&cls)) return 1;

    const stp::ViewerSettings settings = stp::loadSettings();
    Frame frame;
    frame.instance = instance;
    frame.recent.setItems(settings.recent);
    frame.panelVisible = settings.panelVisible;
    frame.statusVisible = settings.statusVisible;
    frame.cubeVisible = settings.cubeVisible;
    frame.panel.setPreferredWidth(settings.panelWidth);
    g_frame = &frame;
    HWND hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, cls.lpszClassName, L"Visor STP", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1280, 820, nullptr, nullptr, instance, nullptr);
    if (hwnd && settings.window.right > settings.window.left) {
        // Se guardo con GetWindowPlacement (coordenadas del area de trabajo): se
        // restaura igual, sin que la ventana se corra si la barra de tareas esta arriba.
        WINDOWPLACEMENT placement = {};
        placement.length = sizeof(placement);
        GetWindowPlacement(hwnd, &placement);
        placement.rcNormalPosition = settings.window;
        placement.showCmd = SW_HIDE;
        SetWindowPlacement(hwnd, &placement);
    }
    if (!hwnd) return 1;
    frame.hwnd = hwnd;
    frame.dpi = windowDpi(hwnd);
    extendFrame(hwnd);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);

    frame.ribbon.create(instance, hwnd, [&frame](int command) { return frame.state(command); });
    frame.ribbon.setDpi(frame.dpi);
    frame.ribbon.setTab(settings.ribbonTab);
    frame.ribbon.setCollapsed(settings.ribbonCollapsed);
    frame.status.create(instance, hwnd);
    frame.status.setDpi(frame.dpi);

    g_view = &frame.view;
    frame.view.setBudget(120000);
    frame.view.enableTools(true);
    frame.view.setChrome(true);
    frame.view.setCubeVisible(settings.cubeVisible);
    if (frame.view.tools()) frame.view.tools()->setSnapSettings(settings.snap);
    frame.view.setMarkupListener([&frame]() {
        frame.ribbon.refresh();
        frame.panel.refresh();
        frame.updateTitle();
    });
    frame.view.setStatusListener([&frame]() { frame.refreshStatus(); });
    RECT client;
    GetClientRect(hwnd, &client);
    if (!frame.view.create(instance, hwnd, client)) return 1;
    frame.view.setDpi(frame.dpi);  // la del monitor de la ventana, no la del sistema
    frame.panel.create(instance, hwnd);
    frame.panel.setDpi(frame.dpi);
    frame.panel.attach(&frame.view);
    frame.start.create(instance, hwnd);
    frame.start.setDpi(frame.dpi);
    frame.start.setRecent(frame.recent.items());
    frame.layout();

    ShowWindow(hwnd, settings.maximized && showCmd == SW_SHOWNORMAL ? SW_SHOWMAXIMIZED : showCmd);
    UpdateWindow(hwnd);

    // Con la linea vacia CommandLineToArgvW devuelve la ruta del propio programa.
    while (commandLine && (*commandLine == L' ' || *commandLine == L'\t')) ++commandLine;
    int argc = 0;
    LPWSTR* argv = commandLine && *commandLine ? CommandLineToArgvW(commandLine, &argc) : nullptr;
    if (argv) {
        if (argc >= 1 && argv[0] && argv[0][0]) frame.openFile(argv[0]);
        LocalFree(argv);
    }
    frame.refreshAll();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (const int command = shortcutCommand(msg)) {
            frame.execute(command);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_frame = nullptr;
    g_view = nullptr;
    return 0;
}
