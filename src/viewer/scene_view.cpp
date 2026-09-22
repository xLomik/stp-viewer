#include "scene_view.h"

#include <windowsx.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <thread>

namespace stp {
namespace {

constexpr UINT WM_SCENE_READY = WM_APP + 11;
constexpr UINT_PTR kQualityTimer = 1;
const wchar_t* kClassName = L"StpSceneView";

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], size);
    return out;
}

std::wstring fileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool isLight(COLORREF color) {
    return (GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114) / 1000 > 140;
}

std::uint32_t toArgb(COLORREF color) {
    return 0xFF000000u | (static_cast<std::uint32_t>(GetRValue(color)) << 16) |
           (static_cast<std::uint32_t>(GetGValue(color)) << 8) |
           static_cast<std::uint32_t>(GetBValue(color));
}

void ensureClass(HINSTANCE instance) {
    WNDCLASSEXW existing = {};
    existing.cbSize = sizeof(existing);
    if (GetClassInfoExW(instance, kClassName, &existing)) return;

    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    cls.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    cls.lpfnWndProc = &SceneView::proc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.lpszClassName = kClassName;
    RegisterClassExW(&cls);
}

}  // namespace

struct SceneLoadResult {
    Mesh mesh;
    LoadStats stats;
    std::wstring title;
    std::wstring error;
    unsigned generation = 0;
};

// Canal compartido entre el hilo de carga y la ventana: si la ventana muere
// antes de que termine la carga, el resultado se libera aqui y no se filtra.
struct SceneLoadChannel {
    std::mutex mutex;
    std::unique_ptr<SceneLoadResult> pending;
    bool dead = false;
};

bool readFileBytes(const std::wstring& path, std::string* out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 512ll * 1024 * 1024) {
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

SceneView::~SceneView() { destroy(); }

bool SceneView::create(HINSTANCE instance, HWND parent, const RECT& rect) {
    m_instance = instance;
    ensureClass(instance);

    m_channel = std::make_shared<SceneLoadChannel>();
    m_hwnd = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                             rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                             parent, nullptr, instance, this);
    if (!m_hwnd) return false;

    HDC dc = GetDC(m_hwnd);
    if (dc) {
        m_dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(m_hwnd, dc);
    }
    m_style.backgroundTop = 0xFF33414F;
    m_style.backgroundBottom = 0xFF141A21;
    m_camera.ortho = true;
    return true;
}

void SceneView::destroy() {
    if (m_channel) {
        std::lock_guard<std::mutex> lock(m_channel->mutex);
        m_channel->dead = true;
        m_channel->pending.reset();
    }
    ++m_generation;
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_channel.reset();
}

void SceneView::setRect(const RECT& rect) {
    if (!m_hwnd) return;
    SetWindowPos(m_hwnd, nullptr, rect.left, rect.top, rect.right - rect.left,
                 rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void SceneView::focus() {
    if (m_hwnd) SetFocus(m_hwnd);
}

void SceneView::setCompact(bool compact) {
    m_compact = compact;
    invalidate();
}

void SceneView::setHostColors(COLORREF background, COLORREF text) {
    m_hostColors = true;
    const std::uint32_t argb = toArgb(background);
    m_style.backgroundTop = argb;
    m_style.backgroundBottom = argb;
    m_style.transparentBackground = false;
    m_textColor = text;
    const bool light = isLight(background);
    m_dimColor = light ? RGB(110, 120, 132) : RGB(150, 162, 176);
    m_style.edgeColor = light ? 0xFF33414E : 0xFF20282F;
    m_frameValid = false;
    invalidate();
}

void SceneView::invalidate() {
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SceneView::loadFile(const std::wstring& path) {
    std::string bytes;
    if (!readFileBytes(path, &bytes)) {
        m_mesh = Mesh();
        m_message = L"No se pudo leer el archivo";
        m_frameValid = false;
        invalidate();
        return;
    }
    loadMemory(std::move(bytes), fileNameOf(path));
}

void SceneView::loadMemory(std::string bytes, const std::wstring& title) {
    if (!m_hwnd || !m_channel) return;

    const unsigned generation = ++m_generation;
    m_loading = true;
    m_mesh = Mesh();
    m_frameValid = false;
    m_title = title;
    m_message = L"Cargando...";
    invalidate();
    UpdateWindow(m_hwnd);

    auto channel = m_channel;
    HWND hwnd = m_hwnd;
    const int budgetMs = m_budgetMs;
    std::thread([channel, hwnd, generation, title, budgetMs, data = std::move(bytes)]() {
        auto result = std::make_unique<SceneLoadResult>();
        result->generation = generation;
        result->title = title;
        std::string error;
        // Tope de tiempo: mas vale una malla incompleta que una ventana colgada.
        if (!loadStepMemory(data.data(), data.size(), &result->mesh, &error, &result->stats,
                            0.0008, budgetMs)) {
            result->error = utf8ToWide(error);
        }

        std::lock_guard<std::mutex> lock(channel->mutex);
        if (channel->dead) return;
        channel->pending = std::move(result);
        if (!PostMessageW(hwnd, WM_SCENE_READY, 0, 0)) channel->pending.reset();
    }).detach();
}

void SceneView::applyModel(SceneLoadResult* result) {
    m_loading = false;
    if (!result || result->generation != m_generation.load()) return;

    if (!result->error.empty() || result->mesh.empty()) {
        m_mesh = Mesh();
        m_message = result->error.empty() ? L"El archivo no contiene geometria legible"
                                          : result->error;
        m_frameValid = false;
        invalidate();
        return;
    }
    m_mesh = std::move(result->mesh);
    m_stats = result->stats;
    m_title = result->title;
    m_message.clear();
    m_truncated = result->stats.truncated;
    fitView();
    requestQualityPass();
}

void SceneView::fitView() {
    if (!m_mesh.bounds.valid()) return;
    const double aspect = m_height > 0 ? static_cast<double>(m_width) / m_height : 1.0;
    m_camera.fit(m_mesh.bounds, aspect);
    m_frameValid = false;
    invalidate();
}

void SceneView::setStandardView(double yaw, double pitch) {
    m_camera.yaw = yaw;
    m_camera.pitch = pitch;
    fitView();
}

void SceneView::render(int supersample) {
    if (m_width <= 0 || m_height <= 0) return;
    m_style.supersample = supersample;
    renderMesh(m_mesh, m_camera, m_style, m_width, m_height, &m_frame);
    m_frameSupersample = supersample;
    m_frameValid = true;
}

void SceneView::requestQualityPass() {
    if (m_hwnd) SetTimer(m_hwnd, kQualityTimer, 120, nullptr);
}

void SceneView::drawTriad(HDC dc) {
    const int size = scaled(m_compact ? 22 : 34);
    const int margin = scaled(m_compact ? 16 : 22);
    const POINT center = {margin + size, m_height - margin - size};

    const Vec3 right = m_camera.right();
    const Vec3 up = m_camera.up();
    struct AxisInfo {
        Vec3 dir;
        COLORREF color;
        const wchar_t* label;
    };
    const AxisInfo axes[3] = {{Vec3(1, 0, 0), RGB(214, 84, 84), L"X"},
                              {Vec3(0, 1, 0), RGB(96, 176, 96), L"Y"},
                              {Vec3(0, 0, 1), RGB(86, 140, 220), L"Z"}};

    SetBkMode(dc, TRANSPARENT);
    for (const AxisInfo& axis : axes) {
        const double sx = dot(axis.dir, right) * size;
        const double sy = -dot(axis.dir, up) * size;
        const POINT tip = {center.x + static_cast<int>(sx), center.y + static_cast<int>(sy)};

        HPEN pen = CreatePen(PS_SOLID, std::max(1, scaled(2)), axis.color);
        HGDIOBJ old = SelectObject(dc, pen);
        MoveToEx(dc, center.x, center.y, nullptr);
        LineTo(dc, tip.x, tip.y);
        SelectObject(dc, old);
        DeleteObject(pen);

        SetTextColor(dc, axis.color);
        RECT label = {tip.x - scaled(8), tip.y - scaled(9), tip.x + scaled(9), tip.y + scaled(9)};
        DrawTextW(dc, axis.label, 1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void SceneView::drawOverlay(HDC dc) {
    HFONT font = CreateFontW(-scaled(m_compact ? 11 : 12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);

    const int pad = scaled(m_compact ? 8 : 12);
    if (!m_mesh.empty()) {
        const Vec3 size = m_mesh.bounds.size();
        wchar_t line[512];
        if (m_compact) {
            swprintf(line, 512, L"%.1f x %.1f x %.1f    %zu triangulos", size.x, size.y, size.z,
                     m_mesh.triangleCount());
        } else {
            swprintf(line, 512, L"%ls    %.2f x %.2f x %.2f    %d solidos, %d caras, %zu triangulos",
                     m_title.c_str(), size.x, size.y, size.z, m_stats.solids, m_stats.faces,
                     m_mesh.triangleCount());
        }
        RECT box = {pad, pad, m_width - pad, pad + scaled(20)};
        SetTextColor(dc, m_textColor);
        DrawTextW(dc, line, -1, &box, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (m_truncated) {
            RECT warn = {pad, pad + scaled(18), m_width - pad, pad + scaled(36)};
            SetTextColor(dc, RGB(226, 170, 90));
            DrawTextW(dc, L"Modelo muy grande: se muestra solo una parte", -1, &warn,
                      DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        RECT help = {pad, m_height - pad - scaled(18), m_width - pad, m_height - pad};
        SetTextColor(dc, m_dimColor);
        const wchar_t* hint =
            m_compact ? L"Arrastrar: girar   Rueda: zoom   F: encuadrar"
                      : L"Arrastrar: girar   |   Rueda: zoom   |   Boton derecho o medio: mover   "
                        L"|   F: encuadrar   |   1-6: vistas   |   W: alambre   |   E: aristas   "
                        L"|   P: perspectiva";
        DrawTextW(dc, hint, -1, &help, DT_RIGHT | DT_SINGLELINE | DT_END_ELLIPSIS);
        drawTriad(dc);
    }
    if (!m_message.empty()) {
        RECT box = {pad, m_height / 2 - scaled(14), m_width - pad, m_height / 2 + scaled(14)};
        SetTextColor(dc, m_textColor);
        DrawTextW(dc, m_message.c_str(), -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(dc, oldFont);
    DeleteObject(font);
}

void SceneView::onPaint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(m_hwnd, &ps);

    if (!m_frameValid && !m_mesh.empty()) render(m_orbiting || m_panning ? 1 : 2);

    if (m_frameValid && m_frame.width > 0 && !m_mesh.empty()) {
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
        const COLORREF fill =
            m_hostColors ? RGB((m_style.backgroundTop >> 16) & 0xFF,
                               (m_style.backgroundTop >> 8) & 0xFF, m_style.backgroundTop & 0xFF)
                         : RGB(24, 30, 38);
        HBRUSH brush = CreateSolidBrush(fill);
        FillRect(dc, &client, brush);
        DeleteObject(brush);
    }

    drawOverlay(dc);
    EndPaint(m_hwnd, &ps);
}

LRESULT SceneView::handle(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_SIZE:
            m_width = LOWORD(lparam);
            m_height = HIWORD(lparam);
            m_frameValid = false;
            invalidate();
            return 0;

        case WM_PAINT:
            onPaint();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_SCENE_READY: {
            std::unique_ptr<SceneLoadResult> result;
            if (m_channel) {
                std::lock_guard<std::mutex> lock(m_channel->mutex);
                result = std::move(m_channel->pending);
            }
            applyModel(result.get());
            return 0;
        }

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            SetFocus(m_hwnd);
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
                const double limit = 1.5533430;  // algo menos de 90 grados
                m_camera.pitch = std::max(-limit, std::min(limit, m_camera.pitch));
                m_frameValid = false;
                invalidate();
            } else if (m_panning) {
                const double perPixel = m_camera.orthoHeight / std::max(1, m_height);
                m_camera.target = m_camera.target - m_camera.right() * (dx * perPixel) +
                                  m_camera.up() * (dy * perPixel);
                m_frameValid = false;
                invalidate();
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            if (m_mesh.empty()) return 0;
            const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            const double factor = delta > 0 ? 0.88 : 1.0 / 0.88;
            m_camera.orthoHeight = std::max(1e-6, m_camera.orthoHeight * factor);
            m_camera.distance = std::max(1e-6, m_camera.distance * factor);
            m_frameValid = false;
            invalidate();
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
                case '1': setStandardView(-1.5707963, 0.0); break;
                case '2': setStandardView(1.5707963, 0.0); break;
                case '3': setStandardView(3.1415927, 0.0); break;
                case '4': setStandardView(0.0, 0.0); break;
                case '5': setStandardView(-1.5707963, 1.5533430); break;
                case '6': setStandardView(-1.5707963, -1.5533430); break;
                case '0':
                case '7': setStandardView(-0.7853982, 0.5235988); break;
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
                default:
                    break;
            }
            return 0;

        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;

        default:
            break;
    }
    return DefWindowProcW(m_hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK SceneView::proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* view = static_cast<SceneView*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
        if (view) view->m_hwnd = hwnd;
    }
    auto* view = reinterpret_cast<SceneView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (view) return view->handle(msg, wparam, lparam);
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace stp
