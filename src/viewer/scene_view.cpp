#include "scene_view.h"

#include <windowsx.h>

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <memory>
#include <mutex>
#include <thread>

#include "image_view.h"
#include "text_overlay.h"

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

std::string narrowUtf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(0, size)), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], size, nullptr, nullptr);
    return out;
}

std::wstring markupPathOf(const std::wstring& model) { return model + L".marcas"; }

// Tamano y fecha de modificacion (UTC, "AAAA-MM-DDTHH:MM:SS").
bool fileStamp(const std::wstring& path, std::uint64_t* size, std::string* date) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    *size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    SYSTEMTIME t = {};
    FileTimeToSystemTime(&data.ftLastWriteTime, &t);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour,
                  t.wMinute, t.wSecond);
    *date = buffer;
    return true;
}

// Escribe en <ruta>.tmp y lo cambia por el original: un corte a mitad de camino
// deja el archivo anterior intacto.
bool writeFileAtomically(const std::wstring& path, const std::string& bytes) {
    const std::wstring temp = path + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                    written == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

}  // namespace

struct SceneLoadResult {
    Mesh mesh;
    LoadStats stats;
    std::wstring title;
    std::wstring error;
    std::vector<std::uint8_t> image;  // vista previa incrustada, si la hay
    PlanarInfo planar;
    std::shared_ptr<PickIndex> pick;
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
    m_camera.ortho = true;
    updateStyle();
    return true;
}

void SceneView::destroy() {
    if (m_image) {
        DeleteObject(m_image);
        m_image = nullptr;
    }
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
    m_hostBackground = background;
    m_textColor = text;
    m_dimColor = isLight(background) ? RGB(110, 120, 132) : RGB(150, 162, 176);
    updateStyle();
    invalidate();
}

// Colores segun el modo. En planos y alambres las lineas son todo el dibujo:
// claras sobre fondo oscuro y oscuras si el Explorador usa tema claro.
void SceneView::updateStyle() {
    const bool lightHost = m_hostColors && isLight(m_hostBackground);
    if (m_hostColors) {
        m_style.backgroundTop = toArgb(m_hostBackground);
        m_style.backgroundBottom = m_style.backgroundTop;
    } else if (m_plan2d) {
        m_style.backgroundTop = 0xFF1C232A;
        m_style.backgroundBottom = 0xFF1C232A;
    } else {
        m_style.backgroundTop = 0xFF33414F;
        m_style.backgroundBottom = 0xFF141A21;
    }
    m_style.transparentBackground = false;

    const bool linesOnly = m_plan2d || !m_style.drawFaces || m_mesh.indices.empty();
    if (linesOnly) {
        m_style.edgeColor = lightHost ? 0xFF1E2833 : 0xFFE3E9EF;
        // Rellenos (SOLID, 3DFACE de un plano) apagados para que las lineas se lean encima.
        m_style.faceColor = lightHost ? 0xFFCBD3DB : 0xFF3A4652;
    } else {
        m_style.edgeColor = lightHost ? 0xFF33414E : 0xFF20282F;
        m_style.faceColor = 0xFFB9C4CC;
    }
    m_style.edgeWidth = m_plan2d ? 1.25 : 1.1;
    m_drawingColor = RGB((m_style.edgeColor >> 16) & 0xFF, (m_style.edgeColor >> 8) & 0xFF,
                         m_style.edgeColor & 0xFF);
    m_frameValid = false;
}

void SceneView::invalidate() {
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
    if (m_statusListener) m_statusListener();
}

void SceneView::standardView(int index) {
    static const double views[7][2] = {{-1.5707963, 0.0}, {1.5707963, 0.0}, {3.1415927, 0.0}, {0.0, 0.0},
                                       {-1.5707963, 1.5533430}, {-1.5707963, -1.5533430}, {-0.7853982, 0.5235988}};
    if (index < 0 || index > 6 || m_mesh.empty()) return;
    setStandardView(views[index][0], views[index][1]);
}

void SceneView::togglePlan2d() {
    if (m_plan2d) setStandardView(-0.7853982, 0.5235988);
    else enterPlanView();
}

void SceneView::setShaded(bool on) {
    m_style.drawFaces = on;
    // Sin caras las aristas oscuras se pierden contra el fondo.
    if (!on) m_style.drawEdges = true;
    updateStyle();
    invalidate();
}

void SceneView::setEdges(bool on) {
    m_style.drawEdges = on;
    m_frameValid = false;
    invalidate();
}

void SceneView::setPerspective(bool on) {
    if (m_plan2d || on == !m_camera.ortho) return;  // un plano solo tiene sentido en ortografica
    m_camera.ortho = !on;
    fitView();
}

int SceneView::zoomPercent() const {
    if (m_mesh.empty() || m_fitSize <= 0) return 0;
    const double now = (m_camera.ortho || m_camera.planView) ? m_camera.orthoHeight : m_camera.distance;
    return now > 0 ? static_cast<int>(std::lround(100.0 * m_fitSize / now)) : 0;
}

std::wstring SceneView::statusText() const {
    if (m_tools) {
        const std::wstring text = m_tools->statusText();
        if (!text.empty()) return text;
    }
    if (m_mesh.empty()) return std::wstring();
    if (m_plan2d) return L"Arrastrar: mover  \u00b7  Rueda: zoom al cursor  \u00b7  Doble clic: encuadrar";
    return L"Arrastrar: girar  \u00b7  Bot\u00f3n derecho o medio: mover  \u00b7  Rueda: zoom  \u00b7  Doble clic: encuadrar";
}

void SceneView::loadFile(const std::wstring& path) {
    saveMarks(nullptr);
    // Las marcas del modelo anterior no pueden quedar vivas mientras carga el
    // nuevo: se guardarian con el nombre equivocado.
    if (m_tools) m_tools->clear();
    m_marksUnreadable = false;
    m_marksForeign = false;
    m_path = path;
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
    m_plan2d = false;
    m_camera.planView = false;
    m_frameValid = false;
    m_title = title;
    m_message = L"Cargando...";
    invalidate();
    UpdateWindow(m_hwnd);

    // La extension guia al lector; si falta, se deduce del contenido.
    std::string extension;
    const std::size_t dot = title.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        for (std::size_t i = dot; i < title.size(); ++i) {
            extension.push_back(static_cast<char>(std::tolower(static_cast<int>(title[i]))));
        }
    }

    m_isDrawingFile = extension == ".dwg";

    auto channel = m_channel;
    HWND hwnd = m_hwnd;
    const int budgetMs = m_budgetMs;
    const bool buildPick = m_toolsEnabled;
    std::thread([channel, hwnd, generation, title, budgetMs, extension, buildPick,
                 data = std::move(bytes)]() {
        auto result = std::make_unique<SceneLoadResult>();
        result->generation = generation;
        result->title = title;
        std::string error;
        // Tope de tiempo: mas vale una malla incompleta que una ventana colgada.
        if (!loadModel(data.data(), data.size(), extension, &result->mesh, &error, &result->stats,
                       0.0008, budgetMs)) {
            result->error = utf8ToWide(error);
            // Sin geometria legible aun queda la imagen que guardo el CAD.
            extractEmbeddedPreview(data.data(), data.size(), &result->image);
        } else {
            result->planar = detectPlanar(result->mesh);
            if (buildPick) {
                // El indice se arma en este hilo: en planos enormes tarda cientos de ms.
                result->pick = std::make_shared<PickIndex>();
                result->pick->build(result->mesh);
            }
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

    if (m_image) {
        DeleteObject(m_image);
        m_image = nullptr;
    }
    if (!result->image.empty()) {
        m_image = decodePreviewImage(result->image, &m_imageWidth, &m_imageHeight);
    }
    m_plan2d = false;
    m_camera.planView = false;
    if (m_image) {
        m_mesh = Mesh();
        m_pick.reset();
        if (m_tools) m_tools->clear();
        updateStyle();
        m_title = result->title;
        m_message.clear();
        m_frameValid = false;
        invalidate();
        return;
    }
    if (!result->error.empty() || result->mesh.empty()) {
        m_mesh = Mesh();
        m_pick.reset();
        if (m_tools) m_tools->clear();
        updateStyle();
        m_message = result->error.empty() ? L"El archivo no contiene geometria legible"
                                          : result->error;
        m_frameValid = false;
        invalidate();
        return;
    }
    m_mesh = std::move(result->mesh);
    m_pick = result->pick;
    if (m_tools) m_tools->clear();
    m_stats = result->stats;
    m_title = result->title;
    m_message.clear();
    m_truncated = result->stats.truncated;
    // Un plano 2D se abre de frente; en isometrica sus lineas se pierden.
    m_planar = result->planar;
    m_plan2d = m_planar.planar;
    updateStyle();
    fitView();
    requestQualityPass();
    loadMarks();
}

void SceneView::fitView() {
    if (!m_mesh.bounds.valid()) return;
    const double aspect = m_height > 0 ? static_cast<double>(m_width) / m_height : 1.0;
    if (m_plan2d) {
        // Deja libres las franjas de arriba y abajo, donde van la etiqueta y la ayuda.
        const double reserve = scaled(m_compact ? 22 : 30);
        const double usable = m_height > 0 ? std::max(0.5, (m_height - 2.0 * reserve) / m_height) : 1.0;
        m_camera.fitPlanar(m_planar, aspect, 1.04 / usable);
    } else {
        m_camera.planView = false;
        m_camera.fit(m_mesh.bounds, aspect);
    }
    m_fitSize = (m_camera.ortho || m_camera.planView) ? m_camera.orthoHeight : m_camera.distance;
    m_frameValid = false;
    invalidate();
}

void SceneView::setStandardView(double yaw, double pitch) {
    if (m_plan2d) {
        m_plan2d = false;
        updateStyle();
    }
    m_camera.planView = false;
    m_camera.yaw = yaw;
    m_camera.pitch = pitch;
    fitView();
}

void SceneView::enterPlanView() {
    if (!m_planar.planar) return;
    m_plan2d = true;
    updateStyle();
    fitView();
}

// Elige supermuestreo y escala de render para que un cuadro tarde lo previsto:
// unos 22 ms mientras se arrastra (fluido) y hasta 300 ms en reposo (nitido).
void SceneView::pickQuality(bool interactive, int* supersample, double* scale) const {
    *supersample = 1;
    *scale = 1.0;
    if (m_msPerSample <= 0.0) {
        // Primer cuadro: sin medida todavia, se arranca prudente.
        *supersample = interactive ? 1 : 2;
        return;
    }

    const double budgetMs = interactive ? 22.0 : 300.0;
    const double pixels = static_cast<double>(m_width) * m_height;
    struct Option {
        double scale;
        int supersample;
    };
    // De mas a menos calidad; se toma la primera que entra en el presupuesto.
    const Option options[] = {{1.0, 3}, {1.0, 2}, {1.0, 1}, {0.75, 1}, {0.5, 1}};
    for (const Option& option : options) {
        if (!interactive && option.scale < 1.0) continue;
        const double samples = pixels * option.scale * option.scale * option.supersample *
                               option.supersample;
        if (samples * m_msPerSample <= budgetMs) {
            *scale = option.scale;
            *supersample = option.supersample;
            return;
        }
    }
    *scale = interactive ? 0.5 : 1.0;
    *supersample = 1;
}

void SceneView::render(bool interactive) {
    if (m_width <= 0 || m_height <= 0) return;

    int supersample = 1;
    double scale = 1.0;
    pickQuality(interactive, &supersample, &scale);

    const int width = std::max(16, static_cast<int>(m_width * scale));
    const int height = std::max(16, static_cast<int>(m_height * scale));

    LARGE_INTEGER frequency = {}, start = {}, stop = {};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);

    m_style.supersample = supersample;
    renderMesh(m_mesh, m_camera, m_style, width, height, &m_frame);

    QueryPerformanceCounter(&stop);
    if (frequency.QuadPart > 0) {
        const double ms = 1000.0 * (stop.QuadPart - start.QuadPart) / frequency.QuadPart;
        const double samples = static_cast<double>(width) * height * supersample * supersample;
        if (samples > 0 && ms > 0) {
            const double measured = ms / samples;
            // Media suave: un cuadro raro no debe cambiar la calidad de golpe.
            m_msPerSample = m_msPerSample > 0 ? m_msPerSample * 0.6 + measured * 0.4 : measured;
        }
    }

    m_frameSupersample = supersample;
    m_frameScale = scale;
    m_frameInteractive = interactive;
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
    if (m_image) {
        RECT box = {pad, pad, m_width - pad, pad + scaled(20)};
        SetTextColor(dc, m_textColor);
        const wchar_t* note = m_isDrawingFile ? L"   Plano (imagen guardada por el CAD)"
                                              : L"   (vista previa guardada por el CAD)";
        DrawTextW(dc, (m_title + note).c_str(), -1, &box,
                  DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else if (m_chrome) {
        // Visor con cinta: titulo, medidas y ayuda van a la barra de titulo, al panel y a
        // la barra de estado. Sobre la vista quedan solo los ejes.
        if (!m_mesh.empty() && !m_plan2d) drawTriad(dc);
    } else if (!m_mesh.empty()) {
        const Vec3 size = m_mesh.bounds.size();
        wchar_t line[512];
        if (m_plan2d) {
            if (m_compact) {
                swprintf(line, 512, L"Plano 2D   %.1f x %.1f", m_planar.width, m_planar.height);
            } else {
                swprintf(line, 512, L"%ls    Plano 2D   %.2f x %.2f", m_title.c_str(),
                         m_planar.width, m_planar.height);
            }
        } else if (m_compact) {
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
        std::wstring hint;
        if (m_plan2d) {
            hint = m_compact ? L"Arrastrar: mover   Rueda: zoom   F: encuadrar   M: medir"
                             : L"Arrastrar: mover   |   Rueda: zoom al cursor   |   "
                               L"F o doble clic: encuadrar   |   7: ver en 3D";
        } else {
            hint = m_compact ? L"Arrastrar: girar   Rueda: zoom   F: encuadrar   M: medir"
                             : L"Arrastrar: girar   |   Rueda: zoom   |   Boton derecho o medio: "
                               L"mover   |   F: encuadrar   |   1-6: vistas   |   W: alambre   |   "
                               L"A: aristas   |   P: perspectiva";
            if (!m_compact && m_planar.planar) hint += L"   |   D: plano 2D";
        }
        if (!m_compact && m_tools) {
            hint += m_tools->editing() ? L"   |   M: medir   |   H U N R E C L: marcar   |   Ctrl+Z: deshacer"
                                       : L"   |   M: medir";
        }
        if (m_tools && !m_tools->statusText().empty()) {
            hint = m_tools->statusText();
            SetTextColor(dc, m_textColor);
        }
        DrawTextW(dc, hint.c_str(), -1, &help, DT_RIGHT | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (!m_plan2d) drawTriad(dc);
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
    HDC target = BeginPaint(m_hwnd, &ps);

    // Todo se compone en memoria y se copia de una vez: sin parpadeo de los
    // textos del plano al mover.
    HDC memory = CreateCompatibleDC(target);
    HBITMAP buffer = memory ? CreateCompatibleBitmap(target, std::max(1, m_width),
                                                     std::max(1, m_height))
                            : nullptr;
    if (memory && buffer) {
        HGDIOBJ old = SelectObject(memory, buffer);
        drawScene(memory);
        BitBlt(target, 0, 0, m_width, m_height, memory, 0, 0, SRCCOPY);
        SelectObject(memory, old);
    } else {
        drawScene(target);
    }
    if (buffer) DeleteObject(buffer);
    if (memory) DeleteDC(memory);
    EndPaint(m_hwnd, &ps);
}

void SceneView::drawScene(HDC dc) {
    if (m_image) {
        RECT client;
        GetClientRect(m_hwnd, &client);
        HBRUSH brush = CreateSolidBrush(m_hostColors ? RGB((m_style.backgroundTop >> 16) & 0xFF,
                                                           (m_style.backgroundTop >> 8) & 0xFF,
                                                           m_style.backgroundTop & 0xFF)
                                                     : RGB(24, 30, 38));
        FillRect(dc, &client, brush);
        DeleteObject(brush);

        const double scale = std::min(static_cast<double>(m_width) / std::max(1, m_imageWidth),
                                      static_cast<double>(m_height) / std::max(1, m_imageHeight));
        const int w = std::max(1, static_cast<int>(m_imageWidth * scale));
        const int h = std::max(1, static_cast<int>(m_imageHeight * scale));
        HDC memory = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(memory, m_image);
        SetStretchBltMode(dc, HALFTONE);
        StretchBlt(dc, (m_width - w) / 2, (m_height - h) / 2, w, h, memory, 0, 0, m_imageWidth,
                   m_imageHeight, SRCCOPY);
        SelectObject(memory, old);
        DeleteDC(memory);
        drawOverlay(dc);
        return;
    }

    if (!m_frameValid && !m_mesh.empty()) render(m_orbiting || m_panning);

    if (m_frameValid && m_frame.width > 0 && !m_mesh.empty()) {
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = m_frame.width;
        info.bmiHeader.biHeight = -m_frame.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        if (m_frame.width == m_width && m_frame.height == m_height) {
            SetDIBitsToDevice(dc, 0, 0, m_frame.width, m_frame.height, 0, 0, 0, m_frame.height,
                              m_frame.pixels.data(), &info, DIB_RGB_COLORS);
        } else {
            // Cuadro renderizado mas pequeno y estirado: es lo que mantiene el
            // giro fluido en equipos lentos.
            SetStretchBltMode(dc, HALFTONE);
            StretchDIBits(dc, 0, 0, m_width, m_height, 0, 0, m_frame.width, m_frame.height,
                          m_frame.pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        }
        drawMeshTexts(dc, m_mesh, m_camera, m_width, m_height, m_drawingColor);
        if (m_tools) m_tools->draw(dc, m_camera, m_width, m_height, 1.0, true);
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
}

void SceneView::loadMarks() {
    if (!m_tools || !m_tools->editing() || m_path.empty()) return;
    std::string text;
    const std::wstring file = markupPathOf(m_path);
    if (!readFileBytes(file, &text)) {
        if (GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES) {
            m_marksUnreadable = true;
            m_tools->showMessage(L"No se pudo leer el archivo de marcas; no se va a modificar", 10000);
        }
        return;  // sin marcas todavia
    }

    MarkupDocument doc;
    const MarkupParseReport report = parseMarkup(text, &doc);
    if (!report.recognized) {
        m_marksForeign = true;
        m_tools->showMessage(L"El archivo de marcas no se reconoce; se reemplaza solo si marcas algo", 10000);
        return;
    }
    m_tools->setDocument(doc);
    std::uint64_t size = 0;
    std::string date;
    if (report.badLines > 0 || report.newerVersion) {
        m_tools->showMessage(L"Algunas marcas no se pudieron leer; el archivo no se toca hasta que marques algo", 10000);
    } else if (fileStamp(m_path, &size, &date) && modelChanged(doc, size, date)) {
        m_tools->showMessage(L"El archivo cambi\u00F3 desde que se marc\u00F3", 10000);
    }
}

bool SceneView::saveMarks(std::wstring* message) {
    if (m_tools) m_tools->commitPendingEdit();
    if (!m_tools || !m_tools->editing() || m_path.empty() || !m_tools->dirty()) return true;
    const std::wstring target = markupPathOf(m_path);
    MarkupDocument doc = m_tools->document();
    fileStamp(m_path, &doc.modelSize, &doc.modelDate);
    doc.modelName = narrowUtf8(fileNameOf(m_path));

    if (m_marksUnreadable) {
        if (message) *message = L"El archivo de marcas no se pudo leer; no se sobrescribe: " + target;
        return false;
    }
    if (m_marksForeign && doc.marks.empty()) return true;  // nunca se borra un archivo ajeno

    bool ok = true;
    if (doc.marks.empty()) {
        ok = DeleteFileW(target.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
    } else {
        ok = writeFileAtomically(target, serializeMarkup(doc));
    }
    if (ok) {
        m_tools->markSaved();
    } else if (message) {
        *message = L"No se pudieron guardar las marcas en " + target;
    }
    return ok;
}

HBITMAP SceneView::renderSnapshot(const Camera& camera, int width, int height, double scale) {
    RenderStyle style = m_style;
    style.supersample = 2;
    style.edgeWidth *= scale;
    Framebuffer frame;
    renderMesh(m_mesh, camera, style, width, height, &frame);

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) return nullptr;
    memcpy(bits, frame.pixels.data(), frame.pixels.size() * sizeof(std::uint32_t));

    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ old = SelectObject(dc, bitmap);
    drawMeshTexts(dc, m_mesh, camera, width, height, m_drawingColor);
    if (m_tools) m_tools->draw(dc, camera, width, height, scale, false);
    GdiFlush();
    SelectObject(dc, old);
    DeleteDC(dc);
    // GDI deja el alfa en cero donde escribe; la imagen exportada es opaca.
    auto* pixels = static_cast<std::uint32_t*>(bits);
    for (std::size_t i = 0; i < frame.pixels.size(); ++i) pixels[i] |= 0xFF000000u;
    return bitmap;
}

Camera SceneView::overviewCamera(double aspect) const {
    Camera camera = m_camera;
    if (m_plan2d) {
        camera.fitPlanar(m_planar, aspect, 1.06);
    } else {
        camera.planView = false;
        camera.yaw = -0.7853981634;
        camera.pitch = 0.5235987756;
        camera.fit(m_mesh.bounds, aspect);
    }
    return camera;
}

void SceneView::enableTools(bool editing) {
    m_toolsEnabled = true;
    m_tools = std::make_unique<MarkupTools>(this, editing);
}

const PickIndex& SceneView::markupPick() const {
    static const PickIndex empty;
    return m_pick ? *m_pick : empty;
}

void SceneView::markupSetCamera(const Camera& camera) {
    m_camera = camera;
    m_frameValid = false;
    invalidate();
}

LRESULT SceneView::handle(UINT msg, WPARAM wparam, LPARAM lparam) {
    // Aviso de salida del cursor: las coordenadas y el marcador se borran.
    if (msg == WM_MOUSEMOVE && !m_trackingMouse) {
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, m_hwnd, 0};
        m_trackingMouse = TrackMouseEvent(&track) != FALSE;
    } else if (msg == WM_MOUSELEAVE) {
        m_trackingMouse = false;
    }
    if (m_tools && !m_mesh.empty() && m_tools->handle(msg, wparam, lparam)) {
        return msg == WM_SETCURSOR ? TRUE : 0;
    }
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
            // En un plano no hay nada que girar: cualquier boton mueve.
            m_orbiting = (msg == WM_LBUTTONDOWN) && !m_plan2d;
            m_panning = !m_orbiting;
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
            if (m_plan2d) {
                // Como en un CAD: se acerca hacia donde apunta el raton.
                POINT cursor = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                ScreenToClient(m_hwnd, &cursor);
                m_camera.zoomAt(factor, cursor.x, cursor.y, m_width, m_height);
            } else {
                m_camera.orthoHeight = std::max(1e-6, m_camera.orthoHeight * factor);
                m_camera.distance = std::max(1e-6, m_camera.distance * factor);
            }
            m_frameValid = false;
            invalidate();
            requestQualityPass();
            return 0;
        }

        case WM_TIMER:
            if (wparam == kQualityTimer) {
                KillTimer(m_hwnd, kQualityTimer);
                if (!m_orbiting && !m_panning && !m_mesh.empty() &&
                    (m_frameInteractive || m_frameScale < 1.0)) {
                    render(false);
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
                case 'D': enterPlanView(); break;
                case 'W': setShaded(!m_style.drawFaces); break;
                case 'A':
                case 'E': setEdges(!m_style.drawEdges); break;
                case 'P': setPerspective(m_camera.ortho); break;
                default:
                    break;
            }
            return 0;

        case WM_GETDLGCODE:
            // Tab solo cuando recorre candidatos de enganche; si no, mueve el foco.
            return DLGC_WANTARROWS | DLGC_WANTCHARS | (m_tools && m_tools->wantsTab() ? DLGC_WANTTAB : 0);

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
