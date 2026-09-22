// Ventana 3D interactiva reutilizable: la usa el visor independiente y el
// manejador del panel de vista previa del Explorador.
#pragma once

#include <windows.h>

#include <atomic>
#include <memory>
#include <string>

#include "../engine/step_model.h"
#include "../render/renderer.h"

namespace stp {

struct SceneLoadChannel;
struct SceneLoadResult;

class SceneView {
public:
    // La clase de ventana la registra el propio componente.
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    ~SceneView();

    // Crea la ventana hija dentro de parent. Devuelve false si falla.
    bool create(HINSTANCE instance, HWND parent, const RECT& rect);
    void destroy();

    HWND hwnd() const { return m_hwnd; }
    void setRect(const RECT& rect);
    void focus();

    // Carga en segundo plano; la ventana muestra "Cargando..." mientras tanto.
    void loadFile(const std::wstring& path);
    void loadMemory(std::string bytes, const std::wstring& title);

    // El panel de vista previa impone los colores del tema del Explorador.
    void setHostColors(COLORREF background, COLORREF text);
    void setCompact(bool compact);
    // Tope de tiempo del mallado, en milisegundos (0 = sin tope).
    void setBudget(int milliseconds) { m_budgetMs = milliseconds; }

    bool hasModel() const { return !m_mesh.empty(); }

private:
    LRESULT handle(UINT msg, WPARAM wparam, LPARAM lparam);

    void onPaint();
    void render(int supersample);
    void drawOverlay(HDC dc);
    void drawTriad(HDC dc);
    void fitView();
    void setStandardView(double yaw, double pitch);
    void requestQualityPass();
    void invalidate();
    int scaled(int value) const { return MulDiv(value, m_dpi, 96); }
    void applyModel(SceneLoadResult* result);

    HWND m_hwnd = nullptr;
    HINSTANCE m_instance = nullptr;
    int m_dpi = 96;
    int m_width = 0;
    int m_height = 0;

    Mesh m_mesh;
    LoadStats m_stats;
    Camera m_camera;
    RenderStyle m_style;
    Framebuffer m_frame;
    int m_frameSupersample = 0;
    bool m_frameValid = false;
    bool m_compact = false;
    bool m_hostColors = false;
    bool m_truncated = false;
    int m_budgetMs = 0;
    COLORREF m_textColor = RGB(226, 232, 238);
    COLORREF m_dimColor = RGB(138, 152, 166);

    std::wstring m_title;
    std::wstring m_message;
    std::shared_ptr<SceneLoadChannel> m_channel;
    std::atomic<unsigned> m_generation{0};
    std::atomic<bool> m_loading{false};

    bool m_orbiting = false;
    bool m_panning = false;
    POINT m_lastMouse = {};
};

// Lee un archivo completo en memoria. Devuelve false si no se puede.
bool readFileBytes(const std::wstring& path, std::string* out);

}  // namespace stp
