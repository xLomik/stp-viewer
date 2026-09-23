// Ventana 3D interactiva reutilizable: la usa el visor independiente y el
// manejador del panel de vista previa del Explorador.
#pragma once

#include <windows.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "../formats/formats.h"
#include "../render/renderer.h"
#include "markup_tools.h"

namespace stp {

struct SceneLoadChannel;
struct SceneLoadResult;

class SceneView : public MarkupHost {
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

    // Herramientas de medir y marcar. editing = false en el panel: solo medir.
    void enableTools(bool editing);
    MarkupTools* tools() { return m_tools.get(); }
    // Guarda <modelo>.marcas si hay cambios. false (y un mensaje) si no se pudo.
    bool saveMarks(std::wstring* message);
    const std::wstring& path() const { return m_path; }
    const Camera& camera() const { return m_camera; }
    bool toolActive() const { return m_tools && m_tools->active(); }
    void setMarkupListener(std::function<void()> listener) { m_markupListener = std::move(listener); }

    // MarkupHost
    HWND markupWindow() const override { return m_hwnd; }
    const Mesh& markupMesh() const override { return m_mesh; }
    const PickIndex& markupPick() const override;
    const Camera& markupCamera() const override { return m_camera; }
    void markupSetCamera(const Camera& camera) override;
    const PlanarInfo* markupPlane() const override { return m_plan2d ? &m_planar : nullptr; }
    int markupWidth() const override { return m_width; }
    int markupHeight() const override { return m_height; }
    int markupDpi() const override { return m_dpi; }
    void markupRedraw() override { invalidate(); }
    void markupChanged() override {
        if (m_markupListener) m_markupListener();
    }

private:
    LRESULT handle(UINT msg, WPARAM wparam, LPARAM lparam);

    void onPaint();
    void render(bool interactive);
    void pickQuality(bool interactive, int* supersample, double* scale) const;
    void drawOverlay(HDC dc);
    void drawTriad(HDC dc);
    void fitView();
    void setStandardView(double yaw, double pitch);
    void enterPlanView();
    void updateStyle();
    void drawScene(HDC dc);
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
    // Dibujo 2D: se mira de frente al plano y se navega como en un CAD.
    PlanarInfo m_planar;
    bool m_plan2d = false;
    Camera m_camera;
    RenderStyle m_style;
    Framebuffer m_frame;
    int m_frameSupersample = 0;
    double m_frameScale = 1.0;
    bool m_frameValid = false;
    bool m_frameInteractive = false;
    // Coste medido por muestra: permite elegir calidad segun la maquina en vez
    // de fijar una que va bien solo en equipos rapidos.
    double m_msPerSample = 0.0;
    bool m_compact = false;
    bool m_hostColors = false;
    COLORREF m_hostBackground = RGB(0, 0, 0);
    COLORREF m_drawingColor = RGB(226, 232, 238);  // textos del plano
    bool m_truncated = false;
    int m_budgetMs = 0;
    COLORREF m_textColor = RGB(226, 232, 238);
    COLORREF m_dimColor = RGB(138, 152, 166);

    std::wstring m_title;
    bool m_isDrawingFile = false;  // .dwg: su imagen es un plano
    std::wstring m_message;
    // Formatos propietarios: se muestra la imagen que el CAD dejo dentro.
    HBITMAP m_image = nullptr;
    int m_imageWidth = 0;
    int m_imageHeight = 0;
    std::shared_ptr<SceneLoadChannel> m_channel;
    std::atomic<unsigned> m_generation{0};
    std::atomic<bool> m_loading{false};

    void loadMarks();
    std::wstring m_path;  // vacio en el panel: ahi no hay ruta
    std::unique_ptr<MarkupTools> m_tools;
    std::shared_ptr<PickIndex> m_pick;
    std::function<void()> m_markupListener;
    bool m_toolsEnabled = false;

    bool m_orbiting = false;
    bool m_panning = false;
    POINT m_lastMouse = {};
};

// Lee un archivo completo en memoria. Devuelve false si no se puede.
bool readFileBytes(const std::wstring& path, std::string* out);

}  // namespace stp
