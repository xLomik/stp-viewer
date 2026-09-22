// Visor 3D de archivos STEP: orbitar, desplazar, acercar.
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>

#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../engine/step_model.h"
#include "../render/renderer.h"

namespace {

constexpr UINT WM_MODEL_READY = WM_APP + 1;
constexpr UINT_PTR kQualityTimer = 1;

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], size);
    return out;
}

std::wstring fileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

struct LoadResult {
    stp::Mesh mesh;
    stp::LoadStats stats;
    std::wstring path;
    std::wstring error;
};

class Viewer {
public:
    bool create(HINSTANCE instance, int showCmd);
    void openFile(const std::wstring& path);
    HWND window() const { return m_hwnd; }

    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

private:
    LRESULT handle(UINT msg, WPARAM wparam, LPARAM lparam);
    void onPaint();
    void render(int supersample);
    void drawOverlay(HDC dc);
    void drawTriad(HDC dc);
    void fitView();
    void setStandardView(double yaw, double pitch);
    void zoomBy(double factor, int mouseX, int mouseY);
    void requestQualityPass();
    void invalidate() { InvalidateRect(m_hwnd, nullptr, FALSE); }
    int dpiScale(int value) const { return MulDiv(value, m_dpi, 96); }
    void promptOpen();

    HWND m_hwnd = nullptr;
    HINSTANCE m_instance = nullptr;
    int m_dpi = 96;

    stp::Mesh m_mesh;
    stp::LoadStats m_stats;
    stp::Camera m_camera;
    stp::RenderStyle m_style;
    stp::Framebuffer m_frame;
    int m_frameSupersample = 0;
    bool m_frameValid = false;

    std::wstring m_title;
    std::wstring m_message = L"Arrastra un archivo .stp o .step aqui  (Ctrl+O para abrir)";
    std::atomic<bool> m_loading{false};

    bool m_orbiting = false;
    bool m_panning = false;
    POINT m_lastMouse = {};
    int m_width = 0;
    int m_height = 0;
};

Viewer* g_viewer = nullptr;

bool readFileBytes(const std::wstring& path, std::string* out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        size.QuadPart > 512ll * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    out->resize(static_cast<size_t>(size.QuadPart));

    size_t done = 0;
    while (done < out->size()) {
        DWORD got = 0;
        const DWORD want =
            static_cast<DWORD>(std::min<size_t>(out->size() - done, 8u * 1024 * 1024));
        if (!ReadFile(file, &(*out)[done], want, &got, nullptr) || got == 0) break;
        done += got;
    }
    CloseHandle(file);
    out->resize(done);
    return done > 0;
}

void Viewer::openFile(const std::wstring& path) {
    if (m_loading.exchange(true)) return;
    m_message = L"Cargando " + fileNameOf(path) + L"...";
    m_frameValid = false;
    invalidate();

    HWND hwnd = m_hwnd;
    std::thread([hwnd, path]() {
        auto result = std::make_unique<LoadResult>();
        result->path = path;
        std::string bytes;
        if (!readFileBytes(path, &bytes)) {
            result->error = L"No se pudo leer el archivo";
        } else {
            std::string error;
            if (!stp::loadStepMemory(bytes.data(), bytes.size(), &result->mesh, &error,
                                     &result->stats, 0.0008)) {
                result->error = utf8ToWide(error);
            }
        }
        PostMessage(hwnd, WM_MODEL_READY, 0, reinterpret_cast<LPARAM>(result.release()));
    }).detach();
}

void Viewer::fitView() {
    if (!m_mesh.bounds.valid()) return;
    const double aspect = m_height > 0 ? static_cast<double>(m_width) / m_height : 1.0;
    m_camera.fit(m_mesh.bounds, aspect);
    m_frameValid = false;
    invalidate();
}

void Viewer::setStandardView(double yaw, double pitch) {
    m_camera.yaw = yaw;
    m_camera.pitch = pitch;
    fitView();
}

void Viewer::zoomBy(double factor, int, int) {
    m_camera.orthoHeight = std::max(1e-6, m_camera.orthoHeight * factor);
    m_camera.distance = std::max(1e-6, m_camera.distance * factor);
    m_frameValid = false;
    invalidate();
}

void Viewer::render(int supersample) {
    if (m_width <= 0 || m_height <= 0) return;
    m_style.supersample = supersample;
    stp::renderMesh(m_mesh, m_camera, m_style, m_width, m_height, &m_frame);
    m_frameSupersample = supersample;
    m_frameValid = true;
}

void Viewer::requestQualityPass() {
    SetTimer(m_hwnd, kQualityTimer, 120, nullptr);
}

void Viewer::drawTriad(HDC dc) {
    const int size = dpiScale(34);
    const int margin = dpiScale(22);
    const POINT center = {margin + size, m_height - margin - size};

    const stp::Vec3 right = m_camera.right();
    const stp::Vec3 up = m_camera.up();
    struct AxisInfo {
        stp::Vec3 dir;
        COLORREF color;
        const wchar_t* label;
    };
    const AxisInfo axes[3] = {{stp::Vec3(1, 0, 0), RGB(226, 96, 96), L"X"},
                              {stp::Vec3(0, 1, 0), RGB(120, 205, 120), L"Y"},
                              {stp::Vec3(0, 0, 1), RGB(110, 160, 235), L"Z"}};

    SetBkMode(dc, TRANSPARENT);
    for (const AxisInfo& axis : axes) {
        const double sx = dot(axis.dir, right) * size;
        const double sy = -dot(axis.dir, up) * size;
        const POINT tip = {center.x + static_cast<int>(sx), center.y + static_cast<int>(sy)};

        HPEN pen = CreatePen(PS_SOLID, std::max(1, dpiScale(2)), axis.color);
        HGDIOBJ old = SelectObject(dc, pen);
        MoveToEx(dc, center.x, center.y, nullptr);
        LineTo(dc, tip.x, tip.y);
        SelectObject(dc, old);
        DeleteObject(pen);

        SetTextColor(dc, axis.color);
        RECT label = {tip.x - dpiScale(8), tip.y - dpiScale(9), tip.x + dpiScale(9),
                      tip.y + dpiScale(9)};
        DrawTextW(dc, axis.label, 1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void Viewer::drawOverlay(HDC dc) {
    HFONT font = CreateFontW(-dpiScale(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);

    const int pad = dpiScale(12);
    if (!m_mesh.empty()) {
        const stp::Vec3 size = m_mesh.bounds.size();
        wchar_t line[512];
        swprintf(line, 512, L"%ls    %.2f x %.2f x %.2f    %d solidos, %d caras, %zu triangulos",
                 m_title.c_str(), size.x, size.y, size.z, m_stats.solids, m_stats.faces,
                 m_mesh.triangleCount());
        RECT box = {pad, pad, m_width - pad, pad + dpiScale(20)};
        SetTextColor(dc, RGB(226, 232, 238));
        DrawTextW(dc, line, -1, &box, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT help = {pad, m_height - pad - dpiScale(18), m_width - pad, m_height - pad};
        SetTextColor(dc, RGB(138, 152, 166));
        DrawTextW(dc,
                  L"Arrastrar: girar   |   Rueda: zoom   |   Boton derecho o medio: mover   |   "
                  L"F: encuadrar   |   1-6: vistas   |   W: alambre   |   E: aristas   |   P: "
                  L"perspectiva",
                  -1, &help, DT_RIGHT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (!m_message.empty()) {
        RECT box = {pad, m_height / 2 - dpiScale(14), m_width - pad, m_height / 2 + dpiScale(14)};
        SetTextColor(dc, RGB(198, 210, 222));
        DrawTextW(dc, m_message.c_str(), -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if (!m_mesh.empty()) drawTriad(dc);

    SelectObject(dc, oldFont);
    DeleteObject(font);
}

void Viewer::onPaint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(m_hwnd, &ps);

    if (!m_frameValid) render(m_orbiting || m_panning ? 1 : 2);

    if (m_frameValid && m_frame.width > 0) {
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = m_frame.width;
        info.bmiHeader.biHeight = -m_frame.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        SetDIBitsToDevice(dc, 0, 0, m_frame.width, m_frame.height, 0, 0, 0, m_frame.height,
                          m_frame.pixels.data(), &info, DIB_RGB_COLORS);
    } else {
        RECT client;
        GetClientRect(m_hwnd, &client);
        HBRUSH brush = CreateSolidBrush(RGB(24, 30, 38));
        FillRect(dc, &client, brush);
        DeleteObject(brush);
    }

    drawOverlay(dc);
    EndPaint(m_hwnd, &ps);
}

void Viewer::promptOpen() {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = m_hwnd;
    dialog.lpstrFilter = L"Archivos STEP (*.stp;*.step)\0*.stp;*.step\0Todos (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&dialog)) openFile(path);
}

LRESULT Viewer::handle(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_SIZE:
            m_width = LOWORD(lparam);
            m_height = HIWORD(lparam);
            m_frameValid = false;
            invalidate();
            return 0;

        case WM_DPICHANGED: {
            m_dpi = HIWORD(wparam);
            RECT* suggested = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(m_hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        case WM_PAINT:
            onPaint();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            SetCapture(m_hwnd);
            m_lastMouse.x = GET_X_LPARAM(lparam);
            m_lastMouse.y = GET_Y_LPARAM(lparam);
            m_orbiting = (msg == WM_LBUTTONDOWN);
            m_panning = (msg != WM_LBUTTONDOWN);
            return 0;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            ReleaseCapture();
            m_orbiting = false;
            m_panning = false;
            requestQualityPass();
            return 0;

        case WM_LBUTTONDBLCLK:
            fitView();
            return 0;

        case WM_MOUSEMOVE: {
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            const int dx = x - m_lastMouse.x;
            const int dy = y - m_lastMouse.y;
            m_lastMouse.x = x;
            m_lastMouse.y = y;
            if (m_mesh.empty()) return 0;

            if (m_orbiting) {
                m_camera.yaw -= dx * 0.01;
                m_camera.pitch += dy * 0.01;
                const double limit = 1.5533430;  // just under 90 degrees
                m_camera.pitch = std::max(-limit, std::min(limit, m_camera.pitch));
                m_frameValid = false;
                invalidate();
            } else if (m_panning) {
                const double perPixel =
                    m_camera.orthoHeight / std::max(1, m_height);
                m_camera.target = m_camera.target - m_camera.right() * (dx * perPixel) +
                                  m_camera.up() * (dy * perPixel);
                m_frameValid = false;
                invalidate();
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            const double factor = delta > 0 ? 0.88 : 1.0 / 0.88;
            zoomBy(factor, 0, 0);
            requestQualityPass();
            return 0;
        }

        case WM_TIMER:
            if (wparam == kQualityTimer) {
                KillTimer(m_hwnd, kQualityTimer);
                if (!m_orbiting && !m_panning && m_frameSupersample < 3 && !m_mesh.empty()) {
                    render(3);
                    invalidate();
                }
            }
            return 0;

        case WM_KEYDOWN:
            switch (wparam) {
                case 'F': fitView(); break;
                case '1': setStandardView(-1.5707963, 0.0); break;          // frente
                case '2': setStandardView(1.5707963, 0.0); break;           // atras
                case '3': setStandardView(3.1415927, 0.0); break;           // izquierda
                case '4': setStandardView(0.0, 0.0); break;                 // derecha
                case '5': setStandardView(-1.5707963, 1.5533430); break;    // superior
                case '6': setStandardView(-1.5707963, -1.5533430); break;   // inferior
                case '0':
                case '7': setStandardView(-0.7853982, 0.5235988); break;    // isometrica
                case 'W':
                    m_style.drawFaces = !m_style.drawFaces;
                    // Sin caras las aristas oscuras se pierden contra el fondo.
                    m_style.edgeColor = m_style.drawFaces ? 0xFF20282F : 0xFFD8E0E8;
                    m_style.drawEdges = true;
                    m_frameValid = false;
                    invalidate();
                    break;
                case 'E':
                    m_style.drawEdges = !m_style.drawEdges;
                    m_frameValid = false;
                    invalidate();
                    break;
                case 'P':
                    m_camera.ortho = !m_camera.ortho;
                    fitView();
                    break;
                case 'O':
                    if (GetKeyState(VK_CONTROL) < 0) promptOpen();
                    break;
                case VK_ESCAPE:
                    PostMessage(m_hwnd, WM_CLOSE, 0, 0);
                    break;
                default:
                    break;
            }
            return 0;

        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wparam);
            wchar_t path[MAX_PATH] = {};
            if (DragQueryFileW(drop, 0, path, MAX_PATH)) openFile(path);
            DragFinish(drop);
            SetForegroundWindow(m_hwnd);
            return 0;
        }

        case WM_MODEL_READY: {
            std::unique_ptr<LoadResult> result(reinterpret_cast<LoadResult*>(lparam));
            m_loading = false;
            if (!result->error.empty() || result->mesh.empty()) {
                m_message = result->error.empty() ? L"El archivo no contiene geometria legible"
                                                  : result->error;
                m_mesh = stp::Mesh();
                m_frameValid = false;
                invalidate();
                return 0;
            }
            m_mesh = std::move(result->mesh);
            m_stats = result->stats;
            m_title = fileNameOf(result->path);
            m_message.clear();
            SetWindowTextW(m_hwnd, (m_title + L" - stp-viewer").c_str());
            fitView();
            requestQualityPass();
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(m_hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK Viewer::proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (g_viewer) {
        g_viewer->m_hwnd = hwnd;
        return g_viewer->handle(msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool Viewer::create(HINSTANCE instance, int showCmd) {
    m_instance = instance;

    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    cls.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    cls.lpfnWndProc = &Viewer::proc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    cls.hIconSm = cls.hIcon;
    cls.lpszClassName = L"StpViewerWindow";
    if (!RegisterClassExW(&cls)) return false;

    m_hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, cls.lpszClassName, L"stp-viewer",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 760, nullptr,
                             nullptr, instance, nullptr);
    if (!m_hwnd) return false;

    HDC dc = GetDC(m_hwnd);
    if (dc) {
        m_dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(m_hwnd, dc);
    }

    m_style.backgroundTop = 0xFF33414F;
    m_style.backgroundBottom = 0xFF141A21;
    m_camera.ortho = true;

    ShowWindow(m_hwnd, showCmd);
    UpdateWindow(m_hwnd);
    return true;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR commandLine, int showCmd) {
    SetProcessDPIAware();

    Viewer viewer;
    g_viewer = &viewer;
    if (!viewer.create(instance, showCmd)) return 1;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(commandLine, &argc);
    if (argv) {
        if (argc >= 1 && argv[0] && argv[0][0]) viewer.openFile(argv[0]);
        LocalFree(argv);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
