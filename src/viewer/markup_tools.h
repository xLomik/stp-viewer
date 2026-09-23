// Herramientas de medir y marcar sobre la vista 3D/2D. SceneView les pasa los
// eventos cuando hay una herramienta activa y les pide dibujar encima del render.
#pragma once

#include <windows.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "../engine/markup.h"
#include "../engine/measure.h"

namespace stp {

enum class Tool { Navigate, Distance, Radius, Angle, Area, Highlight, Underline, Note, Rectangle, Ellipse, Cloud, Pen };

// Lo que las herramientas necesitan de la vista que las hospeda.
class MarkupHost {
public:
    virtual ~MarkupHost() = default;
    virtual HWND markupWindow() const = 0;
    virtual const Mesh& markupMesh() const = 0;
    virtual const PickIndex& markupPick() const = 0;
    virtual const Camera& markupCamera() const = 0;
    virtual void markupSetCamera(const Camera& camera) = 0;
    virtual const PlanarInfo* markupPlane() const = 0;  // null si la vista esta en 3D
    virtual int markupWidth() const = 0;
    virtual int markupHeight() const = 0;
    virtual int markupDpi() const = 0;
    virtual void markupRedraw() = 0;   // repinta sin volver a renderizar la malla
    virtual void markupChanged() = 0;  // marcas o herramienta cambiaron: barra, lista, titulo
};

class MarkupTools {
public:
    MarkupTools(MarkupHost* host, bool editing);

    bool editing() const { return m_editing; }
    Tool tool() const { return m_tool; }
    void setTool(Tool tool);
    // Algo en curso que Esc deberia cancelar antes de cerrar la ventana.
    bool active() const { return m_tool != Tool::Navigate || !m_pending.empty(); }

    bool handle(UINT msg, WPARAM wparam, LPARAM lparam);
    void draw(HDC dc, const Camera& camera, int width, int height, double scale, bool interactive) const;
    std::wstring hint() const;

    const MarkupDocument& document() const { return m_doc; }
    void setDocument(const MarkupDocument& doc);
    void clear();
    bool dirty() const { return m_dirty; }
    void markSaved() { m_dirty = false; }
    void showMessage(const std::wstring& text);
    std::wstring describe(const Mark& mark) const;

private:
    struct Value {
        bool ok = false;
        std::wstring label;   // primera linea (valor principal)
        std::wstring detail;  // segunda linea (deltas, perimetro)
        std::wstring error;
        Vec3 center;          // radio
        std::vector<Vec3> lines;  // angulo entre rectas: cruce, punto en A, punto en B
        AreaResult area;
    };

    bool isMeasureTool() const;
    SnapResult snapAt(int x, int y) const;
    void addMark(Mark mark);
    void clickMeasure(const SnapResult& snap);
    const Value& valueOf(const Mark& mark) const;
    Value evaluate(const Mark& mark) const;
    void drawMeasure(void* graphics, const Mark& mark, const Camera& camera, int width, int height,
                     double scale) const;
    void drawSnap(void* graphics, double scale) const;
    void drawLabel(void* graphics, double x, double y, const std::wstring& first,
                   const std::wstring& second, std::uint32_t color, double scale) const;

    MarkupHost* m_host;
    bool m_editing;
    Tool m_tool = Tool::Navigate;
    MarkupDocument m_doc;
    bool m_dirty = false;
    std::vector<SnapResult> m_pending;  // clics de la medida en curso
    SnapResult m_hover;
    POINT m_mouse = {-1, -1};
    std::wstring m_message;
    ULONGLONG m_messageUntil = 0;
    mutable std::unordered_map<int, Value> m_values;
};

}  // namespace stp
