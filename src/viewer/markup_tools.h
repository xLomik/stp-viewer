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

// Enganche a objetos (F3), modos y restriccion de acotado (Orto F8, Polar F10).
struct SnapSettings {
    bool enabled = true;
    unsigned modes = kSnapDefaultModes;
    SnapConstraint constraint;
};

// Glifo del marcador de enganche centrado en (x, y), de lado 2*size. graphics es
// un Gdiplus::Graphics*. Lo usan los marcadores y los iconos de la interfaz.
void drawSnapGlyph(void* graphics, SnapKind kind, float x, float y, float size, std::uint32_t argb, float width);

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
    bool active() const {
        return m_tool != Tool::Navigate || !m_pending.empty() || m_selected >= 0 || m_edit != nullptr;
    }
    // Confirma el texto de una nota que se esta escribiendo (antes de guardar o exportar).
    void commitPendingEdit() { closeNoteEditor(true); }

    bool handle(UINT msg, WPARAM wparam, LPARAM lparam);
    void draw(HDC dc, const Camera& camera, int width, int height, double scale, bool interactive) const;
    // Mensaje vigente o, si no hay, la ayuda de la herramienta activa.
    std::wstring statusText() const;

    const SnapSettings& snapSettings() const { return m_snap; }
    void setSnapSettings(const SnapSettings& settings);
    void toggleSnap();
    void toggleOrtho();
    void togglePolar();
    // Tab recorre los candidatos: la vista lo pide solo cuando hay mas de uno.
    bool wantsTab() const { return isMeasureTool() && m_candidates.size() > 1; }
    // Punto del modelo bajo el cursor (para la barra de estado).
    bool cursorPoint(Vec3* point) const {
        if (m_cursorValid) *point = m_cursorPoint;
        return m_cursorValid;
    }

    const MarkupDocument& document() const { return m_doc; }
    void setDocument(const MarkupDocument& doc);
    void clear();
    bool dirty() const { return m_dirty; }
    void markSaved() { m_dirty = false; }
    void showMessage(const std::wstring& text, unsigned milliseconds = 4000);
    std::wstring describe(const Mark& mark) const;
    // Para la lista lateral: (id de marca, texto) o (-id de vista, texto).
    std::vector<std::pair<int, std::wstring>> listEntries() const;
    void focusEntry(int code);

    void undo();
    void redo();
    void deleteSelected();
    void cycleColor();
    // Color de la proxima marca (resaltador: su propia paleta).
    void setColor(std::uint32_t argb);
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    std::uint32_t color() const { return m_tool == Tool::Highlight ? m_highlight : m_color; }
    int hiddenCount(const Camera& camera) const;  // marcas de otras vistas
    int selected() const { return m_selected; }
    const Mark* markById(int id) const;
    // Cambios desde el panel de propiedades, con deshacer.
    bool setMarkColor(int id, std::uint32_t argb);
    bool setNoteText(int id, const std::string& utf8);
    void select(int id);

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
    bool isSketchTool() const;
    void pushUndo();
    Vec3 unproject(int x, int y, const Vec3& through) const;
    int viewForSketch();
    void finishSketch();
    void openNoteEditor(int markId);
    void closeNoteEditor(bool commit);
    int hitTest(int x, int y) const;
    void drawSketch(void* graphics, const Mark& mark, const Camera& camera, int width, int height,
                    double scale) const;
    void drawNote(void* graphics, const Mark& mark, const Camera& camera, int width, int height,
                  double scale) const;
    bool screenBounds(const Mark& mark, const Camera& camera, int width, int height, RECT* out) const;
    static LRESULT CALLBACK editProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    std::uint32_t m_color = kMarkRed;
    std::uint32_t m_highlight = kHighlightYellow;
    std::vector<POINT> m_stroke;  // trazo o forma en curso, en pixeles
    bool m_sketching = false;
    bool m_noteAnchored = false;
    SnapResult m_noteAnchor;
    int m_selected = -1;
    POINT m_downAt = {0, 0};
    std::vector<MarkupDocument> m_undo, m_redo;
    HWND m_edit = nullptr;
    WNDPROC m_editDefault = nullptr;
    int m_editMark = -1;
    SnapResult snapAt(int x, int y);
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

    SnapSettings m_snap;
    std::vector<SnapResult> m_candidates;  // bajo el cursor, en el orden de Tab
    std::size_t m_candidate = 0;
    ConstrainedPoint m_constrained;        // restriccion aplicada al punto en curso
    bool m_cursorValid = false;
    Vec3 m_cursorPoint;
};

}  // namespace stp
