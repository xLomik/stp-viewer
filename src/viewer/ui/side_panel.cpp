#include "side_panel.h"

#include <windowsx.h>

#include <algorithm>

#include "../scene_view.h"
#include "commands.h"
#include "theme.h"

namespace stp {
namespace ui {
namespace {

constexpr wchar_t kClass[] = L"StpViewerSidePanel";
const std::uint32_t kSwatches[] = {kMarkRed, kMarkYellow, kMarkGreen, kMarkBlue, kMarkBlack, kMeasureColor};

Icon iconOf(MarkKind kind) {
    switch (kind) {
        case MarkKind::Distance: return Icon::Distance;
        case MarkKind::Radius: return Icon::Radius;
        case MarkKind::Angle: return Icon::Angle;
        case MarkKind::Area: return Icon::Area;
        case MarkKind::Note: return Icon::Note;
        case MarkKind::Highlight: return Icon::Highlight;
        case MarkKind::Underline: return Icon::Underline;
        case MarkKind::Pen: return Icon::Pen;
        case MarkKind::Rectangle: return Icon::Rectangle;
        case MarkKind::Ellipse: return Icon::Ellipse;
        case MarkKind::Cloud: return Icon::Cloud;
    }
    return Icon::None;
}

const wchar_t* kindName(MarkKind kind) {
    switch (kind) {
        case MarkKind::Distance: return L"Distancia";
        case MarkKind::Radius: return L"Radio";
        case MarkKind::Angle: return L"Ángulo";
        case MarkKind::Area: return L"Área";
        case MarkKind::Note: return L"Nota";
        case MarkKind::Highlight: return L"Resaltado";
        case MarkKind::Underline: return L"Subrayado";
        case MarkKind::Pen: return L"Trazo";
        case MarkKind::Rectangle: return L"Rectángulo";
        case MarkKind::Ellipse: return L"Elipse";
        case MarkKind::Cloud: return L"Nube de revisión";
    }
    return L"";
}

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(0, size)), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], size, nullptr, nullptr);
    return out;
}

std::wstring fromUtf8(const std::string& text) { return widen(text.c_str()); }

// El EDIT de Windows quiere \r\n; las notas guardan \n.
std::wstring toEdit(const std::wstring& text) {
    std::wstring out;
    for (wchar_t c : text) {
        if (c == L'\n') out += L"\r\n";
        else out += c;
    }
    return out;
}

std::wstring fromEdit(const std::wstring& text) {
    std::wstring out;
    for (wchar_t c : text) {
        if (c != L'\r') out += c;
    }
    while (!out.empty() && (out.back() == L'\n' || out.back() == L' ')) out.pop_back();
    return out;
}

std::wstring number(double value) { return widen(formatNumber(value, 3).c_str()); }

}  // namespace

bool SidePanel::create(HINSTANCE instance, HWND parent) {
    m_parent = parent;
    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = &SidePanel::proc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.lpszClassName = kClass;
    RegisterClassExW(&cls);
    m_hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 100, 100, parent, nullptr,
                             instance, this);
    if (!m_hwnd) return false;
    m_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL, 0, 0,
                             10, 10, m_hwnd, nullptr, instance, nullptr);
    m_editBrush = CreateSolidBrush(gdi(kBackground));
    if (m_edit) {
        SetWindowLongPtrW(m_edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        m_editDefault = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(m_edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&SidePanel::editProc)));
    }
    setDpi(m_dpi);
    return true;
}

void SidePanel::setDpi(int dpi) {
    m_dpi = dpi;
    HFONT old = m_editFont;
    m_editFont = CreateFontW(-scaled(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (m_edit) SendMessageW(m_edit, WM_SETFONT, reinterpret_cast<WPARAM>(m_editFont), TRUE);
    if (old) DeleteObject(old);  // despues: el EDIT ya usa la nueva
    refresh();
}

void SidePanel::setPreferredWidth(int width) { m_width = std::max(200, std::min(480, width)); }

int SidePanel::splitY() const {
    RECT client;
    GetClientRect(m_hwnd, &client);
    const int free = std::max(0, static_cast<int>(client.bottom) - 2 * headerHeight() - scaled(5));
    return headerHeight() + static_cast<int>(free * m_split);
}

RECT SidePanel::listRect() const {
    RECT client;
    GetClientRect(m_hwnd, &client);
    return RECT{scaled(4), headerHeight(), client.right, splitY()};
}

RECT SidePanel::propsRect() const {
    RECT client;
    GetClientRect(m_hwnd, &client);
    return RECT{scaled(4), splitY() + scaled(5) + headerHeight(), client.right, client.bottom};
}

void SidePanel::refresh() {
    rebuild();
    layoutEdit();
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SidePanel::rebuild() {
    m_rows.clear();
    m_props.clear();
    m_editingNote = -1;
    MarkupTools* tools = m_view ? m_view->tools() : nullptr;
    if (!tools || !m_view->hasModel()) return;
    const MarkupDocument& doc = tools->document();

    auto addMarks = [&](int view, int depth) {
        for (const Mark& mark : doc.marks) {
            if (mark.view != view) continue;
            std::wstring text = tools->describe(mark);
            const std::size_t newline = text.find(L'\n');
            if (newline != std::wstring::npos) text = text.substr(0, newline) + L"…";
            m_rows.push_back({mark.id, depth, text, static_cast<int>(mark.kind), view, false});
        }
    };
    int loose = 0;
    for (const Mark& mark : doc.marks) loose += mark.view == 0 ? 1 : 0;
    if (loose > 0) {
        const bool open = m_closed.count(0) == 0;
        m_rows.push_back({0, 0, L"Modelo (" + std::to_wstring(loose) + L")", -1, 0, open});
        if (open) addMarks(0, 1);
    }
    for (const MarkupView& view : doc.views) {
        int count = 0;
        for (const Mark& mark : doc.marks) count += mark.view == view.id ? 1 : 0;
        const bool open = m_closed.count(view.id) == 0;
        m_rows.push_back({-view.id, 0, fromUtf8(view.name) + L" (" + std::to_wstring(count) + L")", -1, view.id, open});
        if (open) addMarks(view.id, 1);
    }

    const Mark* selected = tools->selected() >= 0 ? tools->markById(tools->selected()) : nullptr;
    if (selected) {
        m_props.push_back({L"Tipo", kindName(selected->kind)});
        if (isMeasurement(selected->kind)) {
            std::wstring value = tools->describe(*selected);
            const std::wstring prefix = std::wstring(kindName(selected->kind)) + L"  ";
            if (value.compare(0, prefix.size(), prefix) == 0) value = value.substr(prefix.size());
            m_props.push_back({L"Valor", value});
        }
        const MarkupView* view = doc.findView(selected->view);
        m_props.push_back({L"Vista", view ? fromUtf8(view->name) : std::wstring(L"Modelo (todas)")});
        m_props.push_back({L"Color", L""});
        if (selected->kind == MarkKind::Note) {
            m_editingNote = selected->id;
            m_props.push_back({L"Texto", L""});
            if (m_edit && GetFocus() != m_edit) SetWindowTextW(m_edit, toEdit(fromUtf8(selected->text)).c_str());
        }
        return;
    }

    const Mesh& mesh = m_view->mesh();
    const std::wstring& path = m_view->path();
    const std::size_t slash = path.find_last_of(L"\\/");
    const std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const std::size_t dot = name.find_last_of(L'.');
    std::wstring format = dot == std::wstring::npos ? L"" : name.substr(dot + 1);
    for (wchar_t& c : format) c = static_cast<wchar_t>(towupper(c));
    const std::wstring units = widen(unitSuffix(mesh.units));
    m_props.push_back({L"Archivo", name});
    m_props.push_back({L"Formato", format});
    m_props.push_back({L"Tipo", m_view->plan2d() || m_view->planar().planar ? L"Plano 2D" : L"Modelo 3D"});
    m_props.push_back({L"Unidades", units.empty() ? L"No declaradas" : units});
    if (m_view->planar().planar) {
        m_props.push_back({L"Tamaño", number(m_view->planar().width) + L" × " + number(m_view->planar().height) +
                                               (units.empty() ? L"" : L" " + units)});
    } else {
        const Vec3 size = mesh.bounds.size();
        m_props.push_back({L"Tamaño", number(size.x) + L" × " + number(size.y) + L" × " + number(size.z) +
                                               (units.empty() ? L"" : L" " + units)});
    }
    const LoadStats& stats = m_view->stats();
    if (stats.solids > 0) m_props.push_back({L"Sólidos", std::to_wstring(stats.solids)});
    if (stats.faces > 0) m_props.push_back({L"Caras", std::to_wstring(stats.faces)});
    if (mesh.triangleCount() > 0) m_props.push_back({L"Triángulos", std::to_wstring(mesh.triangleCount())});
    m_props.push_back({L"Aristas", std::to_wstring(mesh.edgeLines.size() / 2)});
    if (!mesh.texts.empty()) m_props.push_back({L"Textos", std::to_wstring(mesh.texts.size())});
    m_props.push_back({L"Marcas", std::to_wstring(doc.marks.size())});
}

void SidePanel::layoutEdit() {
    if (!m_edit) return;
    if (m_editingNote < 0) {
        ShowWindow(m_edit, SW_HIDE);
        return;
    }
    const RECT props = propsRect();
    const int row = rowHeight();
    const int top = props.top + scaled(4) + static_cast<int>(m_props.size()) * row;
    MoveWindow(m_edit, props.left + scaled(8), top, props.right - props.left - scaled(16),
               std::max(row, std::min(scaled(110), static_cast<int>(props.bottom) - top - scaled(8))), TRUE);
    ShowWindow(m_edit, SW_SHOWNA);
}

void SidePanel::commitNote() {
    MarkupTools* tools = m_view ? m_view->tools() : nullptr;
    if (!tools || m_editingNote < 0 || !m_edit) return;
    const int length = GetWindowTextLengthW(m_edit);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(m_edit, &text[0], length + 1);
    text.resize(static_cast<std::size_t>(length));
    tools->setNoteText(m_editingNote, toUtf8(fromEdit(text)));
}

void SidePanel::paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(m_hwnd, &ps);
    RECT client;
    GetClientRect(m_hwnd, &client);
    m_swatches.clear();
    paintBuffered(dc, client, [&](Gdiplus::Graphics& g) {
        const float w = static_cast<float>(client.right), h = static_cast<float>(client.bottom);
        Gdiplus::SolidBrush back(color(kPanel));
        g.FillRectangle(&back, 0.0f, 0.0f, w, h);
        Gdiplus::Pen border(color(kBorder), 1.0f);
        g.DrawLine(&border, 0.5f, 0.0f, 0.5f, h);
        const auto bold = font(9, m_dpi, Gdiplus::FontStyleBold);
        const auto f = font(9, m_dpi);
        const float pad = static_cast<float>(scaled(10));
        const float icon = static_cast<float>(scaled(16));

        auto header = [&](float top, Icon glyph, const wchar_t* title) {
            Gdiplus::SolidBrush band(color(kBackground));
            g.FillRectangle(&band, 1.0f, top, w, static_cast<float>(headerHeight()));
            drawIcon(g, glyph, Gdiplus::RectF(pad, top + (headerHeight() - icon) / 2, icon, icon), kTextDim);
            drawText(g, title, *bold, Gdiplus::RectF(pad + icon + scaled(8), top, w, static_cast<float>(headerHeight())), kText);
            g.DrawLine(&border, 0.0f, top + headerHeight() - 0.5f, w, top + headerHeight() - 0.5f);
        };
        header(0, Icon::Marks, L"Marcas");

        // Lista de marcas.
        const RECT list = listRect();
        g.SetClip(Gdiplus::RectF(static_cast<float>(list.left), static_cast<float>(list.top),
                                 static_cast<float>(list.right - list.left), static_cast<float>(list.bottom - list.top)));
        MarkupTools* tools = m_view ? m_view->tools() : nullptr;
        const int selected = tools ? tools->selected() : -1;
        if (m_rows.empty()) {
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::SolidBrush ink(color(kTextDim));
            const std::wstring empty = m_view && m_view->hasModel()
                                           ? L"Sin marcas todavía.\nUsa las pestañas Medir o Marcar."
                                           : L"Abre un archivo para ver sus marcas.";
            g.DrawString(empty.c_str(), -1, f.get(),
                         Gdiplus::RectF(static_cast<float>(list.left) + pad, static_cast<float>(list.top) + scaled(16),
                                        static_cast<float>(list.right - list.left) - 2 * pad, 200.0f),
                         &format, &ink);
        }
        for (std::size_t i = 0; i < m_rows.size(); ++i) {
            const Row& row = m_rows[i];
            const float top = static_cast<float>(list.top + static_cast<int>(i) * rowHeight() - m_scroll);
            if (top + rowHeight() < list.top || top > list.bottom) continue;
            const Gdiplus::RectF box(static_cast<float>(list.left), top, w - list.left, static_cast<float>(rowHeight()));
            const bool isSelected = row.kind >= 0 && row.code == selected;
            if (isSelected) {
                Gdiplus::SolidBrush sel(color(kPressed));
                g.FillRectangle(&sel, box);
                Gdiplus::SolidBrush accent(color(kAccent));
                g.FillRectangle(&accent, box.X, box.Y, static_cast<float>(scaled(2)), box.Height);
            } else if (static_cast<int>(i) == m_hot) {
                Gdiplus::SolidBrush hot(color(kHover));
                g.FillRectangle(&hot, box);
            }
            float x = box.X + pad + row.depth * scaled(16);
            const float iy = top + (rowHeight() - icon) / 2;
            if (row.kind < 0) {
                drawIcon(g, row.open ? Icon::ChevronDown : Icon::ChevronRight,
                         Gdiplus::RectF(x - scaled(4), iy + scaled(3), icon - scaled(6), icon - scaled(6)), kTextDim);
                x += scaled(12);
                drawIcon(g, row.code == 0 ? Icon::View3d : Icon::Views, Gdiplus::RectF(x, iy, icon, icon), kTextDim);
                drawText(g, row.text, *bold, Gdiplus::RectF(x + icon + scaled(6), top, w - x - icon - pad, box.Height), kText);
            } else {
                drawIcon(g, iconOf(static_cast<MarkKind>(row.kind)), Gdiplus::RectF(x, iy, icon, icon),
                         isSelected ? kAccent : kTextDim);
                drawText(g, row.text, *f, Gdiplus::RectF(x + icon + scaled(6), top, w - x - icon - pad, box.Height), kText);
            }
        }
        g.ResetClip();

        // Divisor y propiedades.
        const float split = static_cast<float>(splitY());
        Gdiplus::SolidBrush bar(color(kBorder));
        g.FillRectangle(&bar, 1.0f, split + scaled(2), w, 1.0f);
        header(split + scaled(5), Icon::Properties, L"Propiedades");
        const RECT props = propsRect();
        const float nameWidth = std::max(static_cast<float>(scaled(84)), (props.right - props.left) * 0.34f);
        const Mark* mark = tools && selected >= 0 ? tools->markById(selected) : nullptr;
        for (std::size_t i = 0; i < m_props.size(); ++i) {
            const float top = static_cast<float>(props.top + scaled(4) + static_cast<int>(i) * rowHeight());
            if (top > props.bottom) break;
            const float left = static_cast<float>(props.left) + pad;
            drawText(g, m_props[i].name, *f, Gdiplus::RectF(left, top, nameWidth, static_cast<float>(rowHeight())), kTextDim);
            const Gdiplus::RectF value(left + nameWidth, top, w - left - nameWidth - pad, static_cast<float>(rowHeight()));
            if (m_props[i].name == L"Color" && mark) {
                const float d = static_cast<float>(scaled(16));
                float sx = value.X;
                for (std::uint32_t swatch : kSwatches) {
                    const Gdiplus::RectF dotBox(sx, top + (rowHeight() - d) / 2, d, d);
                    Gdiplus::SolidBrush fill{Gdiplus::Color(swatch)};
                    g.FillEllipse(&fill, dotBox);
                    if (swatch == mark->color) {
                        strokeRound(g, Gdiplus::RectF(dotBox.X - 3, dotBox.Y - 3, d + 6, d + 6), (d + 6) / 2, kText, 1.5f);
                    }
                    m_swatches.push_back(RECT{static_cast<LONG>(dotBox.X), static_cast<LONG>(dotBox.Y),
                                              static_cast<LONG>(dotBox.X + d), static_cast<LONG>(dotBox.Y + d)});
                    sx += d + scaled(8);
                }
            } else {
                drawText(g, m_props[i].value, *f, value, kText);
            }
        }
        if (m_props.empty()) {
            drawText(g, L"Sin archivo abierto", *f,
                     Gdiplus::RectF(static_cast<float>(props.left) + pad, static_cast<float>(props.top), w, static_cast<float>(rowHeight())),
                     kTextDim);
        }
    });
    EndPaint(m_hwnd, &ps);
}

LRESULT SidePanel::handle(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_PAINT: paint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
            layoutEdit();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, gdi(kText));
            SetBkColor(dc, gdi(kBackground));
            return reinterpret_cast<LRESULT>(m_editBrush);
        }
        case WM_COMMAND:
            if (reinterpret_cast<HWND>(lparam) == m_edit && HIWORD(wparam) == EN_KILLFOCUS) commitNote();
            return 0;
        case WM_SETCURSOR: {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(m_hwnd, &p);
            if (p.x < scaled(5)) {
                SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
                return TRUE;
            }
            const int split = splitY();
            if (p.y >= split && p.y < split + scaled(5)) {
                SetCursor(LoadCursor(nullptr, IDC_SIZENS));
                return TRUE;
            }
            break;
        }
        case WM_MOUSEMOVE: {
            const int x = GET_X_LPARAM(lparam), y = GET_Y_LPARAM(lparam);
            if (!m_tracking) {
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, m_hwnd, 0};
                m_tracking = TrackMouseEvent(&track) != FALSE;
            }
            if (m_drag == Drag::Width) {
                POINT p = {x, y};
                ClientToScreen(m_hwnd, &p);
                RECT r;
                GetWindowRect(m_hwnd, &r);
                setPreferredWidth(MulDiv(static_cast<int>(r.right - p.x), 96, m_dpi));
                SendMessageW(m_parent, kPanelResized, 0, 0);
                return 0;
            }
            if (m_drag == Drag::Split) {
                RECT client;
                GetClientRect(m_hwnd, &client);
                const int free = std::max(1, static_cast<int>(client.bottom) - 2 * headerHeight() - scaled(5));
                m_split = std::max(0.15, std::min(0.85, static_cast<double>(y - headerHeight()) / free));
                layoutEdit();
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }
            const RECT list = listRect();
            int hot = -1;
            if (y >= list.top && y < list.bottom && x > scaled(5)) {
                hot = (y - list.top + m_scroll) / rowHeight();
                if (hot >= static_cast<int>(m_rows.size())) hot = -1;
            }
            if (hot != m_hot) {
                m_hot = hot;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            m_tracking = false;
            m_hot = -1;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            const int x = GET_X_LPARAM(lparam), y = GET_Y_LPARAM(lparam);
            const int split = splitY();
            if (x < scaled(5)) {
                m_drag = Drag::Width;
                SetCapture(m_hwnd);
                return 0;
            }
            if (y >= split && y < split + scaled(5)) {
                m_drag = Drag::Split;
                SetCapture(m_hwnd);
                return 0;
            }
            MarkupTools* tools = m_view ? m_view->tools() : nullptr;
            if (!tools) return 0;
            // El texto de la nota en edicion se confirma en esa nota antes de elegir otra.
            if (m_edit && GetFocus() == m_edit) {
                commitNote();
                if (m_view) m_view->focus();
            }
            const RECT list = listRect();
            if (y >= list.top && y < list.bottom) {
                const int index = (y - list.top + m_scroll) / rowHeight();
                if (index >= 0 && index < static_cast<int>(m_rows.size())) {
                    const Row row = m_rows[static_cast<std::size_t>(index)];
                    if (row.kind < 0) {
                        // Nodo: el triangulo pliega; el resto va a la vista guardada.
                        if (x < scaled(34) || row.code == 0) {
                            if (m_closed.count(row.view)) m_closed.erase(row.view);
                            else m_closed.insert(row.view);
                        } else {
                            tools->focusEntry(row.code);
                        }
                    } else {
                        tools->focusEntry(row.code);
                    }
                    refresh();
                    PostMessageW(m_parent, WM_COMMAND, MAKEWPARAM(kCmdNone, 0), 0);
                }
                return 0;
            }
            for (std::size_t i = 0; i < m_swatches.size(); ++i) {
                const RECT& r = m_swatches[i];
                if (x >= r.left - 2 && x < r.right + 2 && y >= r.top - 2 && y < r.bottom + 2 && tools->selected() >= 0) {
                    tools->setMarkColor(tools->selected(), kSwatches[i]);
                    refresh();
                    return 0;
                }
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (m_drag != Drag::None) {
                m_drag = Drag::None;
                ReleaseCapture();
            }
            return 0;
        case WM_MOUSEWHEEL: {
            const RECT list = listRect();
            const int content = static_cast<int>(m_rows.size()) * rowHeight();
            const int maxScroll = std::max(0, content - static_cast<int>(list.bottom - list.top));
            m_scroll = std::max(0, std::min(maxScroll, m_scroll - GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA * 3 * rowHeight()));
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_DESTROY:
            if (m_editFont) DeleteObject(m_editFont);
            if (m_editBrush) DeleteObject(m_editBrush);
            m_editFont = nullptr;
            m_editBrush = nullptr;
            break;
        default:
            break;
    }
    return DefWindowProcW(m_hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK SidePanel::editProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<SidePanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_KEYDOWN && wparam == VK_RETURN && GetKeyState(VK_SHIFT) >= 0) {
        // Enter confirma; Shift+Enter hace un renglon nuevo.
        self->commitNote();
        if (self->m_view) self->m_view->focus();
        return 0;
    }
    if (msg == WM_CHAR && wparam == L'\r' && GetKeyState(VK_SHIFT) >= 0) return 0;
    if (msg == WM_KEYDOWN && wparam == VK_ESCAPE) {
        self->m_editingNote = -1;  // sin confirmar
        if (self->m_view) self->m_view->focus();
        self->refresh();
        return 0;
    }
    return CallWindowProcW(self->m_editDefault, hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK SidePanel::proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<SidePanel*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }
    auto* self = reinterpret_cast<SidePanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->handle(msg, wparam, lparam) : DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace ui
}  // namespace stp
