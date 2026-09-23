# Medir y marcar — plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Medir (distancia, radio/diámetro, ángulo, área/perímetro) y marcar (resaltador, subrayado, nota con flecha, rectángulo/elipse/nube, lápiz) en el visor; guardar las marcas en `<modelo>.marcas`; exportar PNG y PDF; medir también en el panel del Explorador.

**Architecture:** Los lectores registran "elementos medibles" exactos en `Mesh` (círculos, caras, contornos, unidades). Un índice de selección (BVH) en `src/engine/measure` resuelve rayo, enganche y cálculos sin depender de Windows. `src/engine/markup` guarda el modelo de marcas y su formato de texto. En Windows, `MarkupTools` recibe los eventos de `SceneView` cuando hay una herramienta activa y dibuja con GDI+; `toolbar` y `export` completan la interfaz. El PDF lo arma un escritor propio portátil.

**Tech Stack:** C++17, mingw-w64 (estático), Win32, GDI y GDI+; pruebas nativas con el arnés de `tests/unit_tests.cpp`; verificación real bajo wine + Xvfb + xdotool.

**Spec:** `docs/superpowers/specs/2026-09-23-medir-y-marcar-design.md`

## Global Constraints

- Sin dependencias nuevas: solo la librería estándar, Win32, GDI y GDI+ (ya enlazada con `-lgdiplus`).
- Todo lo de `src/engine` y `src/export` compila en Linux con `g++` y se prueba con `./build.sh test`.
- Textos de interfaz en español; en literales de código solo ASCII (acentos y símbolos con escapes `\u00XX`).
- Sin herramienta activa, visor y panel se comportan exactamente como hoy.
- Enganche: radio de 8 px; < 5 ms por consulta en el DXF de 1,5 millones de segmentos.
- Formato: 3 decimales sin ceros sobrantes; ángulos en grados con 2 decimales; separador decimal punto.
- Archivo de marcas: `<ruta del modelo>.marcas`, UTF-8, primera línea `stp-viewer-marcas 1`.
- Resaltador 14 px al 40 % de opacidad (amarillo `#FFE14D`, verde `#7CE08A`, rosa `#FF7EB6`); subrayado 3 px; lápiz 2 px; colores `Q`: rojo `#E03C31`, amarillo `#F2C12E`, verde `#2E9E4F`, azul `#2F6FDB`, negro `#1A1F24`.
- Deshacer: pila de 100 pasos. Trazos simplificados con Douglas–Peucker a 0,5 px.
- PDF: A4 apaisado (842 × 595 pt), imágenes JPEG calidad 90, Helvetica WinAnsiEncoding.
- Cada commit termina con:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_015NM5546gRGTfskkpAXPqTF
  ```
- Archivos nuevos `.cpp` se agregan a `ENGINE` (motor) o a las listas del DLL y del visor en `build.sh` **y** `build.bat`.

## Review Focus

1. **Modelo regrabado por el CAD después de marcar** (tamaño o fecha distintos): las marcas cargan y se avisa; nunca se descartan. → prueba `markup_detects_changed_model` (Task 6).
2. **Enganche a puntos tapados** por la propia pieza (un extremo en la cara de atrás de una caja): no debe engancharse. → prueba `snap_ignores_points_hidden_behind_faces` (Task 4).
3. **Bloque espejado o escalado** (INSERT con extrusión −Z o escala no uniforme): el círculo espejado mide su radio real; con escala no uniforme no se registra círculo. → pruebas `features_mirrored_insert_keeps_radius` y `features_nonuniform_insert_drops_circle` (Task 1).
4. **Notas con texto raro** (ñ, comillas, barras, saltos de línea, 10 000 caracteres): ida y vuelta idéntica. → prueba `markup_roundtrip_hostile_text` (Task 6).
5. **Carpeta de solo lectura o disco lleno al guardar**: aviso, sin cuelgue, el `.marcas` anterior intacto y sin `.tmp` huérfano. → paso de verificación bajo wine en Task 9 (atributo de solo lectura).

---

## Mapa de archivos

| Archivo | Nuevo / cambia | Responsabilidad |
|---|---|---|
| `src/engine/features.h` | nuevo | Tipos de elementos medibles y unidades |
| `src/engine/features.cpp` | nuevo | `unitSuffix`, `CircleFeature::pointAt` |
| `src/engine/mesh.h` | cambia | `features`, `units`, `edgeCurve`; `addSegment(a, b, curve)` |
| `src/formats/dxf.cpp` | cambia | Registrar círculos, arcos, contornos, `$INSUNITS`; marcar segmentos curvos |
| `src/engine/step_model.cpp` | cambia | Círculos de aristas, caras con su tipo, unidad de longitud |
| `src/formats/iges.cpp` | cambia | Arcos 100, unidad del campo global 14 |
| `src/engine/bvh.h` | nuevo | Jerarquía de cajas genérica (construcción y recorrido) |
| `src/engine/measure.h/.cpp` | nuevo | Rayo, índice de selección, enganche, cálculos, formato |
| `src/engine/markup.h/.cpp` | nuevo | Modelo de marcas, `.marcas`, comparación de vistas |
| `src/export/pdf_writer.h/.cpp` | nuevo | Escritor PDF portátil |
| `src/viewer/image_view.h/.cpp` | cambia | Exponer `ensureGdiplus()` |
| `src/viewer/markup_tools.h/.cpp` | nuevo | Herramientas: interacción, dibujo, deshacer |
| `src/viewer/toolbar.h/.cpp` | nuevo | Barra de herramientas y lista lateral |
| `src/viewer/export.h/.cpp` | nuevo | Exportar PNG y PDF (GDI+ y `pdf_writer`) |
| `src/viewer/scene_view.h/.cpp` | cambia | Índice de selección, delegar en `MarkupTools`, guardar/cargar marcas |
| `src/viewer/main.cpp` | cambia | Barra, lista, `Ctrl+S`, `Ctrl+E`, guardado al cerrar |
| `src/shellext/preview_handler.cpp` | cambia | Medir con `M` en el panel |
| `tests/unit_tests.cpp` | cambia | Pruebas de cada tarea de motor |
| `build.sh`, `build.bat` | cambian | Listas de archivos |
| `README.md` | cambia | Documentación de uso |

---

### Task 1: Elementos medibles en la malla y en el lector DXF

**Files:**
- Create: `src/engine/features.h`, `src/engine/features.cpp`
- Modify: `src/engine/mesh.h`, `src/formats/dxf.cpp`, `build.sh:14`, `build.bat:9`
- Test: `tests/unit_tests.cpp`

**Interfaces:**
- Consumes: `Mesh`, `Mat4`, `Frame`, `objectFrame()` y `PathVertex` de `dxf.cpp`.
- Produces:
  - `enum class LengthUnit { Unknown, Millimeter, Centimeter, Meter, Inch, Foot };`
  - `const char* unitSuffix(LengthUnit unit);` → `"mm"`, `"cm"`, `"m"`, `"in"`, `"ft"`, `""`
  - `struct CircleFeature { Vec3 center; Vec3 normal; Vec3 xAxis; double radius; double startAngle; double sweep; bool full() const; Vec3 pointAt(double angle) const; };`
  - `enum class SurfaceKind { Plane, Cylinder, Cone, Sphere, Torus, Other };`
  - `struct FaceFeature { std::uint32_t firstTriangle; std::uint32_t triangleCount; SurfaceKind kind; double radius; Vec3 axisOrigin; Vec3 axisDir; };`
  - `struct ContourFeature { std::vector<Vec3> points; std::vector<double> bulges; Vec3 normal; };` (bulge del tramo `points[i] → points[i+1]`, cerrado)
  - `struct MeshFeatures { std::vector<CircleFeature> circles; std::vector<FaceFeature> faces; std::vector<ContourFeature> contours; };`
  - En `Mesh`: `MeshFeatures features; LengthUnit units = LengthUnit::Unknown; std::vector<std::uint8_t> edgeCurve;` y `void addSegment(const Vec3& a, const Vec3& b, bool curve = false);`

- [ ] **Step 1: Escribir las pruebas que fallan**

Agregar a `tests/unit_tests.cpp`, antes de `// --- Planaridad`:

```cpp
// --- Elementos medibles ---------------------------------------------------------

namespace {

std::string headerSection(const Pairs& header) {
    Pairs all = {{0, "SECTION"}, {2, "HEADER"}};
    all.insert(all.end(), header.begin(), header.end());
    all.push_back({0, "ENDSEC"});
    return dxf(all);
}

}  // namespace

TEST(features_dxf_circle_is_exact) {
    const std::string text = entities({{0, "CIRCLE"}, {10, "10"}, {20, "20"}, {30, "0"}, {40, "5"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(mesh.features.circles.size() == 1);
    CHECK(mesh.features.contours.size() == 1);
    if (mesh.features.circles.size() != 1) return;
    const stp::CircleFeature& c = mesh.features.circles[0];
    CHECK_NEAR(c.radius, 5.0, 1e-12);
    CHECK_NEAR(c.center.x, 10.0, 1e-12);
    CHECK_NEAR(c.center.y, 20.0, 1e-12);
    CHECK(c.full());
    CHECK(mesh.units == stp::LengthUnit::Millimeter);  // DXF sin $INSUNITS: mm
}

TEST(features_dxf_arc_keeps_its_range) {
    const std::string text = entities({{0, "ARC"}, {10, "0"}, {20, "0"}, {40, "4"},
                                       {50, "0"}, {51, "90"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(mesh.features.circles.size() == 1);
    CHECK(mesh.features.contours.empty());
    if (mesh.features.circles.empty()) return;
    const stp::CircleFeature& c = mesh.features.circles[0];
    CHECK_NEAR(c.startAngle, 0.0, 1e-12);
    CHECK_NEAR(c.sweep, stp::kPi / 2, 1e-12);
    CHECK(!c.full());
    CHECK_NEAR(c.pointAt(c.startAngle + c.sweep).y, 4.0, 1e-12);
}

TEST(features_dxf_bulge_polyline_registers_arc_and_contour) {
    // Cuadrado 10x10 con el lado de cierre abombado (semicirculo de radio 5).
    const std::string text = entities({{0, "LWPOLYLINE"}, {90, "4"}, {70, "1"},
                                       {10, "0"}, {20, "0"}, {10, "10"}, {20, "0"},
                                       {10, "10"}, {20, "10"}, {10, "0"}, {20, "10"}, {42, "1"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(mesh.features.circles.size() == 1);
    CHECK(mesh.features.contours.size() == 1);
    if (mesh.features.circles.empty() || mesh.features.contours.empty()) return;
    CHECK_NEAR(mesh.features.circles[0].radius, 5.0, 1e-12);
    CHECK(mesh.features.contours[0].points.size() == 4);
    CHECK_NEAR(mesh.features.contours[0].bulges[3], 1.0, 1e-12);
}

TEST(features_dxf_segments_are_flagged_straight_or_curved) {
    const std::string text = entities({{0, "LINE"}, {10, "0"}, {20, "0"}, {11, "10"}, {21, "0"},
                                       {0, "CIRCLE"}, {10, "0"}, {20, "0"}, {40, "1"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(mesh.edgeCurve.size() == mesh.edgeLines.size() / 2);
    if (mesh.edgeCurve.size() < 2) return;
    CHECK(mesh.edgeCurve[0] == 0);
    CHECK(mesh.edgeCurve[1] == 1);
    CHECK(mesh.edgeCurve.back() == 1);
}

TEST(features_mirrored_insert_keeps_radius) {
    const std::string text = document(
        {}, {{0, "BLOCK"}, {2, "B"}, {70, "0"}, {10, "0"}, {20, "0"}, {30, "0"},
             {0, "CIRCLE"}, {10, "2"}, {20, "0"}, {30, "0"}, {40, "3"},
             {0, "ENDBLK"}},
        {{0, "INSERT"}, {2, "B"}, {10, "10"}, {20, "0"}, {30, "0"},
         {210, "0"}, {220, "0"}, {230, "-1"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(mesh.features.circles.size() == 1);
    if (mesh.features.circles.empty()) return;
    CHECK_NEAR(mesh.features.circles[0].radius, 3.0, 1e-12);
    CHECK_NEAR(mesh.features.circles[0].center.x, -12.0, 1e-12);
}

TEST(features_nonuniform_insert_drops_circle) {
    const std::string text = document(
        {}, {{0, "BLOCK"}, {2, "B"}, {70, "0"}, {10, "0"}, {20, "0"}, {30, "0"},
             {0, "CIRCLE"}, {10, "0"}, {20, "0"}, {30, "0"}, {40, "3"},
             {0, "ENDBLK"}},
        {{0, "INSERT"}, {2, "B"}, {10, "0"}, {20, "0"}, {30, "0"}, {41, "2"}, {42, "1"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(mesh.features.circles.empty());
    CHECK(mesh.features.contours.empty());
    CHECK(!mesh.edgeLines.empty());  // se sigue dibujando, como elipse
}

TEST(features_dxf_units_from_insunits) {
    stp::Mesh inches;
    CHECK(loadDxfText(headerSection({{9, "$INSUNITS"}, {70, "1"}}) +
                          entities({{0, "LINE"}, {10, "0"}, {20, "0"}, {11, "1"}, {21, "0"}}),
                      &inches));
    CHECK(inches.units == stp::LengthUnit::Inch);
    stp::Mesh meters;
    CHECK(loadDxfText(headerSection({{9, "$INSUNITS"}, {70, "6"}}) +
                          entities({{0, "LINE"}, {10, "0"}, {20, "0"}, {11, "1"}, {21, "0"}}),
                      &meters));
    CHECK(meters.units == stp::LengthUnit::Meter);
}
```

Nota: `entities()` agrega su propio `EOF`; el `headerSection` va delante, así que el documento queda `HEADER … ENTITIES … EOF`, válido.

- [ ] **Step 2: Correr y ver que falla**

Run: `./build.sh test`
Expected: error de compilación `'struct stp::Mesh' has no member named 'features'`.

- [ ] **Step 3: Crear `src/engine/features.h`**

```cpp
// Elementos medibles: lo que los lectores saben de la geometria exacta
// (circulos, caras, contornos cerrados y unidades) y que la malla de
// triangulos y segmentos ya no conserva.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "geom.h"

namespace stp {

enum class LengthUnit { Unknown, Millimeter, Centimeter, Meter, Inch, Foot };

// "mm", "cm", "m", "in", "ft" o "" si el archivo no declara unidades.
const char* unitSuffix(LengthUnit unit);

// Circulo o arco. Los angulos se miden desde xAxis girando alrededor de normal.
struct CircleFeature {
    Vec3 center;
    Vec3 normal{0, 0, 1};
    Vec3 xAxis{1, 0, 0};
    double radius = 0.0;
    double startAngle = 0.0;
    double sweep = 2 * kPi;  // con signo; |sweep| >= 2*pi es circulo completo

    bool full() const { return std::fabs(sweep) >= 2 * kPi - 1e-9; }
    Vec3 pointAt(double angle) const;
};

enum class SurfaceKind { Plane, Cylinder, Cone, Sphere, Torus, Other };

// Cara de un solido: sus triangulos son indices.[3*first, 3*(first+count)).
struct FaceFeature {
    std::uint32_t firstTriangle = 0;
    std::uint32_t triangleCount = 0;
    SurfaceKind kind = SurfaceKind::Other;
    double radius = 0.0;  // cilindro y esfera
    Vec3 axisOrigin;      // cilindro
    Vec3 axisDir{0, 0, 1};
};

// Contorno cerrado de un plano. bulges[i] es el bulge del tramo
// points[i] -> points[(i+1) % n], medido alrededor de normal (0 = recta).
struct ContourFeature {
    std::vector<Vec3> points;
    std::vector<double> bulges;
    Vec3 normal{0, 0, 1};
};

struct MeshFeatures {
    std::vector<CircleFeature> circles;
    std::vector<FaceFeature> faces;
    std::vector<ContourFeature> contours;
};

}  // namespace stp
```

- [ ] **Step 4: Crear `src/engine/features.cpp`**

```cpp
#include "features.h"

namespace stp {

const char* unitSuffix(LengthUnit unit) {
    switch (unit) {
        case LengthUnit::Millimeter: return "mm";
        case LengthUnit::Centimeter: return "cm";
        case LengthUnit::Meter: return "m";
        case LengthUnit::Inch: return "in";
        case LengthUnit::Foot: return "ft";
        case LengthUnit::Unknown: break;
    }
    return "";
}

Vec3 CircleFeature::pointAt(double angle) const {
    const Vec3 y = cross(normal, xAxis);
    return center + (xAxis * std::cos(angle) + y * std::sin(angle)) * radius;
}

}  // namespace stp
```

- [ ] **Step 5: Cambiar `src/engine/mesh.h`**

Agregar `#include "features.h"` después de `#include "geom.h"`. En `struct Mesh`, después de `std::vector<MeshText> texts;`:

```cpp
    // 1 si el segmento i es parte de una curva muestreada (arco, spline...),
    // 0 si es una recta de verdad: solo las rectas ofrecen extremos y puntos
    // medios para enganchar al medir.
    std::vector<std::uint8_t> edgeCurve;
    MeshFeatures features;
    LengthUnit units = LengthUnit::Unknown;
```

Reemplazar `addSegment`:

```cpp
    void addSegment(const Vec3& a, const Vec3& b, bool curve = false) {
        edgeLines.push_back(a);
        edgeLines.push_back(b);
        edgeCurve.push_back(curve ? 1 : 0);
        bounds.add(a);
        bounds.add(b);
    }
```

- [ ] **Step 6: Agregar `features.cpp` a las listas de compilación**

En `build.sh:14` agregar `src/engine/features.cpp ` al principio de `ENGINE="…"`. En `build.bat:9` agregar `src\engine\features.cpp ` al principio de `set ENGINE=…`.

- [ ] **Step 7: Registrar elementos en `src/formats/dxf.cpp`**

7a. En la clase `DxfReader`, reemplazar la declaración de `emitStrip` y agregar dos métodos:

```cpp
    void emitStrip(const std::vector<Vec3>& points, bool closed, const Mat4& transform,
                   bool curve = false);
    // Registran elementos medibles si la transformacion no deforma los circulos.
    void addCircle(const Vec3& center, const Frame& frame, double radius, double start,
                   double sweep, const Mat4& transform);
    void addContour(const std::vector<Vec3>& points, const std::vector<double>& bulges,
                    const Frame& frame, const Mat4& transform);
```

7b. Reemplazar la definición de `emitStrip`:

```cpp
void DxfReader::emitStrip(const std::vector<Vec3>& points, bool closed, const Mat4& transform,
                          bool curve) {
    if (points.size() < 2) return;
    Vec3 previous = transform.point(points[0]);
    const Vec3 first = previous;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const Vec3 current = transform.point(points[i]);
        M.addSegment(previous, current, curve);
        previous = current;
    }
    if (closed && points.size() > 2) M.addSegment(previous, first, curve);
}
```

7c. Agregar, justo después de `emitStrip`, la función libre `conformalAxes` y los dos métodos. Todo `dxf.cpp` ya está dentro de `namespace stp { namespace {`, así que la función no necesita otro espacio de nombres:

```cpp
// Ejes del objeto transformados. Devuelve false si la transformacion deforma
// los circulos (escala distinta en x e y, o ejes que dejan de ser perpendiculares).
bool conformalAxes(const Frame& frame, const Mat4& transform, Vec3* x, Vec3* y, double* scale) {
    *x = transform.direction(frame.x);
    *y = transform.direction(frame.y);
    const double lx = length(*x), ly = length(*y);
    if (lx < 1e-12 || ly < 1e-12) return false;
    if (std::fabs(lx - ly) > 1e-9 * std::max(lx, ly)) return false;
    if (std::fabs(dot(*x, *y)) > 1e-9 * lx * ly) return false;
    *scale = lx;
    return true;
}

void DxfReader::addCircle(const Vec3& center, const Frame& frame, double radius, double start,
                          double sweep, const Mat4& transform) {
    Vec3 x, y;
    double scale = 1.0;
    if (radius <= 0 || !conformalAxes(frame, transform, &x, &y, &scale)) return;
    CircleFeature circle;
    circle.center = transform.point(center);
    circle.xAxis = normalize(x);
    // Con espejo la normal se invierte y el giro sigue siendo el mismo en pantalla.
    circle.normal = normalize(cross(x, y));
    circle.radius = radius * scale;
    circle.startAngle = start;
    circle.sweep = sweep;
    M.features.circles.push_back(circle);
}

void DxfReader::addContour(const std::vector<Vec3>& points, const std::vector<double>& bulges,
                           const Frame& frame, const Mat4& transform) {
    Vec3 x, y;
    double scale = 1.0;
    bool curved = false;
    for (const double b : bulges) curved = curved || std::fabs(b) > 1e-9;
    const bool conformal = conformalAxes(frame, transform, &x, &y, &scale);
    if (curved && !conformal) return;
    if (points.size() < (curved ? 2u : 3u)) return;
    ContourFeature contour;
    for (const Vec3& p : points) contour.points.push_back(transform.point(p));
    contour.bulges = bulges;
    const Vec3 n = cross(transform.direction(frame.x), transform.direction(frame.y));
    if (length(n) < 1e-18) return;
    contour.normal = normalize(n);
    M.features.contours.push_back(std::move(contour));
}
```

7d. Reemplazar `emitArc` completo:

```cpp
void DxfReader::emitArc(const Entity& entity, double startAngle, double sweep,
                        const Mat4& transform) {
    const Frame frame = objectFrame(entity);
    const double radius = entity.number(40, 0.0);
    if (radius <= 0) return;
    // El centro viene en coordenadas del objeto, no del mundo.
    const Vec3 center = frame.x * entity.number(10) + frame.y * entity.number(20) +
                        frame.z * entity.number(30);

    const int steps = std::max(2, arcSteps(sweep));
    std::vector<Vec3> points;
    points.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double angle = startAngle + sweep * i / steps;
        points.push_back(center + frame.x * (radius * std::cos(angle)) +
                         frame.y * (radius * std::sin(angle)));
    }
    emitStrip(points, false, transform, true);

    addCircle(center, frame, radius, startAngle, sweep, transform);
    if (std::fabs(sweep) >= 2 * kPi - 1e-9) {
        // Un circulo es tambien un contorno cerrado: dos medias vueltas.
        addContour({center + frame.x * radius, center - frame.x * radius}, {1.0, 1.0}, frame,
                   transform);
    }
}
```

7e. Reemplazar `emitPath` completo:

```cpp
void DxfReader::emitPath(const std::vector<PathVertex>& vertices, bool closed, double elevation,
                         const Frame& frame, const Mat4& transform) {
    const std::size_t n = vertices.size();
    if (n < 2) return;
    auto toWorld = [&](double x, double y) { return frame.dirToWorld(Vec3(x, y, elevation)); };

    const std::size_t segments = closed ? n : n - 1;
    for (std::size_t i = 0; i < segments; ++i) {
        const PathVertex& a = vertices[i];
        const PathVertex& b = vertices[(i + 1) % n];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double chord = std::sqrt(dx * dx + dy * dy);
        if (std::fabs(a.bulge) > 1e-9 && chord > 1e-12) {
            // Centro a la izquierda de la cuerda si el bulge es positivo (antihorario).
            const double sweep = 4.0 * std::atan(a.bulge);
            const double offset = chord * (1.0 - a.bulge * a.bulge) / (4.0 * a.bulge);
            const double cx = (a.x + b.x) / 2 - dy / chord * offset;
            const double cy = (a.y + b.y) / 2 + dx / chord * offset;
            const double radius = std::sqrt((a.x - cx) * (a.x - cx) + (a.y - cy) * (a.y - cy));
            const double start = std::atan2(a.y - cy, a.x - cx);
            const int steps = arcSteps(sweep);
            std::vector<Vec3> arc;
            arc.reserve(steps + 1);
            for (int k = 0; k <= steps; ++k) {
                const double angle = start + sweep * k / steps;
                arc.push_back(k == steps ? toWorld(b.x, b.y)
                                         : toWorld(cx + radius * std::cos(angle),
                                                   cy + radius * std::sin(angle)));
            }
            emitStrip(arc, false, transform, true);
            addCircle(toWorld(cx, cy), frame, radius, start, sweep, transform);
        } else {
            emitLine(toWorld(a.x, a.y), toWorld(b.x, b.y), transform);
        }
    }

    if (closed) {
        std::vector<Vec3> points;
        std::vector<double> bulges;
        for (const PathVertex& v : vertices) {
            points.push_back(toWorld(v.x, v.y));
            bulges.push_back(v.bulge);
        }
        addContour(points, bulges, frame, transform);
    }
}
```

7f. Marcar como curvas las elipses y las splines: en `emitEllipse` y en las dos llamadas de `emitSpline` cambiar `emitStrip(points, false, transform);` por `emitStrip(points, false, transform, true);`.

7g. Unidades: en `DxfReader::run`, justo después del bucle que detecta `$ACADVER`, agregar:

```cpp
    // $INSUNITS: 1 pulgada, 2 pie, 4 mm, 5 cm, 6 m. Sin el dato, mm.
    M.units = LengthUnit::Millimeter;
    for (std::size_t i = 0; i + 1 < pairs.size() && i < 4000; ++i) {
        if (pairs[i].code == 9 && pairs[i].value == "$INSUNITS" && pairs[i + 1].code == 70) {
            switch (pairs[i + 1].integer()) {
                case 1: M.units = LengthUnit::Inch; break;
                case 2: M.units = LengthUnit::Foot; break;
                case 5: M.units = LengthUnit::Centimeter; break;
                case 6: M.units = LengthUnit::Meter; break;
                default: break;
            }
            break;
        }
    }
```

- [ ] **Step 8: Correr las pruebas**

Run: `./build.sh test`
Expected: `50 pruebas, 0 fallidas` (43 anteriores + 7 nuevas).

- [ ] **Step 9: Comprobar que el resto compila y que los planos se ven igual**

Run: `./build.sh 2>&1 | grep -E "error" ; ./build/steprender tests/samples/plano_brida.dxf /tmp/brida.bmp 512 | tail -2`
Expected: sin errores; `plano 2D: 238.463 x 177.375 …` como antes.

- [ ] **Step 10: Commit**

```bash
git add src/engine/features.h src/engine/features.cpp src/engine/mesh.h src/formats/dxf.cpp build.sh build.bat tests/unit_tests.cpp
git commit -m "Record measurable circles, contours and units from DXF

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_015NM5546gRGTfskkpAXPqTF"
```

---

### Task 2: Elementos medibles y unidades del STEP

**Files:**
- Modify: `src/engine/step_model.cpp`
- Test: `tests/unit_tests.cpp`

**Interfaces:**
- Consumes: `CircleFeature`, `FaceFeature`, `SurfaceKind`, `LengthUnit`, `Mesh::addSegment(a, b, curve)` (Task 1); `Surface` de `surfaces.h`.
- Produces: `Mesh::features.circles` con cada arista `CIRCLE` (una vez por arista); `Mesh::features.faces` con una entrada por cara mallada, en orden creciente de `firstTriangle`, sin huecos ni solapes; `Mesh::units` desde `LENGTH_UNIT`.

- [ ] **Step 1: Escribir las pruebas que fallan**

Agregar a `tests/unit_tests.cpp` después de las pruebas de la Task 1 (y `#include <fstream>` y `#include <sstream>` arriba):

```cpp
namespace {

std::string readText(const char* path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool loadStepText(const std::string& text, stp::Mesh* mesh) {
    std::string error;
    return stp::loadModel(text.data(), text.size(), ".stp", mesh, &error);
}

}  // namespace

TEST(features_step_hole_is_exact) {
    stp::Mesh mesh;
    CHECK(loadStepText(readText("tests/samples/placa_agujero.stp"), &mesh));
    CHECK(mesh.units == stp::LengthUnit::Millimeter);
    int holeEdges = 0;
    for (const stp::CircleFeature& c : mesh.features.circles) {
        if (std::fabs(c.radius - 10.0) < 1e-9) ++holeEdges;
    }
    CHECK(holeEdges >= 2);  // borde de arriba y de abajo del agujero
    bool cylinder = false;
    for (const stp::FaceFeature& f : mesh.features.faces) {
        if (f.kind == stp::SurfaceKind::Cylinder && std::fabs(f.radius - 10.0) < 1e-9) cylinder = true;
    }
    CHECK(cylinder);
}

TEST(features_step_faces_cover_every_triangle_once) {
    stp::Mesh mesh;
    CHECK(loadStepText(readText("tests/samples/placa_agujero.stp"), &mesh));
    std::uint32_t next = 0;
    bool ordered = true;
    for (const stp::FaceFeature& f : mesh.features.faces) {
        if (f.firstTriangle != next || f.triangleCount == 0) ordered = false;
        next = f.firstTriangle + f.triangleCount;
    }
    CHECK(ordered);
    CHECK(next == mesh.triangleCount());
}

TEST(features_step_straight_edges_are_not_curves) {
    stp::Mesh mesh;
    CHECK(loadStepText(readText("tests/samples/caja.stp"), &mesh));
    CHECK(mesh.edgeCurve.size() == mesh.edgeLines.size() / 2);
    bool anyCurve = false;
    for (const std::uint8_t c : mesh.edgeCurve) anyCurve = anyCurve || c != 0;
    CHECK(!anyCurve);  // una caja solo tiene rectas
}

TEST(features_step_units_inch) {
    std::string text = readText("tests/samples/caja.stp");
    const std::string mm = "( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) )";
    const std::size_t at = text.find(mm);
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    text.replace(at, mm.size(), "( CONVERSION_BASED_UNIT('INCH',#999999) LENGTH_UNIT() NAMED_UNIT(*) )");
    stp::Mesh mesh;
    CHECK(loadStepText(text, &mesh));
    CHECK(mesh.units == stp::LengthUnit::Inch);
}
```

- [ ] **Step 2: Correr y ver que falla**

Run: `./build.sh test`
Expected: `FALLA features_step_hole_is_exact` (sin círculos, unidades `Unknown`), `FALLA features_step_faces_cover_every_triangle_once`, `FALLA features_step_units_inch`. `features_step_straight_edges_are_not_curves` ya pasa (todas se marcan 0 por defecto): es la guarda de regresión del Step 3c.

- [ ] **Step 3: Implementar en `src/engine/step_model.cpp`**

3a. Declarar en `class Builder` (sección `private`, junto a `buildFace`):

```cpp
    void buildFaceTriangles(const Entity* face, const Mat4& xf, Surface* surface, bool* known);
    const Entity* basisCurve(const Entity* curve) const;
    void noteCircle(const Entity* edge, const Mat4& xf);
    void detectUnits();
```

3b. Agregar las definiciones, justo antes de `bool Builder::readBSpline`:

```cpp
// La curva geometrica que hay debajo de SURFACE_CURVE, SEAM_CURVE y compania.
const Entity* Builder::basisCurve(const Entity* curve) const {
    for (int depth = 0; curve && depth < 4; ++depth) {
        if (!curve->is("SURFACE_CURVE") && !curve->is("SEAM_CURVE") &&
            !curve->is("INTERSECTION_CURVE") && !curve->is("TRIMMED_CURVE")) {
            return curve;
        }
        const std::vector<Value>& p = curve->params();
        if (p.size() < 2) return nullptr;
        curve = res(p[1]);
    }
    return curve;
}

// Registra la arista como circulo exacto si su curva es CIRCLE. Los angulos se
// calculan igual que en sampleCurve para que el arco coincida con lo dibujado.
void Builder::noteCircle(const Entity* edge, const Mat4& xf) {
    const std::vector<Value>& p = edge->params();
    if (p.size() < 4) return;
    const Entity* curve = basisCurve(res(p[3]));
    if (!curve || !curve->is("CIRCLE")) return;
    const std::vector<Value>& cp = curve->params();
    Frame f;
    if (cp.size() < 3 || !frameOf(cp[1], &f)) return;
    const double radius = numOf(cp, 2, 0.0);
    if (radius <= 0) return;

    Vec3 p0, p1;
    const bool hasStart = p.size() > 1 && vertexPoint(p[1], &p0);
    const bool hasEnd = p.size() > 2 && vertexPoint(p[2], &p1);
    const bool sameSense = p.size() > 4 ? p[4].boolValue() : true;
    auto angleOf = [&](const Vec3& q) {
        const Vec3 d = q - f.origin;
        return std::atan2(dot(d, f.y), dot(d, f.x));
    };
    double start = 0.0, sweep = 2 * kPi;
    if (hasStart && hasEnd && distance(p0, p1) > std::max(m_weldTol, m_tol * 0.05)) {
        start = angleOf(p0);
        sweep = normalizeAngle(angleOf(p1) - start);
        if (sameSense && sweep <= 0) sweep += 2 * kPi;
        if (!sameSense && sweep >= 0) sweep -= 2 * kPi;
    } else if (hasStart) {
        start = angleOf(p0);
        if (!sameSense) sweep = -2 * kPi;
    }

    CircleFeature circle;
    circle.center = xf.point(f.origin);
    circle.xAxis = normalize(xf.direction(f.x));
    circle.normal = normalize(xf.direction(f.z));
    circle.radius = radius * length(xf.direction(f.x));
    circle.startAngle = start;
    circle.sweep = sweep;
    M.features.circles.push_back(circle);
}

// Unidad de longitud: el LENGTH_UNIT de menor numero (casi siempre hay uno solo).
void Builder::detectUnits() {
    int best = 0;
    for (const auto& entry : F.entities()) {
        const Entity& e = entry.second;
        if (!e.part("LENGTH_UNIT") || (best != 0 && e.id > best)) continue;
        LengthUnit unit = LengthUnit::Unknown;
        if (const std::vector<Value>* si = e.part("SI_UNIT")) {
            std::string prefix, name;
            for (const Value& v : *si) {
                if (v.kind != Value::Kind::Enum) continue;
                if (v.text == "METRE") name = v.text;
                else prefix = v.text;
            }
            if (name != "METRE") continue;
            if (prefix == "MILLI") unit = LengthUnit::Millimeter;
            else if (prefix == "CENTI") unit = LengthUnit::Centimeter;
            else if (prefix.empty()) unit = LengthUnit::Meter;
        } else if (const std::vector<Value>* conversion = e.part("CONVERSION_BASED_UNIT")) {
            if (conversion->empty() || (*conversion)[0].kind != Value::Kind::String) continue;
            std::string name = (*conversion)[0].text;
            for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (name == "INCH") unit = LengthUnit::Inch;
            else if (name == "FOOT") unit = LengthUnit::Foot;
            else if (name == "MILLIMETRE" || name == "MILLIMETER") unit = LengthUnit::Millimeter;
        } else {
            continue;
        }
        best = e.id;
        M.units = unit;
    }
}
```

(Si `<cctype>` no está incluido en `step_model.cpp`, agregarlo.)

3c. En `loopPoints`, reemplazar el bloque que dibuja la arista una sola vez:

```cpp
        if (m_edgeEmitted.insert(edge->id).second) {
            for (std::size_t i = 1; i < sampled->size(); ++i) {
                M.addSegment(xf.point((*sampled)[i - 1]), xf.point((*sampled)[i]));
            }
        }
```

por:

```cpp
        if (m_edgeEmitted.insert(edge->id).second) {
            const std::vector<Value>& ep = edge->params();
            const Entity* basis = ep.size() > 3 ? basisCurve(res(ep[3])) : nullptr;
            // Solo las rectas ofrecen extremos y puntos medios para medir.
            const bool curved = basis && !basis->is("LINE") && !basis->is("POLYLINE");
            for (std::size_t i = 1; i < sampled->size(); ++i) {
                M.addSegment(xf.point((*sampled)[i - 1]), xf.point((*sampled)[i]), curved);
            }
            noteCircle(edge, xf);
        }
```

3d. En `emitCurveSet`, reemplazar el bucle de segmentos:

```cpp
        const Entity* basis = basisCurve(c);
        const bool curved = basis && !basis->is("LINE") && !basis->is("POLYLINE");
        for (std::size_t i = 1; i < pts.size(); ++i) {
            M.addSegment(xf.point(pts[i - 1]), xf.point(pts[i]), curved);
        }
```

3e. Renombrar la función actual `void Builder::buildFace(const Entity* face, const Mat4& xf)` a
`void Builder::buildFaceTriangles(const Entity* face, const Mat4& xf, Surface* surface, bool* known)`
y, dentro, justo después de cada `surfaceOf(...)` que tenga éxito, anotar la superficie:

```cpp
    if (loops.empty()) {
        Surface full;
        if (surfaceOf(res(p[2]), &full)) {
            full.flipped = p.size() > 3 ? !p[3].boolValue() : false;
            *surface = full;
            *known = true;
            addFullSurface(full, xf);
        } else {
            ++M.facesFailed;
        }
        return;
    }
```

y

```cpp
    Surface surf;
    if (surfaceOf(res(p[2]), &surf)) {
        surf.flipped = !sameSense;
        *surface = surf;
        *known = true;
        addTessellated(surf, loops, xf);
    } else {
        addPlanarFallback(loops, xf, !sameSense);
    }
```

Luego agregar la nueva `buildFace` antes de `buildFaceTriangles`:

```cpp
void Builder::buildFace(const Entity* face, const Mat4& xf) {
    const std::size_t first = M.triangleCount();
    Surface surface;
    bool known = false;
    buildFaceTriangles(face, xf, &surface, &known);
    const std::size_t count = M.triangleCount() - first;
    if (count == 0) return;

    FaceFeature feature;
    feature.firstTriangle = static_cast<std::uint32_t>(first);
    feature.triangleCount = static_cast<std::uint32_t>(count);
    feature.kind = SurfaceKind::Plane;  // sin superficie reconocida se malla como plano
    if (known) {
        switch (surface.type) {
            case Surface::Type::Plane: feature.kind = SurfaceKind::Plane; break;
            case Surface::Type::Cylinder: feature.kind = SurfaceKind::Cylinder; break;
            case Surface::Type::Cone: feature.kind = SurfaceKind::Cone; break;
            case Surface::Type::Sphere: feature.kind = SurfaceKind::Sphere; break;
            case Surface::Type::Torus: feature.kind = SurfaceKind::Torus; break;
            case Surface::Type::Freeform: feature.kind = SurfaceKind::Other; break;
        }
        feature.radius = surface.radius * length(xf.direction(surface.frame.x));
        feature.axisOrigin = xf.point(surface.frame.origin);
        feature.axisDir = normalize(xf.direction(surface.frame.z));
    }
    M.features.faces.push_back(feature);
}
```

3f. En `Builder::run`, primera línea: `detectUnits();`.

- [ ] **Step 4: Correr las pruebas**

Run: `./build.sh test`
Expected: `54 pruebas, 0 fallidas`.

- [ ] **Step 5: Comprobar que la malla no cambió**

Run: `./build.sh native >/dev/null && ./build/steprender tests/samples/placa_agujero.stp /tmp/p.bmp 256 | sed -n 2,3p`
Expected: mismas cifras de caras y triángulos que antes del cambio (`git stash; ./build.sh native; …; git stash pop` para compararlas).

- [ ] **Step 6: Commit**

```bash
git add src/engine/step_model.cpp tests/unit_tests.cpp
git commit -m "Record STEP circles, faces and length unit for measuring

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_015NM5546gRGTfskkpAXPqTF"
```

---

### Task 3: Arcos, caras y unidades del IGES

**Files:**
- Modify: `src/formats/iges.cpp`
- Test: `tests/unit_tests.cpp`

**Interfaces:**
- Consumes: lo mismo que Task 2.
- Produces: círculos de las entidades 100 (sueltas y de aristas de caras), `FaceFeature` por cara de 510/144/128, `Mesh::units` del campo global 14 (1 in, 2 mm, 4 ft, 6 m, 10 cm; otro → `Unknown`).

- [ ] **Step 1: Escribir las pruebas que fallan**

Agregar después de las pruebas de la Task 2:

```cpp
namespace {

// Linea IGES de 80 columnas: 72 de datos, la letra de seccion y el numero.
std::string igesLine(const std::string& data, char section, int sequence) {
    std::string line = data;
    line.resize(72, ' ');
    char number[16];
    std::snprintf(number, sizeof(number), "%7d", sequence);
    return line + section + number + "\n";
}

// Archivo IGES con un solo circulo (entidad 100) de radio 5 en (10, 20).
std::string igesCircle(int unitFlag) {
    std::string out = igesLine("stp-viewer prueba", 'S', 1);
    const std::string global = "1H,,1H;,4Hprod,8Hfile.igs,3Hsys,3H1.0,32,38,6,308,15,4Hprod,1.," +
                               std::to_string(unitFlag) +
                               ",2HMM,1,0.1,15H20260923.120000,0.001,100.,4Huser,3Horg,11,0;";
    int g = 0;
    for (std::size_t i = 0; i < global.size(); i += 72) out += igesLine(global.substr(i, 72), 'G', ++g);
    char d1[96], d2[96];
    std::snprintf(d1, sizeof(d1), "%8d%8d%8d%8d%8d%8d%8d%8d%8s", 100, 1, 0, 1, 0, 0, 0, 0, "00000000");
    std::snprintf(d2, sizeof(d2), "%8d%8d%8d%8d%8d%8s%8s%8s%8d", 100, 0, 0, 1, 0, "", "", "", 0);
    out += igesLine(d1, 'D', 1) + igesLine(d2, 'D', 2);
    std::string p = "100,0.,10.,20.,15.,20.,15.,20.;";
    p.resize(64, ' ');
    p += "       1";  // columnas 65-72: puntero al directorio
    out += igesLine(p, 'P', 1);
    out += igesLine("S      1G      1D      2P      1", 'T', 1);
    return out;
}

bool loadIgesText(const std::string& text, stp::Mesh* mesh) {
    std::string error;
    return stp::loadModel(text.data(), text.size(), ".igs", mesh, &error);
}

}  // namespace

TEST(features_iges_arc_is_exact) {
    stp::Mesh mesh;
    CHECK(loadIgesText(igesCircle(2), &mesh));
    CHECK(mesh.features.circles.size() == 1);
    if (mesh.features.circles.empty()) return;
    CHECK_NEAR(mesh.features.circles[0].radius, 5.0, 1e-12);
    CHECK_NEAR(mesh.features.circles[0].center.y, 20.0, 1e-12);
    CHECK(mesh.features.circles[0].full());
    CHECK(mesh.units == stp::LengthUnit::Millimeter);
    CHECK(!mesh.edgeCurve.empty() && mesh.edgeCurve[0] == 1);
}

TEST(features_iges_units) {
    stp::Mesh inch, meter;
    CHECK(loadIgesText(igesCircle(1), &inch));
    CHECK(loadIgesText(igesCircle(6), &meter));
    CHECK(inch.units == stp::LengthUnit::Inch);
    CHECK(meter.units == stp::LengthUnit::Meter);
}
```

- [ ] **Step 2: Correr y ver que falla**

Run: `./build.sh test`
Expected: `FALLA features_iges_arc_is_exact` y `FALLA features_iges_units`.
Si en cambio falla `CHECK(loadIgesText(...))`, el archivo de prueba está mal armado: comparar columna por columna con un IGES real antes de seguir.

- [ ] **Step 3: Implementar en `src/formats/iges.cpp`**

3a. En el espacio de nombres anónimo, después de `trimSpaces`, agregar el lector de la sección global:

```cpp
// Campos de la seccion global, respetando los textos Hollerith (5Hhola).
std::vector<std::string> splitGlobal(const std::string& text, char field, char record) {
    std::vector<std::string> fields;
    std::string current;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t digits = i;
        while (digits < text.size() && std::isdigit(static_cast<unsigned char>(text[digits]))) ++digits;
        if (current.empty() && digits > i && digits < text.size() &&
            (text[digits] == 'H' || text[digits] == 'h')) {
            const std::size_t count = static_cast<std::size_t>(std::atoi(text.substr(i, digits - i).c_str()));
            current = text.substr(digits + 1, count);
            i = digits + 1 + count;
            continue;
        }
        const char c = text[i++];
        if (c == field || c == record) {
            fields.push_back(trimSpaces(current));
            current.clear();
            if (c == record) break;
        } else {
            current.push_back(c);
        }
    }
    return fields;
}

LengthUnit igesUnit(int flag) {
    switch (flag) {
        case 1: return LengthUnit::Inch;
        case 2: return LengthUnit::Millimeter;
        case 4: return LengthUnit::Foot;
        case 6: return LengthUnit::Meter;
        case 10: return LengthUnit::Centimeter;
        default: return LengthUnit::Unknown;
    }
}
```

3b. En `parseSections`, justo después del bloque que detecta los separadores:

```cpp
    const std::vector<std::string> globals = splitGlobal(global, m_fieldSeparator, m_recordSeparator);
    if (globals.size() > 13) M.units = igesUnit(std::atoi(globals[13].c_str()));
```

3c. Declarar en `class IgesReader`:

```cpp
    void noteArc(const Parameters& p, const Mat4& transform) const;
```

y definirlo antes de `emitArc`:

```cpp
// Registra la entidad 100 como circulo exacto (M es una referencia: se puede
// escribir desde metodos const, igual que al dibujar las aristas).
void IgesReader::noteArc(const Parameters& p, const Mat4& transform) const {
    if (p.size() < 8) return;
    const double z = p.number(1);
    const Vec3 center(p.number(2), p.number(3), z);
    const Vec3 start(p.number(4), p.number(5), z);
    const Vec3 end(p.number(6), p.number(7), z);
    const double radius = distance(center, start);
    if (radius < 1e-12) return;
    const Vec3 x = transform.direction(Vec3(1, 0, 0));
    const Vec3 y = transform.direction(Vec3(0, 1, 0));
    const double lx = length(x), ly = length(y);
    if (lx < 1e-12 || std::fabs(lx - ly) > 1e-9 * lx || std::fabs(dot(x, y)) > 1e-9 * lx * ly) return;

    const double a0 = std::atan2(start.y - center.y, start.x - center.x);
    double sweep = std::atan2(end.y - center.y, end.x - center.x) - a0;
    if (distance(start, end) < 1e-9) sweep = 2 * kPi;
    else if (sweep <= 0) sweep += 2 * kPi;

    CircleFeature circle;
    circle.center = transform.point(center);
    circle.xAxis = normalize(x);
    circle.normal = normalize(cross(x, y));
    circle.radius = radius * lx;
    circle.startAngle = a0;
    circle.sweep = sweep;
    M.features.circles.push_back(circle);
}
```

3d. En `emitArc`, cambiar `if (i > 0) M.addSegment(previous, world);` por `if (i > 0) M.addSegment(previous, world, true);` y agregar al final de la función `noteArc(p, transform);`. En `emitCurve`, cambiar su `M.addSegment(previous, world)` por `M.addSegment(previous, world, true)`.

3e. En el bucle de aristas de `loopOnAnalytic` (el bloque con `m_drawnCurves`), reemplazar:

```cpp
            m_drawnCurves.push_back(curvePointer);
            for (std::size_t k = 1; k < part.size(); ++k) M.addSegment(part[k - 1], part[k]);
```

por:

```cpp
            m_drawnCurves.push_back(curvePointer);
            const auto curveEntry = m_directory.find(curvePointer);
            const int curveType = curveEntry == m_directory.end() ? 0 : curveEntry->second.type;
            for (std::size_t k = 1; k < part.size(); ++k) {
                M.addSegment(part[k - 1], part[k], curveType != 110);
            }
            if (curveType == 100) {
                if (const Parameters* arc = parametersOf(curvePointer)) {
                    noteArc(*arc, transformOf(curveEntry->second.transform));
                }
            }
```

3f. Caras: en `IgesReader::run`, dentro del `switch (entry.type)` de superficies, envolver cada caso para registrar la cara. Reemplazar el `switch` completo por:

```cpp
        const std::size_t firstTriangle = M.triangleCount();
        SurfaceKind kind = SurfaceKind::Other;
        switch (entry.type) {
            case 128: {
                // Una superficie usada por una cara ya se dibuja recortada.
                if (entry.subordinate == 1 || entry.subordinate == 3) break;
                const bool trimmedElsewhere =
                    std::find(trimmedSurfaces.begin(), trimmedSurfaces.end(), pointer) !=
                    trimmedSurfaces.end();
                if (!trimmedElsewhere) {
                    emitSurface(pointer, {}, transform);
                    ++surfaces;
                }
                break;
            }
            case 510:
                kind = emitBrepFace(pointer, transform);
                ++surfaces;
                break;
            case 144: {
                const int surfacePointer = p->integer(1);
                const int outer = p->integer(4);
                const int innerCount = p->integer(3);
                std::vector<int> trims;
                if (outer > 0) trims.push_back(outer);
                for (int i = 0; i < innerCount; ++i) trims.push_back(p->integer(5 + i));
                emitSurface(surfacePointer, trims, transformOf(m_directory[surfacePointer].transform));
                ++surfaces;
                break;
            }
            default:
                break;
        }
        if (M.triangleCount() > firstTriangle) {
            FaceFeature face;
            face.firstTriangle = static_cast<std::uint32_t>(firstTriangle);
            face.triangleCount = static_cast<std::uint32_t>(M.triangleCount() - firstTriangle);
            face.kind = kind;
            if (entry.type == 510) {
                face.radius = m_lastFaceRadius;
                face.axisOrigin = m_lastFaceAxisOrigin;
                face.axisDir = m_lastFaceAxisDir;
            }
            M.features.faces.push_back(face);
        }
```

y cambiar `emitBrepFace` para que devuelva el tipo de superficie y deje radio y eje en tres miembros nuevos. En la clase:

```cpp
    SurfaceKind emitBrepFace(int pointer, const Mat4& transform);
    double m_lastFaceRadius = 0.0;
    Vec3 m_lastFaceAxisOrigin;
    Vec3 m_lastFaceAxisDir{0, 0, 1};
```

En la definición: cada `return;` pasa a `return SurfaceKind::Other;`, y al final, después de mallar:

```cpp
    if (!isAnalytic) return SurfaceKind::Other;
    m_lastFaceRadius = analytic.radius;
    m_lastFaceAxisOrigin = analytic.frame.origin;
    m_lastFaceAxisDir = analytic.frame.z;
    switch (analytic.type) {
        case Surface::Type::Plane: return SurfaceKind::Plane;
        case Surface::Type::Cylinder: return SurfaceKind::Cylinder;
        case Surface::Type::Cone: return SurfaceKind::Cone;
        case Surface::Type::Sphere: return SurfaceKind::Sphere;
        case Surface::Type::Torus: return SurfaceKind::Torus;
        default: return SurfaceKind::Other;
    }
```

(El `frame` de `analytic` ya se llevó a coordenadas del mundo al principio de la función, así que radio y eje salen en el mundo.)

- [ ] **Step 4: Correr las pruebas**

Run: `./build.sh test`
Expected: `56 pruebas, 0 fallidas`.

- [ ] **Step 5: Commit**

```bash
git add src/formats/iges.cpp tests/unit_tests.cpp
git commit -m "Record IGES arcs, faces and length unit for measuring

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_015NM5546gRGTfskkpAXPqTF"
```

---

### Task 4: Índice de selección, rayo y enganche

**Files:**
- Create: `src/engine/bvh.h`, `src/engine/measure.h`, `src/engine/measure.cpp`
- Modify: `build.sh:14`, `build.bat:9` (agregar `measure.cpp`)
- Test: `tests/unit_tests.cpp`

**Interfaces:**
- Consumes: `Mesh` con `edgeCurve` y `features` (Tasks 1–3); `Camera`, `projectPoint` de `renderer.h`; `PlanarInfo`.
- Produces (en `measure.h`):
  - `struct Ray { Vec3 origin; Vec3 dir; bool ortho; };`
  - `Ray pixelRay(const Camera& camera, int width, int height, double sx, double sy);`
  - `double worldPerPixel(const Camera& camera, int height, double t);`
  - `bool rayTriangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c, double* t);`
  - `enum class SnapKind { None, Endpoint, Center, Midpoint, OnEdge, OnFace, OnPlane };`
  - `struct SnapResult { SnapKind kind; Vec3 point; int triangle; int segment; int circle; };`
  - `struct SnapOptions { double radiusPixels = 8.0; bool snapping = true; const PlanarInfo* plane = nullptr; };`
  - `class PickIndex { void build(const Mesh&); bool raycast(const Mesh&, const Ray&, double* t, int* triangle) const; SnapResult snap(const Mesh&, const Camera&, int w, int h, double sx, double sy, const SnapOptions&) const; bool empty() const; };`

- [ ] **Step 1: Escribir las pruebas que fallan**

Agregar `#include "../src/engine/measure.h"` y `#include <chrono>` y `#include <random>` al principio de `tests/unit_tests.cpp`, y estas pruebas al final (antes de `int main`):

```cpp
// --- Seleccion y enganche --------------------------------------------------------

namespace {

struct PlanScene {
    stp::Mesh mesh;
    stp::PlanarInfo plane;
    stp::Camera camera;
    stp::PickIndex pick;
    int width = 800, height = 600;

    void finish() {
        plane = stp::detectPlanar(mesh);
        camera.fitPlanar(plane, static_cast<double>(width) / height);
        pick.build(mesh);
    }
    stp::SnapResult snapNear(const stp::Vec3& world, double dx, double dy, bool snapping = true) {
        double sx = 0, sy = 0;
        stp::projectPoint(camera, width, height, world, &sx, &sy);
        stp::SnapOptions options;
        options.snapping = snapping;
        options.plane = &plane;
        return pick.snap(mesh, camera, width, height, sx + dx, sy + dy, options);
    }
};

PlanScene lineAndCircle() {
    PlanScene scene;
    const std::string text = entities({{0, "LINE"}, {10, "0"}, {20, "0"}, {11, "100"}, {21, "0"},
                                       {0, "CIRCLE"}, {10, "50"}, {20, "40"}, {40, "10"}});
    loadDxfText(text, &scene.mesh);
    scene.finish();
    return scene;
}

}  // namespace

TEST(snap_endpoint_near_cursor) {
    PlanScene scene = lineAndCircle();
    const stp::SnapResult near = scene.snapNear(stp::Vec3(100, 0, 0), 5, -3);
    CHECK(near.kind == stp::SnapKind::Endpoint);
    CHECK_NEAR(near.point.x, 100.0, 1e-9);
    const stp::SnapResult far = scene.snapNear(stp::Vec3(100, 0, 0), 20, 0);
    CHECK(far.kind == stp::SnapKind::OnPlane);
}

TEST(snap_midpoint_and_center) {
    PlanScene scene = lineAndCircle();
    const stp::SnapResult mid = scene.snapNear(stp::Vec3(50, 0, 0), 2, 2);
    CHECK(mid.kind == stp::SnapKind::Midpoint);
    CHECK_NEAR(mid.point.x, 50.0, 1e-9);
    const stp::SnapResult center = scene.snapNear(stp::Vec3(50, 40, 0), -3, 1);
    CHECK(center.kind == stp::SnapKind::Center);
    CHECK(center.circle == 0);
}

TEST(snap_on_circle_edge_has_no_fake_endpoints) {
    PlanScene scene = lineAndCircle();
    // Un punto del circulo lejos de su centro: solo puede ser "sobre la arista".
    const stp::SnapResult edge = scene.snapNear(stp::Vec3(60, 40, 0), 0, 2);
    CHECK(edge.kind == stp::SnapKind::OnEdge);
    CHECK(edge.segment >= 1);
    CHECK_NEAR(stp::distance(edge.point, stp::Vec3(50, 40, 0)), 10.0, 0.05);
}

TEST(snap_can_be_disabled) {
    PlanScene scene = lineAndCircle();
    const stp::SnapResult free = scene.snapNear(stp::Vec3(100, 0, 0), 0, 0, false);
    CHECK(free.kind == stp::SnapKind::OnPlane);
    CHECK_NEAR(free.point.x, 100.0, 1e-6);
    CHECK_NEAR(free.point.z, 0.0, 1e-9);
}

TEST(raycast_matches_brute_force) {
    stp::Mesh mesh;
    CHECK(loadStepText(readText("tests/samples/placa_agujero.stp"), &mesh));
    stp::Camera camera;
    camera.fit(mesh.bounds, 1.0);
    stp::PickIndex pick;
    pick.build(mesh);
    for (int i = 0; i < 25; ++i) {
        const stp::Ray ray = stp::pixelRay(camera, 500, 500, 100 + 12 * i, 180 + 6 * i);
        double t = 0;
        int triangle = -1;
        const bool hit = pick.raycast(mesh, ray, &t, &triangle);
        double best = 1e300;
        for (std::size_t k = 0; k < mesh.triangleCount(); ++k) {
            double tk;
            if (stp::rayTriangle(ray, mesh.positions[mesh.indices[3 * k]],
                                 mesh.positions[mesh.indices[3 * k + 1]],
                                 mesh.positions[mesh.indices[3 * k + 2]], &tk)) {
                best = std::min(best, tk);
            }
        }
        CHECK(hit == (best < 1e299));
        if (hit) CHECK_NEAR(t, best, 1e-9);
    }
}

TEST(snap_ignores_points_hidden_behind_faces) {
    stp::Mesh mesh;
    CHECK(loadStepText(readText("tests/samples/caja.stp"), &mesh));
    stp::Camera camera;  // isometrica por defecto: el ojo esta en +X, -Y, +Z
    camera.fit(mesh.bounds, 1.0);
    stp::PickIndex pick;
    pick.build(mesh);
    const stp::Vec3 hidden(mesh.bounds.lo.x, mesh.bounds.hi.y, mesh.bounds.lo.z);
    const stp::Vec3 front(mesh.bounds.hi.x, mesh.bounds.lo.y, mesh.bounds.hi.z);
    double sx, sy;
    stp::SnapOptions options;
    stp::projectPoint(camera, 500, 500, hidden, &sx, &sy);
    const stp::SnapResult behind = pick.snap(mesh, camera, 500, 500, sx, sy, options);
    CHECK(!(behind.kind == stp::SnapKind::Endpoint && stp::distance(behind.point, hidden) < 1e-9));
    stp::projectPoint(camera, 500, 500, front, &sx, &sy);
    const stp::SnapResult visible = pick.snap(mesh, camera, 500, 500, sx + 2, sy - 2, options);
    CHECK(visible.kind == stp::SnapKind::Endpoint);
    CHECK_NEAR(stp::distance(visible.point, front), 0.0, 1e-9);
}

TEST(snap_is_fast_on_huge_drawing) {
    // 20 000 circulos de 72 tramos y 20 000 rectangulos: 1,52 millones de segmentos.
    PlanScene scene;
    std::mt19937 random(7);
    std::uniform_real_distribution<double> x(0, 5000), y(0, 3000), r(2, 30);
    for (int i = 0; i < 20000; ++i) {
        const stp::Vec3 c(x(random), y(random), 0);
        const double radius = r(random);
        for (int k = 0; k < 72; ++k) {
            const double a0 = 2 * stp::kPi * k / 72, a1 = 2 * stp::kPi * (k + 1) / 72;
            scene.mesh.addSegment(c + stp::Vec3(std::cos(a0), std::sin(a0), 0) * radius,
                                  c + stp::Vec3(std::cos(a1), std::sin(a1), 0) * radius, true);
        }
        stp::CircleFeature circle;
        circle.center = c;
        circle.radius = radius;
        scene.mesh.features.circles.push_back(circle);
        const stp::Vec3 o(x(random), y(random), 0);
        addRectangle(&scene.mesh, o, stp::Vec3(40, 0, 0), stp::Vec3(0, 25, 0));
    }
    const auto t0 = std::chrono::steady_clock::now();
    scene.finish();
    const auto t1 = std::chrono::steady_clock::now();
    std::uniform_real_distribution<double> px(0, scene.width), py(0, scene.height);
    stp::SnapOptions options;
    options.plane = &scene.plane;
    const int queries = 300;
    for (int i = 0; i < queries; ++i) {
        scene.pick.snap(scene.mesh, scene.camera, scene.width, scene.height, px(random), py(random), options);
    }
    const auto t2 = std::chrono::steady_clock::now();
    const double buildMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double queryMs = std::chrono::duration<double, std::milli>(t2 - t1).count() / queries;
    std::printf("    indice %.0f ms, consulta %.3f ms\n", buildMs, queryMs);
    CHECK(queryMs < 5.0);
    CHECK(buildMs < 3000.0);
}
```

(`addRectangle` ya existe desde las pruebas de planaridad; esta prueba debe ir **después** de ese bloque.)

- [ ] **Step 2: Correr y ver que falla**

Run: `./build.sh test`
Expected: error de compilación `../src/engine/measure.h: No such file or directory`.

- [ ] **Step 3: Crear `src/engine/bvh.h`**

```cpp
// Jerarquia de cajas (BVH) generica: la usan la seleccion por rayo y el
// enganche, que tienen que responder en milisegundos sobre millones de
// triangulos o segmentos.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "geom.h"

namespace stp {

class Bvh {
public:
    // boxOf(i, &lo, &hi) da la caja de la primitiva i.
    template <typename BoxOf>
    void build(std::size_t count, BoxOf boxOf, std::uint32_t leafSize = 8) {
        m_nodes.clear();
        m_order.resize(count);
        if (count == 0) return;
        std::vector<float> centers(count * 3);
        m_lo.resize(count);
        m_hi.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            m_order[i] = static_cast<std::uint32_t>(i);
            boxOf(i, &m_lo[i], &m_hi[i]);
            centers[3 * i] = static_cast<float>((m_lo[i].x + m_hi[i].x) * 0.5);
            centers[3 * i + 1] = static_cast<float>((m_lo[i].y + m_hi[i].y) * 0.5);
            centers[3 * i + 2] = static_cast<float>((m_lo[i].z + m_hi[i].z) * 0.5);
        }
        m_nodes.reserve(2 * count / leafSize + 2);
        buildRange(centers, 0, static_cast<std::uint32_t>(count), leafSize);
        m_lo.clear();
        m_lo.shrink_to_fit();
        m_hi.clear();
        m_hi.shrink_to_fit();
    }

    bool empty() const { return m_nodes.empty(); }

    // enter(lo, hi) decide si se baja por un nodo; visit(i) recibe cada primitiva.
    template <typename Enter, typename Visit>
    void query(Enter enter, Visit visit) const {
        if (m_nodes.empty()) return;
        std::uint32_t stack[96];
        int top = 0;
        stack[top++] = 0;
        while (top > 0) {
            const std::uint32_t index = stack[--top];
            const Node& node = m_nodes[index];
            if (!enter(node.lo, node.hi)) continue;
            if (node.count > 0) {
                for (std::uint32_t i = node.start; i < node.start + node.count; ++i) visit(m_order[i]);
            } else if (top + 2 <= 96) {
                stack[top++] = node.right;
                stack[top++] = index + 1;
            }
        }
    }

private:
    struct Node {
        Vec3 lo, hi;
        std::uint32_t start = 0, count = 0;  // hoja si count > 0
        std::uint32_t right = 0;             // el hijo izquierdo es index + 1
    };

    std::uint32_t buildRange(const std::vector<float>& centers, std::uint32_t start,
                             std::uint32_t end, std::uint32_t leafSize) {
        const std::uint32_t index = static_cast<std::uint32_t>(m_nodes.size());
        m_nodes.emplace_back();
        BBox bounds, middle;
        for (std::uint32_t i = start; i < end; ++i) {
            const std::uint32_t k = m_order[i];
            bounds.add(m_lo[k]);
            bounds.add(m_hi[k]);
            middle.add(Vec3(centers[3 * k], centers[3 * k + 1], centers[3 * k + 2]));
        }
        m_nodes[index].lo = bounds.lo;
        m_nodes[index].hi = bounds.hi;
        const Vec3 extent = middle.size();
        const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : (extent.y >= extent.z ? 1 : 2);
        const double spread = axis == 0 ? extent.x : (axis == 1 ? extent.y : extent.z);
        if (end - start <= leafSize || spread <= 0) {
            m_nodes[index].start = start;
            m_nodes[index].count = end - start;
            return index;
        }
        const std::uint32_t mid = start + (end - start) / 2;
        std::nth_element(m_order.begin() + start, m_order.begin() + mid, m_order.begin() + end,
                         [&](std::uint32_t a, std::uint32_t b) {
                             return centers[3 * a + axis] < centers[3 * b + axis];
                         });
        buildRange(centers, start, mid, leafSize);
        const std::uint32_t right = buildRange(centers, mid, end, leafSize);
        m_nodes[index].right = right;
        return index;
    }

    std::vector<Node> m_nodes;
    std::vector<std::uint32_t> m_order;
    std::vector<Vec3> m_lo, m_hi;  // solo durante la construccion
};

}  // namespace stp
```

- [ ] **Step 4: Crear `src/engine/measure.h`** (solo la parte de esta tarea; la Task 5 agrega los cálculos al final)

```cpp
// Medir sobre la malla: rayo desde un pixel, indice de seleccion, enganche a
// puntos notables y calculos de distancia, angulo, radio, area y perimetro.
// Sin dependencias de Windows: se prueba en Linux.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../render/renderer.h"
#include "bvh.h"
#include "mesh.h"
#include "planar.h"

namespace stp {

// ortho: la camara es ortografica y el rayo es una recta completa (la geometria
// puede estar detras del plano del ojo y aun asi se dibuja).
struct Ray {
    Vec3 origin;
    Vec3 dir{0, 0, -1};
    bool ortho = true;
};

Ray pixelRay(const Camera& camera, int width, int height, double sx, double sy);
// Lo que mide un pixel en unidades del modelo a la distancia t sobre el rayo.
double worldPerPixel(const Camera& camera, int height, double t);
bool rayTriangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c, double* t);

enum class SnapKind { None, Endpoint, Center, Midpoint, OnEdge, OnFace, OnPlane };

struct SnapResult {
    SnapKind kind = SnapKind::None;
    Vec3 point;
    int triangle = -1;  // triangulo bajo el cursor, si lo hay
    int segment = -1;   // Endpoint, Midpoint y OnEdge sobre un segmento
    int circle = -1;    // Center y extremos de arcos
};

struct SnapOptions {
    double radiusPixels = 8.0;
    bool snapping = true;  // Shift lo apaga
    const PlanarInfo* plane = nullptr;
};

class PickIndex {
public:
    void build(const Mesh& mesh);
    bool empty() const { return m_triangles.empty() && m_segments.empty(); }
    bool raycast(const Mesh& mesh, const Ray& ray, double* t, int* triangle) const;
    SnapResult snap(const Mesh& mesh, const Camera& camera, int width, int height, double sx,
                    double sy, const SnapOptions& options) const;

private:
    struct SnapPoint {
        Vec3 point;
        SnapKind kind = SnapKind::Endpoint;
        int segment = -1;
        int circle = -1;
    };
    Bvh m_triangles;
    Bvh m_segments;
    Bvh m_points;
    std::vector<SnapPoint> m_snapPoints;
    double m_size = 1.0;
};

}  // namespace stp
```

- [ ] **Step 5: Crear `src/engine/measure.cpp`**

```cpp
#include "measure.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace stp {
namespace {

double axisOf(const Vec3& v, int axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }

bool rayHitsBox(const Ray& ray, const Vec3& lo, const Vec3& hi) {
    double t0 = ray.ortho ? -1e300 : 0.0, t1 = 1e300;
    for (int axis = 0; axis < 3; ++axis) {
        const double o = axisOf(ray.origin, axis), d = axisOf(ray.dir, axis);
        const double l = axisOf(lo, axis), h = axisOf(hi, axis);
        if (std::fabs(d) < 1e-300) {
            if (o < l || o > h) return false;
            continue;
        }
        double tn = (l - o) / d, tf = (h - o) / d;
        if (tn > tf) std::swap(tn, tf);
        t0 = std::max(t0, tn);
        t1 = std::min(t1, tf);
        if (t0 > t1) return false;
    }
    return true;
}

// Punto del segmento ab mas cercano a la recta del rayo.
Vec3 closestOnSegment(const Vec3& a, const Vec3& b, const Ray& ray) {
    const Vec3 u = b - a, w = a - ray.origin;
    const double A = dot(u, u), B = dot(u, ray.dir), C = dot(ray.dir, ray.dir);
    const double D = dot(u, w), E = dot(ray.dir, w);
    const double den = A * C - B * B;
    double s = (A > 1e-300 && den > 1e-18 * A * C) ? (B * E - C * D) / den : 0.0;
    s = std::max(0.0, std::min(1.0, s));
    return a + u * s;
}

struct PointKey {
    long long x, y, z;
    bool operator==(const PointKey& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct PointKeyHash {
    std::size_t operator()(const PointKey& k) const {
        return static_cast<std::size_t>(k.x * 73856093LL ^ k.y * 19349663LL ^ k.z * 83492791LL);
    }
};

}  // namespace

Ray pixelRay(const Camera& camera, int width, int height, double sx, double sy) {
    const double aspect = height > 0 ? static_cast<double>(width) / height : 1.0;
    const double nx = (sx / std::max(1, width) - 0.5) * 2.0;
    const double ny = (0.5 - sy / std::max(1, height)) * 2.0;
    const Vec3 right = camera.right(), up = camera.up(), forward = camera.forward();
    Ray ray;
    ray.ortho = camera.ortho;
    if (camera.ortho) {
        const double halfH = camera.orthoHeight * 0.5;
        ray.origin = camera.eye() + right * (nx * halfH * aspect) + up * (ny * halfH);
        ray.dir = forward;
    } else {
        const double t = std::tan(camera.fov * 0.5);
        ray.origin = camera.eye();
        ray.dir = normalize(forward + right * (nx * t * aspect) + up * (ny * t));
    }
    return ray;
}

double worldPerPixel(const Camera& camera, int height, double t) {
    const double h = std::max(1, height);
    if (camera.ortho) return camera.orthoHeight / h;
    return 2.0 * std::tan(camera.fov * 0.5) * std::max(t, 0.0) / h;
}

bool rayTriangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c, double* t) {
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 p = cross(ray.dir, e2);
    const double det = dot(e1, p);
    if (std::fabs(det) < 1e-300) return false;
    const double inv = 1.0 / det;
    const Vec3 s = ray.origin - a;
    const double u = dot(s, p) * inv;
    if (u < -1e-12 || u > 1 + 1e-12) return false;
    const Vec3 q = cross(s, e1);
    const double v = dot(ray.dir, q) * inv;
    if (v < -1e-12 || u + v > 1 + 1e-12) return false;
    const double hit = dot(e2, q) * inv;
    if (!ray.ortho && hit <= 1e-12) return false;
    *t = hit;
    return true;
}

void PickIndex::build(const Mesh& mesh) {
    m_size = std::max(1e-9, mesh.bounds.valid() ? mesh.bounds.diagonal() : 1.0);

    m_triangles.build(mesh.triangleCount(), [&](std::size_t i, Vec3* lo, Vec3* hi) {
        BBox box;
        for (int k = 0; k < 3; ++k) box.add(mesh.positions[mesh.indices[3 * i + k]]);
        *lo = box.lo;
        *hi = box.hi;
    });
    m_segments.build(mesh.edgeLines.size() / 2, [&](std::size_t i, Vec3* lo, Vec3* hi) {
        BBox box;
        box.add(mesh.edgeLines[2 * i]);
        box.add(mesh.edgeLines[2 * i + 1]);
        *lo = box.lo;
        *hi = box.hi;
    });

    // Puntos notables: extremos y medios de las rectas, centros y extremos de arcos.
    m_snapPoints.clear();
    const double quantum = m_size * 1e-9;
    std::unordered_set<PointKey, PointKeyHash> seen;
    auto add = [&](const Vec3& p, SnapKind kind, int segment, int circle) {
        const PointKey key{std::llround(p.x / quantum), std::llround(p.y / quantum),
                           std::llround(p.z / quantum)};
        if (!seen.insert(key).second) return;
        m_snapPoints.push_back({p, kind, segment, circle});
    };
    const std::size_t segments = mesh.edgeLines.size() / 2;
    for (std::size_t i = 0; i < segments; ++i) {
        if (i < mesh.edgeCurve.size() && mesh.edgeCurve[i]) continue;
        const Vec3& a = mesh.edgeLines[2 * i];
        const Vec3& b = mesh.edgeLines[2 * i + 1];
        add(a, SnapKind::Endpoint, static_cast<int>(i), -1);
        add(b, SnapKind::Endpoint, static_cast<int>(i), -1);
    }
    for (std::size_t i = 0; i < segments; ++i) {
        if (i < mesh.edgeCurve.size() && mesh.edgeCurve[i]) continue;
        add((mesh.edgeLines[2 * i] + mesh.edgeLines[2 * i + 1]) * 0.5, SnapKind::Midpoint,
            static_cast<int>(i), -1);
    }
    for (std::size_t i = 0; i < mesh.features.circles.size(); ++i) {
        const CircleFeature& c = mesh.features.circles[i];
        add(c.center, SnapKind::Center, -1, static_cast<int>(i));
        if (!c.full()) {
            add(c.pointAt(c.startAngle), SnapKind::Endpoint, -1, static_cast<int>(i));
            add(c.pointAt(c.startAngle + c.sweep), SnapKind::Endpoint, -1, static_cast<int>(i));
        }
    }
    m_points.build(m_snapPoints.size(), [&](std::size_t i, Vec3* lo, Vec3* hi) {
        *lo = m_snapPoints[i].point;
        *hi = m_snapPoints[i].point;
    });
}

bool PickIndex::raycast(const Mesh& mesh, const Ray& ray, double* t, int* triangle) const {
    double best = 1e300;
    int bestTriangle = -1;
    m_triangles.query([&](const Vec3& lo, const Vec3& hi) { return rayHitsBox(ray, lo, hi); },
                      [&](std::uint32_t i) {
                          double hit;
                          if (rayTriangle(ray, mesh.positions[mesh.indices[3 * i]],
                                          mesh.positions[mesh.indices[3 * i + 1]],
                                          mesh.positions[mesh.indices[3 * i + 2]], &hit) &&
                              hit < best) {
                              best = hit;
                              bestTriangle = static_cast<int>(i);
                          }
                      });
    if (bestTriangle < 0) return false;
    *t = best;
    *triangle = bestTriangle;
    return true;
}

SnapResult PickIndex::snap(const Mesh& mesh, const Camera& camera, int width, int height,
                           double sx, double sy, const SnapOptions& options) const {
    SnapResult result;
    const Ray ray = pixelRay(camera, width, height, sx, sy);
    double hitT = 0.0;
    int hitTriangle = -1;
    const bool hit = raycast(mesh, ray, &hitT, &hitTriangle);
    result.triangle = hit ? hitTriangle : -1;

    // Un punto tapado por la pieza no se engancha.
    const double occlusion = m_size * 1e-3;
    auto visible = [&](const Vec3& p) { return !hit || dot(p - ray.origin, ray.dir) <= hitT + occlusion; };
    auto pixels = [&](const Vec3& p, double* d) {
        double px, py;
        if (!projectPoint(camera, width, height, p, &px, &py)) return false;
        *d = std::hypot(px - sx, py - sy);
        return true;
    };
    auto enter = [&](const Vec3& lo, const Vec3& hi) {
        const Vec3 center = (lo + hi) * 0.5;
        const double t = std::max(0.0, dot(center - ray.origin, ray.dir)) + length(hi - lo) * 0.5;
        const double r = options.radiusPixels * worldPerPixel(camera, height, t);
        return rayHitsBox(ray, lo - Vec3(r, r, r), hi + Vec3(r, r, r));
    };

    if (options.snapping) {
        int bestPriority = 99;
        double bestDistance = 1e300;
        const SnapPoint* best = nullptr;
        m_points.query(enter, [&](std::uint32_t i) {
            const SnapPoint& s = m_snapPoints[i];
            double d;
            if (!pixels(s.point, &d) || d > options.radiusPixels || !visible(s.point)) return;
            const int priority = s.kind == SnapKind::Endpoint ? 0 : (s.kind == SnapKind::Center ? 1 : 2);
            if (priority < bestPriority || (priority == bestPriority && d < bestDistance)) {
                bestPriority = priority;
                bestDistance = d;
                best = &s;
            }
        });
        if (best) {
            result.kind = best->kind;
            result.point = best->point;
            result.segment = best->segment;
            result.circle = best->circle;
            return result;
        }

        double bestEdge = 1e300;
        m_segments.query(enter, [&](std::uint32_t i) {
            const Vec3 q = closestOnSegment(mesh.edgeLines[2 * i], mesh.edgeLines[2 * i + 1], ray);
            double d;
            if (!pixels(q, &d) || d > options.radiusPixels || !visible(q) || d >= bestEdge) return;
            bestEdge = d;
            result.kind = SnapKind::OnEdge;
            result.point = q;
            result.segment = static_cast<int>(i);
        });
        if (result.kind == SnapKind::OnEdge) return result;
    }

    if (hit) {
        result.kind = SnapKind::OnFace;
        result.point = ray.origin + ray.dir * hitT;
        return result;
    }
    if (options.plane && options.plane->planar) {
        const double denominator = dot(ray.dir, options.plane->normal);
        if (std::fabs(denominator) > 1e-12) {
            const double t = dot(options.plane->center - ray.origin, options.plane->normal) / denominator;
            result.kind = SnapKind::OnPlane;
            result.point = ray.origin + ray.dir * t;
        }
    }
    return result;
}

}  // namespace stp
```

- [ ] **Step 6: Agregar a la compilación**

`build.sh:14`: agregar `src/engine/measure.cpp ` dentro de `ENGINE`. `build.bat:9`: agregar `src\engine\measure.cpp `.

- [ ] **Step 7: Correr las pruebas**

Run: `./build.sh test`
Expected: `63 pruebas, 0 fallidas`, y la línea `indice N ms, consulta M ms` con M < 5.

- [ ] **Step 8: Commit**

```bash
git add src/engine/bvh.h src/engine/measure.h src/engine/measure.cpp build.sh build.bat tests/unit_tests.cpp
git commit -m "Add BVH pick index with ray casting and snapping

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_015NM5546gRGTfskkpAXPqTF"
```

---

### Task 5: Cálculos de medida y formato de valores

**Files:**
- Modify: `src/engine/measure.h`, `src/engine/measure.cpp`
- Test: `tests/unit_tests.cpp`

**Interfaces:**
- Consumes: `Mesh`, `MeshFeatures`, `PlanarInfo`, `LengthUnit`, `unitSuffix` (Task 1).
- Produces (agregado al final de `measure.h`):
  - `struct DistanceResult { double total = 0; Vec3 delta; };` — `delta` en ejes del mundo o, con plano, `(Δu, Δv, 0)`.
  - `DistanceResult measureDistance(const Vec3& a, const Vec3& b, const PlanarInfo* plane);`
  - `double angleAt(const Vec3& a, const Vec3& vertex, const Vec3& b);` — grados, 0..180
  - `double angleBetweenLines(const Vec3& a0, const Vec3& a1, const Vec3& pickA, const Vec3& b0, const Vec3& b1, const Vec3& pickB);` — grados
  - `struct RadiusResult { bool ok = false; double radius = 0; Vec3 center; Vec3 normal; std::string error; };`
  - `RadiusResult measureRadius(const Mesh& mesh, const Vec3& point, int triangle, double tolerance);`
  - `struct AreaResult { bool ok = false; double area = 0; double perimeter = 0; std::vector<std::uint32_t> triangles; int contour = -1; std::vector<int> holes; std::string error; };`
  - `AreaResult measureArea(const Mesh& mesh, const Vec3& point, int triangle, const PlanarInfo* plane);`
  - `double contourArea(const ContourFeature& c);`, `double contourPerimeter(const ContourFeature& c);`, `bool contourContains(const ContourFeature& c, const Vec3& p);`
  - `std::vector<Vec3> contourOutline(const ContourFeature& c);` — contorno muestreado (arcos a 32 tramos), para dibujar
  - `std::string formatNumber(double value, int decimals);`, `std::string formatLength(double value, LengthUnit unit);`, `std::string formatArea(double value, LengthUnit unit);`, `std::string formatAngle(double degrees);` — UTF-8
  - `int PickIndex::triangleAt(const Mesh& mesh, const Vec3& point, double tolerance) const;` — triángulo que contiene un punto guardado (para recalcular radio y área al abrir las marcas); `-1` si ninguno.

- [ ] **Step 1: Escribir las pruebas que fallan**

Agregar al final de `tests/unit_tests.cpp`, antes de `int main`:

```cpp
// --- Calculos de medida ---------------------------------------------------------------

TEST(measure_distance_and_deltas) {
    const stp::DistanceResult d = stp::measureDistance(stp::Vec3(1, 2, 3), stp::Vec3(4, 6, 3), nullptr);
    CHECK_NEAR(d.total, 5.0, 1e-12);
    CHECK_NEAR(d.delta.x, 3.0, 1e-12);
    CHECK_NEAR(d.delta.y, 4.0, 1e-12);
    stp::Mesh mesh;
    addRectangle(&mesh, stp::Vec3(0, 3, 0), stp::Vec3(40, 0, 0), stp::Vec3(0, 0, 15));
    const stp::PlanarInfo plane = stp::detectPlanar(mesh);  // plano XZ visto de frente
    const stp::DistanceResult p = stp::measureDistance(stp::Vec3(0, 3, 0), stp::Vec3(40, 3, 15), &plane);
    CHECK_NEAR(p.delta.x, 40.0, 1e-9);  // a lo largo de u (X)
    CHECK_NEAR(p.delta.y, 15.0, 1e-9);  // a lo largo de v (Z)
    CHECK_NEAR(p.delta.z, 0.0, 1e-12);
}

TEST(measure_angles) {
    CHECK_NEAR(stp::angleAt(stp::Vec3(1, 0, 0), stp::Vec3(0, 0, 0), stp::Vec3(0, 5, 0)), 90.0, 1e-9);
    CHECK_NEAR(stp::angleAt(stp::Vec3(1, 0, 0), stp::Vec3(0, 0, 0), stp::Vec3(3, 3, 0)), 45.0, 1e-9);
    // Dos rectas que se cortan en (10,0): el angulo depende de donde se hizo clic.
    const double sharp = stp::angleBetweenLines(stp::Vec3(0, 0, 0), stp::Vec3(20, 0, 0), stp::Vec3(15, 0, 0),
                                                stp::Vec3(10, 0, 0), stp::Vec3(20, 10, 0), stp::Vec3(15, 5, 0));
    CHECK_NEAR(sharp, 45.0, 1e-9);
    const double obtuse = stp::angleBetweenLines(stp::Vec3(0, 0, 0), stp::Vec3(20, 0, 0), stp::Vec3(2, 0, 0),
                                                 stp::Vec3(10, 0, 0), stp::Vec3(20, 10, 0), stp::Vec3(15, 5, 0));
    CHECK_NEAR(obtuse, 135.0, 1e-9);
}

TEST(measure_radius_from_circle_and_cylinder) {
    stp::Mesh drawing;
    CHECK(loadDxfText(entities({{0, "CIRCLE"}, {10, "0"}, {20, "0"}, {40, "5"}}), &drawing));
    const stp::RadiusResult onEdge = stp::measureRadius(drawing, stp::Vec3(0, 5.01, 0), -1, 0.1);
    CHECK(onEdge.ok);
    CHECK_NEAR(onEdge.radius, 5.0, 1e-12);
    const stp::RadiusResult atCenter = stp::measureRadius(drawing, stp::Vec3(0.02, 0, 0), -1, 0.1);
    CHECK(atCenter.ok);
    const stp::RadiusResult nothing = stp::measureRadius(drawing, stp::Vec3(20, 20, 0), -1, 0.1);
    CHECK(!nothing.ok);
    CHECK(nothing.error == "No hay un circulo aqui");

    stp::Mesh part;
    CHECK(loadStepText(readText("tests/samples/placa_agujero.stp"), &part));
    int cylinderTriangle = -1;
    for (const stp::FaceFeature& f : part.features.faces) {
        if (f.kind == stp::SurfaceKind::Cylinder) cylinderTriangle = static_cast<int>(f.firstTriangle);
    }
    CHECK(cylinderTriangle >= 0);
    const std::uint32_t* tri = &part.indices[3 * cylinderTriangle];
    const stp::Vec3 inside = (part.positions[tri[0]] + part.positions[tri[1]] + part.positions[tri[2]]) * (1.0 / 3);
    // Lejos de cualquier arista: tiene que salir de la cara cilindrica.
    const stp::RadiusResult face = stp::measureRadius(part, inside, cylinderTriangle, 1e-6);
    CHECK(face.ok);
    CHECK_NEAR(face.radius, 10.0, 1e-9);

    stp::Mesh stl;
    const std::uint32_t a = stl.addVertex(stp::Vec3(0, 0, 0), stp::Vec3(0, 0, 1));
    const std::uint32_t b = stl.addVertex(stp::Vec3(1, 0, 0), stp::Vec3(0, 0, 1));
    const std::uint32_t c = stl.addVertex(stp::Vec3(0, 1, 0), stp::Vec3(0, 0, 1));
    stl.addTriangle(a, b, c);
    const stp::RadiusResult none = stp::measureRadius(stl, stp::Vec3(0.2, 0.2, 0), 0, 0.1);
    CHECK(!none.ok);
    CHECK(none.error == "Este formato no guarda circulos; usa distancia");
}

TEST(measure_area_of_plate_with_hole) {
    const std::string text = entities({{0, "LWPOLYLINE"}, {90, "4"}, {70, "1"},
                                       {10, "0"}, {20, "0"}, {10, "100"}, {20, "0"},
                                       {10, "100"}, {20, "50"}, {10, "0"}, {20, "50"},
                                       {0, "CIRCLE"}, {10, "50"}, {20, "25"}, {40, "5"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    const stp::PlanarInfo plane = stp::detectPlanar(mesh);
    const stp::AreaResult plate = stp::measureArea(mesh, stp::Vec3(10, 10, 0), -1, &plane);
    CHECK(plate.ok);
    CHECK_NEAR(plate.area, 5000.0 - 25.0 * stp::kPi, 1e-9);
    CHECK_NEAR(plate.perimeter, 300.0 + 10.0 * stp::kPi, 1e-9);
    CHECK(plate.holes.size() == 1);
    const stp::AreaResult hole = stp::measureArea(mesh, stp::Vec3(50, 25, 0), -1, &plane);
    CHECK(hole.ok);
    CHECK_NEAR(hole.area, 25.0 * stp::kPi, 1e-9);
    const stp::AreaResult outside = stp::measureArea(mesh, stp::Vec3(200, 25, 0), -1, &plane);
    CHECK(!outside.ok);
    CHECK(outside.error == "No hay un contorno cerrado aqui");
}

TEST(measure_area_of_bulged_contour_is_exact) {
    // Cuadrado 10x10 con el lado izquierdo abombado hacia afuera: 100 + media circunferencia de r=5.
    stp::Mesh mesh;
    CHECK(loadDxfText(entities({{0, "LWPOLYLINE"}, {90, "4"}, {70, "1"},
                                {10, "0"}, {20, "0"}, {10, "10"}, {20, "0"},
                                {10, "10"}, {20, "10"}, {10, "0"}, {20, "10"}, {42, "1"}}),
                      &mesh));
    CHECK(mesh.features.contours.size() == 1);
    if (mesh.features.contours.empty()) return;
    CHECK_NEAR(stp::contourArea(mesh.features.contours[0]), 100.0 + 12.5 * stp::kPi, 1e-9);
    CHECK_NEAR(stp::contourPerimeter(mesh.features.contours[0]), 30.0 + 5.0 * stp::kPi, 1e-9);
    CHECK(stp::contourContains(mesh.features.contours[0], stp::Vec3(-3, 5, 0)));
    CHECK(!stp::contourContains(mesh.features.contours[0], stp::Vec3(-6, 5, 0)));
}

TEST(measure_area_of_solid_face) {
    stp::Mesh part;
    CHECK(loadStepText(readText("tests/samples/placa_agujero.stp"), &part));
    for (const stp::FaceFeature& f : part.features.faces) {
        if (f.kind != stp::SurfaceKind::Cylinder) continue;
        const std::uint32_t* tri = &part.indices[3 * f.firstTriangle];
        const stp::Vec3 p = (part.positions[tri[0]] + part.positions[tri[1]] + part.positions[tri[2]]) * (1.0 / 3);
        const stp::AreaResult r = stp::measureArea(part, p, static_cast<int>(f.firstTriangle), nullptr);
        CHECK(r.ok);
        // El agujero de la muestra esta partido en dos medias caras cilindricas:
        // cada una mide pi*r*h y su borde son dos medias circunferencias y dos rectas.
        const double height = part.bounds.size().z;
        CHECK(std::fabs(r.area - stp::kPi * 10.0 * height) < 0.01 * r.area);
        CHECK(std::fabs(r.perimeter - (2 * stp::kPi * 10.0 + 2 * height)) < 0.01 * r.perimeter);
        CHECK(r.triangles.size() == f.triangleCount);
    }
}

TEST(measure_area_floods_coplanar_triangles_without_faces) {
    // Malla sin caras (como un STL): dos triangulos coplanares y uno doblado.
    stp::Mesh mesh;
    const stp::Vec3 n(0, 0, 1);
    const std::uint32_t a = mesh.addVertex(stp::Vec3(0, 0, 0), n), b = mesh.addVertex(stp::Vec3(10, 0, 0), n);
    const std::uint32_t c = mesh.addVertex(stp::Vec3(10, 10, 0), n), d = mesh.addVertex(stp::Vec3(0, 10, 0), n);
    const std::uint32_t e = mesh.addVertex(stp::Vec3(10, 10, 5), n);
    mesh.addTriangle(a, b, c);
    mesh.addTriangle(a, c, d);
    mesh.addTriangle(b, e, c);
    const stp::AreaResult r = stp::measureArea(mesh, stp::Vec3(2, 1, 0), 0, nullptr);
    CHECK(r.ok);
    CHECK_NEAR(r.area, 100.0, 1e-9);
    CHECK_NEAR(r.perimeter, 40.0, 1e-9);
}

TEST(pick_finds_triangle_under_saved_point) {
    stp::Mesh part;
    CHECK(loadStepText(readText("tests/samples/placa_agujero.stp"), &part));
    stp::PickIndex pick;
    pick.build(part);
    const std::uint32_t* tri = &part.indices[3 * 7];
    const stp::Vec3 p = (part.positions[tri[0]] + part.positions[tri[1]] + part.positions[tri[2]]) * (1.0 / 3);
    const int found = pick.triangleAt(part, p, 1e-6);
    CHECK(found >= 0);
    if (found >= 0) {
        const std::uint32_t* f = &part.indices[3 * found];
        const stp::Vec3 n = stp::cross(part.positions[f[1]] - part.positions[f[0]], part.positions[f[2]] - part.positions[f[0]]);
        CHECK(std::fabs(stp::dot(p - part.positions[f[0]], stp::normalize(n))) < 1e-6);
    }
    CHECK(pick.triangleAt(part, part.bounds.hi + stp::Vec3(50, 50, 50), 1e-6) == -1);
}

TEST(measure_formatting) {
    CHECK(stp::formatNumber(220.0, 3) == "220");
    CHECK(stp::formatNumber(12.5, 3) == "12.5");
    CHECK(stp::formatNumber(1.0 / 3.0, 3) == "0.333");
    CHECK(stp::formatNumber(-0.0001, 3) == "0");
    CHECK(stp::formatLength(220.0, stp::LengthUnit::Millimeter) == "220 mm");
    CHECK(stp::formatLength(3.25, stp::LengthUnit::Unknown) == "3.25");
    CHECK(stp::formatArea(78.5398, stp::LengthUnit::Millimeter) == "78.54 mm\xC2\xB2");
    CHECK(stp::formatAngle(45.0) == "45\xC2\xB0");
    CHECK(stp::formatAngle(33.3333) == "33.33\xC2\xB0");
}
```

- [ ] **Step 2: Correr y ver que falla**

Run: `./build.sh test`
Expected: error de compilación `'measureDistance' is not a member of 'stp'`.

- [ ] **Step 3: Agregar las declaraciones al final de `measure.h`** (antes del cierre del espacio de nombres)

```cpp
// --- Calculos ----------------------------------------------------------------

struct DistanceResult {
    double total = 0.0;
    Vec3 delta;  // en ejes del mundo; con plano, (du, dv, 0) en los ejes del dibujo
};
DistanceResult measureDistance(const Vec3& a, const Vec3& b, const PlanarInfo* plane);

// Angulo en vertex entre las semirrectas hacia a y hacia b, en grados (0..180).
double angleAt(const Vec3& a, const Vec3& vertex, const Vec3& b);
// Angulo entre dos rectas, del lado de los puntos donde se hizo clic, en grados.
double angleBetweenLines(const Vec3& a0, const Vec3& a1, const Vec3& pickA, const Vec3& b0,
                         const Vec3& b1, const Vec3& pickB);

struct RadiusResult {
    bool ok = false;
    double radius = 0.0;
    Vec3 center;
    Vec3 normal{0, 0, 1};
    std::string error;
};
// tolerance: distancia maxima (unidades del modelo) del punto al circulo o a su centro.
RadiusResult measureRadius(const Mesh& mesh, const Vec3& point, int triangle, double tolerance);

struct AreaResult {
    bool ok = false;
    double area = 0.0;
    double perimeter = 0.0;             // borde exterior mas bordes de agujeros
    std::vector<std::uint32_t> triangles;  // cara medida (3D)
    int contour = -1;                   // contorno medido (plano)
    std::vector<int> holes;             // contornos restados
    std::string error;
};
AreaResult measureArea(const Mesh& mesh, const Vec3& point, int triangle, const PlanarInfo* plane);

double contourArea(const ContourFeature& contour);
double contourPerimeter(const ContourFeature& contour);
bool contourContains(const ContourFeature& contour, const Vec3& point);
std::vector<Vec3> contourOutline(const ContourFeature& contour);

// Numeros con hasta `decimals` decimales, sin ceros sobrantes y con punto.
std::string formatNumber(double value, int decimals);
std::string formatLength(double value, LengthUnit unit);  // "220 mm"
std::string formatArea(double value, LengthUnit unit);    // "78.54 mm²" (2 decimales)
std::string formatAngle(double degrees);                  // "45.5°"
```

Y en `class PickIndex` (sección `public`), agregar:

```cpp
    // Triangulo que contiene point (a menos de tolerance de su plano), o -1.
    int triangleAt(const Mesh& mesh, const Vec3& point, double tolerance) const;
```

- [ ] **Step 4: Implementar al final de `measure.cpp`** (antes del cierre del espacio de nombres; agregar `#include <cstdio>`, `#include <map>`, `#include <unordered_map>` arriba)

```cpp
// --- Calculos ----------------------------------------------------------------

DistanceResult measureDistance(const Vec3& a, const Vec3& b, const PlanarInfo* plane) {
    DistanceResult result;
    const Vec3 d = b - a;
    result.total = length(d);
    result.delta = (plane && plane->planar) ? Vec3(dot(d, plane->u), dot(d, plane->v), 0.0) : d;
    return result;
}

double angleAt(const Vec3& a, const Vec3& vertex, const Vec3& b) {
    const Vec3 u = a - vertex, v = b - vertex;
    const double lu = length(u), lv = length(v);
    if (lu < 1e-300 || lv < 1e-300) return 0.0;
    const double c = std::max(-1.0, std::min(1.0, dot(u, v) / (lu * lv)));
    return std::acos(c) * 180.0 / kPi;
}

double angleBetweenLines(const Vec3& a0, const Vec3& a1, const Vec3& pickA, const Vec3& b0,
                         const Vec3& b1, const Vec3& pickB) {
    const Vec3 u = normalize(a1 - a0), v = normalize(b1 - b0);
    const Vec3 w = a0 - b0;
    const double B = dot(u, v), D = dot(u, w), E = dot(v, w);
    const double den = 1.0 - B * B;
    if (den < 1e-18) return 0.0;  // paralelas
    // Puntos mas cercanos entre las dos rectas; su punto medio es el cruce.
    const double s = (B * E - D) / den;
    const double t = (E - B * D) / den;
    const Vec3 cross = ((a0 + u * s) + (b0 + v * t)) * 0.5;
    const Vec3 da = dot(pickA - cross, u) >= 0 ? u : -u;
    const Vec3 db = dot(pickB - cross, v) >= 0 ? v : -v;
    return angleAt(cross + da, cross, cross + db);
}

namespace {

// Distancia de p al circulo (o al arco) c.
double distanceToCircle(const CircleFeature& c, const Vec3& p) {
    const Vec3 d = p - c.center;
    const double h = dot(d, c.normal);
    const Vec3 inPlane = d - c.normal * h;
    const double rho = length(inPlane);
    double onCurve = std::sqrt(h * h + (rho - c.radius) * (rho - c.radius));
    if (!c.full() && rho > 1e-300) {
        const Vec3 y = cross(c.normal, c.xAxis);
        double angle = std::atan2(dot(inPlane, y), dot(inPlane, c.xAxis)) - c.startAngle;
        const double sweep = c.sweep;
        // Llevar el angulo al rango del barrido, con su signo.
        while (sweep >= 0 && angle < 0) angle += 2 * kPi;
        while (sweep < 0 && angle > 0) angle -= 2 * kPi;
        const bool inside = sweep >= 0 ? angle <= sweep : angle >= sweep;
        if (!inside) {
            onCurve = std::min(distance(p, c.pointAt(c.startAngle)),
                               distance(p, c.pointAt(c.startAngle + c.sweep)));
        }
    }
    return std::min(onCurve, distance(p, c.center));
}

int faceOfTriangle(const Mesh& mesh, int triangle) {
    const std::vector<FaceFeature>& faces = mesh.features.faces;
    if (triangle < 0 || faces.empty()) return -1;
    const auto it = std::upper_bound(faces.begin(), faces.end(), static_cast<std::uint32_t>(triangle),
                                     [](std::uint32_t t, const FaceFeature& f) { return t < f.firstTriangle; });
    if (it == faces.begin()) return -1;
    const FaceFeature& f = *(it - 1);
    if (static_cast<std::uint32_t>(triangle) >= f.firstTriangle + f.triangleCount) return -1;
    return static_cast<int>(it - 1 - faces.begin());
}

struct VertexKey {
    long long x, y, z;
    bool operator<(const VertexKey& o) const {
        return x != o.x ? x < o.x : (y != o.y ? y < o.y : z < o.z);
    }
};

// Bulge -> datos del arco en el plano del contorno: devuelve el barrido con signo y el radio.
void bulgeArc(double chord, double bulge, double* sweep, double* radius) {
    *sweep = 4.0 * std::atan(bulge);
    *radius = chord / (2.0 * std::sin(std::fabs(*sweep) * 0.5));
}

// Base ortonormal del plano del contorno.
void contourBasis(const ContourFeature& c, Vec3* e1, Vec3* e2) {
    const Vec3 ref = std::fabs(c.normal.x) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    *e1 = normalize(ref - c.normal * dot(ref, c.normal));
    *e2 = cross(c.normal, *e1);
}

// Contorno muestreado en 2D (arcos a `arcSamples` tramos).
std::vector<Vec2> contourPolygon(const ContourFeature& c, int arcSamples) {
    Vec3 e1, e2;
    contourBasis(c, &e1, &e2);
    const Vec3 origin = c.points.empty() ? Vec3() : c.points[0];
    auto flat = [&](const Vec3& p) { return Vec2(dot(p - origin, e1), dot(p - origin, e2)); };
    std::vector<Vec2> polygon;
    const std::size_t n = c.points.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = flat(c.points[i]);
        const Vec2 b = flat(c.points[(i + 1) % n]);
        polygon.push_back(a);
        const double bulge = i < c.bulges.size() ? c.bulges[i] : 0.0;
        const double chord = length(b - a);
        if (std::fabs(bulge) <= 1e-9 || chord < 1e-12) continue;
        const double sweep = 4.0 * std::atan(bulge);
        const double offset = chord * (1.0 - bulge * bulge) / (4.0 * bulge);
        const Vec2 mid = (a + b) * 0.5;
        const Vec2 center(mid.x - (b.y - a.y) / chord * offset, mid.y + (b.x - a.x) / chord * offset);
        const double radius = length(a - center);
        const double start = std::atan2(a.y - center.y, a.x - center.x);
        for (int k = 1; k < arcSamples; ++k) {
            const double angle = start + sweep * k / arcSamples;
            polygon.emplace_back(center.x + radius * std::cos(angle), center.y + radius * std::sin(angle));
        }
    }
    return polygon;
}

}  // namespace

RadiusResult measureRadius(const Mesh& mesh, const Vec3& point, int triangle, double tolerance) {
    RadiusResult result;
    double best = 1e300;
    for (const CircleFeature& c : mesh.features.circles) {
        const double d = distanceToCircle(c, point);
        if (d <= tolerance && d < best) {
            best = d;
            result.ok = true;
            result.radius = c.radius;
            result.center = c.center;
            result.normal = c.normal;
        }
    }
    if (result.ok) return result;

    const int face = faceOfTriangle(mesh, triangle);
    if (face >= 0) {
        const FaceFeature& f = mesh.features.faces[static_cast<std::size_t>(face)];
        if (f.kind == SurfaceKind::Cylinder || f.kind == SurfaceKind::Sphere) {
            result.ok = true;
            result.radius = f.radius;
            result.normal = f.axisDir;
            result.center = f.kind == SurfaceKind::Sphere
                                ? f.axisOrigin
                                : f.axisOrigin + f.axisDir * dot(point - f.axisOrigin, f.axisDir);
            return result;
        }
    }
    result.error = (mesh.features.circles.empty() && mesh.features.faces.empty())
                       ? "Este formato no guarda circulos; usa distancia"
                       : "No hay un circulo aqui";
    return result;
}

double contourArea(const ContourFeature& c) {
    const std::size_t n = c.points.size();
    if (n < 2) return 0.0;
    double area = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3& a = c.points[i];
        const Vec3& b = c.points[(i + 1) % n];
        area += dot(cross(a - c.points[0], b - c.points[0]), c.normal) * 0.5;
        const double bulge = i < c.bulges.size() ? c.bulges[i] : 0.0;
        const double chord = distance(a, b);
        if (std::fabs(bulge) > 1e-9 && chord > 1e-12) {
            double sweep, radius;
            bulgeArc(chord, bulge, &sweep, &radius);
            const double segment = radius * radius * 0.5 * (std::fabs(sweep) - std::sin(std::fabs(sweep)));
            area += bulge > 0 ? segment : -segment;
        }
    }
    return std::fabs(area);
}

double contourPerimeter(const ContourFeature& c) {
    const std::size_t n = c.points.size();
    double total = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double chord = distance(c.points[i], c.points[(i + 1) % n]);
        const double bulge = i < c.bulges.size() ? c.bulges[i] : 0.0;
        if (std::fabs(bulge) > 1e-9 && chord > 1e-12) {
            double sweep, radius;
            bulgeArc(chord, bulge, &sweep, &radius);
            total += radius * std::fabs(sweep);
        } else {
            total += chord;
        }
    }
    return total;
}

bool contourContains(const ContourFeature& c, const Vec3& point) {
    if (c.points.empty()) return false;
    const std::vector<Vec2> polygon = contourPolygon(c, 32);
    Vec3 e1, e2;
    contourBasis(c, &e1, &e2);
    const Vec2 p(dot(point - c.points[0], e1), dot(point - c.points[0], e2));
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Vec2& a = polygon[i];
        const Vec2& b = polygon[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

std::vector<Vec3> contourOutline(const ContourFeature& c) {
    std::vector<Vec3> outline;
    if (c.points.empty()) return outline;
    Vec3 e1, e2;
    contourBasis(c, &e1, &e2);
    for (const Vec2& q : contourPolygon(c, 32)) outline.push_back(c.points[0] + e1 * q.x + e2 * q.y);
    return outline;
}

AreaResult measureArea(const Mesh& mesh, const Vec3& point, int triangle, const PlanarInfo* plane) {
    AreaResult result;
    const std::vector<ContourFeature>& contours = mesh.features.contours;

    if (plane && plane->planar && !contours.empty()) {
        // El contorno mas chico que contiene el punto, menos los que tiene adentro.
        int chosen = -1;
        double chosenArea = 1e300;
        std::vector<double> areas(contours.size());
        for (std::size_t i = 0; i < contours.size(); ++i) {
            areas[i] = contourArea(contours[i]);
            if (areas[i] < chosenArea && contourContains(contours[i], point)) {
                chosen = static_cast<int>(i);
                chosenArea = areas[i];
            }
        }
        if (chosen >= 0) {
            std::vector<int> inside;
            for (std::size_t i = 0; i < contours.size(); ++i) {
                if (static_cast<int>(i) == chosen || areas[i] >= chosenArea || contours[i].points.empty()) continue;
                if (contourContains(contours[static_cast<std::size_t>(chosen)], contours[i].points[0])) {
                    inside.push_back(static_cast<int>(i));
                }
            }
            // Solo los hijos directos: los que no estan dentro de otro de la lista.
            for (int i : inside) {
                bool nested = false;
                for (int j : inside) {
                    if (i != j && areas[static_cast<std::size_t>(j)] > areas[static_cast<std::size_t>(i)] &&
                        contourContains(contours[static_cast<std::size_t>(j)], contours[static_cast<std::size_t>(i)].points[0])) {
                        nested = true;
                    }
                }
                if (!nested) result.holes.push_back(i);
            }
            result.ok = true;
            result.contour = chosen;
            result.area = chosenArea;
            result.perimeter = contourPerimeter(contours[static_cast<std::size_t>(chosen)]);
            for (int h : result.holes) {
                result.area -= areas[static_cast<std::size_t>(h)];
                result.perimeter += contourPerimeter(contours[static_cast<std::size_t>(h)]);
            }
            return result;
        }
    }

    if (triangle >= 0 && static_cast<std::size_t>(triangle) < mesh.triangleCount()) {
        const int face = faceOfTriangle(mesh, triangle);
        if (face >= 0) {
            const FaceFeature& f = mesh.features.faces[static_cast<std::size_t>(face)];
            for (std::uint32_t t = f.firstTriangle; t < f.firstTriangle + f.triangleCount; ++t) {
                result.triangles.push_back(t);
            }
        } else {
            // Sin caras (STL, OBJ, PLY): triangulos contiguos con la misma normal.
            const double quantum = std::max(1e-12, mesh.bounds.diagonal() * 1e-9);
            auto key = [&](std::uint32_t v) {
                const Vec3& p = mesh.positions[v];
                return VertexKey{std::llround(p.x / quantum), std::llround(p.y / quantum), std::llround(p.z / quantum)};
            };
            auto normalOf = [&](std::size_t t) {
                const Vec3& a = mesh.positions[mesh.indices[3 * t]];
                return normalize(cross(mesh.positions[mesh.indices[3 * t + 1]] - a, mesh.positions[mesh.indices[3 * t + 2]] - a));
            };
            std::map<std::pair<VertexKey, VertexKey>, std::vector<std::uint32_t>> edges;
            for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
                for (int k = 0; k < 3; ++k) {
                    VertexKey a = key(mesh.indices[3 * t + k]), b = key(mesh.indices[3 * t + (k + 1) % 3]);
                    if (b < a) std::swap(a, b);
                    edges[{a, b}].push_back(static_cast<std::uint32_t>(t));
                }
            }
            const Vec3 seedNormal = normalOf(static_cast<std::size_t>(triangle));
            std::vector<char> taken(mesh.triangleCount(), 0);
            std::vector<std::uint32_t> stack = {static_cast<std::uint32_t>(triangle)};
            taken[static_cast<std::size_t>(triangle)] = 1;
            while (!stack.empty()) {
                const std::uint32_t t = stack.back();
                stack.pop_back();
                result.triangles.push_back(t);
                for (int k = 0; k < 3; ++k) {
                    VertexKey a = key(mesh.indices[3 * t + k]), b = key(mesh.indices[3 * t + (k + 1) % 3]);
                    if (b < a) std::swap(a, b);
                    for (const std::uint32_t other : edges[{a, b}]) {
                        if (taken[other] || std::fabs(dot(normalOf(other), seedNormal)) < 0.99985) continue;  // 1 grado
                        taken[other] = 1;
                        stack.push_back(other);
                    }
                }
            }
        }

        // Area: suma de triangulos. Perimetro: aristas que usa un solo triangulo de la cara.
        const double quantum = std::max(1e-12, mesh.bounds.diagonal() * 1e-9);
        std::map<std::pair<VertexKey, VertexKey>, std::pair<int, double>> border;
        for (const std::uint32_t t : result.triangles) {
            const Vec3& a = mesh.positions[mesh.indices[3 * t]];
            const Vec3& b = mesh.positions[mesh.indices[3 * t + 1]];
            const Vec3& c = mesh.positions[mesh.indices[3 * t + 2]];
            result.area += length(cross(b - a, c - a)) * 0.5;
            for (int k = 0; k < 3; ++k) {
                const Vec3& p = mesh.positions[mesh.indices[3 * t + k]];
                const Vec3& q = mesh.positions[mesh.indices[3 * t + (k + 1) % 3]];
                VertexKey kp{std::llround(p.x / quantum), std::llround(p.y / quantum), std::llround(p.z / quantum)};
                VertexKey kq{std::llround(q.x / quantum), std::llround(q.y / quantum), std::llround(q.z / quantum)};
                if (kq < kp) std::swap(kp, kq);
                if (kp.x == kq.x && kp.y == kq.y && kp.z == kq.z) continue;  // arista degenerada
                auto& entry = border[{kp, kq}];
                ++entry.first;
                entry.second = distance(p, q);
            }
        }
        for (const auto& e : border) {
            if (e.second.first == 1) result.perimeter += e.second.second;
        }
        result.ok = !result.triangles.empty();
        return result;
    }

    result.error = "No hay un contorno cerrado aqui";
    return result;
}

int PickIndex::triangleAt(const Mesh& mesh, const Vec3& point, double tolerance) const {
    int found = -1;
    const Vec3 pad(tolerance, tolerance, tolerance);
    m_triangles.query(
        [&](const Vec3& lo, const Vec3& hi) {
            const Vec3 l = lo - pad, h = hi + pad;
            return point.x >= l.x && point.y >= l.y && point.z >= l.z && point.x <= h.x &&
                   point.y <= h.y && point.z <= h.z;
        },
        [&](std::uint32_t i) {
            if (found >= 0) return;
            const Vec3& a = mesh.positions[mesh.indices[3 * i]];
            const Vec3& b = mesh.positions[mesh.indices[3 * i + 1]];
            const Vec3& c = mesh.positions[mesh.indices[3 * i + 2]];
            const Vec3 n = cross(b - a, c - a);
            const double area2 = length(n);
            if (area2 < 1e-300) return;
            const Vec3 unit = n * (1.0 / area2);
            if (std::fabs(dot(point - a, unit)) > tolerance) return;
            // Coordenadas baricentricas en el plano del triangulo.
            const double wa = dot(cross(c - b, point - b), unit) / area2;
            const double wb = dot(cross(a - c, point - c), unit) / area2;
            const double wc = 1.0 - wa - wb;
            const double slack = -1e-9;
            if (wa >= slack && wb >= slack && wc >= slack) found = static_cast<int>(i);
        });
    return found;
}

std::string formatNumber(double value, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    std::string text = buffer;
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
    }
    if (text == "-0") text = "0";
    return text;
}

std::string formatLength(double value, LengthUnit unit) {
    const std::string suffix = unitSuffix(unit);
    return formatNumber(value, 3) + (suffix.empty() ? "" : " " + suffix);
}

std::string formatArea(double value, LengthUnit unit) {
    const std::string suffix = unitSuffix(unit);
    return formatNumber(value, 2) + (suffix.empty() ? "" : " " + suffix + "\xC2\xB2");
}

std::string formatAngle(double degrees) { return formatNumber(degrees, 2) + "\xC2\xB0"; }
```

Nota sobre el perímetro: los vértices se sueldan por posición (`VertexKey`), así que si una cara cilíndrica completa repite vértices en su costura, esas dos aristas cuentan como compartidas y no suman al perímetro. En la muestra el agujero son dos medias caras (`#210` y `#217` sobre la misma `CYLINDRICAL_SURFACE #203`), por eso la prueba espera πrh de área y 2πr + 2h de perímetro por cara.

- [ ] **Step 5: Correr las pruebas**

Run: `./build.sh test`
Expected: `72 pruebas, 0 fallidas`.

Si `measure_area_of_solid_face` falla, imprimir `r.area`, `r.perimeter` y `height` antes de tocar el código: los valores esperados salen de la geometría de la muestra, no se ajustan a lo que dé. No relajar la tolerancia del 1 %.

- [ ] **Step 6: Commit**

```bash
git add src/engine/measure.h src/engine/measure.cpp tests/unit_tests.cpp
git commit -m "Add distance, angle, radius, area and perimeter calculations

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_015NM5546gRGTfskkpAXPqTF"
```

---

### Task 6: Modelo de marcas y archivo `.marcas`

**Files:**
- Create: `src/engine/markup.h`, `src/engine/markup.cpp`
- Modify: `build.sh:14`, `build.bat:9` (agregar `markup.cpp`)
- Test: `tests/unit_tests.cpp`

**Interfaces:**
- Consumes: `Camera` (`renderer.h`), `Vec3`.
- Produces (en `markup.h`):
  - `enum class MarkKind { Distance, Radius, Angle, Area, Note, Highlight, Underline, Pen, Rectangle, Ellipse, Cloud };`
  - `struct Mark { int id; MarkKind kind; std::vector<Vec3> points; std::uint32_t color; int view; std::string text; };` — `view == 0`: anclada al modelo o al plano, siempre visible.
  - `struct MarkupView { int id; std::string name; Camera camera; };`
  - `struct MarkupDocument { std::string modelName; std::uint64_t modelSize; std::string modelDate; std::vector<MarkupView> views; std::vector<Mark> marks; int nextId; int newId(); const MarkupView* findView(int id) const; };`
  - `struct MarkupParseReport { bool recognized; int badLines; bool newerVersion; };`
  - `std::string serializeMarkup(const MarkupDocument& doc);`
  - `MarkupParseReport parseMarkup(const std::string& text, MarkupDocument* doc);`
  - `bool sameView(const Camera& a, const Camera& b);`
  - `bool markVisible(const Mark& mark, const MarkupDocument& doc, const Camera& camera);`
  - `bool modelChanged(const MarkupDocument& doc, std::uint64_t size, const std::string& date);`
  - `bool isMeasurement(MarkKind kind);`
  - Colores de fábrica: `constexpr std::uint32_t kMarkRed = 0xFFE03C31, kMarkYellow = 0xFFF2C12E, kMarkGreen = 0xFF2E9E4F, kMarkBlue = 0xFF2F6FDB, kMarkBlack = 0xFF1A1F24, kHighlightYellow = 0xFFFFE14D, kHighlightGreen = 0xFF7CE08A, kHighlightPink = 0xFFFF7EB6, kMeasureColor = 0xFFFF8C1A;`

- [ ] **Step 1: Escribir las pruebas que fallan**

Agregar al final de `tests/unit_tests.cpp`, antes de `int main`:

```cpp
// --- Marcas ------------------------------------------------------------------------------

namespace {

stp::MarkupDocument sampleMarkup() {
    stp::MarkupDocument doc;
    doc.modelName = "brida.dxf";
    doc.modelSize = 64385;
    doc.modelDate = "2026-09-23T12:00:00";
    stp::MarkupView view;
    view.id = doc.newId();
    view.name = "Vista 1";
    view.camera.target = stp::Vec3(1.5, -2.25, 3);
    view.camera.yaw = 0.3;
    view.camera.pitch = -0.2;
    view.camera.orthoHeight = 123.456;
    view.camera.planView = true;
    view.camera.planNormal = stp::Vec3(0, -1, 0);
    view.camera.planRight = stp::Vec3(1, 0, 0);
    view.camera.planUp = stp::Vec3(0, 0, 1);
    doc.views.push_back(view);
    const stp::MarkKind kinds[] = {stp::MarkKind::Distance, stp::MarkKind::Radius, stp::MarkKind::Angle,
                                   stp::MarkKind::Area, stp::MarkKind::Note, stp::MarkKind::Highlight,
                                   stp::MarkKind::Underline, stp::MarkKind::Pen, stp::MarkKind::Rectangle,
                                   stp::MarkKind::Ellipse, stp::MarkKind::Cloud};
    for (stp::MarkKind kind : kinds) {
        stp::Mark mark;
        mark.id = doc.newId();
        mark.kind = kind;
        mark.points = {stp::Vec3(0.1, 0.2, 0.3), stp::Vec3(1.0 / 3.0, 1e-9, -7)};
        mark.color = stp::kMarkBlue;
        mark.view = stp::isMeasurement(kind) || kind == stp::MarkKind::Note ? 0 : view.id;
        if (kind == stp::MarkKind::Note) mark.text = "Revisar este agujero";
        doc.marks.push_back(mark);
    }
    return doc;
}

}  // namespace

TEST(markup_roundtrip_is_identical) {
    const stp::MarkupDocument doc = sampleMarkup();
    const std::string text = stp::serializeMarkup(doc);
    CHECK(text.compare(0, 20, "stp-viewer-marcas 1\n") == 0);
    stp::MarkupDocument back;
    const stp::MarkupParseReport report = stp::parseMarkup(text, &back);
    CHECK(report.recognized);
    CHECK(report.badLines == 0);
    CHECK(!report.newerVersion);
    CHECK(stp::serializeMarkup(back) == text);
    CHECK(back.marks.size() == doc.marks.size());
    CHECK(back.views.size() == 1);
    CHECK(back.nextId == doc.nextId);
    if (back.views.size() == 1) {
        CHECK(back.views[0].camera.planView);
        CHECK_NEAR(back.views[0].camera.orthoHeight, 123.456, 0);
    }
    if (!back.marks.empty()) CHECK(back.marks[0].points[1].x == 1.0 / 3.0);
}

TEST(markup_roundtrip_hostile_text) {
    stp::MarkupDocument doc;
    stp::Mark note;
    note.id = doc.newId();
    note.kind = stp::MarkKind::Note;
    note.points = {stp::Vec3(), stp::Vec3(1, 1, 0)};
    note.text = "Ca\xC3\xB1" "o \"interior\" \\ ruta C:\\piezas\nsegunda linea\ttab = x;y";
    note.text += std::string(10000, 'x');
    doc.marks.push_back(note);
    stp::MarkupDocument back;
    stp::parseMarkup(stp::serializeMarkup(doc), &back);
    CHECK(back.marks.size() == 1);
    if (!back.marks.empty()) CHECK(back.marks[0].text == note.text);
    // Una linea por elemento: el salto de linea del texto va escapado.
    const std::string text = stp::serializeMarkup(doc);
    CHECK(std::count(text.begin(), text.end(), '\n') == 3);  // cabecera, modelo, nota
}

TEST(markup_tolerates_damaged_and_future_files) {
    std::string text = stp::serializeMarkup(sampleMarkup());
    text += "medida id=99 tipo=distancia puntos=1,2\n";      // punto incompleto
    text += "garabato id=100 color=#FF0000\n";                // tipo desconocido
    text += "nota id=101 puntos=0,0,0;1,1,1 texto=\"sin cerrar\n";
    stp::MarkupDocument back;
    const stp::MarkupParseReport damaged = stp::parseMarkup(text, &back);
    CHECK(damaged.recognized);
    CHECK(damaged.badLines == 3);
    CHECK(back.marks.size() == sampleMarkup().marks.size());

    std::string future = stp::serializeMarkup(sampleMarkup());
    future.replace(0, 19, "stp-viewer-marcas 7");
    future += "medida id=200 tipo=distancia vista=0 puntos=0,0,0;1,0,0 nueva_clave=1\n";
    stp::MarkupDocument later;
    const stp::MarkupParseReport newer = stp::parseMarkup(future, &later);
    CHECK(newer.newerVersion);
    CHECK(newer.badLines == 0);  // las claves nuevas se ignoran, la linea se entiende
    CHECK(later.marks.size() == sampleMarkup().marks.size() + 1);

    stp::MarkupDocument other;
    CHECK(!stp::parseMarkup("hola\nmundo\n", &other).recognized);
    CHECK(other.marks.empty());
}

TEST(markup_detects_changed_model) {
    const stp::MarkupDocument doc = sampleMarkup();
    CHECK(!stp::modelChanged(doc, 64385, "2026-09-23T12:00:00"));
    CHECK(stp::modelChanged(doc, 64386, "2026-09-23T12:00:00"));
    CHECK(stp::modelChanged(doc, 64385, "2026-09-24T08:00:00"));
    stp::MarkupDocument fresh;  // sin datos del modelo: nada que comparar
    CHECK(!stp::modelChanged(fresh, 1, "x"));
}

TEST(markup_views_match_within_half_percent) {
    stp::Camera a;
    a.orthoHeight = 100.0;
    stp::Camera b = a;
    CHECK(stp::sameView(a, b));
    b.orthoHeight = 100.4;
    CHECK(stp::sameView(a, b));
    b.orthoHeight = 101.0;
    CHECK(!stp::sameView(a, b));
    b = a;
    b.target = a.target + a.right() * 0.4;
    CHECK(stp::sameView(a, b));
    b.target = a.target + a.right() * 1.0;
    CHECK(!stp::sameView(a, b));
    b = a;
    b.yaw += 0.01;
    CHECK(!stp::sameView(a, b));

    const stp::MarkupDocument doc = sampleMarkup();
    const stp::Mark& stroke = doc.marks[5];  // resaltador, en la vista 1
    CHECK(stp::markVisible(stroke, doc, doc.views[0].camera));
    CHECK(!stp::markVisible(stroke, doc, a));
    CHECK(stp::markVisible(doc.marks[0], doc, a));  // las medidas se ven siempre
}
```

- [ ] **Step 2: Correr y ver que falla**

Run: `./build.sh test`
Expected: error de compilación: falta `markup.h` (agregar `#include "../src/engine/markup.h"` arriba del archivo de pruebas junto con los demás).

- [ ] **Step 3: Crear `src/engine/markup.h`**

```cpp
// Marcas de revision: medidas, notas, trazos y formas, con las vistas en que
// se dibujaron, y su archivo de texto <modelo>.marcas.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../render/renderer.h"
#include "geom.h"

namespace stp {

enum class MarkKind { Distance, Radius, Angle, Area, Note, Highlight, Underline, Pen, Rectangle, Ellipse, Cloud };

constexpr std::uint32_t kMarkRed = 0xFFE03C31;
constexpr std::uint32_t kMarkYellow = 0xFFF2C12E;
constexpr std::uint32_t kMarkGreen = 0xFF2E9E4F;
constexpr std::uint32_t kMarkBlue = 0xFF2F6FDB;
constexpr std::uint32_t kMarkBlack = 0xFF1A1F24;
constexpr std::uint32_t kHighlightYellow = 0xFFFFE14D;
constexpr std::uint32_t kHighlightGreen = 0xFF7CE08A;
constexpr std::uint32_t kHighlightPink = 0xFFFF7EB6;
constexpr std::uint32_t kMeasureColor = 0xFFFF8C1A;

struct Mark {
    int id = 0;
    MarkKind kind = MarkKind::Distance;
    // Distancia: a, b. Radio: punto clicado. Angulo: a, vertice, b (o 4 puntos
    // de dos rectas + 2 de clic, ver markup_tools). Area: punto clicado.
    // Nota: ancla, etiqueta. Trazos: la polilinea. Formas: dos esquinas.
    std::vector<Vec3> points;
    std::uint32_t color = kMarkRed;  // ARGB
    int view = 0;                    // 0 = anclada al modelo; si no, MarkupView::id
    std::string text;                // notas (UTF-8)
};

struct MarkupView {
    int id = 0;
    std::string name;
    Camera camera;
};

struct MarkupDocument {
    std::string modelName;
    std::uint64_t modelSize = 0;
    std::string modelDate;  // "AAAA-MM-DDTHH:MM:SS"
    std::vector<MarkupView> views;
    std::vector<Mark> marks;
    int nextId = 1;

    int newId() { return nextId++; }
    const MarkupView* findView(int id) const;
};

struct MarkupParseReport {
    bool recognized = false;  // tenia la cabecera stp-viewer-marcas
    int badLines = 0;         // lineas que no se entendieron y se ignoraron
    bool newerVersion = false;
};

bool isMeasurement(MarkKind kind);
std::string serializeMarkup(const MarkupDocument& doc);
MarkupParseReport parseMarkup(const std::string& text, MarkupDocument* doc);

// Misma orientacion y mismo encuadre, con 0,5 % de tolerancia.
bool sameView(const Camera& a, const Camera& b);
bool markVisible(const Mark& mark, const MarkupDocument& doc, const Camera& camera);
bool modelChanged(const MarkupDocument& doc, std::uint64_t size, const std::string& date);

}  // namespace stp
```

- [ ] **Step 4: Crear `src/engine/markup.cpp`**

```cpp
#include "markup.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace stp {
namespace {

const char* kHeader = "stp-viewer-marcas";
constexpr int kVersion = 1;

struct KindName {
    MarkKind kind;
    const char* line;  // medida, nota, trazo, forma
    const char* type;  // valor de tipo=
};

const KindName kKinds[] = {
    {MarkKind::Distance, "medida", "distancia"},  {MarkKind::Radius, "medida", "radio"},
    {MarkKind::Angle, "medida", "angulo"},        {MarkKind::Area, "medida", "area"},
    {MarkKind::Note, "nota", ""},                 {MarkKind::Highlight, "trazo", "resaltador"},
    {MarkKind::Underline, "trazo", "subrayado"},  {MarkKind::Pen, "trazo", "lapiz"},
    {MarkKind::Rectangle, "forma", "rectangulo"}, {MarkKind::Ellipse, "forma", "elipse"},
    {MarkKind::Cloud, "forma", "nube"},
};

std::string number(double v) {
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.17g", v);
    return buffer;
}

std::string vec(const Vec3& v) { return number(v.x) + "," + number(v.y) + "," + number(v.z); }

std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out.push_back(c); break;
        }
    }
    return out + "\"";
}

std::string colorText(std::uint32_t argb) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%06X", static_cast<unsigned>(argb & 0xFFFFFF));
    return buffer;
}

// Divide "tipo clave=valor clave="texto con espacios"" en clave -> valor.
// Devuelve false si una comilla queda sin cerrar.
bool splitLine(const std::string& line, std::string* kind, std::map<std::string, std::string>* fields) {
    std::size_t i = line.find(' ');
    *kind = line.substr(0, i);
    while (i != std::string::npos && i < line.size()) {
        while (i < line.size() && line[i] == ' ') ++i;
        if (i >= line.size()) break;
        const std::size_t eq = line.find('=', i);
        if (eq == std::string::npos) return false;
        const std::string key = line.substr(i, eq - i);
        std::string value;
        i = eq + 1;
        if (i < line.size() && line[i] == '"') {
            ++i;
            bool closed = false;
            while (i < line.size()) {
                const char c = line[i++];
                if (c == '"') {
                    closed = true;
                    break;
                }
                if (c == '\\' && i < line.size()) {
                    const char e = line[i++];
                    value.push_back(e == 'n' ? '\n' : (e == 'r' ? '\r' : e));
                } else {
                    value.push_back(c);
                }
            }
            if (!closed) return false;
        } else {
            const std::size_t end = line.find(' ', i);
            value = line.substr(i, end == std::string::npos ? std::string::npos : end - i);
            i = end;
        }
        (*fields)[key] = value;
    }
    return true;
}

bool parseNumber(const std::string& text, double* out) {
    if (text.empty()) return false;
    char* end = nullptr;
    *out = std::strtod(text.c_str(), &end);
    return end && *end == '\0' && std::isfinite(*out);
}

bool parseVec(const std::string& text, Vec3* out) {
    const std::size_t a = text.find(',');
    const std::size_t b = a == std::string::npos ? a : text.find(',', a + 1);
    if (b == std::string::npos) return false;
    return parseNumber(text.substr(0, a), &out->x) && parseNumber(text.substr(a + 1, b - a - 1), &out->y) &&
           parseNumber(text.substr(b + 1), &out->z);
}

bool parsePoints(const std::string& text, std::vector<Vec3>* out) {
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(';', start);
        Vec3 p;
        if (!parseVec(text.substr(start, end == std::string::npos ? std::string::npos : end - start), &p)) return false;
        out->push_back(p);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return !out->empty();
}

bool parseColor(const std::string& text, std::uint32_t* out) {
    if (text.size() != 7 || text[0] != '#') return false;
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str() + 1, &end, 16);
    if (!end || *end != '\0') return false;
    *out = 0xFF000000u | static_cast<std::uint32_t>(value);
    return true;
}

}  // namespace

const MarkupView* MarkupDocument::findView(int id) const {
    for (const MarkupView& view : views) {
        if (view.id == id) return &view;
    }
    return nullptr;
}

bool isMeasurement(MarkKind kind) {
    return kind == MarkKind::Distance || kind == MarkKind::Radius || kind == MarkKind::Angle ||
           kind == MarkKind::Area;
}

std::string serializeMarkup(const MarkupDocument& doc) {
    std::string out = std::string(kHeader) + " " + std::to_string(kVersion) + "\n";
    out += "modelo nombre=" + quoted(doc.modelName) + " tamano=" + std::to_string(doc.modelSize) +
           " fecha=" + quoted(doc.modelDate) + "\n";
    for (const MarkupView& v : doc.views) {
        const Camera& c = v.camera;
        out += "vista id=" + std::to_string(v.id) + " nombre=" + quoted(v.name) + " objetivo=" + vec(c.target) +
               " distancia=" + number(c.distance) + " yaw=" + number(c.yaw) + " pitch=" + number(c.pitch) +
               " fov=" + number(c.fov) + " alto_orto=" + number(c.orthoHeight) + " orto=" + (c.ortho ? "1" : "0") +
               " plano=" + (c.planView ? "1" : "0") + " normal=" + vec(c.planNormal) + " derecha=" +
               vec(c.planRight) + " arriba=" + vec(c.planUp) + "\n";
    }
    for (const Mark& m : doc.marks) {
        const KindName* name = nullptr;
        for (const KindName& k : kKinds) {
            if (k.kind == m.kind) name = &k;
        }
        if (!name) continue;
        out += std::string(name->line) + " id=" + std::to_string(m.id);
        if (name->type[0]) out += std::string(" tipo=") + name->type;
        out += " vista=" + std::to_string(m.view) + " color=" + colorText(m.color) + " puntos=";
        for (std::size_t i = 0; i < m.points.size(); ++i) out += (i ? ";" : "") + vec(m.points[i]);
        if (m.kind == MarkKind::Note) out += " texto=" + quoted(m.text);
        out += "\n";
    }
    return out;
}

MarkupParseReport parseMarkup(const std::string& text, MarkupDocument* doc) {
    MarkupParseReport report;
    *doc = MarkupDocument();
    std::size_t start = 0;
    bool first = true;
    int maxId = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (first) {
            first = false;
            const std::string prefix = std::string(kHeader) + " ";
            if (line.compare(0, prefix.size(), prefix) != 0) return report;
            report.recognized = true;
            report.newerVersion = std::atoi(line.c_str() + prefix.size()) > kVersion;
            continue;
        }
        if (line.empty()) continue;

        std::string kind;
        std::map<std::string, std::string> f;
        if (!splitLine(line, &kind, &f)) {
            ++report.badLines;
            continue;
        }
        if (kind == "modelo") {
            doc->modelName = f["nombre"];
            doc->modelSize = std::strtoull(f["tamano"].c_str(), nullptr, 10);
            doc->modelDate = f["fecha"];
            continue;
        }
        if (kind == "vista") {
            MarkupView v;
            v.id = std::atoi(f["id"].c_str());
            v.name = f["nombre"];
            Camera& c = v.camera;
            const bool ok = v.id > 0 && parseVec(f["objetivo"], &c.target) && parseNumber(f["distancia"], &c.distance) &&
                            parseNumber(f["yaw"], &c.yaw) && parseNumber(f["pitch"], &c.pitch) &&
                            parseNumber(f["fov"], &c.fov) && parseNumber(f["alto_orto"], &c.orthoHeight) &&
                            parseVec(f["normal"], &c.planNormal) && parseVec(f["derecha"], &c.planRight) &&
                            parseVec(f["arriba"], &c.planUp);
            if (!ok) {
                ++report.badLines;
                continue;
            }
            c.ortho = f["orto"] == "1";
            c.planView = f["plano"] == "1";
            maxId = std::max(maxId, v.id);
            doc->views.push_back(v);
            continue;
        }
        const KindName* name = nullptr;
        for (const KindName& k : kKinds) {
            if (kind == k.line && f["tipo"] == k.type) name = &k;
        }
        Mark m;
        if (!name || !parsePoints(f["puntos"], &m.points)) {
            ++report.badLines;
            continue;
        }
        m.kind = name->kind;
        m.id = std::atoi(f["id"].c_str());
        m.view = std::atoi(f["vista"].c_str());
        if (f.count("color") && !parseColor(f["color"], &m.color)) {
            ++report.badLines;
            continue;
        }
        m.text = f["texto"];
        maxId = std::max(maxId, m.id);
        doc->marks.push_back(m);
    }
    doc->nextId = maxId + 1;
    return report;
}

bool sameView(const Camera& a, const Camera& b) {
    if (a.planView != b.planView || a.ortho != b.ortho) return false;
    if (dot(a.forward(), b.forward()) < 1.0 - 1e-6 || dot(a.up(), b.up()) < 1.0 - 1e-6) return false;
    const double height = std::max(1e-12, a.orthoHeight);
    if (std::fabs(b.orthoHeight / height - 1.0) > 0.005) return false;
    const Vec3 shift = b.target - a.target;
    const double sideways = std::hypot(dot(shift, a.right()), dot(shift, a.up()));
    return sideways <= 0.005 * height;
}

bool markVisible(const Mark& mark, const MarkupDocument& doc, const Camera& camera) {
    if (mark.view == 0) return true;
    const MarkupView* view = doc.findView(mark.view);
    return view && sameView(view->camera, camera);
}

bool modelChanged(const MarkupDocument& doc, std::uint64_t size, const std::string& date) {
    if (doc.modelSize == 0 && doc.modelDate.empty()) return false;
    return doc.modelSize != size || doc.modelDate != date;
}

}  // namespace stp
```

- [ ] **Step 5: Agregar a la compilación**

`build.sh:14`: `src/engine/markup.cpp ` dentro de `ENGINE`. `build.bat:9`: `src\engine\markup.cpp `.

- [ ] **Step 6: Correr las pruebas**

Run: `./build.sh test`
Expected: `77 pruebas, 0 fallidas`.

- [ ] **Step 7: Commit**

```bash
git add src/engine/markup.h src/engine/markup.cpp build.sh build.bat tests/unit_tests.cpp
git commit -m "Add markup document model and .marcas text format

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_015NM5546gRGTfskkpAXPqTF"
```

---

### Task 7: Medir en el visor y en el panel

**Files:**
- Create: `src/viewer/markup_tools.h`, `src/viewer/markup_tools.cpp`
- Modify: `src/viewer/image_view.h`, `src/viewer/image_view.cpp`, `src/viewer/scene_view.h`, `src/viewer/scene_view.cpp`, `src/viewer/main.cpp`, `src/shellext/preview_handler.cpp`, `build.sh`, `build.bat`
- Test: verificación bajo wine (Step 9); `./build.sh test` sigue en verde.

**Interfaces:**
- Consumes: `PickIndex`, `SnapResult`, `pixelRay`, `worldPerPixel`, `measureDistance`, `measureRadius`, `angleAt`, `angleBetweenLines`, `measureArea`, `contourOutline`, `format*` (Tasks 4–5); `MarkupDocument`, `Mark`, `MarkKind`, `markVisible`, `kMeasureColor` (Task 6).
- Produces:
  - `bool ensureGdiplus();` en `image_view.h`.
  - `enum class Tool { Navigate, Distance, Radius, Angle, Area, Highlight, Underline, Note, Rectangle, Ellipse, Cloud, Pen };`
  - `class MarkupHost` (interfaz que implementa `SceneView`): `markupWindow()`, `markupMesh()`, `markupPick()`, `markupCamera()`, `markupSetCamera(const Camera&)`, `markupPlane()`, `markupWidth()`, `markupHeight()`, `markupDpi()`, `markupRedraw()`, `markupChanged()`.
  - `class MarkupTools` con: `MarkupTools(MarkupHost*, bool editing)`, `Tool tool() const`, `void setTool(Tool)`, `bool active() const`, `bool handle(UINT, WPARAM, LPARAM)`, `void draw(HDC, const Camera&, int width, int height, double scale, bool interactive) const`, `std::wstring hint() const`, `const MarkupDocument& document() const`, `void setDocument(const MarkupDocument&)`, `void clear()`, `bool dirty() const`, `void markSaved()`, `void showMessage(const std::wstring&)`, `std::wstring describe(const Mark&) const`.
  - En `SceneView`: `void enableTools(bool editing)`, `MarkupTools* tools()`, `bool toolActive() const`, `void setMarkupListener(std::function<void()>)`.

- [ ] **Step 1: Exponer GDI+ en `image_view`**

En `src/viewer/image_view.h`, agregar antes del cierre del espacio de nombres:

```cpp
// Arranca GDI+ una vez por proceso. Devuelve false si no esta disponible.
bool ensureGdiplus();
```

En `src/viewer/image_view.cpp`, después del cierre del espacio de nombres anónimo:

```cpp
bool ensureGdiplus() { return GdiPlusSession::ensure(); }
```

- [ ] **Step 2: Crear `src/viewer/markup_tools.h`**

```cpp
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
```

- [ ] **Step 3: Crear `src/viewer/markup_tools.cpp`**

```cpp
#include "markup_tools.h"

#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>

#include "image_view.h"

namespace stp {
namespace {

constexpr UINT_PTR kMessageTimer = 7;
constexpr ULONGLONG kMessageMs = 4000;

std::wstring widen(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(0, size)), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], size);
    return out;
}

Gdiplus::Color gdiColor(std::uint32_t argb, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, static_cast<BYTE>((argb >> 16) & 0xFF), static_cast<BYTE>((argb >> 8) & 0xFF),
                          static_cast<BYTE>(argb & 0xFF));
}

bool toScreen(const Camera& camera, int width, int height, const Vec3& p, Gdiplus::PointF* out) {
    double x, y;
    if (!projectPoint(camera, width, height, p, &x, &y)) return false;
    *out = Gdiplus::PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y));
    return true;
}

}  // namespace

MarkupTools::MarkupTools(MarkupHost* host, bool editing) : m_host(host), m_editing(editing) {
    ensureGdiplus();
}

bool MarkupTools::isMeasureTool() const {
    return m_tool == Tool::Distance || m_tool == Tool::Radius || m_tool == Tool::Angle || m_tool == Tool::Area;
}

void MarkupTools::setTool(Tool tool) {
    // En el panel solo se mide.
    if (!m_editing && tool != Tool::Navigate && tool != Tool::Distance && tool != Tool::Radius &&
        tool != Tool::Angle && tool != Tool::Area) {
        return;
    }
    m_tool = tool;
    m_pending.clear();
    m_hover = SnapResult();
    m_host->markupRedraw();
    m_host->markupChanged();
}

void MarkupTools::setDocument(const MarkupDocument& doc) {
    m_doc = doc;
    m_values.clear();
    m_pending.clear();
    m_dirty = false;
    m_host->markupRedraw();
    m_host->markupChanged();
}

void MarkupTools::clear() { setDocument(MarkupDocument()); }

void MarkupTools::showMessage(const std::wstring& text) {
    m_message = text;
    m_messageUntil = GetTickCount64() + kMessageMs;
    SetTimer(m_host->markupWindow(), kMessageTimer, static_cast<UINT>(kMessageMs + 50), nullptr);
    m_host->markupRedraw();
}

std::wstring MarkupTools::hint() const {
    if (!m_message.empty() && GetTickCount64() < m_messageUntil) return m_message;
    const std::wstring tail = L"   (Shift: sin enganche, Esc: terminar)";
    switch (m_tool) {
        case Tool::Distance:
            return (m_pending.empty() ? L"Distancia: clic en el primer punto" : L"Distancia: clic en el segundo punto") + tail;
        case Tool::Radius: return L"Radio: clic sobre un circulo, un arco o un agujero" + tail;
        case Tool::Angle:
            return (m_pending.empty() ? L"Angulo: clic en tres puntos (vertice al medio) o en dos lineas"
                                      : L"Angulo: siguiente punto o segunda linea") + tail;
        case Tool::Area: return L"Area: clic dentro de un contorno cerrado o sobre una cara" + tail;
        default: return std::wstring();
    }
}

SnapResult MarkupTools::snapAt(int x, int y) const {
    SnapOptions options;
    options.snapping = GetKeyState(VK_SHIFT) >= 0;
    options.plane = m_host->markupPlane();
    options.radiusPixels = 8.0 * m_host->markupDpi() / 96.0;
    return m_host->markupPick().snap(m_host->markupMesh(), m_host->markupCamera(), m_host->markupWidth(),
                                     m_host->markupHeight(), x, y, options);
}

void MarkupTools::addMark(Mark mark) {
    mark.id = m_doc.newId();
    m_doc.marks.push_back(mark);
    m_dirty = true;
    m_host->markupRedraw();
    m_host->markupChanged();
}

void MarkupTools::clickMeasure(const SnapResult& snap) {
    if (snap.kind == SnapKind::None) {
        showMessage(L"Aqui no hay nada que medir");
        return;
    }
    Mark mark;
    mark.color = kMeasureColor;
    switch (m_tool) {
        case Tool::Distance:
            m_pending.push_back(snap);
            if (m_pending.size() < 2) break;
            mark.kind = MarkKind::Distance;
            mark.points = {m_pending[0].point, m_pending[1].point};
            m_pending.clear();
            addMark(mark);
            break;
        case Tool::Radius:
        case Tool::Area: {
            mark.kind = m_tool == Tool::Radius ? MarkKind::Radius : MarkKind::Area;
            mark.points = {snap.point};
            const Value value = evaluate(mark);
            if (!value.ok) {
                showMessage(value.error);
                break;
            }
            addMark(mark);
            break;
        }
        case Tool::Angle: {
            const Mesh& mesh = m_host->markupMesh();
            auto straight = [&](const SnapResult& s) {
                return s.kind == SnapKind::OnEdge && s.segment >= 0 &&
                       (static_cast<std::size_t>(s.segment) >= mesh.edgeCurve.size() || !mesh.edgeCurve[s.segment]);
            };
            if (m_pending.size() == 1 && straight(m_pending[0]) && straight(snap) &&
                snap.segment != m_pending[0].segment) {
                // Dos lineas: A (a0, a1, clic) y B (b0, b1, clic).
                const std::size_t a = static_cast<std::size_t>(m_pending[0].segment) * 2;
                const std::size_t b = static_cast<std::size_t>(snap.segment) * 2;
                mark.kind = MarkKind::Angle;
                mark.points = {mesh.edgeLines[a], mesh.edgeLines[a + 1], m_pending[0].point,
                               mesh.edgeLines[b], mesh.edgeLines[b + 1], snap.point};
                m_pending.clear();
                addMark(mark);
                break;
            }
            m_pending.push_back(snap);
            if (m_pending.size() < 3) break;
            mark.kind = MarkKind::Angle;
            mark.points = {m_pending[0].point, m_pending[1].point, m_pending[2].point};
            m_pending.clear();
            addMark(mark);
            break;
        }
        default:
            break;
    }
    m_host->markupRedraw();
}

bool MarkupTools::handle(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_TIMER:
            if (wparam != kMessageTimer) return false;
            KillTimer(m_host->markupWindow(), kMessageTimer);
            m_message.clear();
            m_host->markupRedraw();
            return true;

        case WM_SETCURSOR:
            if (m_tool == Tool::Navigate || LOWORD(lparam) != HTCLIENT) return false;
            // Sin nada que medir bajo el cursor: "no disponible".
            SetCursor(LoadCursor(nullptr, isMeasureTool() && m_mouse.x >= 0 && m_hover.kind == SnapKind::None
                                              ? IDC_NO : IDC_CROSS));
            return true;

        case WM_MOUSEMOVE:
            m_mouse = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (isMeasureTool()) {
                m_hover = snapAt(m_mouse.x, m_mouse.y);
                m_host->markupRedraw();
            }
            return false;  // la vista sigue moviendo con el boton derecho

        case WM_LBUTTONDOWN:
            if (!isMeasureTool()) return false;
            SetFocus(m_host->markupWindow());
            clickMeasure(snapAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)));
            return true;

        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            return m_tool != Tool::Navigate;

        case WM_KEYDOWN: {
            if (wparam == 'M') {
                setTool(Tool::Distance);
                return true;
            }
            if (isMeasureTool() && wparam >= '1' && wparam <= '4') {
                const Tool tools[] = {Tool::Distance, Tool::Radius, Tool::Angle, Tool::Area};
                setTool(tools[wparam - '1']);
                return true;
            }
            if (wparam == VK_ESCAPE) {
                if (!m_pending.empty()) {
                    m_pending.clear();
                    m_host->markupRedraw();
                    return true;
                }
                if (m_tool != Tool::Navigate) {
                    setTool(Tool::Navigate);
                    return true;
                }
                return false;
            }
            if (wparam == VK_DELETE && !m_doc.marks.empty()) {
                // Hasta que exista la seleccion (Task 8): borra la ultima marca.
                m_values.erase(m_doc.marks.back().id);
                m_doc.marks.pop_back();
                m_dirty = true;
                m_host->markupRedraw();
                m_host->markupChanged();
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

const MarkupTools::Value& MarkupTools::valueOf(const Mark& mark) const {
    auto it = m_values.find(mark.id);
    if (it == m_values.end()) it = m_values.emplace(mark.id, evaluate(mark)).first;
    return it->second;
}

MarkupTools::Value MarkupTools::evaluate(const Mark& mark) const {
    Value v;
    const Mesh& mesh = m_host->markupMesh();
    const PlanarInfo* plane = m_host->markupPlane();
    const LengthUnit unit = mesh.units;
    const double tolerance = std::max(1e-9, mesh.bounds.diagonal() * 1e-6);
    switch (mark.kind) {
        case MarkKind::Distance: {
            if (mark.points.size() < 2) break;
            const DistanceResult d = measureDistance(mark.points[0], mark.points[1], plane);
            v.ok = true;
            v.label = widen(formatLength(d.total, unit));
            v.detail = L"ΔX " + widen(formatNumber(d.delta.x, 3)) + L"   ΔY " + widen(formatNumber(d.delta.y, 3));
            if (!plane) v.detail += L"   ΔZ " + widen(formatNumber(d.delta.z, 3));
            break;
        }
        case MarkKind::Radius: {
            if (mark.points.empty()) break;
            const int triangle = m_host->markupPick().triangleAt(mesh, mark.points[0], tolerance);
            // Tolerancia de 8 px a la escala actual: lo que el usuario considero "sobre el circulo".
            const double reach = 8.0 * worldPerPixel(m_host->markupCamera(), m_host->markupHeight(), 1.0);
            const RadiusResult r = measureRadius(mesh, mark.points[0], triangle, std::max(reach, tolerance));
            if (!r.ok) {
                v.error = widen(r.error);
                break;
            }
            v.ok = true;
            v.center = r.center;
            v.label = L"R " + widen(formatLength(r.radius, unit));
            v.detail = L"Ø " + widen(formatLength(2 * r.radius, unit));
            break;
        }
        case MarkKind::Angle: {
            double degrees = 0.0;
            if (mark.points.size() == 3) {
                degrees = angleAt(mark.points[0], mark.points[1], mark.points[2]);
            } else if (mark.points.size() == 6) {
                degrees = angleBetweenLines(mark.points[0], mark.points[1], mark.points[2], mark.points[3],
                                            mark.points[4], mark.points[5]);
            } else {
                break;
            }
            v.ok = true;
            v.label = widen(formatAngle(degrees));
            break;
        }
        case MarkKind::Area: {
            if (mark.points.empty()) break;
            const int triangle = m_host->markupPick().triangleAt(mesh, mark.points[0], tolerance);
            v.area = measureArea(mesh, mark.points[0], triangle, plane);
            if (!v.area.ok) {
                v.error = widen(v.area.error);
                break;
            }
            v.ok = true;
            v.label = L"A " + widen(formatArea(v.area.area, unit));
            v.detail = L"P " + widen(formatLength(v.area.perimeter, unit));
            break;
        }
        default:
            break;
    }
    return v;
}

std::wstring MarkupTools::describe(const Mark& mark) const {
    const Value& v = isMeasurement(mark.kind) ? valueOf(mark) : Value();
    switch (mark.kind) {
        case MarkKind::Distance: return L"Distancia  " + v.label;
        case MarkKind::Radius: return L"Radio  " + v.label + L"  " + v.detail;
        case MarkKind::Angle: return L"Angulo  " + v.label;
        case MarkKind::Area: return L"Area  " + v.label + L"  " + v.detail;
        case MarkKind::Note: return L"Nota: " + widen(mark.text);
        case MarkKind::Highlight: return L"Resaltado";
        case MarkKind::Underline: return L"Subrayado";
        case MarkKind::Pen: return L"Trazo";
        case MarkKind::Rectangle: return L"Rectangulo";
        case MarkKind::Ellipse: return L"Elipse";
        case MarkKind::Cloud: return L"Nube de revision";
    }
    return std::wstring();
}

void MarkupTools::drawLabel(void* graphics, double x, double y, const std::wstring& first,
                            const std::wstring& second, std::uint32_t color, double scale) const {
    Gdiplus::Graphics& g = *static_cast<Gdiplus::Graphics*>(graphics);
    const double dpi = m_host->markupDpi() / 96.0;
    Gdiplus::Font font(L"Segoe UI", static_cast<Gdiplus::REAL>(12.0 * dpi * scale), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font small(L"Segoe UI", static_cast<Gdiplus::REAL>(10.5 * dpi * scale), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::RectF a, b;
    g.MeasureString(first.c_str(), -1, &font, Gdiplus::PointF(0, 0), &a);
    if (!second.empty()) g.MeasureString(second.c_str(), -1, &small, Gdiplus::PointF(0, 0), &b);
    const Gdiplus::REAL pad = static_cast<Gdiplus::REAL>(4.0 * dpi * scale);
    const Gdiplus::REAL w = std::max(a.Width, b.Width) + 2 * pad;
    const Gdiplus::REAL h = a.Height + b.Height + 2 * pad;
    const Gdiplus::REAL left = static_cast<Gdiplus::REAL>(x) - w / 2;
    const Gdiplus::REAL top = static_cast<Gdiplus::REAL>(y) - h - pad;
    Gdiplus::SolidBrush fill(Gdiplus::Color(235, 255, 255, 255));
    Gdiplus::Pen border(gdiColor(color), static_cast<Gdiplus::REAL>(1.5 * scale));
    g.FillRectangle(&fill, left, top, w, h);
    g.DrawRectangle(&border, left, top, w, h);
    Gdiplus::SolidBrush ink(gdiColor(kMarkBlack));
    g.DrawString(first.c_str(), -1, &font, Gdiplus::PointF(left + pad, top + pad), &ink);
    if (!second.empty()) g.DrawString(second.c_str(), -1, &small, Gdiplus::PointF(left + pad, top + pad + a.Height), &ink);
}

void MarkupTools::drawMeasure(void* graphics, const Mark& mark, const Camera& camera, int width, int height,
                              double scale) const {
    Gdiplus::Graphics& g = *static_cast<Gdiplus::Graphics*>(graphics);
    const Value& v = valueOf(mark);
    const double dpi = m_host->markupDpi() / 96.0;
    Gdiplus::Pen pen(gdiColor(mark.color), static_cast<Gdiplus::REAL>(2.0 * dpi * scale));
    Gdiplus::PointF p[6];
    for (std::size_t i = 0; i < mark.points.size() && i < 6; ++i) {
        if (!toScreen(camera, width, height, mark.points[i], &p[i])) return;
    }
    switch (mark.kind) {
        case MarkKind::Distance: {
            g.DrawLine(&pen, p[0], p[1]);
            // Marcas de extremo perpendiculares, como en una cota.
            const double dx = p[1].X - p[0].X, dy = p[1].Y - p[0].Y;
            const double len = std::max(1e-6, std::hypot(dx, dy));
            const Gdiplus::REAL nx = static_cast<Gdiplus::REAL>(-dy / len * 6 * dpi * scale);
            const Gdiplus::REAL ny = static_cast<Gdiplus::REAL>(dx / len * 6 * dpi * scale);
            for (int i = 0; i < 2; ++i) g.DrawLine(&pen, p[i].X - nx, p[i].Y - ny, p[i].X + nx, p[i].Y + ny);
            drawLabel(graphics, (p[0].X + p[1].X) / 2, (p[0].Y + p[1].Y) / 2, v.label, v.detail, mark.color, scale);
            break;
        }
        case MarkKind::Radius: {
            if (!v.ok) break;
            Gdiplus::PointF c;
            if (!toScreen(camera, width, height, v.center, &c)) break;
            Gdiplus::AdjustableArrowCap arrow(4, 4, TRUE);
            pen.SetCustomEndCap(&arrow);
            g.DrawLine(&pen, c, p[0]);
            const Gdiplus::REAL s = static_cast<Gdiplus::REAL>(4 * dpi * scale);
            Gdiplus::Pen thin(gdiColor(mark.color), static_cast<Gdiplus::REAL>(1.5 * scale));
            g.DrawLine(&thin, c.X - s, c.Y, c.X + s, c.Y);
            g.DrawLine(&thin, c.X, c.Y - s, c.X, c.Y + s);
            drawLabel(graphics, p[0].X, p[0].Y, v.label, v.detail, mark.color, scale);
            break;
        }
        case MarkKind::Angle: {
            Gdiplus::PointF vertex, a, b;
            if (mark.points.size() == 3) {
                vertex = p[1];
                a = p[0];
                b = p[2];
            } else {
                // Dos rectas: del clic en cada una hasta el punto donde se cruzan.
                a = p[2];
                b = p[5];
                vertex = a;
                const Vec3 u = normalize(mark.points[1] - mark.points[0]);
                const Vec3 w = normalize(mark.points[4] - mark.points[3]);
                const Vec3 d = mark.points[0] - mark.points[3];
                const double B = dot(u, w), D = dot(u, d), E = dot(w, d), den = 1 - B * B;
                if (den > 1e-18) {
                    const double s = (B * E - D) / den;
                    toScreen(camera, width, height, mark.points[0] + u * s, &vertex);
                }
            }
            Gdiplus::Pen dashed(gdiColor(mark.color), static_cast<Gdiplus::REAL>(1.5 * dpi * scale));
            dashed.SetDashStyle(Gdiplus::DashStyleDash);
            g.DrawLine(&dashed, vertex, a);
            g.DrawLine(&dashed, vertex, b);
            const double a0 = std::atan2(a.Y - vertex.Y, a.X - vertex.X) * 180 / kPi;
            double sweep = std::atan2(b.Y - vertex.Y, b.X - vertex.X) * 180 / kPi - a0;
            while (sweep > 180) sweep -= 360;
            while (sweep < -180) sweep += 360;
            const Gdiplus::REAL r = static_cast<Gdiplus::REAL>(26 * dpi * scale);
            g.DrawArc(&pen, vertex.X - r, vertex.Y - r, 2 * r, 2 * r, static_cast<Gdiplus::REAL>(a0),
                      static_cast<Gdiplus::REAL>(sweep));
            const double mid = (a0 + sweep / 2) * kPi / 180;
            drawLabel(graphics, vertex.X + std::cos(mid) * r * 1.8, vertex.Y + std::sin(mid) * r * 1.8 + 12 * dpi * scale,
                      v.label, v.detail, mark.color, scale);
            break;
        }
        case MarkKind::Area: {
            if (!v.ok) break;
            Gdiplus::SolidBrush fill(gdiColor(mark.color, 70));
            const Mesh& mesh = m_host->markupMesh();
            if (v.area.contour >= 0) {
                Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);
                std::vector<int> rings = {v.area.contour};
                rings.insert(rings.end(), v.area.holes.begin(), v.area.holes.end());
                for (const int ring : rings) {
                    std::vector<Gdiplus::PointF> poly;
                    for (const Vec3& q : contourOutline(mesh.features.contours[static_cast<std::size_t>(ring)])) {
                        Gdiplus::PointF s;
                        if (toScreen(camera, width, height, q, &s)) poly.push_back(s);
                    }
                    if (poly.size() >= 3) path.AddPolygon(poly.data(), static_cast<INT>(poly.size()));
                }
                g.FillPath(&fill, &path);
            } else {
                for (const std::uint32_t t : v.area.triangles) {
                    Gdiplus::PointF tri[3];
                    bool ok = true;
                    for (int k = 0; k < 3; ++k) ok = ok && toScreen(camera, width, height, mesh.positions[mesh.indices[3 * t + k]], &tri[k]);
                    if (ok) g.FillPolygon(&fill, tri, 3);
                }
            }
            drawLabel(graphics, p[0].X, p[0].Y, v.label, v.detail, mark.color, scale);
            break;
        }
        default:
            break;
    }
}

void MarkupTools::drawSnap(void* graphics, double scale) const {
    if (!isMeasureTool() || m_hover.kind == SnapKind::None) return;
    Gdiplus::Graphics& g = *static_cast<Gdiplus::Graphics*>(graphics);
    Gdiplus::PointF c;
    if (!toScreen(m_host->markupCamera(), m_host->markupWidth(), m_host->markupHeight(), m_hover.point, &c)) return;
    const double dpi = m_host->markupDpi() / 96.0;
    const Gdiplus::REAL s = static_cast<Gdiplus::REAL>(5 * dpi * scale);
    Gdiplus::Pen pen(Gdiplus::Color(255, 0, 200, 83), static_cast<Gdiplus::REAL>(2 * dpi * scale));
    switch (m_hover.kind) {
        case SnapKind::Endpoint: g.DrawRectangle(&pen, c.X - s, c.Y - s, 2 * s, 2 * s); break;
        case SnapKind::Center:
            g.DrawEllipse(&pen, c.X - s, c.Y - s, 2 * s, 2 * s);
            g.DrawLine(&pen, c.X - s, c.Y, c.X + s, c.Y);
            g.DrawLine(&pen, c.X, c.Y - s, c.X, c.Y + s);
            break;
        case SnapKind::Midpoint: {
            const Gdiplus::PointF tri[3] = {{c.X, c.Y - s}, {c.X + s, c.Y + s}, {c.X - s, c.Y + s}};
            g.DrawPolygon(&pen, tri, 3);
            break;
        }
        case SnapKind::OnEdge:
            g.DrawLine(&pen, c.X - s, c.Y - s, c.X + s, c.Y + s);
            g.DrawLine(&pen, c.X - s, c.Y + s, c.X + s, c.Y - s);
            break;
        default: {
            Gdiplus::SolidBrush dot(Gdiplus::Color(255, 0, 200, 83));
            g.FillEllipse(&dot, c.X - s / 2, c.Y - s / 2, s, s);
            break;
        }
    }
}

void MarkupTools::draw(HDC dc, const Camera& camera, int width, int height, double scale, bool interactive) const {
    if (!ensureGdiplus()) return;
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    for (const Mark& mark : m_doc.marks) {
        if (!markVisible(mark, m_doc, camera)) continue;
        if (isMeasurement(mark.kind)) drawMeasure(&g, mark, camera, width, height, scale);
    }
    if (!interactive) return;

    // Medida en curso: linea de goma hasta el cursor.
    if (!m_pending.empty() && m_hover.kind != SnapKind::None) {
        Gdiplus::PointF a, b;
        if (toScreen(camera, width, height, m_pending.back().point, &a) &&
            toScreen(camera, width, height, m_hover.point, &b)) {
            Gdiplus::Pen rubber(gdiColor(kMeasureColor), 1.5f);
            rubber.SetDashStyle(Gdiplus::DashStyleDash);
            g.DrawLine(&rubber, a, b);
            if (m_tool == Tool::Distance) {
                const DistanceResult d = measureDistance(m_pending.back().point, m_hover.point, m_host->markupPlane());
                drawLabel(&g, (a.X + b.X) / 2, (a.Y + b.Y) / 2,
                          widen(formatLength(d.total, m_host->markupMesh().units)), L"", kMeasureColor, scale);
            }
        }
    }
    drawSnap(&g, scale);
}

}  // namespace stp
```

- [ ] **Step 4: Conectar `SceneView` (`scene_view.h`)**

4a. Agregar `#include <functional>` y `#include "markup_tools.h"` a los includes.

4b. Cambiar la declaración de la clase para implementar la interfaz: `class SceneView : public MarkupHost {`.

4c. En la sección `public`, agregar:

```cpp
    // Herramientas de medir y marcar. editing = false en el panel: solo medir.
    void enableTools(bool editing);
    MarkupTools* tools() { return m_tools.get(); }
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
```

4d. En la sección privada, agregar los miembros:

```cpp
    std::unique_ptr<MarkupTools> m_tools;
    std::shared_ptr<PickIndex> m_pick;
    std::function<void()> m_markupListener;
    bool m_toolsEnabled = false;
```

- [ ] **Step 5: Conectar `SceneView` (`scene_view.cpp`)**

5a. En `struct SceneLoadResult`, agregar `std::shared_ptr<PickIndex> pick;`.

5b. En `loadMemory`, capturar `const bool buildPick = m_toolsEnabled;` junto a `budgetMs`, agregarlo a la lista de captura de la lambda, y en la rama de carga exitosa (donde se llama a `detectPlanar`):

```cpp
        } else {
            result->planar = detectPlanar(result->mesh);
            if (buildPick) {
                // El indice se arma en este hilo: en planos enormes tarda cientos de ms.
                result->pick = std::make_shared<PickIndex>();
                result->pick->build(result->mesh);
            }
        }
```

5c. En `applyModel`, en el camino exitoso, justo después de `m_mesh = std::move(result->mesh);`:

```cpp
    m_pick = result->pick;
    if (m_tools) m_tools->clear();
```

y en las dos ramas que vacían la malla (`m_mesh = Mesh();`) agregar `m_pick.reset(); if (m_tools) m_tools->clear();`.

5d. Agregar las definiciones:

```cpp
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
```

5e. Al principio de `SceneView::handle`, antes del `switch`:

```cpp
    if (m_tools && !m_mesh.empty() && m_tools->handle(msg, wparam, lparam)) {
        return msg == WM_SETCURSOR ? TRUE : 0;
    }
```

5f. En `drawScene`, justo después de `drawMeshTexts(dc, m_mesh, m_camera, m_width, m_height, m_drawingColor);`:

```cpp
        if (m_tools) m_tools->draw(dc, m_camera, m_width, m_height, 1.0, true);
```

5g. En `drawOverlay`, justo antes de `DrawTextW(dc, hint.c_str(), -1, &help, …)`:

```cpp
        if (m_tools && !m_tools->hint().empty()) {
            hint = m_tools->hint();
            SetTextColor(dc, m_textColor);
        }
```

y agregar al final de las dos cadenas de ayuda largas (2D y 3D) `L"   |   M: medir"`. En la ayuda compacta del panel, agregar `L"   M: medir"`.

5h. Aristas con `A`: en el `switch (wparam)` de `WM_KEYDOWN`, cambiar `case 'E':` por `case 'A': case 'E':` (la `E` la usará la herramienta elipse en el visor, Task 8). En la ayuda larga 3D cambiar `L"E: aristas"` por `L"A: aristas"`.

- [ ] **Step 6: Visor y panel**

6a. `src/viewer/main.cpp`, después de `view.setBudget(120000);`: `view.enableTools(true);`.

6b. En el bucle de mensajes de `wWinMain`, cambiar la condición de Escape para que solo cierre si no hay herramienta en curso:

```cpp
            if ((msg.wParam == 'O' && GetKeyState(VK_CONTROL) < 0) ||
                (msg.wParam == VK_ESCAPE && !view.toolActive())) {
```

6c. `src/shellext/preview_handler.cpp`: justo después de que `m_view.create(...)` tenga éxito (donde se llama a `m_view.setCompact(true);`), agregar `m_view.enableTools(false);`.

- [ ] **Step 7: Agregar a la compilación**

En `build.sh` y `build.bat`, agregar `src/viewer/markup_tools.cpp` (`src\viewer\markup_tools.cpp`) a las dos líneas que ya listan `src/viewer/text_overlay.cpp` (DLL y visor).

- [ ] **Step 8: Compilar y correr las pruebas**

Run: `./build.sh test && ./build.sh 2>&1 | grep -E "error|warning: unused" ; ls -la dist/stpviewer.exe dist/StepShellExt.dll`
Expected: `77 pruebas, 0 fallidas`; sin errores; los dos binarios con fecha nueva.

- [ ] **Step 9: Verificar bajo wine**

Guardar como `/tmp/medir.sh` y correr con `xvfb-run -a -s "-screen 0 1100x760x24" sh /tmp/medir.sh`:

```sh
#!/bin/sh
export WINEDEBUG=-all
cd "$(git rev-parse --show-toplevel)"
S=/tmp/capturas; mkdir -p $S
wine dist/stpviewer.exe tests/samples/plano_brida.dxf &
sleep 6
W=$(xdotool search --name "plano_brida" | head -1)
xdotool windowmove $W 0 0; xdotool windowsize $W 1000 700; sleep 2
import -window root -crop 1000x700+0+0 $S/antes.png
xdotool key m; sleep 1
import -window root -crop 1000x700+0+0 $S/medir_modo.png
```

Abrir `antes.png` y ubicar en píxeles: los dos extremos de la línea de cota inferior (la que dice 220), el borde de un agujero de perno y un punto dentro de la brida lejos de los agujeros. Con esas coordenadas, extender el script:

```sh
xdotool mousemove XI YI click 1; sleep 0.5          # extremo izquierdo de la cota de 220
xdotool mousemove XD YD; sleep 0.5
import -window root -crop 1000x700+0+0 $S/medir_goma.png
xdotool click 1; sleep 1
import -window root -crop 1000x700+0+0 $S/medir_distancia.png
xdotool key 2; xdotool mousemove XP YP click 1; sleep 1   # borde del agujero de perno
import -window root -crop 1000x700+0+0 $S/medir_radio.png
xdotool key 4; xdotool mousemove XC YC click 1; sleep 1   # dentro de la brida, fuera de los agujeros
import -window root -crop 1000x700+0+0 $S/medir_area.png
xdotool key Escape; xdotool key Escape; sleep 1; xdotool key Escape
```

Expected (mirando cada captura):
- `medir_modo.png`: la ayuda inferior dice "Distancia: clic en el primer punto".
- `medir_goma.png`: línea discontinua naranja con etiqueta de distancia viva y marcador de enganche.
- `medir_distancia.png`: cota naranja con "220 mm", "ΔX 220   ΔY 0".
- `medir_radio.png`: "R 5 mm  Ø 10 mm".
- `medir_area.png`: zona sombreada y "A … mm²  P … mm"; la ayuda vuelve a la normal con Esc.
- El primer Esc termina la herramienta, el segundo no hace nada visible, el tercero cierra el visor.

Panel: `wine dist/previewtest.exe dist/StepShellExt.dll tests/samples/placa_agujero.stp &`, esperar 7 s, `xdotool key m`, clic en dos vértices visibles de la placa, captura: aparece la cota. No hay barra de herramientas.

- [ ] **Step 10: Commit**

```bash
git add src/viewer/markup_tools.h src/viewer/markup_tools.cpp src/viewer/image_view.h src/viewer/image_view.cpp src/viewer/scene_view.h src/viewer/scene_view.cpp src/viewer/main.cpp src/shellext/preview_handler.cpp build.sh build.bat
git commit -m "Measure distance, radius, angle and area in viewer and preview pane

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_015NM5546gRGTfskkpAXPqTF"
```

---

### Task 8: Herramientas de marcado, selección y deshacer

**Files:**
- Modify: `src/viewer/markup_tools.h`, `src/viewer/markup_tools.cpp`, `src/viewer/scene_view.cpp` (solo la ayuda)
- Test: `./build.sh test` sigue en verde; verificación bajo wine (Step 7).

**Interfaces:**
- Consumes: todo lo de la Task 7; `sameView`, `markVisible`, colores `kMark*` y `kHighlight*` (Task 6); `pixelRay` (Task 4).
- Produces (públicos en `MarkupTools`): `void undo()`, `void redo()`, `void deleteSelected()`, `void cycleColor()`, `std::uint32_t color() const`, `int hiddenCount(const Camera&) const`, `int selected() const` (id o −1), `void select(int id)`.

- [ ] **Step 1: Ampliar `markup_tools.h`**

En la sección `public` de `MarkupTools`, agregar:

```cpp
    void undo();
    void redo();
    void deleteSelected();
    void cycleColor();
    std::uint32_t color() const { return m_tool == Tool::Highlight ? m_highlight : m_color; }
    int hiddenCount(const Camera& camera) const;  // marcas de otras vistas
    int selected() const { return m_selected; }
    void select(int id);
```

En la sección `private`, agregar:

```cpp
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
```

- [ ] **Step 2: Reemplazar `addMark`, `setDocument` y agregar deshacer en `markup_tools.cpp`**

En el espacio de nombres anónimo del principio, agregar:

```cpp
constexpr UINT kNoteCommit = WM_APP + 21;
constexpr std::size_t kUndoDepth = 100;

std::string narrow(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(0, size)), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], size, nullptr, nullptr);
    }
    return out;
}

// Douglas-Peucker sobre el trazo en pantalla.
void simplify(const std::vector<POINT>& in, double tolerance, std::vector<POINT>* out) {
    if (in.size() < 3) {
        *out = in;
        return;
    }
    std::vector<char> keep(in.size(), 0);
    keep.front() = keep.back() = 1;
    std::vector<std::pair<std::size_t, std::size_t>> stack = {{0, in.size() - 1}};
    while (!stack.empty()) {
        const auto range = stack.back();
        stack.pop_back();
        const double ax = in[range.first].x, ay = in[range.first].y;
        const double bx = in[range.second].x, by = in[range.second].y;
        const double len = std::max(1e-9, std::hypot(bx - ax, by - ay));
        double worst = 0;
        std::size_t at = 0;
        for (std::size_t i = range.first + 1; i < range.second; ++i) {
            const double d = std::fabs((bx - ax) * (ay - in[i].y) - (ax - in[i].x) * (by - ay)) / len;
            if (d > worst) {
                worst = d;
                at = i;
            }
        }
        if (worst > tolerance) {
            keep[at] = 1;
            stack.push_back({range.first, at});
            stack.push_back({at, range.second});
        }
    }
    out->clear();
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (keep[i]) out->push_back(in[i]);
    }
}

double segmentDistance(double px, double py, double ax, double ay, double bx, double by) {
    const double dx = bx - ax, dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0;
    t = std::max(0.0, std::min(1.0, t));
    return std::hypot(px - (ax + dx * t), py - (ay + dy * t));
}
```

Reemplazar `addMark` y `setDocument` por:

```cpp
void MarkupTools::pushUndo() {
    m_undo.push_back(m_doc);
    if (m_undo.size() > kUndoDepth) m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void MarkupTools::addMark(Mark mark) {
    pushUndo();
    mark.id = m_doc.newId();
    m_doc.marks.push_back(mark);
    m_dirty = true;
    m_host->markupRedraw();
    m_host->markupChanged();
}

void MarkupTools::setDocument(const MarkupDocument& doc) {
    closeNoteEditor(false);
    m_doc = doc;
    m_values.clear();
    m_pending.clear();
    m_undo.clear();
    m_redo.clear();
    m_selected = -1;
    m_dirty = false;
    m_host->markupRedraw();
    m_host->markupChanged();
}

void MarkupTools::undo() {
    if (m_undo.empty()) return;
    m_redo.push_back(m_doc);
    m_doc = m_undo.back();
    m_undo.pop_back();
    m_values.clear();
    m_selected = -1;
    m_dirty = true;
    m_host->markupRedraw();
    m_host->markupChanged();
}

void MarkupTools::redo() {
    if (m_redo.empty()) return;
    m_undo.push_back(m_doc);
    m_doc = m_redo.back();
    m_redo.pop_back();
    m_values.clear();
    m_selected = -1;
    m_dirty = true;
    m_host->markupRedraw();
    m_host->markupChanged();
}

void MarkupTools::select(int id) {
    m_selected = id;
    m_host->markupRedraw();
}

void MarkupTools::deleteSelected() {
    const auto it = std::find_if(m_doc.marks.begin(), m_doc.marks.end(),
                                 [&](const Mark& m) { return m.id == m_selected; });
    if (it == m_doc.marks.end()) return;
    pushUndo();
    m_values.erase(it->id);
    m_doc.marks.erase(it);
    m_selected = -1;
    m_dirty = true;
    m_host->markupRedraw();
    m_host->markupChanged();
}

void MarkupTools::cycleColor() {
    if (m_tool == Tool::Highlight) {
        m_highlight = m_highlight == kHighlightYellow ? kHighlightGreen
                      : m_highlight == kHighlightGreen ? kHighlightPink : kHighlightYellow;
    } else {
        const std::uint32_t colors[] = {kMarkRed, kMarkYellow, kMarkGreen, kMarkBlue, kMarkBlack};
        std::size_t i = 0;
        while (i < 5 && colors[i] != m_color) ++i;
        m_color = colors[(i + 1) % 5];
    }
    m_host->markupChanged();
}

int MarkupTools::hiddenCount(const Camera& camera) const {
    int count = 0;
    for (const Mark& mark : m_doc.marks) {
        if (!markVisible(mark, m_doc, camera)) ++count;
    }
    return count;
}

bool MarkupTools::isSketchTool() const {
    return m_tool == Tool::Highlight || m_tool == Tool::Underline || m_tool == Tool::Pen ||
           m_tool == Tool::Rectangle || m_tool == Tool::Ellipse || m_tool == Tool::Cloud;
}
```

- [ ] **Step 3: Anclaje de trazos y notas**

```cpp
// Punto de pantalla llevado al plano del dibujo (2D) o al plano perpendicular a
// la vista que pasa por `through` (3D).
Vec3 MarkupTools::unproject(int x, int y, const Vec3& through) const {
    const Camera& camera = m_host->markupCamera();
    const Ray ray = pixelRay(camera, m_host->markupWidth(), m_host->markupHeight(), x, y);
    Vec3 normal = camera.forward(), origin = through;
    if (const PlanarInfo* plane = m_host->markupPlane()) {
        normal = plane->normal;
        origin = plane->center;
    }
    const double den = dot(ray.dir, normal);
    if (std::fabs(den) < 1e-12) return origin;
    return ray.origin + ray.dir * (dot(origin - ray.origin, normal) / den);
}

// En 2D los trazos se ven siempre; en 3D pertenecen a la vista en que se dibujan.
int MarkupTools::viewForSketch() {
    if (m_host->markupPlane()) return 0;
    const Camera& camera = m_host->markupCamera();
    for (const MarkupView& view : m_doc.views) {
        if (sameView(view.camera, camera)) return view.id;
    }
    MarkupView view;
    view.id = m_doc.newId();
    view.name = "Vista " + std::to_string(m_doc.views.size() + 1);
    view.camera = camera;
    m_doc.views.push_back(view);
    return view.id;
}

void MarkupTools::finishSketch() {
    m_sketching = false;
    if (m_stroke.empty()) return;
    long minX = m_stroke[0].x, maxX = minX, minY = m_stroke[0].y, maxY = minY;
    for (const POINT& p : m_stroke) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    if (maxX - minX < 3 && maxY - minY < 3) {  // un clic suelto no es un trazo
        m_stroke.clear();
        m_host->markupRedraw();
        return;
    }
    std::vector<POINT> points;
    if (m_tool == Tool::Pen || m_tool == Tool::Highlight) simplify(m_stroke, 0.5, &points);
    else points = {m_stroke.front(), m_stroke.back()};

    pushUndo();
    Mark mark;
    mark.kind = m_tool == Tool::Highlight ? MarkKind::Highlight
                : m_tool == Tool::Underline ? MarkKind::Underline
                : m_tool == Tool::Pen ? MarkKind::Pen
                : m_tool == Tool::Rectangle ? MarkKind::Rectangle
                : m_tool == Tool::Ellipse ? MarkKind::Ellipse : MarkKind::Cloud;
    mark.color = color();
    mark.view = viewForSketch();
    const Vec3 depth = m_host->markupCamera().target;
    for (const POINT& p : points) mark.points.push_back(unproject(p.x, p.y, depth));
    mark.id = m_doc.newId();
    m_doc.marks.push_back(mark);
    m_stroke.clear();
    m_dirty = true;
    m_host->markupRedraw();
    m_host->markupChanged();
}
```

- [ ] **Step 4: Editor de notas**

```cpp
LRESULT CALLBACK MarkupTools::editProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<MarkupTools*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);
    if (msg == WM_KEYDOWN && wparam == VK_RETURN && GetKeyState(VK_SHIFT) >= 0) {
        PostMessageW(self->m_host->markupWindow(), kNoteCommit, 1, 0);
        return 0;
    }
    if (msg == WM_KEYDOWN && wparam == VK_ESCAPE) {
        PostMessageW(self->m_host->markupWindow(), kNoteCommit, 0, 0);
        return 0;
    }
    if (msg == WM_KILLFOCUS) PostMessageW(self->m_host->markupWindow(), kNoteCommit, 1, 0);
    return CallWindowProcW(self->m_editDefault, hwnd, msg, wparam, lparam);
}

void MarkupTools::openNoteEditor(int markId) {
    closeNoteEditor(true);
    const auto it = std::find_if(m_doc.marks.begin(), m_doc.marks.end(), [&](const Mark& m) { return m.id == markId; });
    if (it == m_doc.marks.end() || it->points.size() < 2) return;
    double x, y;
    if (!projectPoint(m_host->markupCamera(), m_host->markupWidth(), m_host->markupHeight(), it->points[1], &x, &y)) return;
    const int dpi = m_host->markupDpi();
    m_edit = CreateWindowExW(0, L"EDIT", widen(it->text).c_str(),
                             WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                             static_cast<int>(x), static_cast<int>(y), MulDiv(260, dpi, 96), MulDiv(64, dpi, 96),
                             m_host->markupWindow(), nullptr, nullptr, nullptr);
    if (!m_edit) return;
    SendMessageW(m_edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SetWindowLongPtrW(m_edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    m_editDefault = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(m_edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&MarkupTools::editProc)));
    m_editMark = markId;
    SendMessageW(m_edit, EM_SETSEL, 0, -1);
    SetFocus(m_edit);
}

void MarkupTools::closeNoteEditor(bool commit) {
    if (!m_edit) return;
    HWND edit = m_edit;
    m_edit = nullptr;  // antes de destruir: WM_KILLFOCUS no debe volver a entrar
    const int length = GetWindowTextLengthW(edit);
    std::wstring text(static_cast<std::size_t>(length), L'\0');
    if (length > 0) GetWindowTextW(edit, &text[0], length + 1);
    DestroyWindow(edit);

    const auto it = std::find_if(m_doc.marks.begin(), m_doc.marks.end(), [&](const Mark& m) { return m.id == m_editMark; });
    m_editMark = -1;
    if (it != m_doc.marks.end()) {
        std::string utf8 = narrow(text);
        utf8.erase(std::remove(utf8.begin(), utf8.end(), '\r'), utf8.end());
        if (commit && !utf8.empty()) {
            // Una nota nueva ya guardo su paso de deshacer al crearse; editar una
            // existente guarda uno nuevo.
            if (utf8 != it->text && !it->text.empty()) pushUndo();
            it->text = utf8;
            m_dirty = true;
        } else if (it->text.empty()) {
            m_doc.marks.erase(it);  // nota nueva sin texto: se descarta
        }
    }
    SetFocus(m_host->markupWindow());
    m_host->markupRedraw();
    m_host->markupChanged();
}
```

- [ ] **Step 5: Selección y dibujo de trazos, formas y notas**

```cpp
bool MarkupTools::screenBounds(const Mark& mark, const Camera& camera, int width, int height, RECT* out) const {
    bool any = false;
    for (const Vec3& p : mark.points) {
        double x, y;
        if (!projectPoint(camera, width, height, p, &x, &y)) continue;
        const LONG ix = static_cast<LONG>(x), iy = static_cast<LONG>(y);
        if (!any) *out = {ix, iy, ix, iy};
        out->left = std::min(out->left, ix); out->right = std::max(out->right, ix);
        out->top = std::min(out->top, iy); out->bottom = std::max(out->bottom, iy);
        any = true;
    }
    return any;
}

int MarkupTools::hitTest(int x, int y) const {
    const Camera& camera = m_host->markupCamera();
    const int w = m_host->markupWidth(), h = m_host->markupHeight();
    const double reach = 6.0 * m_host->markupDpi() / 96.0;
    for (auto it = m_doc.marks.rbegin(); it != m_doc.marks.rend(); ++it) {
        const Mark& mark = *it;
        if (!markVisible(mark, m_doc, camera)) continue;
        std::vector<Gdiplus::PointF> p;
        for (const Vec3& q : mark.points) {
            Gdiplus::PointF s;
            if (toScreen(camera, w, h, q, &s)) p.push_back(s);
        }
        if (p.empty()) continue;
        double best = 1e300;
        if (mark.kind == MarkKind::Rectangle || mark.kind == MarkKind::Ellipse || mark.kind == MarkKind::Cloud) {
            if (p.size() < 2) continue;
            const double l = std::min(p[0].X, p[1].X), r = std::max(p[0].X, p[1].X);
            const double t = std::min(p[0].Y, p[1].Y), b = std::max(p[0].Y, p[1].Y);
            best = std::min({segmentDistance(x, y, l, t, r, t), segmentDistance(x, y, r, t, r, b),
                             segmentDistance(x, y, r, b, l, b), segmentDistance(x, y, l, b, l, t)});
        } else {
            for (std::size_t i = 0; i + 1 < p.size(); ++i) {
                best = std::min(best, segmentDistance(x, y, p[i].X, p[i].Y, p[i + 1].X, p[i + 1].Y));
            }
            // Etiqueta de medidas y caja de notas: un recuadro generoso alrededor del ultimo punto.
            const Gdiplus::PointF& label = mark.kind == MarkKind::Note ? p[1 % p.size()] : p[0];
            if (mark.kind == MarkKind::Note || isMeasurement(mark.kind)) {
                if (std::fabs(x - label.X) < 90 * reach / 6 && std::fabs(y - label.Y) < 24 * reach / 6) best = 0;
            }
            if (p.size() == 1) best = std::min(best, std::hypot(x - p[0].X, y - p[0].Y));
        }
        const double width = mark.kind == MarkKind::Highlight ? 7.0 * reach / 6 : reach;
        if (best <= width) return mark.id;
    }
    return -1;
}

void MarkupTools::drawSketch(void* graphics, const Mark& mark, const Camera& camera, int width, int height,
                             double scale) const {
    Gdiplus::Graphics& g = *static_cast<Gdiplus::Graphics*>(graphics);
    const double dpi = m_host->markupDpi() / 96.0;
    std::vector<Gdiplus::PointF> p;
    for (const Vec3& q : mark.points) {
        Gdiplus::PointF s;
        if (!toScreen(camera, width, height, q, &s)) return;
        p.push_back(s);
    }
    if (p.size() < 2) return;
    const bool highlight = mark.kind == MarkKind::Highlight;
    const double widthPx = highlight ? 14 : (mark.kind == MarkKind::Underline ? 3 : 2);
    Gdiplus::Pen pen(gdiColor(mark.color, highlight ? 102 : 255), static_cast<Gdiplus::REAL>(widthPx * dpi * scale));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    const Gdiplus::REAL l = std::min(p[0].X, p[1].X), r = std::max(p[0].X, p[1].X);
    const Gdiplus::REAL t = std::min(p[0].Y, p[1].Y), b = std::max(p[0].Y, p[1].Y);
    switch (mark.kind) {
        case MarkKind::Highlight:
        case MarkKind::Pen:
        case MarkKind::Underline:
            g.DrawLines(&pen, p.data(), static_cast<INT>(p.size()));
            break;
        case MarkKind::Rectangle: g.DrawRectangle(&pen, l, t, r - l, b - t); break;
        case MarkKind::Ellipse: g.DrawEllipse(&pen, l, t, r - l, b - t); break;
        case MarkKind::Cloud: {
            // Arcos hacia afuera a lo largo de los cuatro lados.
            const Gdiplus::REAL bump = static_cast<Gdiplus::REAL>(10 * dpi * scale);
            Gdiplus::GraphicsPath path;
            auto side = [&](Gdiplus::REAL length, auto center, Gdiplus::REAL start) {
                const int n = std::max(1, static_cast<int>(length / bump));
                const Gdiplus::REAL step = length / n;
                for (int i = 0; i < n; ++i) {
                    const Gdiplus::PointF c = center((i + 0.5f) * step);
                    path.AddArc(c.X - step / 2, c.Y - step / 2, step, step, start, 180.0f);
                }
            };
            side(r - l, [&](Gdiplus::REAL d) { return Gdiplus::PointF(l + d, t); }, 180.0f);
            side(b - t, [&](Gdiplus::REAL d) { return Gdiplus::PointF(r, t + d); }, 270.0f);
            side(r - l, [&](Gdiplus::REAL d) { return Gdiplus::PointF(r - d, b); }, 0.0f);
            side(b - t, [&](Gdiplus::REAL d) { return Gdiplus::PointF(l, b - d); }, 90.0f);
            g.DrawPath(&pen, &path);
            break;
        }
        default:
            break;
    }
}

void MarkupTools::drawNote(void* graphics, const Mark& mark, const Camera& camera, int width, int height,
                           double scale) const {
    Gdiplus::Graphics& g = *static_cast<Gdiplus::Graphics*>(graphics);
    if (mark.points.size() < 2 || mark.id == m_editMark) return;  // mientras se edita la tapa el cuadro de texto
    Gdiplus::PointF anchor, label;
    if (!toScreen(camera, width, height, mark.points[0], &anchor) ||
        !toScreen(camera, width, height, mark.points[1], &label)) {
        return;
    }
    const double dpi = m_host->markupDpi() / 96.0;
    Gdiplus::Pen pen(gdiColor(mark.color), static_cast<Gdiplus::REAL>(2 * dpi * scale));
    Gdiplus::AdjustableArrowCap arrow(4, 4, TRUE);
    pen.SetCustomEndCap(&arrow);
    g.DrawLine(&pen, label, anchor);

    Gdiplus::Font font(L"Segoe UI", static_cast<Gdiplus::REAL>(12 * dpi * scale), Gdiplus::FontStyleRegular,
                       Gdiplus::UnitPixel);
    const std::wstring text = widen(mark.text);
    const Gdiplus::RectF layout(0, 0, static_cast<Gdiplus::REAL>(260 * dpi * scale), 10000);
    Gdiplus::RectF box;
    g.MeasureString(text.c_str(), -1, &font, layout, &box);
    const Gdiplus::REAL pad = static_cast<Gdiplus::REAL>(5 * dpi * scale);
    const Gdiplus::RectF frame(label.X, label.Y, box.Width + 2 * pad, box.Height + 2 * pad);
    Gdiplus::SolidBrush fill(Gdiplus::Color(240, 255, 255, 255));
    Gdiplus::Pen border(gdiColor(mark.color), static_cast<Gdiplus::REAL>(1.5 * dpi * scale));
    g.FillRectangle(&fill, frame);
    g.DrawRectangle(&border, frame.X, frame.Y, frame.Width, frame.Height);
    Gdiplus::SolidBrush ink(gdiColor(kMarkBlack));
    const Gdiplus::RectF inner(frame.X + pad, frame.Y + pad, box.Width + 1, box.Height + 1);
    g.DrawString(text.c_str(), -1, &font, inner, nullptr, &ink);
}
```

- [ ] **Step 6: Reemplazar `handle` completo**

```cpp
bool MarkupTools::handle(UINT msg, WPARAM wparam, LPARAM lparam) {
    const int x = GET_X_LPARAM(lparam), y = GET_Y_LPARAM(lparam);
    switch (msg) {
        case kNoteCommit:
            closeNoteEditor(wparam != 0);
            return true;

        case WM_TIMER:
            if (wparam != kMessageTimer) return false;
            KillTimer(m_host->markupWindow(), kMessageTimer);
            m_message.clear();
            m_host->markupRedraw();
            return true;

        case WM_SETCURSOR:
            if (m_tool == Tool::Navigate || LOWORD(lparam) != HTCLIENT) return false;
            // Sin nada que medir bajo el cursor: "no disponible".
            SetCursor(LoadCursor(nullptr, isMeasureTool() && m_mouse.x >= 0 && m_hover.kind == SnapKind::None
                                              ? IDC_NO : IDC_CROSS));
            return true;

        case WM_MOUSEMOVE:
            m_mouse = {x, y};
            if (isMeasureTool() || (m_tool == Tool::Note && !m_noteAnchored)) {
                m_hover = snapAt(x, y);
                m_host->markupRedraw();
            } else if (m_tool == Tool::Note) {
                m_host->markupRedraw();  // linea de goma de la nota
            }
            if (m_sketching) {
                POINT p = {x, y};
                if (m_tool == Tool::Underline && GetKeyState(VK_SHIFT) < 0) {
                    // Shift: horizontal o vertical, lo que este mas cerca.
                    if (std::abs(p.x - m_stroke[0].x) >= std::abs(p.y - m_stroke[0].y)) p.y = m_stroke[0].y;
                    else p.x = m_stroke[0].x;
                }
                if (m_tool == Tool::Pen || m_tool == Tool::Highlight) m_stroke.push_back(p);
                else if (m_stroke.size() == 1) m_stroke.push_back(p);
                else m_stroke[1] = p;
                m_host->markupRedraw();
                return true;
            }
            return false;

        case WM_LBUTTONDOWN:
            SetFocus(m_host->markupWindow());
            m_downAt = {x, y};
            if (isMeasureTool()) {
                clickMeasure(snapAt(x, y));
                return true;
            }
            if (isSketchTool()) {
                SetCapture(m_host->markupWindow());
                m_sketching = true;
                m_stroke = {POINT{x, y}};
                return true;
            }
            if (m_tool == Tool::Note) {
                if (!m_noteAnchored) {
                    m_noteAnchor = snapAt(x, y);
                    if (m_noteAnchor.kind == SnapKind::None) {
                        m_noteAnchor.point = unproject(x, y, m_host->markupCamera().target);
                    }
                    m_noteAnchored = true;
                } else {
                    pushUndo();
                    Mark note;
                    note.kind = MarkKind::Note;
                    note.color = m_color;
                    note.points = {m_noteAnchor.point, unproject(x, y, m_noteAnchor.point)};
                    note.id = m_doc.newId();
                    m_doc.marks.push_back(note);
                    m_noteAnchored = false;
                    m_dirty = true;
                    openNoteEditor(note.id);
                }
                m_host->markupRedraw();
                return true;
            }
            return false;  // Navegar: la vista gira o mueve

        case WM_LBUTTONUP:
            if (m_sketching) {
                ReleaseCapture();
                finishSketch();
                return true;
            }
            if (m_tool == Tool::Navigate && m_editing && std::abs(x - m_downAt.x) < 3 && std::abs(y - m_downAt.y) < 3) {
                select(hitTest(x, y));
                m_host->markupChanged();
            }
            return m_tool != Tool::Navigate;

        case WM_LBUTTONDBLCLK:
            if (m_tool == Tool::Navigate && m_editing) {
                const int id = hitTest(x, y);
                const auto it = std::find_if(m_doc.marks.begin(), m_doc.marks.end(), [&](const Mark& m) { return m.id == id; });
                if (it != m_doc.marks.end() && it->kind == MarkKind::Note) {
                    openNoteEditor(id);
                    return true;
                }
                return false;
            }
            return m_tool != Tool::Navigate;

        case WM_KEYDOWN: {
            const bool control = GetKeyState(VK_CONTROL) < 0;
            if (control && wparam == 'Z') { undo(); return true; }
            if (control && wparam == 'Y') { redo(); return true; }
            if (control) return false;
            if (wparam == 'M') { setTool(Tool::Distance); return true; }
            if (isMeasureTool() && wparam >= '1' && wparam <= '4') {
                const Tool tools[] = {Tool::Distance, Tool::Radius, Tool::Angle, Tool::Area};
                setTool(tools[wparam - '1']);
                return true;
            }
            if (m_editing) {
                switch (wparam) {
                    case 'H': setTool(Tool::Highlight); return true;
                    case 'U': setTool(Tool::Underline); return true;
                    case 'N': setTool(Tool::Note); return true;
                    case 'R': setTool(Tool::Rectangle); return true;
                    case 'E': setTool(Tool::Ellipse); return true;
                    case 'C': setTool(Tool::Cloud); return true;
                    case 'L': setTool(Tool::Pen); return true;
                    case 'Q': cycleColor(); return true;
                    default: break;
                }
            }
            if (wparam == VK_ESCAPE) {
                if (m_sketching || !m_pending.empty() || m_noteAnchored) {
                    if (m_sketching) ReleaseCapture();
                    m_sketching = false;
                    m_stroke.clear();
                    m_pending.clear();
                    m_noteAnchored = false;
                    m_host->markupRedraw();
                    return true;
                }
                if (m_tool != Tool::Navigate) { setTool(Tool::Navigate); return true; }
                if (m_selected >= 0) { select(-1); return true; }
                return false;
            }
            if (wparam == VK_DELETE) {
                if (m_selected >= 0) { deleteSelected(); return true; }
                if (!m_editing && !m_doc.marks.empty()) {  // panel: borra la ultima medida
                    pushUndo();
                    m_values.erase(m_doc.marks.back().id);
                    m_doc.marks.pop_back();
                    m_host->markupRedraw();
                    return true;
                }
            }
            return false;
        }
        default:
            return false;
    }
}
```

Y reemplazar `setTool` para que también limpie trazos y notas a medio hacer (agregar después de `m_pending.clear();`):

```cpp
    m_stroke.clear();
    m_sketching = false;
    m_noteAnchored = false;
```

- [ ] **Step 7: Reemplazar `draw` completo y ampliar `hint`**

```cpp
void MarkupTools::draw(HDC dc, const Camera& camera, int width, int height, double scale, bool interactive) const {
    if (!ensureGdiplus()) return;
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    const double dpi = m_host->markupDpi() / 96.0;

    for (const Mark& mark : m_doc.marks) {
        if (!markVisible(mark, m_doc, camera)) continue;
        if (isMeasurement(mark.kind)) drawMeasure(&g, mark, camera, width, height, scale);
        else if (mark.kind == MarkKind::Note) drawNote(&g, mark, camera, width, height, scale);
        else drawSketch(&g, mark, camera, width, height, scale);
        if (interactive && mark.id == m_selected) {
            RECT box;
            if (screenBounds(mark, camera, width, height, &box)) {
                Gdiplus::Pen dashed(Gdiplus::Color(255, 47, 111, 219), static_cast<Gdiplus::REAL>(1.5 * dpi));
                dashed.SetDashStyle(Gdiplus::DashStyleDash);
                const Gdiplus::REAL pad = static_cast<Gdiplus::REAL>(8 * dpi);
                g.DrawRectangle(&dashed, box.left - pad, box.top - pad, box.right - box.left + 2 * pad,
                                box.bottom - box.top + 2 * pad);
            }
        }
    }
    if (!interactive) return;

    const int hidden = hiddenCount(camera);
    if (hidden > 0) {
        drawLabel(&g, width / 2.0, 64 * dpi,
                  std::to_wstring(hidden) + (hidden == 1 ? L" marca en otra vista" : L" marcas en otras vistas") +
                      L" — F2 para verlas",
                  L"", kMarkBlue, 1.0);
    }

    // Trazo o forma en curso, dibujado como si ya existiera.
    if (m_sketching && m_stroke.size() >= 2) {
        Mark preview;
        preview.kind = m_tool == Tool::Highlight ? MarkKind::Highlight
                       : m_tool == Tool::Underline ? MarkKind::Underline
                       : m_tool == Tool::Pen ? MarkKind::Pen
                       : m_tool == Tool::Rectangle ? MarkKind::Rectangle
                       : m_tool == Tool::Ellipse ? MarkKind::Ellipse : MarkKind::Cloud;
        preview.color = color();
        const Vec3 depth = camera.target;
        for (const POINT& p : m_stroke) preview.points.push_back(unproject(p.x, p.y, depth));
        drawSketch(&g, preview, camera, width, height, scale);
    }

    // Nota a medio hacer: flecha de goma desde el ancla hasta el cursor.
    if (m_tool == Tool::Note && m_noteAnchored) {
        Gdiplus::PointF a;
        if (toScreen(camera, width, height, m_noteAnchor.point, &a)) {
            Gdiplus::Pen rubber(gdiColor(m_color), static_cast<Gdiplus::REAL>(1.5 * dpi));
            rubber.SetDashStyle(Gdiplus::DashStyleDash);
            g.DrawLine(&rubber, a, Gdiplus::PointF(static_cast<Gdiplus::REAL>(m_mouse.x), static_cast<Gdiplus::REAL>(m_mouse.y)));
        }
    }

    if (!m_pending.empty() && m_hover.kind != SnapKind::None) {
        Gdiplus::PointF a, b;
        if (toScreen(camera, width, height, m_pending.back().point, &a) &&
            toScreen(camera, width, height, m_hover.point, &b)) {
            Gdiplus::Pen rubber(gdiColor(kMeasureColor), 1.5f);
            rubber.SetDashStyle(Gdiplus::DashStyleDash);
            g.DrawLine(&rubber, a, b);
            if (m_tool == Tool::Distance) {
                const DistanceResult d = measureDistance(m_pending.back().point, m_hover.point, m_host->markupPlane());
                drawLabel(&g, (a.X + b.X) / 2, (a.Y + b.Y) / 2,
                          widen(formatLength(d.total, m_host->markupMesh().units)), L"", kMeasureColor, scale);
            }
        }
    }
    if (isMeasureTool() || (m_tool == Tool::Note && !m_noteAnchored)) drawSnap(&g, scale);
}
```

En `drawSnap`, cambiar la primera línea a `if (m_hover.kind == SnapKind::None) return;` (la herramienta Nota también la usa).

En `hint`, agregar los casos antes de `default`:

```cpp
        case Tool::Highlight: return L"Resaltador: arrastrar   (Q: color, Esc: terminar)";
        case Tool::Underline: return L"Subrayado: arrastrar   (Shift: recto, Q: color, Esc: terminar)";
        case Tool::Pen: return L"Lapiz: arrastrar   (Q: color, Esc: terminar)";
        case Tool::Rectangle: return L"Rectangulo: arrastrar de esquina a esquina   (Q: color, Esc: terminar)";
        case Tool::Ellipse: return L"Elipse: arrastrar de esquina a esquina   (Q: color, Esc: terminar)";
        case Tool::Cloud: return L"Nube de revision: arrastrar de esquina a esquina   (Q: color, Esc: terminar)";
        case Tool::Note:
            return (m_noteAnchored ? L"Nota: clic donde va el texto" : L"Nota: clic en el punto a senalar") +
                   std::wstring(L"   (Esc: terminar)");
```

- [ ] **Step 8: Ayuda del visor**

En `scene_view.cpp`, en `drawOverlay`, al final de las dos cadenas de ayuda largas (2D y 3D), reemplazar el `L"   |   M: medir"` agregado en la Task 7 por:

```cpp
L"   |   M: medir   |   H U N R E C L: marcar   |   Ctrl+Z: deshacer"
```

(Solo en el visor: si `m_tools && m_tools->editing()`; en el panel queda `M: medir`. Construir la cadena con un `if` en vez de dos literales.)

- [ ] **Step 9: Compilar y verificar bajo wine**

Run: `./build.sh test && ./build.sh 2>&1 | grep -E "error"`
Expected: `77 pruebas, 0 fallidas`; sin errores.

Script `/tmp/marcar.sh` (misma cabecera que `/tmp/medir.sh`: abre `plano_brida.dxf` a 1000×700 y guarda `antes.png`), y después:

```sh
xdotool key h; xdotool mousemove 300 300 mousedown 1 mousemove 360 310 mousemove 460 320 mouseup 1; sleep 0.5
xdotool key u; xdotool mousemove 300 360 mousedown 1 mousemove 450 365 mouseup 1; sleep 0.5
xdotool key c; xdotool mousemove 600 150 mousedown 1 mousemove 760 260 mouseup 1; sleep 0.5
xdotool key r; xdotool mousemove 120 420 mousedown 1 mousemove 220 480 mouseup 1; sleep 0.5
xdotool key e; xdotool mousemove 250 420 mousedown 1 mousemove 330 480 mouseup 1; sleep 0.5
xdotool key l; xdotool mousemove 500 500 mousedown 1 mousemove 520 540 mousemove 560 520 mousemove 600 560 mouseup 1; sleep 0.5
xdotool key n; xdotool mousemove 355 350 click 1; xdotool mousemove 430 250 click 1; sleep 0.5
xdotool type "Revisar este agujero"; xdotool key Return; sleep 1
xdotool key Escape; sleep 0.5
import -window root -crop 1000x700+0+0 $S/marcas.png
xdotool mousemove 305 362 click 1; sleep 0.3; xdotool key Delete; sleep 0.5   # selecciona y borra el subrayado
import -window root -crop 1000x700+0+0 $S/marcas_borrada.png
xdotool key ctrl+z; sleep 0.5
import -window root -crop 1000x700+0+0 $S/marcas_deshacer.png
```

Expected:
- `marcas.png`: resaltado amarillo translúcido, subrayado rojo, nube de revisión, rectángulo, elipse, trazo de lápiz y una nota "Revisar este agujero" con flecha.
- `marcas_borrada.png`: el subrayado ya no está.
- `marcas_deshacer.png`: el subrayado volvió.

Con `placa_agujero.stp` (3D): dibujar un rectángulo, pulsar `7` después de girar la pieza con arrastre: el rectángulo desaparece y aparece "1 marca en otra vista — F2 para verlas".

- [ ] **Step 10: Commit**

```bash
git add src/viewer/markup_tools.h src/viewer/markup_tools.cpp src/viewer/scene_view.cpp
git commit -m "Add highlighter, underline, notes, shapes, pen, selection and undo

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_015NM5546gRGTfskkpAXPqTF"
```

---

### Task 9: Guardar y cargar el archivo `.marcas`

**Files:**
- Modify: `src/viewer/scene_view.h`, `src/viewer/scene_view.cpp`, `src/viewer/main.cpp`
- Test: `./build.sh test` sigue en verde (el formato ya está probado en la Task 6); verificación bajo wine (Step 5), incluida la carpeta de solo lectura (Review Focus 5).

**Interfaces:**
- Consumes: `serializeMarkup`, `parseMarkup`, `modelChanged` (Task 6); `MarkupTools::setDocument`, `document`, `dirty`, `markSaved`, `showMessage`, `editing` (Tasks 7–8); `readFileBytes` (existente).
- Produces (públicos en `SceneView`): `bool saveMarks(std::wstring* message);`, `const std::wstring& path() const;`, `const Camera& camera() const;`.

- [ ] **Step 1: Declarar en `scene_view.h`**

Públicos:

```cpp
    // Guarda <modelo>.marcas si hay cambios. false (y un mensaje) si no se pudo.
    bool saveMarks(std::wstring* message);
    const std::wstring& path() const { return m_path; }
    const Camera& camera() const { return m_camera; }
```

Privados:

```cpp
    void loadMarks();
    std::wstring m_path;  // vacio en el panel: ahi no hay ruta
```

- [ ] **Step 2: Funciones de archivo en `scene_view.cpp`**

En el espacio de nombres anónimo:

```cpp
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
```

(Agregar `#include <cstdio>` si falta.)

- [ ] **Step 3: Cargar y guardar en `SceneView`**

3a. En `loadFile`, al principio: `saveMarks(nullptr);`. Y antes de llamar a `loadMemory`: `m_path = path;`. En `loadMemory` no se toca `m_path`: el panel llama directo a `loadMemory` y su `m_path` queda vacío.

3b. En `applyModel`, al final del camino exitoso (después de `requestQualityPass();`): `loadMarks();`.

3c. Definiciones:

```cpp
void SceneView::loadMarks() {
    if (!m_tools || !m_tools->editing() || m_path.empty()) return;
    std::string text;
    if (!readFileBytes(markupPathOf(m_path), &text)) return;  // sin marcas todavia

    MarkupDocument doc;
    const MarkupParseReport report = parseMarkup(text, &doc);
    if (!report.recognized) {
        m_tools->showMessage(L"El archivo de marcas no se reconoce; se reemplaza solo si marcas algo");
        return;
    }
    m_tools->setDocument(doc);
    std::uint64_t size = 0;
    std::string date;
    if (report.badLines > 0 || report.newerVersion) {
        m_tools->showMessage(L"Algunas marcas no se pudieron leer; el archivo no se toca hasta que marques algo");
    } else if (fileStamp(m_path, &size, &date) && modelChanged(doc, size, date)) {
        m_tools->showMessage(L"El archivo cambió desde que se marcó");
    }
}

bool SceneView::saveMarks(std::wstring* message) {
    if (!m_tools || !m_tools->editing() || m_path.empty() || !m_tools->dirty()) return true;
    const std::wstring target = markupPathOf(m_path);
    MarkupDocument doc = m_tools->document();
    fileStamp(m_path, &doc.modelSize, &doc.modelDate);
    doc.modelName = narrowUtf8(fileNameOf(m_path));

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
```

Protección del `.marcas` dañado o desconocido: `saveMarks` solo escribe si `dirty()`, y `setDocument` deja `dirty() == false` al abrir, así que no se pisa hasta que el usuario cambia algo. En el caso "no se reconoce", `setDocument` no se llama y las marcas quedan vacías: si el usuario marca algo, se reemplaza, que es lo que dice el aviso.

- [ ] **Step 4: Visor (`main.cpp`)**

4a. En `openFile`, antes de `g_view->loadFile(path);`:

```cpp
    std::wstring problem;
    if (!g_view->saveMarks(&problem)) MessageBoxW(frame, problem.c_str(), L"stp-viewer", MB_ICONWARNING);
```

4b. Título con asterisco cuando hay cambios: agregar en el espacio de nombres anónimo

```cpp
void updateTitle(HWND frame) {
    if (!g_view || g_currentFile.empty()) return;
    const bool dirty = g_view->tools() && g_view->tools()->dirty();
    SetWindowTextW(frame, (fileNameOf(g_currentFile) + (dirty ? L" *" : L"") + L" - stp-viewer").c_str());
}
```

y en `wWinMain`, después de `view.enableTools(true);`: `view.setMarkupListener([frame]() { updateTitle(frame); });`.

4c. En `frameProc`, agregar el caso de cierre:

```cpp
        case WM_CLOSE: {
            std::wstring problem;
            if (g_view && !g_view->saveMarks(&problem)) {
                const std::wstring question = problem + L"\n\n¿Cerrar de todos modos?";
                if (MessageBoxW(hwnd, question.c_str(), L"stp-viewer", MB_YESNO | MB_ICONWARNING) != IDYES) return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        }
```

4d. `Ctrl+S` en el `WM_KEYDOWN` de `frameProc`:

```cpp
            if (wparam == 'S' && GetKeyState(VK_CONTROL) < 0 && g_view && g_view->tools()) {
                std::wstring problem;
                const bool saved = g_view->saveMarks(&problem);
                g_view->tools()->showMessage(saved ? L"Marcas guardadas" : problem);
                updateTitle(hwnd);
                return 0;
            }
```

4e. En el bucle de mensajes, reenviar también `Ctrl+S` desde la vista al marco: agregar a la condición `|| (msg.wParam == 'S' && GetKeyState(VK_CONTROL) < 0)`.

- [ ] **Step 5: Compilar y verificar bajo wine**

Run: `./build.sh test && ./build.sh 2>&1 | grep -E "error"`
Expected: `77 pruebas, 0 fallidas`; sin errores.

Guardar y reabrir (`/tmp/guardar.sh`, con la cabecera de `/tmp/medir.sh` pero sobre una copia `cp tests/samples/plano_brida.dxf /tmp/prueba/brida.dxf`):

```sh
xdotool key c; xdotool mousemove 600 150 mousedown 1 mousemove 760 260 mouseup 1; sleep 0.5
xdotool key Escape; xdotool key Escape; sleep 2          # cierra: guarda solo
cat /tmp/prueba/brida.dxf.marcas
wine dist/stpviewer.exe /tmp/prueba/brida.dxf & sleep 6
import -window root -crop 1000x700+0+0 $S/reabierto.png
```

Expected: el `cat` muestra `stp-viewer-marcas 1`, la línea `modelo nombre="brida.dxf" tamano=… fecha="…"` y una línea `forma … tipo=nube`; `reabierto.png` muestra la nube.

Modelo cambiado: `touch /tmp/prueba/brida.dxf` y reabrir → aviso "El archivo cambió desde que se marcó" en la barra inferior, la nube sigue.

Solo lectura (Review Focus 5): `chmod 555 /tmp/prueba`, abrir, dibujar un rectángulo, `ctrl+s` → mensaje "No se pudieron guardar las marcas en …"; `ls /tmp/prueba` no muestra ningún `.tmp` y el `.marcas` anterior sigue igual (`md5sum` antes y después). Cerrar con Esc → pregunta "¿Cerrar de todos modos?"; `xdotool key Return` cierra. `chmod 755 /tmp/prueba` al terminar.

Borrar todo: abrir, seleccionar la nube (clic en su borde), `Delete`, cerrar → el `.marcas` ya no existe.

- [ ] **Step 6: Commit**

```bash
git add src/viewer/scene_view.h src/viewer/scene_view.cpp src/viewer/main.cpp
git commit -m "Save and load markup next to the model file

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_015NM5546gRGTfskkpAXPqTF"
```

---

### Task 10: Barra de herramientas y lista lateral

**Files:**
- Create: `src/viewer/toolbar.h`, `src/viewer/toolbar.cpp`
- Modify: `src/viewer/markup_tools.h`, `src/viewer/markup_tools.cpp`, `src/viewer/main.cpp`, `build.sh`, `build.bat` (solo la línea del visor)
- Test: verificación bajo wine (Step 6).

**Interfaces:**
- Consumes: `Tool`, `MarkupTools::setTool`, `tool`, `color`, `cycleColor`, `describe`, `document`, `select` (Tasks 7–8); `MarkupHost::markupSetCamera`.
- Produces:
  - `class Toolbar { bool create(HINSTANCE, HWND parent); HWND hwnd() const; int width() const; void setState(Tool tool, std::uint32_t color); }` y los comandos `kCommandTool = 1000` (+ `static_cast<int>(Tool)`), `kCommandColor = 1100`, `kCommandList = 1101`, `kCommandSave = 1102`, `kCommandExport = 1103`.
  - En `MarkupTools`: `std::vector<std::pair<int, std::wstring>> listEntries() const;` (código > 0 = id de marca, < 0 = −id de vista) y `void focusEntry(int code);`.

- [ ] **Step 1: Lista de marcas y vistas en `MarkupTools`**

Declarar en la parte pública de `markup_tools.h`:

```cpp
    // Para la lista lateral: (id de marca, texto) o (-id de vista, texto).
    std::vector<std::pair<int, std::wstring>> listEntries() const;
    void focusEntry(int code);
```

Definir en `markup_tools.cpp`:

```cpp
std::vector<std::pair<int, std::wstring>> MarkupTools::listEntries() const {
    std::vector<std::pair<int, std::wstring>> entries;
    for (const MarkupView& view : m_doc.views) {
        int count = 0;
        for (const Mark& mark : m_doc.marks) count += mark.view == view.id ? 1 : 0;
        entries.push_back({-view.id, widen(view.name) + L"  (" + std::to_wstring(count) + L")"});
    }
    for (const Mark& mark : m_doc.marks) {
        entries.push_back({mark.id, (mark.view ? L"    " : L"") + describe(mark)});
    }
    return entries;
}

void MarkupTools::focusEntry(int code) {
    if (code < 0) {
        if (const MarkupView* view = m_doc.findView(-code)) m_host->markupSetCamera(view->camera);
        return;
    }
    const auto it = std::find_if(m_doc.marks.begin(), m_doc.marks.end(), [&](const Mark& m) { return m.id == code; });
    if (it == m_doc.marks.end() || it->points.empty()) return;
    if (it->view) {
        if (const MarkupView* view = m_doc.findView(it->view)) m_host->markupSetCamera(view->camera);
    } else {
        // Centra la marca sin cambiar el zoom ni la orientacion.
        Camera camera = m_host->markupCamera();
        const Vec3 shift = it->points[0] - camera.target;
        camera.target = camera.target + camera.right() * dot(shift, camera.right()) + camera.up() * dot(shift, camera.up());
        m_host->markupSetCamera(camera);
    }
    select(code);
}
```

- [ ] **Step 2: Crear `src/viewer/toolbar.h`**

```cpp
// Barra vertical de herramientas del visor: iconos dibujados por codigo, sin
// archivos de imagen, con tooltip y atajo.
#pragma once

#include <windows.h>

#include <cstdint>

#include "markup_tools.h"

namespace stp {

constexpr int kCommandTool = 1000;  // + static_cast<int>(Tool)
constexpr int kCommandColor = 1100;
constexpr int kCommandList = 1101;
constexpr int kCommandSave = 1102;
constexpr int kCommandExport = 1103;

class Toolbar {
public:
    bool create(HINSTANCE instance, HWND parent);
    HWND hwnd() const { return m_hwnd; }
    int width() const;
    void setState(Tool tool, std::uint32_t color);

private:
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void paint();
    int buttonAt(int y) const;
    int buttonTop(int index) const;
    int buttonSize() const;

    HWND m_hwnd = nullptr;
    HWND m_tips = nullptr;
    int m_dpi = 96;
    Tool m_tool = Tool::Navigate;
    std::uint32_t m_color = kMarkRed;
    int m_hot = -1;
};

}  // namespace stp
```

- [ ] **Step 3: Crear `src/viewer/toolbar.cpp`**

```cpp
#include "toolbar.h"

#include <windowsx.h>
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cmath>

#include "image_view.h"

namespace stp {
namespace {

struct Button {
    int command;      // 0 = separador
    const wchar_t* tip;
};

const Button kButtons[] = {
    {kCommandTool + static_cast<int>(Tool::Navigate), L"Navegar (Esc)"},
    {0, nullptr},
    {kCommandTool + static_cast<int>(Tool::Distance), L"Medir distancia (M, 1)"},
    {kCommandTool + static_cast<int>(Tool::Radius), L"Medir radio y diámetro (M, 2)"},
    {kCommandTool + static_cast<int>(Tool::Angle), L"Medir ángulo (M, 3)"},
    {kCommandTool + static_cast<int>(Tool::Area), L"Medir área y perímetro (M, 4)"},
    {0, nullptr},
    {kCommandTool + static_cast<int>(Tool::Highlight), L"Resaltador (H)"},
    {kCommandTool + static_cast<int>(Tool::Underline), L"Subrayado (U)"},
    {kCommandTool + static_cast<int>(Tool::Note), L"Nota con flecha (N)"},
    {kCommandTool + static_cast<int>(Tool::Rectangle), L"Rectángulo (R)"},
    {kCommandTool + static_cast<int>(Tool::Ellipse), L"Elipse (E)"},
    {kCommandTool + static_cast<int>(Tool::Cloud), L"Nube de revisión (C)"},
    {kCommandTool + static_cast<int>(Tool::Pen), L"Lápiz (L)"},
    {kCommandColor, L"Color (Q)"},
    {0, nullptr},
    {kCommandList, L"Lista de marcas (F2)"},
    {kCommandSave, L"Guardar marcas (Ctrl+S)"},
    {kCommandExport, L"Exportar PNG o PDF (Ctrl+E)"},
};
constexpr int kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);
const wchar_t* kClass = L"StpToolbar";

Gdiplus::Color argb(std::uint32_t c, BYTE a = 255) {
    return Gdiplus::Color(a, static_cast<BYTE>(c >> 16), static_cast<BYTE>(c >> 8), static_cast<BYTE>(c));
}

// Iconos de 24x24 unidades, escalados a la caja.
void drawIcon(Gdiplus::Graphics& g, int command, const Gdiplus::RectF& box, std::uint32_t color) {
    const Gdiplus::REAL s = box.Width / 24.0f;
    auto P = [&](float x, float y) { return Gdiplus::PointF(box.X + x * s, box.Y + y * s); };
    Gdiplus::Pen ink(Gdiplus::Color(255, 40, 48, 56), 1.8f * s);
    ink.SetStartCap(Gdiplus::LineCapRound);
    ink.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::SolidBrush solid(Gdiplus::Color(255, 40, 48, 56));
    const int tool = command - kCommandTool;
    if (command == kCommandColor) {
        Gdiplus::SolidBrush swatch(argb(color));
        g.FillEllipse(&swatch, box.X + 5 * s, box.Y + 5 * s, 14 * s, 14 * s);
        g.DrawEllipse(&ink, box.X + 5 * s, box.Y + 5 * s, 14 * s, 14 * s);
        return;
    }
    switch (command) {
        case kCommandList:
            for (int i = 0; i < 3; ++i) g.DrawLine(&ink, P(5, 7 + 5.0f * i), P(19, 7 + 5.0f * i));
            return;
        case kCommandSave:
            g.DrawRectangle(&ink, box.X + 5 * s, box.Y + 5 * s, 14 * s, 14 * s);
            g.DrawRectangle(&ink, box.X + 8 * s, box.Y + 5 * s, 8 * s, 5 * s);
            return;
        case kCommandExport:
            g.DrawLine(&ink, P(12, 16), P(12, 5));
            g.DrawLine(&ink, P(8, 9), P(12, 5));
            g.DrawLine(&ink, P(16, 9), P(12, 5));
            g.DrawLine(&ink, P(5, 14), P(5, 19));
            g.DrawLine(&ink, P(5, 19), P(19, 19));
            g.DrawLine(&ink, P(19, 19), P(19, 14));
            return;
        default:
            break;
    }
    switch (static_cast<Tool>(tool)) {
        case Tool::Navigate: {
            const Gdiplus::PointF arrow[] = {P(7, 4), P(7, 19), P(11, 15), P(14, 21), P(16, 20), P(13, 14), P(18, 14)};
            g.FillPolygon(&solid, arrow, 7);
            break;
        }
        case Tool::Distance:
            g.DrawLine(&ink, P(4, 12), P(20, 12));
            g.DrawLine(&ink, P(4, 8), P(4, 16));
            g.DrawLine(&ink, P(20, 8), P(20, 16));
            break;
        case Tool::Radius:
            g.DrawEllipse(&ink, box.X + 4 * s, box.Y + 4 * s, 16 * s, 16 * s);
            g.DrawLine(&ink, P(12, 12), P(18, 7));
            break;
        case Tool::Angle:
            g.DrawLine(&ink, P(5, 19), P(20, 19));
            g.DrawLine(&ink, P(5, 19), P(16, 6));
            g.DrawArc(&ink, box.X - 3 * s, box.Y + 11 * s, 16 * s, 16 * s, -50.0f, 50.0f);
            break;
        case Tool::Area: {
            Gdiplus::SolidBrush fill(Gdiplus::Color(120, 255, 140, 26));
            g.FillRectangle(&fill, box.X + 5 * s, box.Y + 5 * s, 14 * s, 14 * s);
            g.DrawRectangle(&ink, box.X + 5 * s, box.Y + 5 * s, 14 * s, 14 * s);
            break;
        }
        case Tool::Highlight: {
            Gdiplus::Pen marker(argb(kHighlightYellow, 170), 6 * s);
            marker.SetStartCap(Gdiplus::LineCapRound);
            marker.SetEndCap(Gdiplus::LineCapRound);
            g.DrawLine(&marker, P(5, 15), P(19, 9));
            break;
        }
        case Tool::Underline: {
            Gdiplus::Font font(L"Segoe UI", 13 * s, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            g.DrawString(L"U", 1, &font, P(7, 1), &solid);
            Gdiplus::Pen red(argb(kMarkRed), 2 * s);
            g.DrawLine(&red, P(5, 20), P(19, 20));
            break;
        }
        case Tool::Note:
            g.DrawRectangle(&ink, box.X + 9 * s, box.Y + 4 * s, 11 * s, 8 * s);
            g.DrawLine(&ink, P(9, 12), P(4, 20));
            break;
        case Tool::Rectangle: g.DrawRectangle(&ink, box.X + 4 * s, box.Y + 6 * s, 16 * s, 12 * s); break;
        case Tool::Ellipse: g.DrawEllipse(&ink, box.X + 4 * s, box.Y + 6 * s, 16 * s, 12 * s); break;
        case Tool::Cloud:
            for (int i = 0; i < 3; ++i) g.DrawArc(&ink, box.X + (4 + 5.5f * i) * s, box.Y + 8 * s, 6 * s, 6 * s, 180.0f, 180.0f);
            for (int i = 0; i < 3; ++i) g.DrawArc(&ink, box.X + (4 + 5.5f * i) * s, box.Y + 12 * s, 6 * s, 6 * s, 0.0f, 180.0f);
            break;
        case Tool::Pen: {
            const Gdiplus::PointF wave[] = {P(4, 16), P(8, 9), P(12, 16), P(16, 9), P(20, 14)};
            g.DrawCurve(&ink, wave, 5);
            break;
        }
    }
}

}  // namespace

bool Toolbar::create(HINSTANCE instance, HWND parent) {
    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    if (!GetClassInfoExW(instance, kClass, &cls)) {
        cls.lpfnWndProc = &Toolbar::proc;
        cls.hInstance = instance;
        cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        cls.lpszClassName = kClass;
        RegisterClassExW(&cls);
    }
    m_hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent, nullptr, instance, this);
    if (!m_hwnd) return false;
    HDC dc = GetDC(m_hwnd);
    m_dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(m_hwnd, dc);

    INITCOMMONCONTROLSEX init = {sizeof(init), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&init);
    m_tips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT,
                             CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, m_hwnd, nullptr, instance, nullptr);
    for (int i = 0; i < kButtonCount; ++i) {
        if (!kButtons[i].command) continue;
        TOOLINFOW info = {};
        info.cbSize = TTTOOLINFOW_V2_SIZE;
        info.uFlags = TTF_SUBCLASS;
        info.hwnd = m_hwnd;
        info.uId = static_cast<UINT_PTR>(i);
        info.rect = {0, buttonTop(i), width(), buttonTop(i) + buttonSize()};
        info.lpszText = const_cast<wchar_t*>(kButtons[i].tip);
        SendMessageW(m_tips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    }
    return true;
}

int Toolbar::buttonSize() const { return MulDiv(36, m_dpi, 96); }
int Toolbar::width() const { return MulDiv(44, m_dpi, 96); }

int Toolbar::buttonTop(int index) const {
    int y = MulDiv(6, m_dpi, 96);
    for (int i = 0; i < index; ++i) y += kButtons[i].command ? buttonSize() : MulDiv(10, m_dpi, 96);
    return y;
}

int Toolbar::buttonAt(int y) const {
    for (int i = 0; i < kButtonCount; ++i) {
        if (kButtons[i].command && y >= buttonTop(i) && y < buttonTop(i) + buttonSize()) return i;
    }
    return -1;
}

void Toolbar::setState(Tool tool, std::uint32_t color) {
    m_tool = tool;
    m_color = color;
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void Toolbar::paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(m_hwnd, &ps);
    if (ensureGdiplus()) {
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const Gdiplus::REAL pad = static_cast<Gdiplus::REAL>(MulDiv(6, m_dpi, 96));
        const Gdiplus::REAL left = (width() - buttonSize()) / 2.0f;
        for (int i = 0; i < kButtonCount; ++i) {
            const Gdiplus::REAL top = static_cast<Gdiplus::REAL>(buttonTop(i));
            if (!kButtons[i].command) {
                Gdiplus::Pen line(Gdiplus::Color(255, 190, 196, 204), 1.0f);
                const Gdiplus::REAL y = top + MulDiv(5, m_dpi, 96);
                g.DrawLine(&line, left, y, left + buttonSize(), y);
                continue;
            }
            const bool pressed = kButtons[i].command == kCommandTool + static_cast<int>(m_tool);
            if (pressed || i == m_hot) {
                Gdiplus::SolidBrush back(pressed ? Gdiplus::Color(255, 205, 222, 247) : Gdiplus::Color(255, 229, 233, 238));
                g.FillRectangle(&back, left, top, static_cast<Gdiplus::REAL>(buttonSize()), static_cast<Gdiplus::REAL>(buttonSize()));
            }
            const Gdiplus::RectF box(left + pad, top + pad, buttonSize() - 2 * pad, buttonSize() - 2 * pad);
            drawIcon(g, kButtons[i].command, box, m_color);
        }
    }
    EndPaint(m_hwnd, &ps);
}

LRESULT CALLBACK Toolbar::proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<Toolbar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);
    switch (msg) {
        case WM_PAINT:
            self->paint();
            return 0;
        case WM_MOUSEMOVE: {
            const int hot = self->buttonAt(GET_Y_LPARAM(lparam));
            if (hot != self->m_hot) {
                self->m_hot = hot;
                InvalidateRect(hwnd, nullptr, TRUE);
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&track);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            self->m_hot = -1;
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_LBUTTONDOWN: {
            const int index = self->buttonAt(GET_Y_LPARAM(lparam));
            if (index >= 0) PostMessageW(GetParent(hwnd), WM_COMMAND, static_cast<WPARAM>(kButtons[index].command), 0);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

}  // namespace stp
```

- [ ] **Step 4: Montar barra y lista en `main.cpp`**

4a. Includes: `#include "toolbar.h"`. Globales en el espacio de nombres anónimo:

```cpp
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
```

con este helper (Windows 7 no tiene `GetDpiForWindow`):

```cpp
int GetDpiForWindowSafe(HWND hwnd) {
    HDC dc = GetDC(hwnd);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(hwnd, dc);
    return dpi;
}
```

(declarado antes de `layout`).

4b. En `frameProc`, `WM_SIZE` pasa a llamar `layout(hwnd); return 0;`. Agregar:

```cpp
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
```

Para que los botones Guardar y Exportar funcionen sin la tecla Ctrl, en `WM_KEYDOWN` del marco aceptar `'S'` y `'E'` cuando `lparam == 0` (enviados por la barra) o con Ctrl: cambiar la condición de guardado a `wparam == 'S' && (GetKeyState(VK_CONTROL) < 0 || lparam == 0)`.

`F2` en `WM_KEYDOWN` del marco:

```cpp
            if (wparam == VK_F2) {
                g_listVisible = !g_listVisible;
                layout(hwnd);
                return 0;
            }
```

y en el bucle de mensajes agregar `|| msg.wParam == VK_F2` a la condición que reenvía teclas de la vista al marco.

4c. En `wWinMain`, después de crear la vista:

```cpp
    g_toolbar.create(instance, frame);
    g_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                             WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0, 10, 10, frame,
                             nullptr, instance, nullptr);
    SendMessageW(g_list, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    layout(frame);
```

y reemplazar el listener de la Task 9 por:

```cpp
    view.setMarkupListener([frame]() {
        updateTitle(frame);
        if (g_view && g_view->tools()) g_toolbar.setState(g_view->tools()->tool(), g_view->tools()->color());
        refreshList();
    });
```

- [ ] **Step 5: Agregar a la compilación**

En `build.sh` y `build.bat`, solo en la línea del visor (`stpviewer.exe`), agregar `src/viewer/toolbar.cpp` (`src\viewer\toolbar.cpp`).

- [ ] **Step 6: Compilar y verificar bajo wine**

Run: `./build.sh 2>&1 | grep -E "error"`
Expected: sin errores.

Con `plano_brida.dxf` abierto a 1000×700: captura inicial → barra vertical a la izquierda con 16 botones y separadores; pasar el ratón sobre el segundo botón (≈ x 22, y 64) 1 s → tooltip "Medir distancia (M, 1)". Clic en el botón de nube, dibujar una nube, `F2` → lista a la derecha con "Nube de revisión". Clic en la entrada → la nube queda seleccionada (recuadro punteado azul). Con `placa_agujero.stp`, dibujar un rectángulo, girar, `F2`, clic en "Vista 1 (1)" → vuelve la vista y reaparece el rectángulo.

- [ ] **Step 7: Commit**

```bash
git add src/viewer/toolbar.h src/viewer/toolbar.cpp src/viewer/markup_tools.h src/viewer/markup_tools.cpp src/viewer/main.cpp build.sh build.bat
git commit -m "Add viewer toolbar and markup side list

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_015NM5546gRGTfskkpAXPqTF"
```

---

### Task 11: Exportar PNG y PDF

**Files:**
- Create: `src/export/pdf_writer.h`, `src/export/pdf_writer.cpp`, `src/viewer/export.h`, `src/viewer/export.cpp`
- Modify: `src/viewer/scene_view.h`, `src/viewer/scene_view.cpp`, `src/viewer/main.cpp`, `build.sh`, `build.bat`
- Test: `tests/unit_tests.cpp` (escritor PDF); verificación bajo wine con `pdfinfo` (Step 8).

**Interfaces:**
- Consumes: `MarkupTools::draw(dc, camera, w, h, scale, false)`, `describe`, `document` (Tasks 7–8); `SceneView::path`, `camera`, `saveMarks` (Task 9); `renderMesh`, `drawMeshTexts`, `ensureGdiplus`.
- Produces:
  - `struct PdfText { double x, y, size; std::string text; bool bold; };` (UTF-8, puntos, origen abajo a la izquierda)
  - `class PdfWriter { explicit PdfWriter(double width = 842, double height = 595); void addPage(const std::vector<std::uint8_t>& jpeg, int pixelWidth, int pixelHeight, double x, double y, double w, double h, const std::vector<PdfText>& texts); std::size_t pageCount() const; std::string finish() const; };`
  - `std::string utf8ToWinAnsi(const std::string& utf8);`
  - `bool savePng(HBITMAP bitmap, const std::wstring& path);`, `bool encodeJpeg(HBITMAP bitmap, ULONG quality, std::vector<std::uint8_t>* out);`
  - En `SceneView`: `HBITMAP renderSnapshot(const Camera& camera, int width, int height, double scale);`, `Camera overviewCamera(double aspect) const;`

- [ ] **Step 1: Escribir las pruebas del PDF que fallan**

Agregar `#include "../src/export/pdf_writer.h"` y `#include <cstdlib>` arriba, y al final (antes de `int main`):

```cpp
// --- PDF --------------------------------------------------------------------------------

TEST(pdf_structure_has_valid_xref) {
    stp::PdfWriter pdf;
    const std::vector<std::uint8_t> fakeJpeg = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0xFF, 0xD9};
    pdf.addPage(fakeJpeg, 4, 3, 36, 48, 770, 500, {{36, 570, 14, "Vista general", true}});
    pdf.addPage({}, 0, 0, 0, 0, 0, 0, {{36, 560, 10, "Medidas y notas", false}});
    CHECK(pdf.pageCount() == 2);
    const std::string file = pdf.finish();
    CHECK(file.compare(0, 8, "%PDF-1.4") == 0);
    CHECK(file.find("/Count 2") != std::string::npos);
    CHECK(file.find("/Filter /DCTDecode") != std::string::npos);

    const std::size_t startxref = file.rfind("startxref");
    CHECK(startxref != std::string::npos);
    if (startxref == std::string::npos) return;
    const std::size_t xref = std::strtoul(file.c_str() + startxref + 10, nullptr, 10);
    CHECK(file.compare(xref, 4, "xref") == 0);
    // "xref\n0 N\n" y una entrada de 20 bytes por objeto.
    const std::size_t countAt = file.find('\n', xref) + 1;
    const int objects = std::atoi(file.c_str() + countAt + 2);
    CHECK(objects >= 8);  // catalogo, paginas, 2 fuentes, 2 paginas, 2 contenidos, 1 imagen
    const std::size_t entries = file.find('\n', countAt) + 1;
    for (int k = 1; k < objects; ++k) {
        const std::size_t offset = std::strtoul(file.c_str() + entries + 20 * k, nullptr, 10);
        CHECK(file.compare(offset, std::to_string(k).size() + 6, std::to_string(k) + " 0 obj") == 0);
    }
}

TEST(pdf_text_is_winansi_and_escaped) {
    CHECK(stp::utf8ToWinAnsi("\xC3\x98 \xC3\xB1 \xE2\x82\xAC \xE2\x80\x94 \xE4\xB8\xAD") == "\xD8 \xF1 \x80 \x97 ?");
    stp::PdfWriter pdf;
    pdf.addPage({}, 0, 0, 0, 0, 0, 0, {{10, 10, 10, "a(b)c\\", false}});
    CHECK(pdf.finish().find("(a\\(b\\)c\\\\) Tj") != std::string::npos);
}
```

- [ ] **Step 2: Correr y ver que falla**

Run: `./build.sh test`
Expected: error de compilación: falta `pdf_writer.h`.

- [ ] **Step 3: Crear `src/export/pdf_writer.h`**

```cpp
// Escritor de PDF minimo: paginas con una imagen JPEG y lineas de texto en
// Helvetica. Suficiente para exportar una revision; sin dependencias.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace stp {

struct PdfText {
    double x = 0.0, y = 0.0;  // puntos, origen abajo a la izquierda
    double size = 10.0;
    std::string text;         // UTF-8; lo que no entra en WinAnsi sale como '?'
    bool bold = false;
};

class PdfWriter {
public:
    explicit PdfWriter(double width = 842.0, double height = 595.0);  // A4 apaisado

    // jpeg vacio: pagina solo con texto.
    void addPage(const std::vector<std::uint8_t>& jpeg, int pixelWidth, int pixelHeight, double x, double y,
                 double w, double h, const std::vector<PdfText>& texts);
    std::size_t pageCount() const { return m_pages.size(); }
    std::string finish() const;

private:
    struct Page {
        std::vector<std::uint8_t> jpeg;
        int pixelWidth = 0, pixelHeight = 0;
        double x = 0, y = 0, w = 0, h = 0;
        std::vector<PdfText> texts;
    };
    double m_width, m_height;
    std::vector<Page> m_pages;
};

std::string utf8ToWinAnsi(const std::string& utf8);

}  // namespace stp
```

- [ ] **Step 4: Crear `src/export/pdf_writer.cpp`**

```cpp
#include "pdf_writer.h"

#include <cstdio>

namespace stp {
namespace {

std::string fixed(double v) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", v);
    return buffer;
}

std::string escaped(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '(' || c == ')' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

// Codigos 0x80-0x9F de Windows-1252 y su caracter Unicode.
const unsigned kHigh[32] = {0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                            0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
                            0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                            0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};

}  // namespace

std::string utf8ToWinAnsi(const std::string& utf8) {
    std::string out;
    std::size_t i = 0;
    while (i < utf8.size()) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        unsigned cp = c;
        int extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : -1;
        if (extra < 0 || i + extra >= utf8.size() + (extra == 0)) {
            out.push_back('?');
            ++i;
            continue;
        }
        if (extra > 0) {
            cp = c & (0x3F >> extra);
            for (int k = 1; k <= extra; ++k) cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
        }
        i += extra + 1;
        if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF)) {
            out.push_back(static_cast<char>(cp));
            continue;
        }
        char mapped = '?';
        for (unsigned k = 0; k < 32; ++k) {
            if (kHigh[k] && kHigh[k] == cp) mapped = static_cast<char>(0x80 + k);
        }
        out.push_back(mapped);
    }
    return out;
}

PdfWriter::PdfWriter(double width, double height) : m_width(width), m_height(height) {}

void PdfWriter::addPage(const std::vector<std::uint8_t>& jpeg, int pixelWidth, int pixelHeight, double x, double y,
                        double w, double h, const std::vector<PdfText>& texts) {
    Page page;
    page.jpeg = jpeg;
    page.pixelWidth = pixelWidth;
    page.pixelHeight = pixelHeight;
    page.x = x;
    page.y = y;
    page.w = w;
    page.h = h;
    page.texts = texts;
    m_pages.push_back(std::move(page));
}

std::string PdfWriter::finish() const {
    // Numeracion: 1 catalogo, 2 paginas, 3 y 4 fuentes, luego por pagina: pagina, contenido [, imagen].
    std::vector<int> pageIds, contentIds, imageIds;
    int next = 5;
    for (const Page& page : m_pages) {
        pageIds.push_back(next++);
        contentIds.push_back(next++);
        imageIds.push_back(page.jpeg.empty() ? 0 : next++);
    }

    std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets(static_cast<std::size_t>(next), 0);
    auto begin = [&](int id) {
        offsets[static_cast<std::size_t>(id)] = out.size();
        out += std::to_string(id) + " 0 obj\n";
    };

    begin(1);
    out += "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
    begin(2);
    out += "<< /Type /Pages /Kids [";
    for (const int id : pageIds) out += " " + std::to_string(id) + " 0 R";
    out += " ] /Count " + std::to_string(m_pages.size()) + " >>\nendobj\n";
    begin(3);
    out += "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>\nendobj\n";
    begin(4);
    out += "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>\nendobj\n";

    for (std::size_t i = 0; i < m_pages.size(); ++i) {
        const Page& page = m_pages[i];
        begin(pageIds[i]);
        out += "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + fixed(m_width) + " " + fixed(m_height) +
               "] /Resources << /Font << /F1 3 0 R /F2 4 0 R >>";
        if (imageIds[i]) out += " /XObject << /Im0 " + std::to_string(imageIds[i]) + " 0 R >>";
        out += " >> /Contents " + std::to_string(contentIds[i]) + " 0 R >>\nendobj\n";

        std::string content;
        if (imageIds[i]) {
            content += "q " + fixed(page.w) + " 0 0 " + fixed(page.h) + " " + fixed(page.x) + " " + fixed(page.y) +
                       " cm /Im0 Do Q\n";
        }
        for (const PdfText& text : page.texts) {
            content += "BT /" + std::string(text.bold ? "F2 " : "F1 ") + fixed(text.size) + " Tf " + fixed(text.x) +
                       " " + fixed(text.y) + " Td (" + escaped(utf8ToWinAnsi(text.text)) + ") Tj ET\n";
        }
        begin(contentIds[i]);
        out += "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream\nendobj\n";

        if (imageIds[i]) {
            begin(imageIds[i]);
            out += "<< /Type /XObject /Subtype /Image /Width " + std::to_string(page.pixelWidth) + " /Height " +
                   std::to_string(page.pixelHeight) +
                   " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length " +
                   std::to_string(page.jpeg.size()) + " >>\nstream\n";
            out.append(reinterpret_cast<const char*>(page.jpeg.data()), page.jpeg.size());
            out += "\nendstream\nendobj\n";
        }
    }

    const std::size_t xref = out.size();
    out += "xref\n0 " + std::to_string(next) + "\n0000000000 65535 f \n";
    for (int id = 1; id < next; ++id) {
        char line[32];
        std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offsets[static_cast<std::size_t>(id)]);
        out += line;
    }
    out += "trailer\n<< /Size " + std::to_string(next) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) +
           "\n%%EOF\n";
    return out;
}

}  // namespace stp
```

- [ ] **Step 5: Agregar a la compilación y correr las pruebas**

`build.sh:14` y `build.bat:9`: agregar `src/export/pdf_writer.cpp` (`src\export\pdf_writer.cpp`) a `ENGINE`.

Run: `./build.sh test`
Expected: `79 pruebas, 0 fallidas`.

- [ ] **Step 6: Exportar desde el visor**

6a. Crear `src/viewer/export.h`:

```cpp
// Guardar imagenes con GDI+: PNG para compartir una vista, JPEG para el PDF.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace stp {

bool savePng(HBITMAP bitmap, const std::wstring& path);
bool encodeJpeg(HBITMAP bitmap, ULONG quality, std::vector<std::uint8_t>* out);

}  // namespace stp
```

6b. Crear `src/viewer/export.cpp`:

```cpp
#include "export.h"

#include <objidl.h>
#include <gdiplus.h>

#include <memory>

#include "image_view.h"

namespace stp {
namespace {

bool encoderFor(const wchar_t* mime, CLSID* clsid) {
    UINT count = 0, size = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &size) != Gdiplus::Ok || size == 0) return false;
    std::unique_ptr<BYTE[]> buffer(new BYTE[size]);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.get());
    if (Gdiplus::GetImageEncoders(count, size, codecs) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(codecs[i].MimeType, mime) == 0) {
            *clsid = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

}  // namespace

bool savePng(HBITMAP bitmap, const std::wstring& path) {
    CLSID png;
    if (!ensureGdiplus() || !encoderFor(L"image/png", &png)) return false;
    Gdiplus::Bitmap image(bitmap, nullptr);
    return image.Save(path.c_str(), &png, nullptr) == Gdiplus::Ok;
}

bool encodeJpeg(HBITMAP bitmap, ULONG quality, std::vector<std::uint8_t>* out) {
    CLSID jpeg;
    if (!ensureGdiplus() || !encoderFor(L"image/jpeg", &jpeg)) return false;
    Gdiplus::Bitmap image(bitmap, nullptr);
    Gdiplus::EncoderParameters params;
    params.Count = 1;
    params.Parameter[0].Guid = Gdiplus::EncoderQuality;
    params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Value = &quality;
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK) return false;
    bool ok = image.Save(stream, &jpeg, &params) == Gdiplus::Ok;
    if (ok) {
        HGLOBAL memory = nullptr;
        ok = GetHGlobalFromStream(stream, &memory) == S_OK;
        STATSTG stat = {};
        ok = ok && stream->Stat(&stat, STATFLAG_NONAME) == S_OK;
        const void* data = ok ? GlobalLock(memory) : nullptr;
        if (data) {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            out->assign(bytes, bytes + stat.cbSize.QuadPart);
            GlobalUnlock(memory);
        } else {
            ok = false;
        }
    }
    stream->Release();
    return ok;
}

}  // namespace stp
```

6c. En `SceneView` (declarar en la parte pública de `scene_view.h`):

```cpp
    // Render sin pantalla con textos y marcas, para exportar. El llamante libera el HBITMAP.
    HBITMAP renderSnapshot(const Camera& camera, int width, int height, double scale);
    // Vista general: el plano de frente o la pieza en isometrica, encuadrada.
    Camera overviewCamera(double aspect) const;
```

Definir en `scene_view.cpp`:

```cpp
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
```

(Agregar `#include "export.h"` no hace falta aquí: la exportación la arma `main.cpp`.)

6d. En `main.cpp`, agregar `#include <lmcons.h>`, `#include "export.h"`, `#include "../export/pdf_writer.h"` y, en el espacio de nombres anónimo:

```cpp
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
```

6e. En `WM_KEYDOWN` del marco:

```cpp
            if (wparam == 'E' && (GetKeyState(VK_CONTROL) < 0 || lparam == 0)) {
                exportMarkup(hwnd);
                return 0;
            }
```

y en el bucle de mensajes reenviar también `Ctrl+E` desde la vista (`|| (msg.wParam == 'E' && GetKeyState(VK_CONTROL) < 0)`), **antes** de que la vista lo tome como la herramienta Elipse: la condición del bucle ya se evalúa antes de `DispatchMessageW`, así que alcanza con agregarla ahí. `MarkupTools::handle` ignora las teclas con Ctrl salvo Z e Y (Task 8), así que `Ctrl+E` nunca activa la elipse.

- [ ] **Step 7: Agregar a la compilación**

En `build.sh` y `build.bat`, línea del visor: agregar `src/viewer/export.cpp` (`src\viewer\export.cpp`).

- [ ] **Step 8: Compilar y verificar bajo wine**

Run: `./build.sh test && ./build.sh 2>&1 | grep -E "error"`
Expected: `79 pruebas, 0 fallidas`; sin errores.

Con `placa_agujero.stp`: medir una distancia, poner una nota, dibujar una nube (se crea "Vista 1"), girar la pieza y dibujar un rectángulo (se crea "Vista 2"). `ctrl+e`, en el diálogo escribir `/tmp/prueba/revision.pdf` (`xdotool type`), `Return`.

```sh
pdfinfo /tmp/prueba/revision.pdf | grep -E "Pages|Page size"
pdftoppm -r 40 -png /tmp/prueba/revision.pdf /tmp/capturas/pdf
```

Expected: `Pages: 4` (general, Vista 1, Vista 2, medidas y notas) y `Page size: 842 x 595 pts (A4)`. En las imágenes: la general muestra medida y nota pero no la nube ni el rectángulo; cada vista muestra su marca; la última página lista "1.  Distancia  … mm" y "2.  Nota: …". Luego exportar como PNG (`/tmp/prueba/vista.png`) y comprobar con `file` que es `PNG image data, 2000 x 1400` (el doble de la vista).

- [ ] **Step 9: Commit**

```bash
git add src/export/pdf_writer.h src/export/pdf_writer.cpp src/viewer/export.h src/viewer/export.cpp src/viewer/scene_view.h src/viewer/scene_view.cpp src/viewer/main.cpp build.sh build.bat tests/unit_tests.cpp
git commit -m "Export marked views to PNG and multi-page PDF

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_015NM5546gRGTfskkpAXPqTF"
```

---

### Task 12: Documentación, verificación completa y entrega

**Files:**
- Modify: `README.md`
- Test: todo lo anterior, de punta a punta.

**Interfaces:**
- Consumes: todo.
- Produces: README actualizado, binarios en `dist/`, zip para el usuario.

- [ ] **Step 1: README**

1a. Después de la sección "Planos 2D", agregar:

```markdown
## Medir y marcar

En el visor hay una barra de herramientas a la izquierda. Con ella se puede
**medir** y **marcar** una pieza o un plano, guardar las marcas junto al archivo
y exportar la revisión para mandarla a un compañero.

| Herramienta | Tecla | Cómo se usa |
|---|---|---|
| Distancia | `M` y `1` | Clic en dos puntos; se engancha a extremos, puntos medios y centros. Muestra el total y ΔX ΔY ΔZ |
| Radio y diámetro | `M` y `2` | Clic sobre un círculo, un arco o un agujero |
| Ángulo | `M` y `3` | Tres clics (vértice en el medio) o clic en dos líneas |
| Área y perímetro | `M` y `4` | Clic dentro de un contorno cerrado del plano o sobre una cara de la pieza |
| Resaltador | `H` | Arrastrar; `Q` cambia entre amarillo, verde y rosa |
| Subrayado | `U` | Arrastrar; con `Shift` sale horizontal o vertical |
| Nota con flecha | `N` | Clic en el punto a señalar, clic donde va el texto, escribir y `Enter` |
| Rectángulo, elipse, nube | `R`, `E`, `C` | Arrastrar de esquina a esquina |
| Lápiz | `L` | Trazo libre |
| Color | `Q` | Rojo, amarillo, verde, azul, negro |

- `Shift` mientras se mide: sin enganche. `Esc`: termina la herramienta.
- Clic en una marca la selecciona; `Supr` la borra; doble clic en una nota
  edita su texto. `Ctrl+Z` y `Ctrl+Y` deshacen y rehacen.
- En un plano 2D las marcas se ven siempre. En una pieza 3D, las medidas y
  las notas siguen a la pieza al girarla; los trazos y formas quedan en la
  vista en que se dibujaron ("Vista 1", "Vista 2"...): al girar se ocultan y
  `F2` abre la lista para volver a esa vista.
- Las marcas se guardan solas al cerrar o al abrir otro archivo (y con
  `Ctrl+S`) en `<archivo>.marcas`, al lado del modelo: si se copia la carpeta,
  las marcas van con ella. Si el modelo cambia después de marcarlo, se avisa.
- `Ctrl+E` exporta: **PDF** con la vista general, una página por cada vista
  marcada y la tabla de medidas y notas; o **PNG** de la vista actual.
- En el **panel de vista previa** se puede medir con `M` (sin barra); esas
  medidas no se guardan. Windows no le dice al panel dónde está el archivo, así
  que ahí no se ven las marcas guardadas.
```

1b. En la tabla de "Uso del visor y del panel", cambiar la fila `| Aristas | \`E\` |` por `| Aristas | \`A\` (en el panel también \`E\`) |` y agregar las filas:

```markdown
| Medir | `M` (luego `1`-`4`) |
| Marcar (solo visor) | `H` `U` `N` `R` `E` `C` `L`, color `Q` |
| Lista de marcas | `F2` |
| Guardar marcas / Exportar | `Ctrl+S` / `Ctrl+E` |
```

1c. En "Limitaciones conocidas", agregar:

```markdown
- Las marcas de trazo y forma en 3D pertenecen a la vista en que se dibujaron;
  no se proyectan sobre la superficie de la pieza.
- STL, OBJ y PLY no guardan círculos: en esos formatos se mide distancia,
  ángulo y área, pero no radio.
- El PDF usa Helvetica con codificación Windows: los caracteres fuera del
  alfabeto latino salen como `?`.
```

1d. En "Como esta hecho", agregar `src/export     escritor de PDF` y ampliar la línea de `src/engine` a `lector ISO 10303-21, geometria, mallado, NURBS, deteccion de planos, medicion y marcas`.

- [ ] **Step 2: Verificación completa**

Run: `./build.sh test`
Expected: `79 pruebas, 0 fallidas`, consulta de enganche < 5 ms.

Run: `./build.sh 2>&1 | grep -E "error|warning" | grep -v thumbtest`
Expected: sin salida.

Repetir, en una sola sesión de wine, los scripts de las Tasks 7, 8, 9, 10 y 11 y revisar cada captura contra lo esperado en cada tarea. Además:
- Miniatura sin cambios: `wine dist/thumbtest.exe dist/StepShellExt.dll tests/samples/plano_brida.dxf /tmp/capturas/mini.bmp 256` → igual a la de antes (hoja blanca con el plano).
- DXF grande (`python3` con ezdxf, 20 000 círculos, como en la verificación de planos 2D): abrir en el visor, `m`, mover el ratón por la vista: sin saltos; cronometrar la carga (`time` sobre `steprender` no incluye el índice; medir con la barra de estado, que aparece tras la carga): menos de 1,5 s más que antes.
- Benchmark de render: `benchrender tests/samples/placa_agujero.stp 900 40` antes (commit de partida) y después: diferencias dentro del ruido (±10 %).

- [ ] **Step 3: Commit del README y push**

```bash
git add README.md
git commit -m "Document measuring and markup tools

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_015NM5546gRGTfskkpAXPqTF"
git push -u origin main
```

- [ ] **Step 4: Entregar**

Armar `stp-viewer.zip` con `StepShellExt.dll`, `stpviewer.exe`, `instalar.bat`, `desinstalar.bat`, `diagnostico.bat` y la carpeta `ejemplos` (`plano_brida.dxf`, `pieza_3d.dxf`, `placa_agujero.stp`, más un `plano_brida.dxf.marcas` de ejemplo con una medida, una nota y una nube), y mandarlo al usuario con tres capturas: medidas en el plano, marcas en la pieza 3D y la primera página del PDF.
