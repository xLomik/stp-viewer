# OSNAP, restricciones e interfaz profesional — plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enganche a objetos por modos (OSNAP) con Orto/Polar para medir sin pelear (largo de ranura en 2 clics) y una interfaz de visor CAD profesional (cinta, cubo de vistas, panel lateral, barra de estado, tema grafito).

**Architecture:** El motor (`src/engine/measure.*`) gana modos de enganche, candidatos ordenados (`snapAll`), puntos dinámicos (intersección, extensión, perpendicular, tangente) y restricciones (`applyConstraint`); todo portable y probado en Linux. La lógica pura de interfaz (distribución de la cinta, cubo de vistas, recientes) vive en `src/ui/` y también se prueba en Linux. La capa Win32/GDI+ (`src/viewer/ui/`) dibuja tema, iconos, barra de título, cinta, barra de estado, panel, pantalla de inicio y cubo; `main.cpp` pasa a ser un marco que despacha comandos.

**Tech Stack:** C++17, mingw-w64 (cruzado), Win32 + GDI+ + DWM (dinámico), pruebas nativas con `./build.sh test`, verificación bajo wine + Xvfb + xdotool.

**Spec:** `docs/superpowers/specs/2026-09-25-osnap-e-interfaz-design.md`

## Global Constraints

- C++17; compilan `./build.sh test` (g++ Linux) y `./build.sh` (mingw-w64) sin avisos nuevos con `-Wall -Wextra`.
- Literales de código en ASCII: acentos con `\uXXXX` (`L"Intersección"`, `"Intersección"`); comentarios en español sin acentos, como el resto del código.
- Interfaz en español.
- Motor y `src/ui/` sin dependencias de Windows (se compilan en la lista `ENGINE` / `UI_CORE` de `build.sh` y `build.bat`).
- Colores: fondo `#1F2329`, paneles `#262B32`, bordes `#363C45`, texto `#E6E9ED`, secundario `#9AA3AD`, acento `#2F80ED`, hover `#2D333B`, presionado `#343B45`.
- Fuente Segoe UI con respaldo sans-serif genérica (`uiFont`), tamaños 9/10/12 pt escalados por DPI.
- Radio de captura 8 px escalado por DPI; prioridad Intersección > Extremo > Centro > Cuadrante > Medio > Perpendicular > Tangente > Extensión; "Más cercano" solo si no hay otro; empate = a menos de 1,5 px del más cercano.
- Modos por defecto: Extremo, Medio, Centro, Cuadrante, Intersección. Polar por defecto 45°, captura 3°. Orto y Polar se excluyen.
- Atajos: `F3` enganche, `F8` Orto, `F10` Polar, `Tab`/`Shift+Tab` candidatos, `Shift` apaga el enganche mientras se mantiene, `F2` panel.
- Rendimiento: enganche con todos los modos < 5 ms por consulta en el plano de 1,5 millones de segmentos.
- Configuración en `HKCU\Software\stp-viewer`; si falla, valores por defecto sin avisar. El panel del Explorador solo lee.
- Commits con las líneas `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` y `Claude-Session: https://claude.ai/code/session_015NM5546gRGTfskkpAXPqTF`. Se trabaja y se empuja en `main`.
- Tareas de interfaz Win32 (5 a 10): el encabezado de cada componente es el contrato; el cuerpo sigue el comportamiento descrito paso a paso y se verifica con capturas bajo wine (no hay pruebas nativas de pintura).

## Review Focus

1. Arcos girados o con barrido negativo (bulge negativo, INSERT espejado): cuadrantes, medio de arco e intersecciones deben caer dentro del arco real → prueba `snap_quadrants_respect_negative_sweep` (Tarea 1).
2. Rectas casi paralelas o que se tocan en un extremo compartido (esquinas de polilínea, uniones tangentes recta-arco): no deben dar Intersección ni Extensión espurias → pruebas `snap_polyline_corner_is_not_intersection` y `snap_tangent_joint_is_not_intersection` (Tarea 1).
3. Orto/Polar sin primer punto o con el cursor encima del primer punto (vector nulo): no aplican y no producen NaN → prueba `constraint_degenerate_inputs` (Tarea 2).
4. Ventana muy angosta o con DPI 150 %: la cinta se reduce sin solaparse → prueba `ribbon_layout_shrinks_right_to_left` y `ribbon_layout_never_overflows_when_possible` (Tarea 3).
5. Recientes con la misma ruta en distinta caja de letras o repetida: una sola entrada, la más nueva arriba, máximo 10 → prueba `recent_files_dedupe_case_insensitive` (Tarea 3).

---

## Mapa de archivos

| Archivo | Responsabilidad |
|---|---|
| `src/engine/measure.h/.cpp` | Modos, candidatos, puntos dinámicos, restricciones (modificar) |
| `src/ui/ribbon_layout.h/.cpp` | Tamaño de cada grupo de la cinta según el ancho (nuevo, portable) |
| `src/ui/view_cube_math.h/.cpp` | Región del cubo bajo el cursor y orientación de cámara (nuevo, portable) |
| `src/ui/recent_files.h/.cpp` | Lista de recientes (nuevo, portable) |
| `src/viewer/ui/theme.h/.cpp` | Paleta, fuentes, primitivas de dibujo GDI+ (nuevo) |
| `src/viewer/ui/icons.h/.cpp` | Iconos vectoriales (nuevo) |
| `src/viewer/ui/commands.h/.cpp` | Tabla de comandos: texto, atajo, ayuda, icono (nuevo) |
| `src/viewer/ui/tooltip.h/.cpp` | Tooltip enriquecido (nuevo) |
| `src/viewer/ui/popup_menu.h/.cpp` | Menú emergente con el tema (nuevo) |
| `src/viewer/ui/ribbon.h/.cpp` | Cinta con pestañas (nuevo) |
| `src/viewer/ui/status_bar.h/.cpp` | Barra de estado (nuevo) |
| `src/viewer/ui/side_panel.h/.cpp` | Panel Marcas / Propiedades (nuevo) |
| `src/viewer/ui/start_page.h/.cpp` | Pantalla de inicio (nuevo) |
| `src/viewer/ui/view_cube.h/.cpp` | Dibujo e interacción del cubo de vistas (nuevo) |
| `src/viewer/settings.h/.cpp` | Registro HKCU (nuevo) |
| `src/viewer/markup_tools.h/.cpp` | OSNAP en las herramientas, marcadores, rótulos (modificar) |
| `src/viewer/scene_view.h/.cpp` | Comandos de vista públicos, cubo, mini barra del panel (modificar) |
| `src/viewer/main.cpp` | Marco: barra de título, disposición, despacho de comandos (reescribir) |
| `src/viewer/toolbar.h/.cpp` | Se borra |
| `build.sh`, `build.bat` | Listas de fuentes |
| `tests/unit_tests.cpp` | Pruebas nuevas |
| `README.md` | Documentación |

---

### Task 1: Motor de enganche por modos

**Files:**
- Modify: `src/engine/measure.h`
- Modify: `src/engine/measure.cpp`
- Test: `tests/unit_tests.cpp` (sección "Seleccion y enganche")

**Interfaces:**
- Consumes: `Mesh`, `CircleFeature`, `Bvh`, `Camera`, `projectPoint`, `pixelRay` (existentes).
- Produces:
  - `enum class SnapKind { None, Endpoint, Center, Midpoint, OnEdge, OnFace, OnPlane, Quadrant, Intersection, Extension, Perpendicular, Tangent };` (`OnEdge` = "Más cercano")
  - `enum SnapMode : unsigned { kSnapEndpoint = 1u << 0, kSnapMidpoint = 1u << 1, kSnapCenter = 1u << 2, kSnapQuadrant = 1u << 3, kSnapIntersection = 1u << 4, kSnapExtension = 1u << 5, kSnapPerpendicular = 1u << 6, kSnapTangent = 1u << 7, kSnapNearest = 1u << 8 };`
  - `constexpr unsigned kSnapDefaultModes`, `constexpr unsigned kSnapAllModes = 0x1FF;`
  - `unsigned snapModeOf(SnapKind kind);` `const char* snapKindName(SnapKind kind);` (UTF-8)
  - `SnapResult` gana `double pixels`.
  - `SnapOptions { double radiusPixels = 8.0; bool snapping = true; unsigned modes = kSnapDefaultModes; const Vec3* from = nullptr; const PlanarInfo* plane = nullptr; }`
  - `std::vector<SnapResult> PickIndex::snapAll(const Mesh&, const Camera&, int width, int height, double sx, double sy, const SnapOptions&) const;`
  - `SnapResult PickIndex::surfaceAt(const Mesh&, const Camera&, int width, int height, double sx, double sy, const PlanarInfo* plane) const;`
  - `snap()` = primer candidato de `snapAll` o `surfaceAt`.

- [ ] **Step 1: Pruebas que fallan**

En `tests/unit_tests.cpp`, reemplazar `PlanScene::snapNear` por la versión con modos y agregar `snapAllNear`; cambiar `snap_on_circle_edge_has_no_fake_endpoints` (el punto a 0° ahora es un cuadrante) y agregar las pruebas nuevas después de `snap_can_be_disabled`:

```cpp
    stp::SnapResult snapNear(const stp::Vec3& world, double dx, double dy, bool snapping = true,
                             unsigned modes = stp::kSnapDefaultModes, const stp::Vec3* from = nullptr) {
        double sx = 0, sy = 0;
        stp::projectPoint(camera, width, height, world, &sx, &sy);
        stp::SnapOptions options;
        options.snapping = snapping;
        options.modes = modes;
        options.from = from;
        options.plane = &plane;
        return pick.snap(mesh, camera, width, height, sx + dx, sy + dy, options);
    }
    std::vector<stp::SnapResult> snapAllNear(const stp::Vec3& world, double dx, double dy,
                                             unsigned modes = stp::kSnapDefaultModes,
                                             const stp::Vec3* from = nullptr) {
        double sx = 0, sy = 0;
        stp::projectPoint(camera, width, height, world, &sx, &sy);
        stp::SnapOptions options;
        options.modes = modes;
        options.from = from;
        options.plane = &plane;
        return pick.snapAll(mesh, camera, width, height, sx + dx, sy + dy, options);
    }
```

```cpp
PlanScene sceneFrom(const std::string& text) {
    PlanScene scene;
    CHECK(loadDxfText(text, &scene.mesh));
    scene.finish();
    return scene;
}

bool hasKind(const std::vector<stp::SnapResult>& all, stp::SnapKind kind) {
    for (const stp::SnapResult& s : all) {
        if (s.kind == kind) return true;
    }
    return false;
}
```

```cpp
TEST(snap_on_circle_edge_has_no_fake_endpoints) {
    PlanScene scene = lineAndCircle();
    // Un punto del circulo a 30 grados: solo puede ser "mas cercano".
    const stp::Vec3 p(50 + 10 * std::cos(stp::kPi / 6), 40 + 10 * std::sin(stp::kPi / 6), 0);
    const stp::SnapResult edge = scene.snapNear(p, 0, 1, true, stp::kSnapDefaultModes | stp::kSnapNearest);
    CHECK(edge.kind == stp::SnapKind::OnEdge);
    CHECK(edge.segment >= 1);
    CHECK_NEAR(stp::distance(edge.point, stp::Vec3(50, 40, 0)), 10.0, 0.05);
    // Sin "mas cercano" no se ofrece nada: cae en el plano.
    CHECK(scene.snapNear(p, 0, 1).kind == stp::SnapKind::OnPlane);
}

TEST(snap_slot_length_and_width_from_quadrants) {
    PlanScene scene;
    CHECK(loadModelFile("tests/samples/plano_brida.dxf", &scene.mesh));
    scene.finish();
    const stp::SnapResult bottom = scene.snapNear(stp::Vec3(170, 35, 0), 2, 1);
    const stp::SnapResult top = scene.snapNear(stp::Vec3(170, 105, 0), -1, 2);
    CHECK(bottom.kind == stp::SnapKind::Quadrant);
    CHECK(top.kind == stp::SnapKind::Quadrant);
    CHECK_NEAR(stp::distance(bottom.point, top.point), 70.0, 1e-9);
    const stp::SnapResult left = scene.snapNear(stp::Vec3(160, 45, 0), 1, 1);
    const stp::SnapResult right = scene.snapNear(stp::Vec3(180, 45, 0), -1, 1);
    CHECK(left.kind == stp::SnapKind::Endpoint);
    CHECK(right.kind == stp::SnapKind::Endpoint);
    CHECK_NEAR(stp::distance(left.point, right.point), 20.0, 1e-9);
}

TEST(snap_tab_order_puts_endpoint_before_quadrant) {
    PlanScene scene;
    CHECK(loadModelFile("tests/samples/plano_brida.dxf", &scene.mesh));
    scene.finish();
    const std::vector<stp::SnapResult> all = scene.snapAllNear(stp::Vec3(160, 45, 0), 0, 0);
    CHECK(all.size() >= 2);
    if (all.size() >= 2) {
        CHECK(all[0].kind == stp::SnapKind::Endpoint);
        CHECK(all[1].kind == stp::SnapKind::Quadrant);
        CHECK_NEAR(stp::distance(all[1].point, stp::Vec3(160, 45, 0)), 0.0, 1e-9);
    }
    CHECK(!hasKind(all, stp::SnapKind::Intersection));
}

TEST(snap_arc_midpoint) {
    PlanScene scene = sceneFrom(entities({{0, "ARC"}, {10, "0"}, {20, "0"}, {40, "10"}, {50, "0"}, {51, "90"}}));
    const stp::Vec3 mid(10 * std::cos(stp::kPi / 4), 10 * std::sin(stp::kPi / 4), 0);
    const stp::SnapResult r = scene.snapNear(mid, 1, -1);
    CHECK(r.kind == stp::SnapKind::Midpoint);
    CHECK_NEAR(stp::distance(r.point, mid), 0.0, 1e-9);
}

TEST(snap_quadrants_respect_negative_sweep) {
    // Arco de 0 a 90 grados recorrido al reves (normal -Z): sigue cubriendo el
    // primer cuadrante del dibujo y no ofrece el cuadrante de 180 grados.
    PlanScene scene = sceneFrom(entities({{0, "ARC"}, {10, "0"}, {20, "0"}, {40, "10"}, {50, "90"},
                                          {51, "180"}, {210, "0"}, {220, "0"}, {230, "-1"}}));
    CHECK(scene.mesh.features.circles.size() == 1);
    // Con normal -Z, x del OCS es -X: el arco va de (0,10) a (10,0) por el primer cuadrante.
    const std::vector<stp::SnapResult> right = scene.snapAllNear(stp::Vec3(10, 0, 0), 0, 0, stp::kSnapQuadrant);
    CHECK(hasKind(right, stp::SnapKind::Quadrant));
    const std::vector<stp::SnapResult> left = scene.snapAllNear(stp::Vec3(-10, 0, 0), 0, 0, stp::kSnapQuadrant);
    CHECK(left.empty());
    const stp::SnapResult mid = scene.snapNear(stp::Vec3(7.0710678, 7.0710678, 0), 0, 0, true, stp::kSnapMidpoint);
    CHECK(mid.kind == stp::SnapKind::Midpoint);
    CHECK_NEAR(mid.point.x, 7.0710678, 1e-6);
}

TEST(snap_line_line_and_line_circle_intersections) {
    PlanScene scene = sceneFrom(entities({{0, "LINE"}, {10, "0"}, {20, "0"}, {11, "100"}, {21, "100"},
                                          {0, "LINE"}, {10, "0"}, {20, "100"}, {11, "100"}, {21, "0"},
                                          {0, "CIRCLE"}, {10, "50"}, {20, "140"}, {40, "10"},
                                          {0, "LINE"}, {10, "0"}, {20, "145"}, {11, "100"}, {21, "145"}}));
    const stp::SnapResult cross = scene.snapNear(stp::Vec3(50, 50, 0), 2, 1);
    CHECK(cross.kind == stp::SnapKind::Intersection);
    CHECK_NEAR(stp::distance(cross.point, stp::Vec3(50, 50, 0)), 0.0, 1e-9);
    const stp::Vec3 hit(50 + std::sqrt(75.0), 145, 0);
    const stp::SnapResult lc = scene.snapNear(hit, -1, 1);
    CHECK(lc.kind == stp::SnapKind::Intersection);
    CHECK_NEAR(stp::distance(lc.point, hit), 0.0, 1e-9);
}

TEST(snap_circle_circle_intersection) {
    PlanScene scene = sceneFrom(entities({{0, "CIRCLE"}, {10, "0"}, {20, "0"}, {40, "10"},
                                          {0, "CIRCLE"}, {10, "12"}, {20, "0"}, {40, "10"}}));
    const stp::Vec3 hit(6, 8, 0);
    const stp::SnapResult r = scene.snapNear(hit, 1, 1);
    CHECK(r.kind == stp::SnapKind::Intersection);
    CHECK_NEAR(stp::distance(r.point, hit), 0.0, 1e-9);
}

TEST(snap_polyline_corner_is_not_intersection) {
    PlanScene scene = sceneFrom(entities({{0, "LWPOLYLINE"}, {90, "4"}, {70, "1"},
                                          {10, "0"}, {20, "0"}, {10, "100"}, {20, "0"},
                                          {10, "100"}, {20, "50"}, {10, "0"}, {20, "50"}}));
    const std::vector<stp::SnapResult> all = scene.snapAllNear(stp::Vec3(100, 0, 0), 1, 1);
    CHECK(!all.empty() && all[0].kind == stp::SnapKind::Endpoint);
    CHECK(!hasKind(all, stp::SnapKind::Intersection));
}

TEST(snap_tangent_joint_is_not_intersection) {
    // Recta que sigue en un arco tangente (esquina redondeada): la union es un extremo.
    const double b = std::tan(stp::kPi / 8);
    PlanScene scene = sceneFrom(entities({{0, "LWPOLYLINE"}, {90, "3"}, {70, "0"},
                                          {10, "0"}, {20, "0"}, {10, "90"}, {20, "0"}, {42, std::to_string(b)},
                                          {10, "100"}, {20, "10"}}));
    const std::vector<stp::SnapResult> all = scene.snapAllNear(stp::Vec3(90, 0, 0), 0, 0);
    CHECK(!all.empty() && all[0].kind == stp::SnapKind::Endpoint);
    CHECK(!hasKind(all, stp::SnapKind::Intersection));
}

TEST(snap_sampled_arc_pieces_do_not_intersect) {
    PlanScene scene = sceneFrom(entities({{0, "CIRCLE"}, {10, "0"}, {20, "0"}, {40, "10"}}));
    const stp::Vec3 p(10 * std::cos(0.5), 10 * std::sin(0.5), 0);
    CHECK(scene.snapAllNear(p, 0, 0, stp::kSnapIntersection).empty());
}

TEST(snap_extension_finds_rounded_corner_vertex) {
    const double b = std::tan(stp::kPi / 8);
    PlanScene scene = sceneFrom(entities({{0, "LWPOLYLINE"}, {90, "4"}, {70, "0"},
                                          {10, "0"}, {20, "0"}, {10, "90"}, {20, "0"}, {42, std::to_string(b)},
                                          {10, "100"}, {20, "10"}, {10, "100"}, {20, "50"}}));
    const stp::SnapResult off = scene.snapNear(stp::Vec3(100, 0, 0), 1, 1);
    CHECK(off.kind == stp::SnapKind::OnPlane);  // Extension esta apagada por defecto
    const stp::SnapResult r = scene.snapNear(stp::Vec3(100, 0, 0), 1, 1, true,
                                             stp::kSnapDefaultModes | stp::kSnapExtension);
    CHECK(r.kind == stp::SnapKind::Extension);
    CHECK_NEAR(stp::distance(r.point, stp::Vec3(100, 0, 0)), 0.0, 1e-9);
}

TEST(snap_perpendicular_to_line_and_circle) {
    PlanScene scene = sceneFrom(entities({{0, "LINE"}, {10, "30"}, {20, "-50"}, {11, "30"}, {21, "50"},
                                          {0, "CIRCLE"}, {10, "80"}, {20, "0"}, {40, "10"}}));
    const stp::Vec3 from(10, 0, 0);
    const stp::SnapResult none = scene.snapNear(stp::Vec3(30, 20, 0), 0, 0, true, stp::kSnapPerpendicular);
    CHECK(none.kind == stp::SnapKind::OnPlane);  // sin primer punto no hay perpendicular
    const stp::SnapResult line = scene.snapNear(stp::Vec3(30, 20, 0), 1, 0, true, stp::kSnapPerpendicular, &from);
    CHECK(line.kind == stp::SnapKind::Perpendicular);
    CHECK_NEAR(stp::distance(line.point, stp::Vec3(30, 0, 0)), 0.0, 1e-9);
    const stp::Vec3 onCircle(80 - 10 * std::cos(0.6), 10 * std::sin(0.6), 0);
    const stp::SnapResult circle = scene.snapNear(onCircle, 0, 0, true, stp::kSnapPerpendicular, &from);
    CHECK(circle.kind == stp::SnapKind::Perpendicular);
    CHECK_NEAR(stp::distance(circle.point, stp::Vec3(70, 0, 0)), 0.0, 1e-9);
}

TEST(snap_tangent_from_first_point) {
    PlanScene scene = sceneFrom(entities({{0, "CIRCLE"}, {10, "0"}, {20, "0"}, {40, "5"},
                                          {0, "POINT"}, {10, "10"}, {20, "0"}}));
    const stp::Vec3 from(10, 0, 0);
    const stp::Vec3 tangent(2.5, 5 * std::sin(stp::kPi / 3), 0);
    const stp::SnapResult r = scene.snapNear(tangent, 1, 1, true, stp::kSnapTangent, &from);
    CHECK(r.kind == stp::SnapKind::Tangent);
    CHECK_NEAR(r.point.x, 2.5, 1e-9);
    CHECK_NEAR(r.point.y, 4.330127019, 1e-6);
}

TEST(snap_disabled_modes_are_not_offered) {
    PlanScene scene = lineAndCircle();
    CHECK(scene.snapAllNear(stp::Vec3(100, 0, 0), 0, 0, stp::kSnapMidpoint).empty());
    CHECK(scene.snapAllNear(stp::Vec3(50, 40, 0), 0, 0, stp::kSnapEndpoint).empty());
    CHECK(scene.snapNear(stp::Vec3(100, 0, 0), 0, 0, true, 0).kind == stp::SnapKind::OnPlane);
    CHECK(std::string(stp::snapKindName(stp::SnapKind::Quadrant)) == "Cuadrante");
    CHECK(stp::snapModeOf(stp::SnapKind::OnEdge) == stp::kSnapNearest);
}
```

Y una segunda prueba de rendimiento, después de `snap_is_fast_on_huge_drawing`, que reutiliza su escena (extraer la construcción a `PlanScene hugeScene()` en el `namespace` anónimo y usarla en las dos):

```cpp
TEST(snap_all_modes_is_fast_on_huge_drawing) {
    PlanScene scene = hugeScene();
    std::mt19937 random(11);
    std::uniform_real_distribution<double> px(0, scene.width), py(0, scene.height);
    const stp::Vec3 from(2500, 1500, 0);
    stp::SnapOptions options;
    options.plane = &scene.plane;
    options.modes = stp::kSnapAllModes;
    options.from = &from;
    const int queries = 300;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < queries; ++i) {
        scene.pick.snap(scene.mesh, scene.camera, scene.width, scene.height, px(random), py(random), options);
    }
    // Tambien con zoom: 20 veces mas cerca hay menos elementos pero mas grandes.
    scene.camera.orthoHeight /= 20;
    for (int i = 0; i < queries; ++i) {
        scene.pick.snap(scene.mesh, scene.camera, scene.width, scene.height, px(random), py(random), options);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double queryMs = std::chrono::duration<double, std::milli>(t1 - t0).count() / (2 * queries);
    std::printf("    todos los modos: consulta %.3f ms\n", queryMs);
    CHECK(queryMs < 5.0);
}
```

`loadModelFile(path, mesh)` es un ayudante nuevo del `namespace` anónimo de pruebas (junto a `loadDxfText`):

```cpp
bool loadModelFile(const char* path, stp::Mesh* mesh) {
    const std::string bytes = readText(path);
    const char* dot = std::strrchr(path, '.');
    std::string error;
    return stp::loadModel(bytes.data(), bytes.size(), dot ? dot : "", mesh, &error);
}
```

(si `readText` está definido más abajo que el ayudante, declarar `loadModelFile` justo después de `readText`; agregar `#include <cstring>`).

- [ ] **Step 2: Confirmar que fallan**

Run: `./build.sh test`
Expected: error de compilación (`snapAll`, `kSnapNearest`, `SnapKind::Quadrant` no existen).

- [ ] **Step 3: Encabezado**

En `src/engine/measure.h`, reemplazar desde `enum class SnapKind` hasta el final de `class PickIndex`:

```cpp
// OnEdge es "Mas cercano": el punto de una arista mas cercano al cursor.
enum class SnapKind { None, Endpoint, Center, Midpoint, OnEdge, OnFace, OnPlane,
                      Quadrant, Intersection, Extension, Perpendicular, Tangent };

// Modos de enganche (mascara de bits) que el usuario enciende y apaga.
enum SnapMode : unsigned {
    kSnapEndpoint = 1u << 0,
    kSnapMidpoint = 1u << 1,
    kSnapCenter = 1u << 2,
    kSnapQuadrant = 1u << 3,
    kSnapIntersection = 1u << 4,
    kSnapExtension = 1u << 5,
    kSnapPerpendicular = 1u << 6,
    kSnapTangent = 1u << 7,
    kSnapNearest = 1u << 8,
};
constexpr unsigned kSnapDefaultModes =
    kSnapEndpoint | kSnapMidpoint | kSnapCenter | kSnapQuadrant | kSnapIntersection;
constexpr unsigned kSnapAllModes = 0x1FF;

// Modo que ofrece ese tipo de enganche; 0 para cara, plano y ninguno.
unsigned snapModeOf(SnapKind kind);
// Nombre para mostrar, en UTF-8 ("Cuadrante"); "" para cara, plano y ninguno.
const char* snapKindName(SnapKind kind);

struct SnapResult {
    SnapKind kind = SnapKind::None;
    Vec3 point;
    int triangle = -1;  // triangulo bajo el cursor, si lo hay
    int segment = -1;   // segmento del que sale el punto, si lo hay
    int circle = -1;    // circulo o arco del que sale el punto, si lo hay
    double pixels = 0.0;  // distancia al cursor (a su elemento en Perpendicular y Tangente)
};

struct SnapOptions {
    double radiusPixels = 8.0;
    bool snapping = true;  // Shift y F3 lo apagan
    unsigned modes = kSnapDefaultModes;
    const Vec3* from = nullptr;  // primer punto de la medida: habilita Perpendicular y Tangente
    const PlanarInfo* plane = nullptr;
};

class PickIndex {
public:
    void build(const Mesh& mesh);
    bool empty() const { return m_triangles.empty() && m_segments.empty(); }
    bool raycast(const Mesh& mesh, const Ray& ray, double* t, int* triangle) const;
    // Mejor candidato de snapAll o, si no hay, el punto de la cara o del plano.
    SnapResult snap(const Mesh& mesh, const Camera& camera, int width, int height, double sx,
                    double sy, const SnapOptions& options) const;
    // Candidatos de enganche bajo el cursor, del mejor al peor (Tab los recorre).
    std::vector<SnapResult> snapAll(const Mesh& mesh, const Camera& camera, int width, int height,
                                    double sx, double sy, const SnapOptions& options) const;
    // Punto de la cara bajo el cursor (3D) o del plano del dibujo (2D), sin enganche.
    SnapResult surfaceAt(const Mesh& mesh, const Camera& camera, int width, int height, double sx,
                         double sy, const PlanarInfo* plane) const;
    // Triangulo que contiene point (a menos de tolerance de su plano), o -1.
    int triangleAt(const Mesh& mesh, const Vec3& point, double tolerance) const;

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
    Bvh m_circles;
    std::vector<SnapPoint> m_snapPoints;
    double m_size = 1.0;
};
```

- [ ] **Step 4: Ayudantes geométricos**

En `src/engine/measure.cpp`, dentro del primer `namespace {` (después de `closestOnSegment`), agregar; y en `PointKey` agregar el campo `int kind` al final, a la comparación y al hash (`^ k.kind * 2654435761LL`):

```cpp
int snapPriority(SnapKind kind) {
    switch (kind) {
        case SnapKind::Intersection: return 0;
        case SnapKind::Endpoint: return 1;
        case SnapKind::Center: return 2;
        case SnapKind::Quadrant: return 3;
        case SnapKind::Midpoint: return 4;
        case SnapKind::Perpendicular: return 5;
        case SnapKind::Tangent: return 6;
        case SnapKind::Extension: return 7;
        default: return 8;  // Mas cercano
    }
}

// true si p (sobre el circulo) cae dentro del barrido del arco, con tolerance
// en unidades del modelo medida a lo largo de la curva.
bool inArc(const CircleFeature& c, const Vec3& p, double tolerance) {
    if (c.full()) return true;
    const Vec3 d = p - c.center;
    const Vec3 inPlane = d - c.normal * dot(d, c.normal);
    if (length(inPlane) < 1e-300) return false;
    const Vec3 y = cross(c.normal, c.xAxis);
    double a = std::atan2(dot(inPlane, y), dot(inPlane, c.xAxis)) - c.startAngle;
    if (c.sweep < 0) a = -a;
    a = std::fmod(a, 2 * kPi);
    if (a < 0) a += 2 * kPi;
    const double slack = c.radius > 0 ? tolerance / c.radius : 0.0;
    return a <= std::fabs(c.sweep) + slack || a >= 2 * kPi - slack;
}

// Ejes del plano del circulo para los cuadrantes: X del mundo proyectado (o Y
// si X es perpendicular al plano) y su giro de 90 grados.
void quadrantAxes(const CircleFeature& c, Vec3* e1, Vec3* e2) {
    const Vec3 n = normalize(c.normal);
    Vec3 ref = Vec3(1, 0, 0) - n * n.x;
    if (length(ref) < 1e-6) ref = Vec3(0, 1, 0) - n * n.y;
    *e1 = normalize(ref);
    *e2 = cross(n, *e1);
}

// Punto del circulo (o del arco) mas cercano al rayo. false si el circulo se ve de canto.
bool closestOnCircle(const CircleFeature& c, const Ray& ray, Vec3* out) {
    const double den = dot(ray.dir, c.normal);
    if (std::fabs(den) < 1e-9) return false;
    const Vec3 p = ray.origin + ray.dir * (dot(c.center - ray.origin, c.normal) / den);
    Vec3 d = p - c.center;
    d = d - c.normal * dot(d, c.normal);
    if (length(d) < 1e-300) return false;
    Vec3 q = c.center + normalize(d) * c.radius;
    if (!inArc(c, q, 0.0)) {
        const Vec3 s = c.pointAt(c.startAngle), e = c.pointAt(c.startAngle + c.sweep);
        q = distance(q, s) < distance(q, e) ? s : e;
    }
    *out = q;
    return true;
}

// Cruce de las rectas p0 + s*u y q0 + t*v. false si son paralelas o no se tocan.
bool crossLines(const Vec3& p0, const Vec3& u, const Vec3& q0, const Vec3& v, double tolerance,
                double* s, double* t) {
    const double A = dot(u, u), B = dot(u, v), C = dot(v, v);
    const double den = A * C - B * B;
    if (A < 1e-300 || C < 1e-300 || den <= 1e-12 * A * C) return false;
    const Vec3 w = p0 - q0;
    const double D = dot(u, w), E = dot(v, w);
    *s = (B * E - C * D) / den;
    *t = (A * E - B * D) / den;
    return distance(p0 + u * *s, q0 + v * *t) <= tolerance;
}

// Cruces del tramo ab con el circulo (o arco) c en su plano. Devuelve cuantos.
int segmentCircle(const Vec3& a, const Vec3& b, const CircleFeature& c, double tolerance, Vec3 out[2]) {
    if (std::fabs(dot(a - c.center, c.normal)) > tolerance ||
        std::fabs(dot(b - c.center, c.normal)) > tolerance) {
        return 0;
    }
    const Vec3 d = b - a;
    const double A = dot(d, d);
    if (A < 1e-300) return 0;
    const double length = std::sqrt(A);
    const double s0 = dot(c.center - a, d) / A;
    const double h = distance(a + d * s0, c.center);
    if (h > c.radius + tolerance) return 0;
    const double half = std::sqrt(std::max(0.0, c.radius * c.radius - h * h)) / length;
    const double slack = tolerance / length;
    const double roots[2] = {s0 - half, s0 + half};
    int count = 0;
    for (int k = 0; k < (half * length < tolerance ? 1 : 2); ++k) {
        const double s = roots[k];
        if (s < -slack || s > 1 + slack) continue;
        const Vec3 p = a + d * s;
        if (inArc(c, p, tolerance)) out[count++] = p;
    }
    return count;
}

// Cruces de dos circulos (o arcos) del mismo plano. Devuelve cuantos.
int circleCircle(const CircleFeature& c1, const CircleFeature& c2, double tolerance, Vec3 out[2]) {
    if (length(cross(c1.normal, c2.normal)) > 1e-9 ||
        std::fabs(dot(c2.center - c1.center, c1.normal)) > tolerance) {
        return 0;
    }
    const Vec3 between = c2.center - c1.center;
    const double d = length(between);
    if (d < tolerance || d > c1.radius + c2.radius + tolerance ||
        d < std::fabs(c1.radius - c2.radius) - tolerance) {
        return 0;
    }
    const Vec3 e = between * (1.0 / d);
    const double a = (c1.radius * c1.radius - c2.radius * c2.radius + d * d) / (2 * d);
    const double h = std::sqrt(std::max(0.0, c1.radius * c1.radius - a * a));
    const Vec3 base = c1.center + e * a;
    const Vec3 side = normalize(cross(c1.normal, e));
    const Vec3 points[2] = {base + side * h, base - side * h};
    int count = 0;
    for (int k = 0; k < (h < tolerance ? 1 : 2); ++k) {
        if (inArc(c1, points[k], tolerance) && inArc(c2, points[k], tolerance)) out[count++] = points[k];
    }
    return count;
}
```

- [ ] **Step 5: Puntos fijos nuevos en `build`**

En `PickIndex::build`: la clave de `add` incluye el tipo (`PointKey{..., static_cast<int>(kind)}`); en el bucle de círculos agregar los cuadrantes y el medio de arco, y construir `m_circles`:

```cpp
    for (std::size_t i = 0; i < mesh.features.circles.size(); ++i) {
        const CircleFeature& c = mesh.features.circles[i];
        const int index = static_cast<int>(i);
        add(c.center, SnapKind::Center, -1, index);
        if (!c.full()) {
            add(c.pointAt(c.startAngle), SnapKind::Endpoint, -1, index);
            add(c.pointAt(c.startAngle + c.sweep), SnapKind::Endpoint, -1, index);
            add(c.pointAt(c.startAngle + c.sweep * 0.5), SnapKind::Midpoint, -1, index);
        }
        Vec3 e1, e2;
        quadrantAxes(c, &e1, &e2);
        const Vec3 quadrants[4] = {c.center + e1 * c.radius, c.center + e2 * c.radius,
                                   c.center - e1 * c.radius, c.center - e2 * c.radius};
        for (const Vec3& q : quadrants) {
            if (inArc(c, q, m_size * 1e-9)) add(q, SnapKind::Quadrant, -1, index);
        }
    }
    m_circles.build(mesh.features.circles.size(), [&](std::size_t i, Vec3* lo, Vec3* hi) {
        const CircleFeature& c = mesh.features.circles[i];
        const Vec3 r(c.radius, c.radius, c.radius);
        *lo = c.center - r;
        *hi = c.center + r;
    });
```

- [ ] **Step 6: `snapModeOf`, `snapKindName`, `surfaceAt`, `snapAll`, `snap`**

Reemplazar `PickIndex::snap` completo por:

```cpp
unsigned snapModeOf(SnapKind kind) {
    switch (kind) {
        case SnapKind::Endpoint: return kSnapEndpoint;
        case SnapKind::Midpoint: return kSnapMidpoint;
        case SnapKind::Center: return kSnapCenter;
        case SnapKind::Quadrant: return kSnapQuadrant;
        case SnapKind::Intersection: return kSnapIntersection;
        case SnapKind::Extension: return kSnapExtension;
        case SnapKind::Perpendicular: return kSnapPerpendicular;
        case SnapKind::Tangent: return kSnapTangent;
        case SnapKind::OnEdge: return kSnapNearest;
        default: return 0;
    }
}

const char* snapKindName(SnapKind kind) {
    switch (kind) {
        case SnapKind::Endpoint: return "Extremo";
        case SnapKind::Midpoint: return "Punto medio";
        case SnapKind::Center: return "Centro";
        case SnapKind::Quadrant: return "Cuadrante";
        case SnapKind::Intersection: return "Intersección";
        case SnapKind::Extension: return "Extensión";
        case SnapKind::Perpendicular: return "Perpendicular";
        case SnapKind::Tangent: return "Tangente";
        case SnapKind::OnEdge: return "Más cercano";
        default: return "";
    }
}

SnapResult PickIndex::surfaceAt(const Mesh& mesh, const Camera& camera, int width, int height,
                                double sx, double sy, const PlanarInfo* plane) const {
    SnapResult result;
    const Ray ray = pixelRay(camera, width, height, sx, sy);
    double hitT = 0.0;
    int hitTriangle = -1;
    if (raycast(mesh, ray, &hitT, &hitTriangle)) {
        result.kind = SnapKind::OnFace;
        result.triangle = hitTriangle;
        result.point = ray.origin + ray.dir * hitT;
        return result;
    }
    if (plane && plane->planar) {
        const double denominator = dot(ray.dir, plane->normal);
        if (std::fabs(denominator) > 1e-12) {
            const double t = dot(plane->center - ray.origin, plane->normal) / denominator;
            result.kind = SnapKind::OnPlane;
            result.point = ray.origin + ray.dir * t;
        }
    }
    return result;
}

SnapResult PickIndex::snap(const Mesh& mesh, const Camera& camera, int width, int height,
                           double sx, double sy, const SnapOptions& options) const {
    const std::vector<SnapResult> all = snapAll(mesh, camera, width, height, sx, sy, options);
    if (!all.empty()) return all.front();
    return surfaceAt(mesh, camera, width, height, sx, sy, options.plane);
}

std::vector<SnapResult> PickIndex::snapAll(const Mesh& mesh, const Camera& camera, int width,
                                           int height, double sx, double sy,
                                           const SnapOptions& options) const {
    std::vector<SnapResult> found;
    const unsigned modes = options.modes;
    if (!options.snapping || modes == 0) return found;
    const Ray ray = pixelRay(camera, width, height, sx, sy);
    double hitT = 0.0;
    int hitTriangle = -1;
    const int triangle = raycast(mesh, ray, &hitT, &hitTriangle) ? hitTriangle : -1;
    const double R = options.radiusPixels;
    const double tolerance = m_size * 1e-7;

    // Un punto tapado por la pieza no se engancha. Se prueba con el rayo que pasa
    // justo por el punto, no con el del cursor: cerca de una cara vista de canto el
    // rayo del cursor la toca bastante antes que una esquina que si se ve.
    const double occlusion = m_size * 1e-4;
    auto visible = [&](const Vec3& p) {
        double px, py;
        if (!projectPoint(camera, width, height, p, &px, &py)) return false;
        const Ray through = pixelRay(camera, width, height, px, py);
        double t = 0.0;
        int hidden = -1;
        if (!raycast(mesh, through, &t, &hidden)) return true;
        return dot(p - through.origin, through.dir) <= t + occlusion;
    };
    auto pixels = [&](const Vec3& p, double* d) {
        double px, py;
        if (!projectPoint(camera, width, height, p, &px, &py)) return false;
        *d = std::hypot(px - sx, py - sy);
        return true;
    };
    // Cajas que pasan a menos de factor * R pixeles del rayo del cursor.
    auto near = [&](double factor) {
        return [&, factor](const Vec3& lo, const Vec3& hi) {
            const Vec3 center = (lo + hi) * 0.5;
            const double t = std::max(0.0, dot(center - ray.origin, ray.dir)) + length(hi - lo) * 0.5;
            const double r = factor * R * worldPerPixel(camera, height, t);
            return rayHitsBox(ray, lo - Vec3(r, r, r), hi + Vec3(r, r, r));
        };
    };
    auto offer = [&](SnapKind kind, const Vec3& p, double d, int segment, int circle) {
        if (!(modes & snapModeOf(kind))) return false;
        for (SnapResult& s : found) {
            if (s.kind == kind && distance(s.point, p) <= tolerance) {
                s.pixels = std::min(s.pixels, d);
                return true;
            }
        }
        if (!visible(p)) return false;
        SnapResult r;
        r.kind = kind;
        r.point = p;
        r.triangle = triangle;
        r.segment = segment;
        r.circle = circle;
        r.pixels = d;
        found.push_back(r);
        return true;
    };
    auto straight = [&](std::size_t i) { return i >= mesh.edgeCurve.size() || !mesh.edgeCurve[i]; };

    // Puntos fijos: extremos, medios, centros y cuadrantes.
    if (modes & (kSnapEndpoint | kSnapMidpoint | kSnapCenter | kSnapQuadrant)) {
        m_points.query(near(1.0), [&](std::uint32_t i) {
            const SnapPoint& s = m_snapPoints[i];
            double d;
            if (!(modes & snapModeOf(s.kind)) || !pixels(s.point, &d) || d > R) return;
            offer(s.kind, s.point, d, s.segment, s.circle);
        });
    }

    // Elementos bajo el cursor, del mas cercano al mas lejano.
    struct Near {
        int index;
        double pixels;
        Vec3 point;
    };
    auto byPixels = [](const Near& a, const Near& b) { return a.pixels < b.pixels; };
    std::vector<Near> segments, circles;
    if (modes & (kSnapIntersection | kSnapPerpendicular | kSnapNearest)) {
        m_segments.query(near(1.0), [&](std::uint32_t i) {
            const Vec3 q = closestOnSegment(mesh.edgeLines[2 * i], mesh.edgeLines[2 * i + 1], ray);
            double d;
            if (pixels(q, &d) && d <= R) segments.push_back({static_cast<int>(i), d, q});
        });
        std::sort(segments.begin(), segments.end(), byPixels);
    }
    if (modes & (kSnapIntersection | kSnapPerpendicular | kSnapTangent)) {
        m_circles.query(near(1.0), [&](std::uint32_t i) {
            Vec3 q;
            double d;
            if (closestOnCircle(mesh.features.circles[i], ray, &q) && pixels(q, &d) && d <= R) {
                circles.push_back({static_cast<int>(i), d, q});
            }
        });
        std::sort(circles.begin(), circles.end(), byPixels);
        if (circles.size() > 16) circles.resize(16);
    }
    std::vector<Near> lines;
    for (const Near& n : segments) {
        if (straight(static_cast<std::size_t>(n.index)) && lines.size() < 32) lines.push_back(n);
    }
    auto lineA = [&](const Near& n) { return mesh.edgeLines[2 * n.index]; };
    auto lineB = [&](const Near& n) { return mesh.edgeLines[2 * n.index + 1]; };
    auto arcEnds = [&](const CircleFeature& c, const Vec3& p) {
        return !c.full() && (distance(p, c.pointAt(c.startAngle)) <= tolerance ||
                             distance(p, c.pointAt(c.startAngle + c.sweep)) <= tolerance);
    };
    auto offerCross = [&](const Vec3& p, int segment, int circle) {
        double d;
        if (pixels(p, &d) && d <= R) offer(SnapKind::Intersection, p, d, segment, circle);
    };

    if (modes & kSnapIntersection) {
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const Vec3 a1 = lineA(lines[i]), b1 = lineB(lines[i]);
            for (std::size_t j = i + 1; j < lines.size(); ++j) {
                const Vec3 a2 = lineA(lines[j]), b2 = lineB(lines[j]);
                double s, t;
                if (!crossLines(a1, b1 - a1, a2, b2 - a2, tolerance, &s, &t)) continue;
                const double e1 = tolerance / std::max(1e-300, distance(a1, b1));
                const double e2 = tolerance / std::max(1e-300, distance(a2, b2));
                if (s < -e1 || s > 1 + e1 || t < -e2 || t > 1 + e2) continue;
                const bool end1 = s <= e1 || s >= 1 - e1, end2 = t <= e2 || t >= 1 - e2;
                if (end1 && end2) continue;  // esquina de polilinea: ya es un extremo
                offerCross(a1 + (b1 - a1) * s, lines[i].index, -1);
            }
            for (const Near& c : circles) {
                const CircleFeature& circle = mesh.features.circles[c.index];
                Vec3 hits[2];
                const int count = segmentCircle(a1, b1, circle, tolerance, hits);
                for (int k = 0; k < count; ++k) {
                    const bool lineEnd = distance(hits[k], a1) <= tolerance || distance(hits[k], b1) <= tolerance;
                    if (lineEnd && arcEnds(circle, hits[k])) continue;  // union recta-arco
                    offerCross(hits[k], lines[i].index, c.index);
                }
            }
        }
        for (std::size_t i = 0; i < circles.size(); ++i) {
            const CircleFeature& c1 = mesh.features.circles[circles[i].index];
            for (std::size_t j = i + 1; j < circles.size(); ++j) {
                const CircleFeature& c2 = mesh.features.circles[circles[j].index];
                Vec3 hits[2];
                const int count = circleCircle(c1, c2, tolerance, hits);
                for (int k = 0; k < count; ++k) {
                    if (arcEnds(c1, hits[k]) && arcEnds(c2, hits[k])) continue;  // union arco-arco
                    offerCross(hits[k], -1, circles[i].index);
                }
            }
        }
    }

    if (options.from && (modes & kSnapPerpendicular)) {
        const Vec3& from = *options.from;
        for (const Near& n : lines) {
            const Vec3 a = lineA(n), u = lineB(n) - a;
            const double A = dot(u, u);
            if (A < 1e-300) continue;
            const Vec3 foot = a + u * (dot(from - a, u) / A);
            if (distance(foot, from) > tolerance) offer(SnapKind::Perpendicular, foot, n.pixels, n.index, -1);
        }
        for (const Near& n : circles) {
            const CircleFeature& c = mesh.features.circles[n.index];
            Vec3 d = from - c.center;
            d = d - c.normal * dot(d, c.normal);
            if (length(d) <= tolerance) continue;
            const Vec3 dir = normalize(d);
            const Vec3 feet[2] = {c.center + dir * c.radius, c.center - dir * c.radius};
            int best = -1;
            double bestPixels = 1e300;
            for (int k = 0; k < 2; ++k) {
                double px;
                if (inArc(c, feet[k], tolerance) && pixels(feet[k], &px) && px < bestPixels) {
                    bestPixels = px;
                    best = k;
                }
            }
            if (best >= 0) offer(SnapKind::Perpendicular, feet[best], n.pixels, -1, n.index);
        }
    }

    if (options.from && (modes & kSnapTangent)) {
        for (const Near& n : circles) {
            const CircleFeature& c = mesh.features.circles[n.index];
            const Vec3 fromInPlane = *options.from - c.normal * dot(*options.from - c.center, c.normal);
            const Vec3 v = fromInPlane - c.center;
            const double dist = length(v);
            if (dist <= c.radius + tolerance) continue;
            const Vec3 e = v * (1.0 / dist), side = cross(c.normal, e);
            const double cosA = c.radius / dist, sinA = std::sqrt(std::max(0.0, 1 - cosA * cosA));
            const Vec3 touch[2] = {c.center + (e * cosA + side * sinA) * c.radius,
                                   c.center + (e * cosA - side * sinA) * c.radius};
            int best = -1;
            double bestPixels = 1e300;
            for (int k = 0; k < 2; ++k) {
                double px;
                if (inArc(c, touch[k], tolerance) && pixels(touch[k], &px) && px < bestPixels) {
                    bestPixels = px;
                    best = k;
                }
            }
            if (best >= 0) offer(SnapKind::Tangent, touch[best], n.pixels, -1, n.index);
        }
    }

    if (modes & kSnapExtension) {
        // En una esquina redondeada las rectas terminan lejos del vertice: se buscan
        // hasta 12 radios, pero solo las que, prolongadas, pasan bajo el cursor.
        std::vector<Near> reach;
        m_segments.query(near(12.0), [&](std::uint32_t i) {
            if (!straight(i)) return;
            double ax, ay, bx, by;
            if (!projectPoint(camera, width, height, mesh.edgeLines[2 * i], &ax, &ay) ||
                !projectPoint(camera, width, height, mesh.edgeLines[2 * i + 1], &bx, &by)) {
                return;
            }
            const double lx = bx - ax, ly = by - ay, len = std::hypot(lx, ly);
            if (len < 1e-9 || std::fabs((sx - ax) * ly - (sy - ay) * lx) / len > R) return;
            const double s = std::max(0.0, std::min(1.0, ((sx - ax) * lx + (sy - ay) * ly) / (len * len)));
            const double d = std::hypot(ax + lx * s - sx, ay + ly * s - sy);
            if (d <= 12.0 * R) reach.push_back({static_cast<int>(i), d, Vec3()});
        });
        std::sort(reach.begin(), reach.end(), byPixels);
        if (reach.size() > 24) reach.resize(24);
        for (std::size_t i = 0; i < reach.size(); ++i) {
            const Vec3 a1 = lineA(reach[i]), b1 = lineB(reach[i]);
            for (std::size_t j = i + 1; j < reach.size(); ++j) {
                const Vec3 a2 = lineA(reach[j]), b2 = lineB(reach[j]);
                double s, t;
                if (!crossLines(a1, b1 - a1, a2, b2 - a2, tolerance, &s, &t)) continue;
                const double e1 = tolerance / std::max(1e-300, distance(a1, b1));
                const double e2 = tolerance / std::max(1e-300, distance(a2, b2));
                const bool inside = s >= -e1 && s <= 1 + e1 && t >= -e2 && t <= 1 + e2;
                if (inside) continue;  // es un cruce de verdad o un extremo
                const Vec3 p = a1 + (b1 - a1) * s;
                double d;
                if (pixels(p, &d) && d <= R) offer(SnapKind::Extension, p, d, reach[i].index, -1);
            }
        }
    }

    if (modes & kSnapNearest) {
        for (const Near& n : segments) {
            if (offer(SnapKind::OnEdge, n.point, n.pixels, n.index, -1)) break;
        }
    }

    // "Mas cercano" solo si no hay nada mejor; empate a 1,5 px: gana la prioridad.
    const bool other = std::any_of(found.begin(), found.end(),
                                   [](const SnapResult& s) { return s.kind != SnapKind::OnEdge; });
    if (other) {
        found.erase(std::remove_if(found.begin(), found.end(),
                                   [](const SnapResult& s) { return s.kind == SnapKind::OnEdge; }),
                    found.end());
    }
    double best = 1e300;
    for (const SnapResult& s : found) best = std::min(best, s.pixels);
    std::stable_sort(found.begin(), found.end(), [&](const SnapResult& a, const SnapResult& b) {
        const bool farA = a.pixels > best + 1.5, farB = b.pixels > best + 1.5;
        if (farA != farB) return !farA;
        const int pa = snapPriority(a.kind), pb = snapPriority(b.kind);
        if (pa != pb) return pa < pb;
        return a.pixels < b.pixels;
    });
    return found;
}
```

- [ ] **Step 7: Correr las pruebas**

Run: `./build.sh test`
Expected: todas pasan (83 anteriores + 14 nuevas). Si `snap_slot_length_and_width_from_quadrants` falla por un candidato de otra entidad del plano (cotas), revisar con `printf` qué tipo y punto gana y ajustar el desplazamiento del cursor de la prueba, no la prioridad.

- [ ] **Step 8: Commit**

```bash
git add src/engine/measure.h src/engine/measure.cpp tests/unit_tests.cpp
git commit -m "Add object snap modes: quadrant, intersection, extension, perpendicular, tangent"
```

---

### Task 2: Restricciones Orto y Polar

**Files:**
- Modify: `src/engine/measure.h`, `src/engine/measure.cpp`
- Test: `tests/unit_tests.cpp`

**Interfaces:**
- Consumes: `Camera::right()`, `Camera::up()`, `PlanarInfo`.
- Produces:
  - `enum class ConstraintKind { None, Ortho, Polar };`
  - `struct SnapConstraint { ConstraintKind kind = ConstraintKind::None; double polarStepDegrees = 45.0; };`
  - `struct ConstrainedPoint { Vec3 point; bool applied = false; const char* axisName = nullptr; double angleDegrees = 0.0; };` (`axisName`: `"Horizontal"`, `"Vertical"`, `"X"`, `"Y"`, `"Z"` para Orto; `nullptr` en Polar)
  - `ConstrainedPoint applyConstraint(const Vec3& from, const Vec3& to, const SnapConstraint& constraint, const Camera& camera, const PlanarInfo* plane);`
  - `const char* distanceAxisName(const Vec3& a, const Vec3& b, const PlanarInfo* plane, double tolerance);` — nombre del eje si la distancia a→b está sobre un solo eje (para rotular una cota ya guardada), si no `nullptr`.

- [ ] **Step 1: Pruebas que fallan**

```cpp
// --- Restricciones -------------------------------------------------------------------

namespace {
stp::PlanarInfo xyPlane() {
    stp::PlanarInfo plane;
    plane.planar = true;
    return plane;  // u = X, v = Y, normal = Z
}
}  // namespace

TEST(constraint_ortho_in_plan_is_horizontal_or_vertical) {
    const stp::PlanarInfo plane = xyPlane();
    stp::Camera camera;
    camera.fitPlanar(plane, 1.0);
    stp::SnapConstraint ortho;
    ortho.kind = stp::ConstraintKind::Ortho;
    const stp::ConstrainedPoint h = stp::applyConstraint(stp::Vec3(0, 0, 0), stp::Vec3(10, 3, 0), ortho, camera, &plane);
    CHECK(h.applied);
    CHECK_NEAR(stp::distance(h.point, stp::Vec3(10, 0, 0)), 0.0, 1e-12);
    CHECK(std::string(h.axisName) == "Horizontal");
    const stp::ConstrainedPoint v = stp::applyConstraint(stp::Vec3(1, 1, 0), stp::Vec3(3, 10, 0), ortho, camera, &plane);
    CHECK_NEAR(stp::distance(v.point, stp::Vec3(1, 10, 0)), 0.0, 1e-12);
    CHECK(std::string(v.axisName) == "Vertical");
}

TEST(constraint_ortho_in_3d_follows_screen_axis) {
    stp::Camera camera;  // isometrica: Z hacia arriba en pantalla
    stp::SnapConstraint ortho;
    ortho.kind = stp::ConstraintKind::Ortho;
    const stp::ConstrainedPoint z = stp::applyConstraint(stp::Vec3(0, 0, 0), stp::Vec3(0.2, 0.1, 5), ortho, camera, nullptr);
    CHECK(z.applied);
    CHECK(std::string(z.axisName) == "Z");
    CHECK_NEAR(stp::distance(z.point, stp::Vec3(0, 0, 5)), 0.0, 1e-12);
    const stp::ConstrainedPoint x = stp::applyConstraint(stp::Vec3(0, 0, 0), stp::Vec3(6, 0.3, 0.2), ortho, camera, nullptr);
    CHECK(std::string(x.axisName) == "X");
    CHECK_NEAR(stp::distance(x.point, stp::Vec3(6, 0, 0)), 0.0, 1e-12);
}

TEST(constraint_polar_snaps_near_multiples) {
    const stp::PlanarInfo plane = xyPlane();
    stp::Camera camera;
    camera.fitPlanar(plane, 1.0);
    stp::SnapConstraint polar;
    polar.kind = stp::ConstraintKind::Polar;
    polar.polarStepDegrees = 45;
    const stp::ConstrainedPoint p = stp::applyConstraint(stp::Vec3(0, 0, 0), stp::Vec3(10, 9.5, 0), polar, camera, &plane);
    CHECK(p.applied);
    CHECK_NEAR(p.angleDegrees, 45.0, 1e-9);
    CHECK_NEAR(p.point.x, p.point.y, 1e-9);
    CHECK_NEAR(p.point.x, (10 + 9.5) / std::sqrt(2.0) / std::sqrt(2.0), 1e-9);
    const stp::ConstrainedPoint free = stp::applyConstraint(stp::Vec3(0, 0, 0), stp::Vec3(10, 6, 0), polar, camera, &plane);
    CHECK(!free.applied);
    CHECK_NEAR(stp::distance(free.point, stp::Vec3(10, 6, 0)), 0.0, 1e-12);
    polar.polarStepDegrees = 30;
    const stp::ConstrainedPoint down = stp::applyConstraint(stp::Vec3(0, 0, 0), stp::Vec3(-10, -0.3, 0), polar, camera, &plane);
    CHECK(down.applied);
    CHECK_NEAR(down.angleDegrees, 180.0, 1e-9);
}

TEST(constraint_degenerate_inputs) {
    const stp::PlanarInfo plane = xyPlane();
    stp::Camera camera;
    camera.fitPlanar(plane, 1.0);
    stp::SnapConstraint c;
    c.kind = stp::ConstraintKind::Ortho;
    const stp::ConstrainedPoint same = stp::applyConstraint(stp::Vec3(1, 1, 0), stp::Vec3(1, 1, 0), c, camera, &plane);
    CHECK(!same.applied);
    CHECK(std::isfinite(same.point.x) && std::isfinite(same.point.y));
    c.kind = stp::ConstraintKind::Polar;
    c.polarStepDegrees = 0;  // angulo invalido: no aplica
    CHECK(!stp::applyConstraint(stp::Vec3(0, 0, 0), stp::Vec3(5, 5, 0), c, camera, &plane).applied);
    c.kind = stp::ConstraintKind::None;
    CHECK(!stp::applyConstraint(stp::Vec3(0, 0, 0), stp::Vec3(5, 1, 0), c, camera, &plane).applied);
}

TEST(distance_axis_name_for_saved_marks) {
    const stp::PlanarInfo plane = xyPlane();
    CHECK(std::string(stp::distanceAxisName(stp::Vec3(0, 0, 0), stp::Vec3(7, 0, 0), &plane, 1e-9)) == "Horizontal");
    CHECK(std::string(stp::distanceAxisName(stp::Vec3(0, 0, 0), stp::Vec3(0, 0, 7), nullptr, 1e-9)) == "Z");
    CHECK(stp::distanceAxisName(stp::Vec3(0, 0, 0), stp::Vec3(3, 4, 0), &plane, 1e-9) == nullptr);
    CHECK(stp::distanceAxisName(stp::Vec3(0, 0, 0), stp::Vec3(0, 0, 0), &plane, 1e-9) == nullptr);
}
```

- [ ] **Step 2: Confirmar que fallan**

Run: `./build.sh test` → error de compilación (`applyConstraint` no existe).

- [ ] **Step 3: Implementación**

En `measure.h`, después de `measureDistance`:

```cpp
// Restricciones de acotado: Orto (ejes) y Polar (multiplos de un angulo).
enum class ConstraintKind { None, Ortho, Polar };
struct SnapConstraint {
    ConstraintKind kind = ConstraintKind::None;
    double polarStepDegrees = 45.0;
};
struct ConstrainedPoint {
    Vec3 point;
    bool applied = false;
    const char* axisName = nullptr;  // Orto: "Horizontal", "Vertical", "X", "Y" o "Z"
    double angleDegrees = 0.0;       // Polar: angulo fijado, 0..360
};
// Lleva `to` a la direccion permitida desde `from`. En 2D (plane) los ejes son los
// del dibujo; en 3D, Orto usa el eje del mundo que mas se parece en pantalla a la
// direccion del cursor y Polar mide el angulo en el plano de la pantalla.
ConstrainedPoint applyConstraint(const Vec3& from, const Vec3& to, const SnapConstraint& constraint,
                                 const Camera& camera, const PlanarInfo* plane);
// Eje sobre el que queda la distancia a->b, o nullptr si no cae sobre uno solo.
const char* distanceAxisName(const Vec3& a, const Vec3& b, const PlanarInfo* plane, double tolerance);
```

En `measure.cpp`, después de `measureDistance`:

```cpp
ConstrainedPoint applyConstraint(const Vec3& from, const Vec3& to, const SnapConstraint& constraint,
                                 const Camera& camera, const PlanarInfo* plane) {
    ConstrainedPoint result;
    result.point = to;
    const Vec3 d = to - from;
    if (constraint.kind == ConstraintKind::None || length(d) < 1e-300) return result;
    const bool planar = plane && plane->planar;
    const Vec3 sx = planar ? plane->u : camera.right();
    const Vec3 sy = planar ? plane->v : camera.up();

    if (constraint.kind == ConstraintKind::Ortho) {
        if (planar) {
            const double du = dot(d, plane->u), dv = dot(d, plane->v);
            const bool horizontal = std::fabs(du) >= std::fabs(dv);
            result.point = from + (horizontal ? plane->u * du : plane->v * dv);
            result.axisName = horizontal ? "Horizontal" : "Vertical";
            result.applied = true;
            return result;
        }
        // Eje del mundo cuya imagen en pantalla apunta mas parecido al cursor.
        const Vec3 axes[3] = {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)};
        static const char* const names[3] = {"X", "Y", "Z"};
        const double mx = dot(d, sx), my = dot(d, sy), ml = std::hypot(mx, my);
        int best = -1;
        double bestCos = -1.0;
        for (int k = 0; k < 3; ++k) {
            const double ax = dot(axes[k], sx), ay = dot(axes[k], sy), al = std::hypot(ax, ay);
            if (al < 0.2) continue;  // eje casi de punta: no sirve para elegir con el cursor
            const double c = ml > 1e-300 ? std::fabs(ax * mx + ay * my) / (al * ml) : 0.0;
            if (c > bestCos) {
                bestCos = c;
                best = k;
            }
        }
        if (best < 0) return result;
        result.point = from + axes[best] * dot(d, axes[best]);
        result.axisName = names[best];
        result.applied = true;
        return result;
    }

    const double step = constraint.polarStepDegrees;
    if (!(step > 0.0 && step <= 180.0)) return result;
    const double x = dot(d, sx), y = dot(d, sy);
    if (std::hypot(x, y) < 1e-300) return result;
    double angle = std::atan2(y, x) * 180.0 / kPi;
    if (angle < 0) angle += 360.0;
    double snapped = std::round(angle / step) * step;
    if (std::fabs(angle - snapped) > 3.0) return result;
    if (snapped >= 360.0) snapped -= 360.0;
    const double r = snapped * kPi / 180.0;
    const Vec3 dir = sx * std::cos(r) + sy * std::sin(r);
    result.point = from + dir * dot(d, dir);
    result.angleDegrees = snapped;
    result.applied = true;
    return result;
}

const char* distanceAxisName(const Vec3& a, const Vec3& b, const PlanarInfo* plane, double tolerance) {
    const Vec3 d = b - a;
    if (length(d) <= tolerance) return nullptr;
    if (plane && plane->planar) {
        const double du = std::fabs(dot(d, plane->u)), dv = std::fabs(dot(d, plane->v));
        if (dv <= tolerance) return "Horizontal";
        if (du <= tolerance) return "Vertical";
        return nullptr;
    }
    const double x = std::fabs(d.x), y = std::fabs(d.y), z = std::fabs(d.z);
    if (y <= tolerance && z <= tolerance) return "X";
    if (x <= tolerance && z <= tolerance) return "Y";
    if (x <= tolerance && y <= tolerance) return "Z";
    return nullptr;
}
```

- [ ] **Step 4: Correr las pruebas**

Run: `./build.sh test` → todas pasan.

- [ ] **Step 5: Commit**

```bash
git add src/engine/measure.h src/engine/measure.cpp tests/unit_tests.cpp
git commit -m "Add ortho and polar constraints for measuring"
```

---
### Task 3: Lógica portable de la interfaz (cinta, cubo, recientes)

**Files:**
- Create: `src/ui/ribbon_layout.h`, `src/ui/ribbon_layout.cpp`
- Create: `src/ui/view_cube_math.h`, `src/ui/view_cube_math.cpp`
- Create: `src/ui/recent_files.h`, `src/ui/recent_files.cpp`
- Modify: `build.sh`, `build.bat` (agregar los tres `.cpp` al final de `ENGINE`)
- Test: `tests/unit_tests.cpp` (sección nueva "Interfaz")

**Interfaces:**
- Consumes: `Camera` (`src/render/renderer.h`).
- Produces (namespace `stp::ui`):
  - `enum class GroupSize { Large, Small, Collapsed };`
  - `struct RibbonGroupSpec { int large = 0; int small = 0; int collapsed = 0; };`
  - `std::vector<GroupSize> layoutRibbon(const std::vector<RibbonGroupSpec>& groups, int available, int gap, bool narrow);`
  - `int ribbonWidth(const std::vector<RibbonGroupSpec>& groups, const std::vector<GroupSize>& sizes, int gap);`
  - `struct CubeRegion { int x = 0, y = 0, z = 0; bool valid() const; bool operator==(const CubeRegion&) const; };`
  - `CubeRegion cubeRegionAt(const Camera& camera, double dx, double dy, double halfSize);`
  - `void cubeOrientation(const CubeRegion& region, double* yaw, double* pitch);`
  - `const char* cubeFaceName(int x, int y, int z);` (UTF-8, `nullptr` si no es cara)
  - `void interpolateOrientation(double yaw0, double pitch0, double yaw1, double pitch1, double t, double* yaw, double* pitch);`
  - `bool samePath(const std::wstring& a, const std::wstring& b);`
  - `class RecentFiles { static constexpr std::size_t kLimit = 10; void add(const std::wstring&); bool remove(const std::wstring&); const std::vector<std::wstring>& items() const; void setItems(const std::vector<std::wstring>&); };`

- [ ] **Step 1: Pruebas que fallan**

En `tests/unit_tests.cpp` agregar los `#include "../src/ui/ribbon_layout.h"`, `"../src/ui/view_cube_math.h"`, `"../src/ui/recent_files.h"` y al final:

```cpp
// --- Interfaz (logica pura) ------------------------------------------------------------

namespace {
bool sizesAre(const std::vector<stp::ui::GroupSize>& got, std::initializer_list<stp::ui::GroupSize> want) {
    return got == std::vector<stp::ui::GroupSize>(want);
}
}  // namespace

TEST(ribbon_layout_shrinks_right_to_left) {
    using stp::ui::GroupSize;
    const std::vector<stp::ui::RibbonGroupSpec> groups(3, stp::ui::RibbonGroupSpec{200, 120, 50});
    const GroupSize L = GroupSize::Large, S = GroupSize::Small, C = GroupSize::Collapsed;
    CHECK(sizesAre(stp::ui::layoutRibbon(groups, 700, 4, false), {L, L, L}));
    CHECK(sizesAre(stp::ui::layoutRibbon(groups, 540, 4, false), {L, L, S}));
    CHECK(sizesAre(stp::ui::layoutRibbon(groups, 400, 4, false), {S, S, S}));
    CHECK(sizesAre(stp::ui::layoutRibbon(groups, 300, 4, false), {S, S, C}));
    CHECK(sizesAre(stp::ui::layoutRibbon(groups, 100, 4, false), {C, C, C}));
    CHECK(sizesAre(stp::ui::layoutRibbon(groups, 5000, 4, true), {S, S, S}));
    CHECK(stp::ui::ribbonWidth(groups, {L, L, S}, 4) == 528);
}

TEST(ribbon_layout_never_overflows_when_possible) {
    std::mt19937 random(3);
    std::uniform_int_distribution<int> large(80, 260), count(1, 7), width(100, 1600);
    for (int round = 0; round < 500; ++round) {
        std::vector<stp::ui::RibbonGroupSpec> groups(static_cast<std::size_t>(count(random)));
        for (auto& g : groups) {
            g.large = large(random);
            g.small = g.large * 2 / 3;
            g.collapsed = 48;
        }
        const int available = width(random);
        const auto sizes = stp::ui::layoutRibbon(groups, available, 6, false);
        CHECK(sizes.size() == groups.size());
        const std::vector<stp::ui::GroupSize> tiny(groups.size(), stp::ui::GroupSize::Collapsed);
        if (stp::ui::ribbonWidth(groups, tiny, 6) <= available) {
            CHECK(stp::ui::ribbonWidth(groups, sizes, 6) <= available);
        }
    }
}

TEST(view_cube_region_under_cursor) {
    stp::Camera front;
    front.yaw = -stp::kPi / 2;
    front.pitch = 0.0;
    CHECK((stp::ui::cubeRegionAt(front, 0, 0, 50) == stp::ui::CubeRegion{0, -1, 0}));
    CHECK((stp::ui::cubeRegionAt(front, 45, 0, 50) == stp::ui::CubeRegion{1, -1, 0}));
    CHECK((stp::ui::cubeRegionAt(front, 45, -45, 50) == stp::ui::CubeRegion{1, -1, 1}));
    CHECK((stp::ui::cubeRegionAt(front, 0, 45, 50) == stp::ui::CubeRegion{0, -1, -1}));
    CHECK(!stp::ui::cubeRegionAt(front, 60, 0, 50).valid());
    stp::Camera iso;  // isometrica: se ve la esquina (+X, -Y, +Z) en el centro
    CHECK((stp::ui::cubeRegionAt(iso, 0, 0, 50) == stp::ui::CubeRegion{1, -1, 1}));
    CHECK(std::string(stp::ui::cubeFaceName(0, 0, 1)) == "Superior");
    CHECK(std::string(stp::ui::cubeFaceName(0, -1, 0)) == "Frente");
    CHECK(stp::ui::cubeFaceName(1, -1, 0) == nullptr);
}

TEST(view_cube_orientation_per_region) {
    double yaw = 0, pitch = 0;
    stp::ui::cubeOrientation({0, -1, 0}, &yaw, &pitch);
    CHECK_NEAR(yaw, -stp::kPi / 2, 1e-12);
    CHECK_NEAR(pitch, 0.0, 1e-12);
    stp::ui::cubeOrientation({0, 0, 1}, &yaw, &pitch);
    CHECK_NEAR(pitch, 1.5533430, 1e-7);  // limite de la orbita, como la tecla 5
    CHECK_NEAR(yaw, -stp::kPi / 2, 1e-12);
    stp::ui::cubeOrientation({1, -1, 1}, &yaw, &pitch);
    CHECK_NEAR(yaw, -stp::kPi / 4, 1e-12);
    CHECK_NEAR(pitch, std::asin(1 / std::sqrt(3.0)), 1e-12);
    stp::ui::cubeOrientation({-1, 0, 0}, &yaw, &pitch);
    CHECK_NEAR(std::fabs(yaw), stp::kPi, 1e-12);
    // La animacion gira por el camino corto.
    stp::ui::interpolateOrientation(3.0, 0.0, -3.0, 0.0, 0.5, &yaw, &pitch);
    CHECK_NEAR(std::fabs(yaw), stp::kPi, 1e-3);
    stp::ui::interpolateOrientation(0.0, 0.0, 1.0, 0.5, 1.0, &yaw, &pitch);
    CHECK_NEAR(yaw, 1.0, 1e-12);
    CHECK_NEAR(pitch, 0.5, 1e-12);
}

TEST(recent_files_dedupe_case_insensitive) {
    stp::ui::RecentFiles recent;
    recent.add(L"C:\\Planos\\a.dxf");
    recent.add(L"C:\\Planos\\b.stp");
    recent.add(L"c:/planos/A.DXF");
    CHECK(recent.items().size() == 2);
    CHECK(recent.items()[0] == L"c:/planos/A.DXF");
    CHECK(recent.items()[1] == L"C:\\Planos\\b.stp");
    for (int i = 0; i < 15; ++i) recent.add(L"C:\\x\\" + std::to_wstring(i) + L".stp");
    CHECK(recent.items().size() == stp::ui::RecentFiles::kLimit);
    CHECK(recent.items()[0] == L"C:\\x\\14.stp");
    CHECK(recent.remove(L"c:\\X\\14.STP"));
    CHECK(!recent.remove(L"C:\\nada.stp"));
    CHECK(recent.items()[0] == L"C:\\x\\13.stp");
    recent.setItems({L"a", L"A", L"", L"b"});
    CHECK(recent.items().size() == 2);
    CHECK(stp::ui::samePath(L"C:\\A\\b.dxf", L"c:/a/B.DXF"));
}
```

- [ ] **Step 2: Confirmar que fallan**

Run: `./build.sh test` → error de compilación (no existen los encabezados).

- [ ] **Step 3: `ribbon_layout`**

`src/ui/ribbon_layout.h`:

```cpp
// Distribucion de la cinta: que tamano toma cada grupo segun el ancho disponible.
// Sin dependencias de Windows: se prueba en Linux.
#pragma once

#include <vector>

namespace stp {
namespace ui {

enum class GroupSize { Large, Small, Collapsed };

// Anchos en pixeles ya escalados por DPI de cada forma del grupo.
struct RibbonGroupSpec {
    int large = 0;      // botones grandes con texto debajo
    int small = 0;      // iconos pequenos con texto al lado, en columnas
    int collapsed = 0;  // un solo boton que despliega el grupo
};

// Reduce de derecha a izquierda: primero cada grupo a Small (del ultimo al
// primero), despues a Collapsed. narrow (ventana < 640 px): nunca Large.
std::vector<GroupSize> layoutRibbon(const std::vector<RibbonGroupSpec>& groups, int available, int gap,
                                    bool narrow);
int ribbonWidth(const std::vector<RibbonGroupSpec>& groups, const std::vector<GroupSize>& sizes, int gap);

}  // namespace ui
}  // namespace stp
```

`src/ui/ribbon_layout.cpp`:

```cpp
#include "ribbon_layout.h"

namespace stp {
namespace ui {

int ribbonWidth(const std::vector<RibbonGroupSpec>& groups, const std::vector<GroupSize>& sizes, int gap) {
    int total = 0;
    for (std::size_t i = 0; i < groups.size() && i < sizes.size(); ++i) {
        if (i > 0) total += gap;
        switch (sizes[i]) {
            case GroupSize::Large: total += groups[i].large; break;
            case GroupSize::Small: total += groups[i].small; break;
            case GroupSize::Collapsed: total += groups[i].collapsed; break;
        }
    }
    return total;
}

std::vector<GroupSize> layoutRibbon(const std::vector<RibbonGroupSpec>& groups, int available, int gap,
                                    bool narrow) {
    std::vector<GroupSize> sizes(groups.size(), narrow ? GroupSize::Small : GroupSize::Large);
    for (const GroupSize target : {GroupSize::Small, GroupSize::Collapsed}) {
        for (std::size_t k = groups.size(); k-- > 0;) {
            if (ribbonWidth(groups, sizes, gap) <= available) return sizes;
            if (static_cast<int>(sizes[k]) < static_cast<int>(target)) sizes[k] = target;
        }
    }
    return sizes;
}

}  // namespace ui
}  // namespace stp
```

- [ ] **Step 4: `view_cube_math`**

`src/ui/view_cube_math.h`:

```cpp
// Cubo de vistas: que cara, arista o esquina esta bajo el cursor y a que
// orientacion de camara lleva. Sin dependencias de Windows.
#pragma once

#include "../render/renderer.h"

namespace stp {
namespace ui {

// Cada componente es -1, 0 o 1: una no nula = cara, dos = arista, tres = esquina.
struct CubeRegion {
    int x = 0, y = 0, z = 0;
    bool valid() const { return x != 0 || y != 0 || z != 0; }
    bool operator==(const CubeRegion& o) const { return x == o.x && y == o.y && z == o.z; }
};

// (dx, dy): pixel medido desde el centro del cubo, y hacia abajo. halfSize: mitad
// de la arista en pixeles. El cubo se ve con la misma orientacion que la camara.
CubeRegion cubeRegionAt(const Camera& camera, double dx, double dy, double halfSize);
// Orientacion (yaw, pitch) de una camara que mira la region desde afuera; el
// pitch se limita como la orbita (89 grados).
void cubeOrientation(const CubeRegion& region, double* yaw, double* pitch);
// "Frente" (-Y), "Atr\u00e1s" (+Y), "Derecha" (+X), "Izquierda" (-X),
// "Superior" (+Z), "Inferior" (-Z), en UTF-8; nullptr si no es una cara.
const char* cubeFaceName(int x, int y, int z);
// Giro suave (t de 0 a 1) por el camino corto del yaw.
void interpolateOrientation(double yaw0, double pitch0, double yaw1, double pitch1, double t, double* yaw,
                            double* pitch);

}  // namespace ui
}  // namespace stp
```

`src/ui/view_cube_math.cpp`:

```cpp
#include "view_cube_math.h"

#include <algorithm>
#include <cmath>

namespace stp {
namespace ui {
namespace {
constexpr double kPitchLimit = 1.5533430;  // 89 grados, el de la orbita del visor
constexpr double kBand = 0.6;              // mas alla, el clic es de arista o esquina

double axis(const Vec3& v, int k) { return k == 0 ? v.x : (k == 1 ? v.y : v.z); }
}  // namespace

CubeRegion cubeRegionAt(const Camera& camera, double dx, double dy, double halfSize) {
    CubeRegion region;
    if (halfSize <= 0) return region;
    const Vec3 right = camera.right(), up = camera.up(), forward = camera.forward();
    const Vec3 origin = right * (dx / halfSize) - up * (dy / halfSize) - forward * 4.0;
    double t0 = -1e300, t1 = 1e300;
    for (int k = 0; k < 3; ++k) {
        const double o = axis(origin, k), d = axis(forward, k);
        if (std::fabs(d) < 1e-12) {
            if (o < -1 || o > 1) return region;
            continue;
        }
        double tn = (-1 - o) / d, tf = (1 - o) / d;
        if (tn > tf) std::swap(tn, tf);
        t0 = std::max(t0, tn);
        t1 = std::min(t1, tf);
        if (t0 > t1) return region;
    }
    const Vec3 p = origin + forward * t0;
    int face = 0;
    for (int k = 1; k < 3; ++k) {
        if (std::fabs(axis(p, k)) > std::fabs(axis(p, face))) face = k;
    }
    int out[3];
    for (int k = 0; k < 3; ++k) {
        const double c = axis(p, k);
        out[k] = (k == face || std::fabs(c) > kBand) ? (c > 0 ? 1 : -1) : 0;
    }
    region.x = out[0];
    region.y = out[1];
    region.z = out[2];
    return region;
}

void cubeOrientation(const CubeRegion& region, double* yaw, double* pitch) {
    const Vec3 dir = normalize(Vec3(region.x, region.y, region.z));
    *pitch = std::max(-kPitchLimit, std::min(kPitchLimit, std::asin(dir.z)));
    *yaw = std::hypot(dir.x, dir.y) > 1e-9 ? std::atan2(dir.y, dir.x) : -kPi / 2;
}

const char* cubeFaceName(int x, int y, int z) {
    if (std::abs(x) + std::abs(y) + std::abs(z) != 1) return nullptr;
    if (x > 0) return "Derecha";
    if (x < 0) return "Izquierda";
    if (y > 0) return "Atr\u00e1s";
    if (y < 0) return "Frente";
    return z > 0 ? "Superior" : "Inferior";
}

void interpolateOrientation(double yaw0, double pitch0, double yaw1, double pitch1, double t, double* yaw,
                            double* pitch) {
    t = std::max(0.0, std::min(1.0, t));
    const double s = t * t * (3 - 2 * t);  // arranca y frena suave
    double dy = std::fmod(yaw1 - yaw0, 2 * kPi);
    if (dy > kPi) dy -= 2 * kPi;
    if (dy < -kPi) dy += 2 * kPi;
    *yaw = t >= 1.0 ? yaw1 : yaw0 + dy * s;
    *pitch = t >= 1.0 ? pitch1 : pitch0 + (pitch1 - pitch0) * s;
}

}  // namespace ui
}  // namespace stp
```

- [ ] **Step 5: `recent_files`**

`src/ui/recent_files.h`:

```cpp
// Lista de archivos recientes: el mas nuevo arriba, sin repetidos, hasta 10.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace stp {
namespace ui {

// Rutas de Windows: sin distinguir mayusculas y con '/' igual a '\\'.
bool samePath(const std::wstring& a, const std::wstring& b);

class RecentFiles {
public:
    static constexpr std::size_t kLimit = 10;
    void add(const std::wstring& path);
    bool remove(const std::wstring& path);
    const std::vector<std::wstring>& items() const { return m_items; }
    // Carga una lista guardada: ignora vacias y repetidas, conserva el orden.
    void setItems(const std::vector<std::wstring>& items);

private:
    std::vector<std::wstring> m_items;
};

}  // namespace ui
}  // namespace stp
```

`src/ui/recent_files.cpp`:

```cpp
#include "recent_files.h"

#include <algorithm>
#include <cwctype>

namespace stp {
namespace ui {
namespace {
wchar_t fold(wchar_t c) { return c == L'/' ? L'\\' : static_cast<wchar_t>(std::towlower(c)); }
}  // namespace

bool samePath(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (fold(a[i]) != fold(b[i])) return false;
    }
    return true;
}

void RecentFiles::add(const std::wstring& path) {
    if (path.empty()) return;
    remove(path);
    m_items.insert(m_items.begin(), path);
    if (m_items.size() > kLimit) m_items.resize(kLimit);
}

bool RecentFiles::remove(const std::wstring& path) {
    const auto it = std::find_if(m_items.begin(), m_items.end(),
                                 [&](const std::wstring& item) { return samePath(item, path); });
    if (it == m_items.end()) return false;
    m_items.erase(it);
    return true;
}

void RecentFiles::setItems(const std::vector<std::wstring>& items) {
    m_items.clear();
    for (const std::wstring& item : items) {
        if (item.empty() || m_items.size() >= kLimit) continue;
        const bool repeated = std::any_of(m_items.begin(), m_items.end(),
                                          [&](const std::wstring& seen) { return samePath(seen, item); });
        if (!repeated) m_items.push_back(item);
    }
}

}  // namespace ui
}  // namespace stp
```

- [ ] **Step 6: Listas de compilación**

En `build.sh` y `build.bat`, agregar al final de `ENGINE`: `src/ui/ribbon_layout.cpp src/ui/view_cube_math.cpp src/ui/recent_files.cpp` (con `\` en `build.bat`).

- [ ] **Step 7: Correr las pruebas**

Run: `./build.sh test` → todas pasan. Run: `./build.sh` → compila la DLL y el visor.

- [ ] **Step 8: Commit**

```bash
git add src/ui build.sh build.bat tests/unit_tests.cpp
git commit -m "Add portable ribbon layout, view cube math and recent files list"
```

---

### Task 4: Enganche en las herramientas de medir

**Files:**
- Modify: `src/viewer/markup_tools.h`, `src/viewer/markup_tools.cpp`
- Modify: `src/viewer/scene_view.h`, `src/viewer/scene_view.cpp` (reenviar `F3/F8/F10/Tab` a las herramientas)

**Interfaces:**
- Consumes: Task 1 (`snapAll`, `surfaceAt`, `SnapOptions::modes/from`, `snapKindName`), Task 2 (`applyConstraint`, `distanceAxisName`, `SnapConstraint`).
- Produces:
  - `struct SnapSettings { bool enabled = true; unsigned modes = kSnapDefaultModes; SnapConstraint constraint; };`
  - `const SnapSettings& MarkupTools::snapSettings() const;`
  - `void MarkupTools::setSnapSettings(const SnapSettings& settings);` (llama `markupChanged()` y `markupRedraw()`)
  - `void MarkupTools::toggleSnap();` `void MarkupTools::toggleOrtho();` `void MarkupTools::togglePolar();` (Orto y Polar se excluyen; muestran "Enganche activado/desactivado", "Orto activado"... con `showMessage`)
  - `bool MarkupTools::cursorPoint(Vec3* point) const;` (último punto bajo el cursor, para la barra de estado; false si el cursor no está en la vista)
  - `std::wstring MarkupTools::statusText() const;` (mensaje vigente o la ayuda de la herramienta: reemplaza lo que hoy se dibuja sobre la vista)

- [ ] **Step 1: Estado de enganche**

En `markup_tools.h`: agregar `SnapSettings` antes de `class MarkupTools`, los métodos de arriba y los miembros:

```cpp
    SnapSettings m_snap;
    std::vector<SnapResult> m_candidates;  // bajo el cursor, en el orden de Tab
    std::size_t m_candidate = 0;
    ConstrainedPoint m_constrained;        // restriccion aplicada al punto en curso
    bool m_cursorValid = false;
    Vec3 m_cursorPoint;
```

- [ ] **Step 2: `snapAt` con modos, primer punto y candidatos**

Reemplazar `MarkupTools::snapAt` (deja de ser `const`: guarda los candidatos):

```cpp
SnapResult MarkupTools::snapAt(int x, int y) {
    const Mesh& mesh = m_host->markupMesh();
    const Camera& camera = m_host->markupCamera();
    const int w = m_host->markupWidth(), h = m_host->markupHeight();
    SnapOptions options;
    // El area no engancha: un punto de arista esta en dos caras y se mediria la
    // que no se ve. Se usa el punto de la superficie bajo el cursor.
    options.snapping = m_snap.enabled && GetKeyState(VK_SHIFT) >= 0 && m_tool != Tool::Area;
    options.plane = m_host->markupPlane();
    options.radiusPixels = 8.0 * m_host->markupDpi() / 96.0;
    options.modes = m_snap.modes;
    // Radio y angulo entre rectas se eligen tocando la arista.
    if (m_tool == Tool::Radius || m_tool == Tool::Angle) options.modes |= kSnapNearest;
    const Vec3 from = m_pending.empty() ? Vec3() : m_pending.back().point;
    if (!m_pending.empty() && (m_tool == Tool::Distance || m_tool == Tool::Angle)) options.from = &from;

    std::vector<SnapResult> all = m_host->markupPick().snapAll(mesh, camera, w, h, x, y, options);
    const bool same = all.size() == m_candidates.size() &&
                      std::equal(all.begin(), all.end(), m_candidates.begin(), [](const SnapResult& a, const SnapResult& b) {
                          return a.kind == b.kind && distance(a.point, b.point) < 1e-12;
                      });
    if (!same) m_candidate = 0;
    m_candidates = std::move(all);
    SnapResult snap = m_candidates.empty()
                          ? m_host->markupPick().surfaceAt(mesh, camera, w, h, x, y, options.plane)
                          : m_candidates[std::min(m_candidate, m_candidates.size() - 1)];

    // La restriccion se aplica al punto ya enganchado (segundo clic de distancia y angulo).
    m_constrained = ConstrainedPoint();
    if (snap.kind != SnapKind::None && !m_pending.empty() &&
        (m_tool == Tool::Distance || m_tool == Tool::Angle)) {
        m_constrained = applyConstraint(from, snap.point, m_snap.constraint, camera, options.plane);
        if (m_constrained.applied && distance(m_constrained.point, snap.point) > 1e-12) {
            snap.point = m_constrained.point;
            snap.kind = options.plane ? SnapKind::OnPlane : SnapKind::OnFace;
            snap.segment = snap.circle = -1;
        }
    }
    m_cursorValid = snap.kind != SnapKind::None;
    m_cursorPoint = snap.point;
    return snap;
}
```

(Si el punto enganchado ya cumple la restricción, conserva su tipo: la cota de la ranura con Orto sigue mostrando "Cuadrante".)

- [ ] **Step 3: Teclas**

En `MarkupTools::handle`, al principio del `switch` de mensajes, atender `WM_KEYDOWN`/`WM_SYSKEYDOWN` antes de lo existente:

```cpp
        case WM_SYSKEYDOWN:
        case WM_KEYDOWN: {
            if (wparam == VK_F3) { toggleSnap(); return true; }
            if (wparam == VK_F8) { toggleOrtho(); return true; }
            if (wparam == VK_F10) { togglePolar(); return true; }
            if (wparam == VK_TAB && isMeasureTool() && m_candidates.size() > 1) {
                const std::size_t n = m_candidates.size();
                m_candidate = GetKeyState(VK_SHIFT) < 0 ? (m_candidate + n - 1) % n : (m_candidate + 1) % n;
                m_hover = m_candidates[m_candidate];
                m_host->markupRedraw();
                return true;
            }
            if (msg == WM_SYSKEYDOWN) return false;
            // ...lo existente de WM_KEYDOWN sigue aqui
```

`F3/F8/F10` funcionan con cualquier herramienta (también navegando) para que el estado se pueda preparar antes de medir. `SceneView::handle` reenvía `WM_SYSKEYDOWN` a las herramientas igual que `WM_KEYDOWN`, y devuelve `DLGC_WANTTAB` en `WM_GETDLGCODE` (el panel del Explorador se lo come si no). Subrayado: en `WM_MOUSEMOVE` la condición `GetKeyState(VK_SHIFT) < 0` pasa a `GetKeyState(VK_SHIFT) < 0 || m_snap.constraint.kind == ConstraintKind::Ortho`.

`toggleOrtho`:

```cpp
void MarkupTools::toggleOrtho() {
    m_snap.constraint.kind = m_snap.constraint.kind == ConstraintKind::Ortho ? ConstraintKind::None : ConstraintKind::Ortho;
    showMessage(m_snap.constraint.kind == ConstraintKind::Ortho ? L"Orto activado" : L"Orto desactivado", 1500);
    m_host->markupChanged();
    m_host->markupRedraw();
}
```

`togglePolar` igual con `ConstraintKind::Polar` ("Polar activado (45\u00b0)"), `toggleSnap` con `m_snap.enabled` ("Enganche activado"/"Enganche desactivado").

- [ ] **Step 4: Marcadores y rótulo**

Reemplazar el `switch` de `drawSnap` por un glifo por tipo (verde `0,200,83`, trazo `1.5 * scale`, `s = 5 * scale`):

| Tipo | Glifo |
|---|---|
| Endpoint | cuadrado 2s |
| Midpoint | triángulo con vértice arriba |
| Center | círculo radio s |
| Quadrant | rombo (cuadrado girado 45°) |
| Intersection | X de 2s |
| Extension | X de 2s con pluma punteada (`DashStyleDash`) y dos trazos punteados de 3s que prolongan hacia el cursor |
| Perpendicular | escuadra: L de lado 2s con un cuadradito de s/2 en el ángulo |
| Tangent | círculo radio s y recta tangente horizontal encima de 2.6s |
| OnEdge (Más cercano) | reloj de arena: dos triángulos opuestos por el vértice |

Rótulo: `snapKindName(m_hover.kind)` convertido con `MultiByteToWideChar(CP_UTF8, ...)`, fuente `uiFont(8.5 * scale)`, fondo `#262B32` con 90 % de opacidad, borde `#363C45`, texto `#E6E9ED`, a `(+10, +10) * scale` del marcador (se corre a la izquierda o arriba si se sale de la vista). No se dibuja con `OnFace`/`OnPlane`.

Con `m_constrained.applied`: línea guía punteada azul (`#2F80ED`) desde el primer punto, atravesando toda la vista en la dirección fijada; junto al cursor, rótulo "Horizontal"/"Vertical"/"X"/"Y"/"Z" (Orto) o `formatAngle(angleDegrees)` (Polar), con el mismo estilo.

- [ ] **Step 5: Cota con eje**

En `MarkupTools::evaluate`, caso distancia: `const char* axis = distanceAxisName(a, b, plane, 1e-9 * size)` (`size` = diagonal de `mesh.bounds`); si no es nulo, `label = widen(axis) + L": " + formatLength(...)` y `detail` vacío (las componentes sobran).

- [ ] **Step 6: Cursor y texto de estado**

`cursorPoint` devuelve `m_cursorValid`/`m_cursorPoint` (se invalida en `WM_MOUSELEAVE`: `SceneView` ya llama `TrackMouseEvent`; si no, agregarlo). `statusText()` devuelve `m_message` si está vigente y, si no, lo que devolvía `hint()`. `hint()` se borra y `drawOverlay` de `SceneView` deja de pintar la ayuda cuando hay marco (ver Task 6: `setChrome(true)`); el panel del Explorador la sigue mostrando en la mini barra (Task 10).

- [ ] **Step 7: Compilar y verificar bajo wine**

Run: `./build.sh test && ./build.sh`
Luego abrir `tests/samples/plano_brida.dxf` con `wine dist/stpviewer.exe` en Xvfb, tecla `D`, mover con xdotool a (170,35) y (170,105) del dibujo (convertir con la escala de la vista), clic en ambos, captura con `import -window root`.
Expected: marcador rombo con rótulo "Cuadrante" en cada punta y cota "70 mm". Repetir con `F8` y puntos desalineados: cota "Vertical: 70 mm". `Tab` sobre (160,45) cambia de "Extremo" a "Cuadrante".

- [ ] **Step 8: Commit**

```bash
git add src/viewer/markup_tools.h src/viewer/markup_tools.cpp src/viewer/scene_view.h src/viewer/scene_view.cpp
git commit -m "Wire object snap, Tab cycling and ortho/polar into measuring tools"
```

---
### Task 5: Tema, iconos y tabla de comandos

**Files:**
- Create: `src/viewer/ui/theme.h`, `src/viewer/ui/theme.cpp`
- Create: `src/viewer/ui/icons.h`, `src/viewer/ui/icons.cpp`
- Create: `src/viewer/ui/commands.h`, `src/viewer/ui/commands.cpp`
- Modify: `src/viewer/markup_tools.cpp` (usa `ui::font` en lugar de su `uiFont` local), `build.sh`, `build.bat` (lista `VIEWER_UI`)

**Interfaces:**
- Consumes: `ensureGdiplus()` (existente, `image_view.h`), `Tool` (`markup_tools.h`).
- Produces (namespace `stp::ui`):

```cpp
// theme.h
namespace stp { namespace ui {
// Paleta grafito (ARGB).
constexpr Gdiplus::ARGB kBackground = 0xFF1F2329, kPanel = 0xFF262B32, kBorder = 0xFF363C45,
    kText = 0xFFE6E9ED, kTextDim = 0xFF9AA3AD, kAccent = 0xFF2F80ED, kHover = 0xFF2D333B,
    kPressed = 0xFF343B45, kSnapGreen = 0xFF00C853;
inline COLORREF gdi(Gdiplus::ARGB c) { return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255); }
inline int scale(int value, int dpi) { return MulDiv(value, dpi, 96); }
// Segoe UI en puntos a ese DPI; sin Segoe UI, la sans-serif generica.
std::unique_ptr<Gdiplus::Font> font(double points, int dpi, INT style = Gdiplus::FontStyleRegular);
// Igual pero en pixeles (para dibujos que ya vienen escalados).
std::unique_ptr<Gdiplus::Font> fontPixels(double pixels, INT style = Gdiplus::FontStyleRegular);
void fillRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, Gdiplus::ARGB color);
void strokeRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, Gdiplus::ARGB color, float width);
// Texto en una sola linea, recortado con "..." si no cabe; align 0 izq, 1 centro, 2 der.
void drawText(Gdiplus::Graphics& g, const std::wstring& text, const Gdiplus::Font& f,
              const Gdiplus::RectF& r, Gdiplus::ARGB color, int align = 0);
Gdiplus::SizeF measureText(Gdiplus::Graphics& g, const std::wstring& text, const Gdiplus::Font& f);
// Mezcla lineal de colores (t de 0 a 1): transicion de hover de 120 ms.
Gdiplus::ARGB blend(Gdiplus::ARGB a, Gdiplus::ARGB b, double t);
// Pinta en un HDC con doble buffer: crea bitmap, Graphics con antialias, llama draw y copia.
void paintBuffered(HDC dc, const RECT& rect, const std::function<void(Gdiplus::Graphics&)>& draw);
std::wstring widen(const char* utf8);
}}
```

```cpp
// icons.h
namespace stp { namespace ui {
enum class Icon {
    Open, Recent, Save, ExportPdf, ExportPng, About,
    Navigate, Fit, Views, Plan2d, View3d,
    Distance, Radius, Angle, Area,
    SnapEndpoint, SnapMidpoint, SnapCenter, SnapQuadrant, SnapIntersection, SnapExtension,
    SnapPerpendicular, SnapTangent, SnapNearest, Ortho, Polar,
    Highlight, Underline, Note, Rectangle, Ellipse, Cloud, Pen, Color, Undo, Redo, Delete,
    Shaded, Wireframe, Edges, Perspective, Panel, StatusBar, ViewCube,
    Home, Minimize, Maximize, Restore, Close, ChevronDown, Marks, Properties, File, Folder,
};
// Dibuja el icono dentro de box (cuadrado) con trazo 1,5 px a 100 % escalado con box.
void drawIcon(Gdiplus::Graphics& g, Icon icon, const Gdiplus::RectF& box, Gdiplus::ARGB color);
Icon iconForSnap(SnapKind kind);  // glifos del marcador de enganche
}}
```

```cpp
// commands.h
namespace stp { namespace ui {
enum Command : int {
    kCmdNone = 0,
    kCmdOpen = 100, kCmdRecent, kCmdSaveMarks, kCmdExportPdf, kCmdExportPng, kCmdAbout,
    kCmdNavigate = 200, kCmdFit, kCmdViews, kCmdPlan2d,
    kCmdViewFront, kCmdViewBack, kCmdViewLeft, kCmdViewRight, kCmdViewTop, kCmdViewBottom, kCmdViewIso,
    kCmdTool = 300,  // + static_cast<int>(Tool)
    kCmdSnapToggle = 400, kCmdSnapMode,  // kCmdSnapMode + indice del bit (0..8)
    kCmdOrtho = 420, kCmdPolar, kCmdPolarStep,  // kCmdPolarStep + {15,30,45,90}
    kCmdColor = 500, kCmdUndo, kCmdRedo, kCmdDelete,
    kCmdShaded = 600, kCmdWireframe, kCmdEdges, kCmdPerspective, kCmdPanel, kCmdStatusBar, kCmdViewCube,
    kCmdRibbonToggle = 700,
    kCmdRecentFirst = 800,  // + indice en la lista de recientes
};
struct CommandInfo {
    int id;
    Icon icon;
    const wchar_t* label;     // texto del boton
    const wchar_t* shortcut;  // "Ctrl+O", "F8"... o nullptr
    const wchar_t* help;      // una linea para el tooltip
};
const CommandInfo* commandInfo(int id);  // nullptr si no existe
// Estado que la cinta, el menu y la barra de estado consultan al pintar.
struct CommandState { bool enabled = true; bool checked = false; };
}}
```

- [ ] **Step 1: `theme.cpp`**

Implementar las funciones del encabezado. `font()` = `fontPixels(points * dpi / 72.0, style)`; `fontPixels` es el `uiFont` de `markup_tools.cpp` movido aquí (se borra el local y `markup_tools.cpp` llama `ui::fontPixels`). `fillRound`/`strokeRound` con `GraphicsPath` de cuatro `AddArc`. `drawText` con `StringFormat` `StringTrimmingEllipsisCharacter`, `StringFormatFlagsNoWrap`, `LineAlignmentCenter`. `paintBuffered`: `CreateCompatibleDC` + `CreateCompatibleBitmap`, `Graphics` con `SmoothingModeAntiAlias` y `TextRenderingHintClearTypeGridFit`, `BitBlt`. `widen` con `MultiByteToWideChar(CP_UTF8, ...)`.

- [ ] **Step 2: `icons.cpp`**

Un `switch` por icono; cada uno se dibuja en una grilla de 16×16 unidades escalada a `box` (`Matrix` con `Scale(box.Width / 16)` y `Translate(box.X, box.Y)`), pluma de 1,5 unidades con `LineCapRound`/`LineJoinRound`. Formas (unidades de 16):
- Open: carpeta (polígono 2,4 → 6,4 → 7.5,5.5 → 14,5.5 → 14,13 → 2,13) + flecha arriba.
- Save: disquete (rect 2..14 con muesca 5..11 arriba, rect interior 5,9..11,14).
- ExportPdf/ExportPng: hoja con esquina doblada y rótulo "PDF"/"PNG" en `fontPixels(4.2)`.
- Navigate: cursor flecha. Fit: cuatro esquinas en L. Views: cubo isométrico de alambre. Plan2d: hoja con cota. View3d: cubo sombreado.
- Distance: recta con topes y flechas. Radius: círculo con radio. Angle: dos rectas con arco. Area: polígono relleno al 35 %.
- Snap*: los mismos glifos que `drawSnap` (Task 4) en 16 unidades. Ortho: escuadra en L con flechas. Polar: dos rectas a 45° con arco punteado.
- Highlight: marcador inclinado. Underline: "U" subrayada (trazos, no fuente). Note: globo de diálogo. Rectangle, Ellipse, Cloud (cinco arcos), Pen (lápiz). Color: círculo relleno (el color lo pasa el llamador). Undo/Redo: flecha curva. Delete: papelera.
- Shaded: esfera con relleno degradado. Wireframe: esfera de meridianos. Edges: cubo con aristas gruesas. Perspective: rectángulo trapezoidal. Panel: ventana con franja derecha. StatusBar: ventana con franja abajo. ViewCube: cubo pequeño con cara superior rellena.
- Home: casa. Minimize: raya. Maximize: cuadrado. Restore: dos cuadrados. Close: X. ChevronDown: V. Marks: lista con viñetas. Properties: tres reglas con perillas. File: hoja. Folder: carpeta.
- `iconForSnap`: `Endpoint→SnapEndpoint`, …, `OnEdge→SnapNearest`.

- [ ] **Step 3: `commands.cpp`**

Tabla estática `const CommandInfo kCommands[]` con todos los comandos y textos (en `\u` para acentos), por ejemplo:

```cpp
{kCmdOpen, Icon::Open, L"Abrir", L"Ctrl+O", L"Abre un modelo o plano (STEP, IGES, DXF, STL...)."},
{kCmdSaveMarks, Icon::Save, L"Guardar marcas", L"Ctrl+S", L"Guarda las medidas y marcas junto al archivo."},
{kCmdExportPdf, Icon::ExportPdf, L"Exportar PDF", L"Ctrl+E", L"Una página por vista guardada, con sus marcas."},
{kCmdExportPng, Icon::ExportPng, L"Exportar PNG", nullptr, L"Guarda la vista actual como imagen."},
{kCmdTool + static_cast<int>(Tool::Distance), Icon::Distance, L"Distancia", L"D", L"Dos puntos. Con Orto (F8) mide solo horizontal o vertical."},
{kCmdSnapToggle, Icon::SnapEndpoint, L"OSNAP", L"F3", L"Enciende o apaga el enganche a objetos."},
{kCmdSnapMode + 3, Icon::SnapQuadrant, L"Cuadrante", nullptr, L"0°, 90°, 180° y 270° de círculos y arcos."},
{kCmdOrtho, Icon::Ortho, L"Orto", L"F8", L"Fija la medida a horizontal o vertical."},
{kCmdPolar, Icon::Polar, L"Polar", L"F10", L"Fija la dirección a múltiplos del ángulo elegido."},
```

(uno por cada `Command`, incluidas las 7 vistas estándar, las 12 herramientas, los 9 modos y los 4 pasos polares). `commandInfo` recorre la tabla.

- [ ] **Step 4: Compilar**

`build.sh`/`build.bat`: `VIEWER_UI="src/viewer/ui/theme.cpp src/viewer/ui/icons.cpp src/viewer/ui/commands.cpp"` (la lista crece en las tareas siguientes) y agregarla a la DLL y al visor.
Run: `./build.sh test && ./build.sh` → compila sin avisos.
Los iconos se revisan en las capturas de la cinta (Task 6).

- [ ] **Step 5: Commit**

```bash
git add src/viewer/ui src/viewer/markup_tools.cpp build.sh build.bat
git commit -m "Add graphite theme, vector icons and command table"
```

---

### Task 6: Marco, barra de título, cinta, barra de estado y tooltips

**Files:**
- Create: `src/viewer/ui/tooltip.h/.cpp`, `src/viewer/ui/popup_menu.h/.cpp`, `src/viewer/ui/ribbon.h/.cpp`, `src/viewer/ui/status_bar.h/.cpp`
- Rewrite: `src/viewer/main.cpp`
- Delete: `src/viewer/toolbar.h`, `src/viewer/toolbar.cpp`
- Modify: `src/viewer/scene_view.h/.cpp` (comandos de vista públicos, estado, escucha del cursor, `setChrome`)
- Modify: `build.sh`, `build.bat`

**Interfaces:**
- Consumes: Task 3 (`layoutRibbon`), Task 4 (`SnapSettings`, `toggle*`, `cursorPoint`, `statusText`), Task 5 (tema, iconos, comandos).
- Produces:

```cpp
// tooltip.h: ventana emergente propia (WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE)
class Tooltip {
public:
    bool create(HINSTANCE instance, HWND owner);
    // Aparece tras 500 ms quieto; titulo en negrita, atajo a la derecha, ayuda debajo.
    void show(const CommandInfo& info, POINT screenAnchor, int dpi);
    void hide();
};

// popup_menu.h: menu con el tema. Bloquea hasta elegir (bucle propio) y devuelve el id o 0.
struct MenuItem { int id = 0; std::wstring text; std::wstring shortcut; bool checked = false;
                  bool enabled = true; bool separator = false; bool dim = false; /* reciente inexistente */ };
int showPopupMenu(HWND owner, POINT screen, const std::vector<MenuItem>& items, int dpi);

// ribbon.h
class Ribbon {
public:
    using StateProvider = std::function<CommandState(int id)>;
    bool create(HINSTANCE instance, HWND parent, StateProvider state);  // WM_COMMAND(id) al padre
    HWND hwnd() const;
    int height() const;          // 0 plegada: solo la fila de pestanas
    void setDpi(int dpi);
    void setTab(int tab);        // 0 Archivo, 1 Inicio, 2 Medir, 3 Marcar, 4 Vista
    int tab() const;
    void setCollapsed(bool collapsed);
    bool collapsed() const;
    void refresh();              // vuelve a preguntar el estado de los botones y repinta
    void setColor(std::uint32_t argb);  // muestra del color actual
};

// status_bar.h
class StatusBar {
public:
    bool create(HINSTANCE instance, HWND parent);  // WM_COMMAND al padre (kCmdSnapToggle, kCmdOrtho...)
    HWND hwnd() const;
    int height() const;
    void setDpi(int dpi);
    struct State {
        std::wstring coords;     // "X 170.000  Y 35.000 mm" o vacio
        bool snap = true, ortho = false, polar = false;
        unsigned snapModes = kSnapDefaultModes;
        double polarStep = 45;
        bool plan2d = false;
        std::wstring units;      // "mm" o "sin unidades"
        int zoomPercent = 0;     // 0 = no se muestra
        std::wstring message;
    };
    void setState(const State& state);
};
```

`SceneView` gana:

```cpp
    // Comandos de vista para la cinta.
    void fit();                                  // antes privado fitView
    void standardView(int command);              // kCmdViewFront..kCmdViewIso (como las teclas 1..7)
    void togglePlan2d();                         // tecla P
    bool plan2d() const { return m_plan2d; }
    bool canPlan2d() const { return m_planar.planar; }
    void setShaded(bool on); bool shaded() const;           // drawFaces
    void setEdges(bool on); bool edges() const;             // drawEdges
    void setWireframe(bool on); bool wireframe() const;     // caras ocultas, aristas si
    void setPerspective(bool on); bool perspective() const; // !camera.ortho
    void setChrome(bool chrome);  // true en el visor: sin textos de ayuda sobre la vista
    int zoomPercent() const;      // 100 = encuadre inicial
    LengthUnit units() const { return m_mesh.units; }
    const LoadStats& stats() const { return m_stats; }
    void setStatusListener(std::function<void()> listener);  // cursor, mensajes, carga
```

- [ ] **Step 1: Marco sin borde estándar con barra de título propia**

`main.cpp` crea la ventana con `WS_OVERLAPPEDWINDOW` y:
- `WM_NCCALCSIZE` con `wparam` TRUE: devuelve 0 dejando el área cliente igual a la ventana (maximizada: recorta el borde `GetSystemMetrics(SM_CXFRAME) + SM_CXPADDEDBORDER` para no salirse del monitor).
- `DwmExtendFrameIntoClientArea` con márgenes `{0, 0, 1, 0}` cargado con `LoadLibrary(L"dwmapi.dll")` (si falla, sigue sin sombra).
- `WM_NCHITTEST`: bordes de 6 px escalados → `HTLEFT`…`HTBOTTOMRIGHT` (no si maximizada); franja superior de 32 px escalados → `HTCAPTION` salvo sobre el icono (`HTSYSMENU`) y los tres botones (`HTCLIENT`, se manejan con el ratón); resto `HTCLIENT`.
- Pintura de la franja: fondo `kBackground`, icono de la app 16 px, título "archivo — Visor STP" (con `*` si hay cambios), botones 46×32 px escalados con hover `kHover` y el de cerrar `#C42B1C`. Clic en los botones: `ShowWindow(SW_MINIMIZE)`, `SW_MAXIMIZE`/`SW_RESTORE`, `WM_CLOSE`. Doble clic en la franja y Aero Snap los resuelve Windows por `HTCAPTION`. Clic derecho en la franja: `GetSystemMenu` + `TrackPopupMenu` + `WM_SYSCOMMAND`.

- [ ] **Step 2: Cinta**

`ribbon.cpp`: una ventana hija. Fila de pestañas (28 px) + cuerpo (92 px) a 96 DPI. Contenido por pestaña (grupos rotulados abajo en `kTextDim` 8 pt, separador vertical `kBorder`):
- Archivo: [Archivo: Abrir, Recientes▾] [Marcas: Guardar marcas] [Exportar: Exportar PDF, Exportar PNG] [Ayuda: Acerca de].
- Inicio: [Navegar: Navegar, Encuadrar] [Vistas: Vistas estándar▾, Plano 2D/3D].
- Medir: [Medir: Distancia, Radio, Ángulo, Área] [Enganche: OSNAP grande + 9 casillas en 3 columnas pequeñas] [Restricciones: Orto, Polar▾ (15/30/45/90)].
- Marcar: [Dibujar: Resaltador, Subrayado, Nota, Rectángulo, Elipse, Nube, Lápiz] [Color: muestra▾] [Editar: Deshacer, Rehacer, Borrar].
- Vista: [Estilo: Sombreado, Alambre, Aristas, Perspectiva] [Mostrar: Panel, Barra de estado, Cubo de vistas].

Botón grande: icono 32 px arriba + texto debajo (2 líneas máx.). Pequeño: icono 16 px + texto al lado, en columnas de 3. Colapsado: icono 32 + nombre del grupo + ▾, abre `showPopupMenu` con los comandos del grupo. Anchos de cada grupo medidos con `measureText` → `RibbonGroupSpec` → `layoutRibbon(groups, width, gap, width < scale(640))`. Estados con `StateProvider`: `checked` → fondo `kPressed` y borde inferior `kAccent` de 2 px; deshabilitado → icono y texto en `kTextDim` al 50 %. Hover con transición de 120 ms (`SetTimer` 15 ms, `blend`). Tooltip tras 500 ms quieto sobre un botón. Doble clic en una pestaña → `setCollapsed(!collapsed)` y `WM_COMMAND(kCmdRibbonToggle)` al padre; plegada, un clic en una pestaña abre el cuerpo flotante hasta el siguiente clic.
- Casillas de enganche: cuadrito 12 px con tilde cuando el bit está encendido.

- [ ] **Step 3: Barra de estado**

`status_bar.cpp`: 26 px a 96 DPI, fondo `kPanel`, borde superior `kBorder`. De izquierda a derecha: coordenadas (ancho fijo medido con "X -00000.000  Y -00000.000  Z -00000.000 mm"), botones-píldora OSNAP · ORTO · POLAR (encendido: texto blanco sobre `kAccent`; apagado: `kTextDim` sin fondo; clic → `WM_COMMAND` al padre; clic derecho en OSNAP → menú con los 9 modos y "Configuración de enganche…" omitido (YAGNI); clic derecho en POLAR → menú 15°/30°/45°/90°), luego a la derecha: mensaje (recortado con …), "2D"/"3D", unidades, "Zoom 125 %".

- [ ] **Step 4: `main.cpp` como marco**

Estructura:

```cpp
struct Frame {
    HWND hwnd; int dpi;
    stp::SceneView view; stp::ui::Ribbon ribbon; stp::ui::StatusBar status;
    bool statusVisible = true;
    void layout();                  // titulo | cinta | [vista | panel] | estado
    void execute(int command);      // despacho de todos los comandos
    stp::ui::CommandState state(int command);
    void refreshStatus();           // arma StatusBar::State desde view y tools
    bool openFile(const std::wstring& path);
    void exportPdf(); void exportPng();
    void updateTitle();
};
```

`execute` reúne lo que hoy hacen `WM_COMMAND`, `WM_KEYDOWN` y el bucle de mensajes: abrir, recientes, guardar, exportar PDF (lo actual de `exportMarkup`), exportar PNG (`renderSnapshot` + `savePng`), acerca de (`MessageBoxW` con versión), herramientas (`tools()->setTool`), enganche (`setSnapSettings`), vistas (`view.standardView`), estilos, panel, barra de estado, cubo. Los atajos del bucle de mensajes (`Ctrl+O/S/E`, `F2`, `Esc`) se mantienen y pasan por `execute`. `F3/F8/F10/Tab` llegan a la vista por su propio `WM_KEYDOWN`/`WM_SYSKEYDOWN` (Task 4); `F10` no debe activar el menú del sistema: el marco no tiene menú, y la vista devuelve 0 en `WM_SYSKEYDOWN` de `VK_F10`.

El listener de marcas (`setMarkupListener`) llama `ribbon.refresh()`, `status` y el panel (Task 7). El listener de estado llama `refreshStatus()`.

- [ ] **Step 5: Retirar la barra vertical y la lista**

Borrar `toolbar.h/.cpp` (`git rm`), sus usos en `main.cpp`, `g_list`, `refreshList` y `build.sh`/`build.bat`. `SceneView::setChrome(true)` desactiva en `drawOverlay` los textos de ayuda y el mensaje de marcas (pasan a la barra de estado); los avisos de carga (`m_message`) también van a la barra de estado vía el listener.

- [ ] **Step 6: Verificar bajo wine**

Run: `./build.sh`; abrir el visor en Xvfb 1400×900 con `plano_brida.dxf`; capturas de cada pestaña (clic en pestañas con xdotool), tooltip sobre "Distancia", menú de la píldora OSNAP, ventana a 600 px de ancho (cinta en iconos pequeños / colapsada), maximizar y restaurar con los botones propios, arrastrar desde la franja del título.
Expected: nada se solapa; título y botones alineados; texto legible; los comandos funcionan.

- [ ] **Step 7: Commit**

```bash
git add -A src/viewer build.sh build.bat
git commit -m "Replace toolbar with custom title bar, ribbon and status bar"
```

---

### Task 7: Panel lateral (Marcas / Propiedades) y pantalla de inicio

**Files:**
- Create: `src/viewer/ui/side_panel.h/.cpp`, `src/viewer/ui/start_page.h/.cpp`
- Modify: `src/viewer/main.cpp`, `src/viewer/markup_tools.h/.cpp` (acceso a color y texto de la marca seleccionada), `build.sh`, `build.bat`

**Interfaces:**
- Consumes: `MarkupTools::listEntries()`, `focusEntry(code)`, `describe(mark)`, `select(id)`, `selected()`, `document()`; `SceneView::stats()`, `units()`, `plan2d()`; Task 3 `RecentFiles`.
- Produces:

```cpp
class SidePanel {
public:
    bool create(HINSTANCE instance, HWND parent);
    HWND hwnd() const;
    void setDpi(int dpi);
    void attach(SceneView* view);  // lee marcas, seleccion y modelo de aqui
    void refresh();                // tras markupChanged o al cargar
    int preferredWidth() const;    // ancho elegido por el usuario (arrastre del borde izquierdo)
    void setPreferredWidth(int width);
    void setFloating(bool floating);  // ventana angosta: flotante encima de la vista
};

// MarkupTools
    bool setMarkColor(int id, std::uint32_t argb);        // con deshacer
    bool setNoteText(int id, const std::string& utf8);    // con deshacer
    const Mark* markById(int id) const;

class StartPage {
public:
    bool create(HINSTANCE instance, HWND parent);  // WM_COMMAND(kCmdOpen / kCmdRecentFirst + i)
    HWND hwnd() const;
    void setDpi(int dpi);
    void setRecent(const std::vector<std::wstring>& items);  // inexistentes en gris
};
```

- [ ] **Step 1: Panel**

Dos secciones con encabezado plegable (icono Marks/Properties + título 10 pt): **Marcas** arriba (lista propia dibujada, filas de 24 px: vistas como nodos con triángulo ▸/▾ y sus marcas indentadas con el icono de su tipo — Distance, Radius… — y `describe(mark)` recortado; hover `kHover`, seleccionada `kPressed` con barra `kAccent` a la izquierda; clic → `focusEntry(code)`; rueda desplaza; `Supr` borra la seleccionada). **Propiedades** abajo (divisor arrastrable): con marca seleccionada: tipo, valor (`describe`), color (6 muestras redondas `kMarkRed`… clicables → `setMarkColor`), vista a la que pertenece, y para notas un `EDIT` multilínea con el tema (`WM_CTLCOLOREDIT` → `kBackground`/`kText`) que confirma con `EN_KILLFOCUS`/Enter → `setNoteText`. Sin selección: archivo, formato (extensión), unidades, tamaño (`bounds.size()` con `formatLength`), caras/triángulos/segmentos (`stats()`), 2D/3D. Borde izquierdo de 4 px arrastrable (`SetCapture`) entre 200 y 480 px escalados. Fondo `kPanel`, borde `kBorder`.

- [ ] **Step 2: Pantalla de inicio**

Se muestra en lugar de la vista cuando no hay archivo: logo (icono de la app a 64 px), "Visor STP" 20 pt, subtítulo "STEP · IGES · DXF · STL · OBJ · 3MF" en `kTextDim`, botón primario "Abrir archivo" (`kAccent`, 36 px de alto, Ctrl+O), lista "Recientes" (nombre en `kText`, carpeta en `kTextDim`; inexistentes `GetFileAttributesW == INVALID_FILE_ATTRIBUTES` en gris con "(no se encuentra)"; clic en uno inexistente → `MessageBoxW` "¿Quitarlo de la lista?" → `WM_COMMAND(kCmdRecentFirst + i)` con `lparam = 1` para quitar), y abajo "o arrastra un archivo aquí". `DragAcceptFiles` del marco ya cubre soltar.

- [ ] **Step 3: Disposición**

`Frame::layout`: con archivo, vista a la izquierda y panel a la derecha si visible (`F2` / botón Panel). Ventana < 640 px escalados: el panel se oculta solo y `F2` lo abre `setFloating(true)` encima del borde derecho de la vista.

- [ ] **Step 4: Verificar bajo wine**

Capturas: pantalla de inicio con 3 recientes (uno inexistente), panel con marcas de `plano_brida.dxf` (dos distancias y una nota), propiedades de la nota editando su texto, propiedades del modelo sin selección, panel flotante a 600 px.

- [ ] **Step 5: Commit**

```bash
git add -A src/viewer build.sh build.bat
git commit -m "Add side panel for marks and properties, and start page"
```

---

### Task 8: Cubo de vistas

**Files:**
- Create: `src/viewer/ui/view_cube.h/.cpp`
- Modify: `src/viewer/scene_view.h/.cpp`, `build.sh`, `build.bat`

**Interfaces:**
- Consumes: Task 3 (`cubeRegionAt`, `cubeOrientation`, `cubeFaceName`, `interpolateOrientation`), Task 5 (tema, `Icon::Home`).
- Produces:

```cpp
class ViewCube {
public:
    // Rectangulo que ocupa arriba a la derecha de una vista de w x h (con margen).
    RECT bounds(int width, int height, int dpi, bool compact) const;
    void draw(Gdiplus::Graphics& g, const Camera& camera, const PlanarInfo* plan, const RECT& bounds,
              int dpi) const;  // plan != null: brujula con la flecha "arriba" del dibujo
    // Region o boton bajo el punto; false si esta fuera.
    enum class Hit { None, Region, Home };
    Hit hitTest(const Camera& camera, const RECT& bounds, POINT p, ui::CubeRegion* region) const;
    void setHover(Hit hit, const ui::CubeRegion& region);
};
```

`SceneView` gana `setCubeVisible(bool)`, `cubeVisible()`, y una animación: `animateTo(yaw, pitch)` con `SetTimer` de 15 ms durante 250 ms usando `interpolateOrientation` (render interactivo en cada paso y pase de calidad al final).

- [ ] **Step 1: Dibujo**

Cubo de 90 px escalados (60 en el panel compacto) dibujado con la orientación de la cámara: proyectar los 8 vértices de `[-1,1]^3` con `right/up` de la cámara, ordenar las 6 caras por profundidad (`dot(normal, forward)` < 0 son visibles), rellenar visibles con `kPanel` (con hover: `kHover`; región bajo el cursor resaltada en `kAccent` al 60 %), aristas `kBorder`, y el nombre de la cara (`cubeFaceName`) centrado en 8 pt en `kText` transformado con la cara (matriz afín de las dos aristas de la cara) — si la cara está muy inclinada (área proyectada < 25 %), sin texto. Debajo, un anillo de brújula (elipse proyectada del plano XY con "N" hacia +Y). Botón Inicio (Icon::Home 16 px) arriba a la izquierda del cubo.
En 2D (`m_plan2d`): solo la brújula circular con una flecha que apunta a `planUp` proyectado y la letra "N" si `planUp` es +Y del mundo; sin clics salvo Inicio (= encuadrar).

- [ ] **Step 2: Interacción**

`SceneView::handle`: antes de las herramientas y de la órbita, si el ratón cae en `m_cube.bounds(...)`: `WM_MOUSEMOVE` → hover y repintar solo el overlay; `WM_LBUTTONUP` en región → `cubeOrientation` → `animateTo`; en Home → `animateTo(-0.7853982, 0.5235988)` + `fit()`. Arrastrar sobre el cubo orbita igual que en la vista.

- [ ] **Step 3: Verificar bajo wine**

Capturas: cubo en isométrica con hover sobre "Frente", tras clic en una esquina (mitad de la animación y final), vista superior, brújula en `plano_brida.dxf`, panel del Explorador (`previewtest.exe`) con cubo pequeño.

- [ ] **Step 4: Commit**

```bash
git add -A src/viewer build.sh build.bat
git commit -m "Add animated view cube and 2D compass"
```

---

### Task 9: Configuración en el registro, recientes y DPI

**Files:**
- Create: `src/viewer/settings.h`, `src/viewer/settings.cpp`
- Modify: `src/viewer/main.cpp`, `src/viewer/scene_view.h/.cpp`, `src/viewer/ui/*.cpp` (reaccionar a `setDpi`), `build.sh`, `build.bat`

**Interfaces:**
- Produces:

```cpp
struct ViewerSettings {
    unsigned snapModes = kSnapDefaultModes;
    bool snapOn = true, orthoOn = false, polarOn = false;
    double polarStep = 45;
    bool ribbonCollapsed = false;
    int ribbonTab = 2;           // Medir
    bool panelVisible = true;
    int panelWidth = 280;        // a 96 DPI
    bool statusVisible = true, cubeVisible = true;
    RECT window = {0, 0, 0, 0};  // vacio = por defecto
    bool maximized = false;
    std::vector<std::wstring> recent;
};
ViewerSettings loadSettings();              // HKCU\Software\stp-viewer; lo que falte o sea invalido, por defecto
void saveSettings(const ViewerSettings&);   // sin avisar si falla
SnapSettings loadSnapSettings();            // solo modos y estados: lo usa el panel del Explorador
```

- [ ] **Step 1: `settings.cpp`**

Valores `REG_DWORD`: `SnapModes` (enmascarado con `kSnapAllModes`), `Snap`, `Ortho`, `Polar` (si ambos, gana Orto), `PolarStep` (solo 15/30/45/90; si no, 45), `RibbonCollapsed`, `RibbonTab` (0..4), `Panel`, `PanelWidth` (160..800), `StatusBar`, `ViewCube`, `Maximized`; `REG_BINARY` `Window` (RECT, se descarta si no cae en ningún monitor con `MonitorFromRect(MONITOR_DEFAULTTONULL)`); `REG_MULTI_SZ` `Recent` → `RecentFiles::setItems`. `RegCreateKeyExW`/`RegSetValueExW`/`RegGetValueW`; cada lectura fallida deja el valor por defecto.

- [ ] **Step 2: Uso**

`wWinMain`: `loadSettings()` antes de crear la ventana (posición/tamaño/maximizada, pestaña, cinta plegada, panel, barra, cubo) y aplica `SnapSettings` a las herramientas al cargar cada archivo. `openFile` agrega a recientes. `WM_CLOSE` (tras confirmar) guarda todo con `GetWindowPlacement`. Menú Recientes (botón de la cinta y pantalla de inicio) desde `RecentFiles::items()`; inexistentes en gris y al elegirlos se ofrece quitarlos.

- [ ] **Step 3: DPI**

`WM_DPICHANGED`: `dpi = HIWORD(wparam)`; `SetWindowPos` con el `RECT*` sugerido; `setDpi(dpi)` en cinta, barra de estado, panel, pantalla de inicio, tooltip; la vista ya lee su DPI (verificar que `SceneView` actualiza `m_dpi` en `WM_DPICHANGED_AFTERPARENT` o al cambiar de tamaño con `GetDpiForWindow` cargado dinámicamente de `user32`).

- [ ] **Step 4: Verificar bajo wine**

Cerrar con Orto encendido, pestaña Marcar y panel oculto → reabrir: se conserva. `wine reg query HKCU\\Software\\stp-viewer`. Correr con `WINEDPI` a 144 (`wine reg add "HKCU\\Control Panel\\Desktop" /v LogPixels /t REG_DWORD /d 144 /f`) y capturar: todo escalado.

- [ ] **Step 5: Commit**

```bash
git add -A src/viewer build.sh build.bat
git commit -m "Persist viewer settings and recent files, react to DPI changes"
```

---

### Task 10: Mini barra del panel del Explorador

**Files:**
- Modify: `src/viewer/scene_view.h/.cpp`
- Modify: `src/shellext/preview_handler.cpp` (si hace falta para el foco/teclas)

**Interfaces:**
- Consumes: Task 4 (`SnapSettings`, `toggleSnap`, `toggleOrtho`), Task 5 (iconos, tema), Task 8 (cubo compacto), Task 9 (`loadSnapSettings`).
- Produces: nada nuevo hacia afuera; `SceneView` dibuja y atiende la mini barra cuando `m_toolsEnabled && !editing` (el panel).

- [ ] **Step 1: Mini barra**

Píldora semitransparente (`kPanel` al 85 %, borde `kBorder`, radio 8 px) centrada arriba con 7 botones de 28 px: Navegar, Distancia, Radio, Ángulo, Área, separador, OSNAP, ORTO (encendidos con `kAccent`). Tooltip simple (título + atajo) con `TTM` de la vista o el `Tooltip` de Task 6. Sobre fondo claro del tema del Explorador (`m_hostColors` con luminancia > 0.5) la píldora usa blanco al 90 % y texto oscuro. Debajo de la píldora, la línea de ayuda / mensaje (`statusText()`), en 9 pt, solo mientras hay herramienta activa o mensaje vigente.
Al crear las herramientas del panel: `tools()->setSnapSettings(loadSnapSettings())` (solo lectura).

- [ ] **Step 2: Verificar bajo wine**

`wine dist/previewtest.exe tests/samples/plano_brida.dxf` (y un STEP): capturas con la mini barra, midiendo la ranura (70 mm) con marcadores "Cuadrante", y con tema claro simulado.

- [ ] **Step 3: Commit**

```bash
git add -A src/viewer src/shellext
git commit -m "Add floating measure bar to the Explorer preview pane"
```

---

### Task 11: README, verificación completa y entrega

**Files:**
- Modify: `README.md`

- [ ] **Step 1: README**

Sección nueva "Enganche y restricciones" (tabla de modos con su marcador, prioridad, `F3/F8/F10/Tab/Shift`, ejemplo de la ranura) y sección "Interfaz" (cinta por pestañas, cubo de vistas, panel `F2`, barra de estado y sus menús, pantalla de inicio, configuración en `HKCU\Software\stp-viewer`). Actualizar la tabla de atajos (quitar lo de la barra vertical).

- [ ] **Step 2: Verificación**

- `./build.sh test` (todas pasan) y `./build.sh` (sin avisos nuevos).
- Wine, visor a 100 % y 150 %, ventana angosta (600 px) y ancha (1600 px): cada pestaña, tooltip, cubo, barra de estado con OSNAP/ORTO/POLAR y su menú, pantalla de inicio, panel lateral, medida de la ranura (70 y 20 mm), Orto y Polar en un STEP.
- Panel del Explorador (`previewtest.exe`) con plano y pieza.
- Benchmark de render sin regresión: `src/tools/benchrender.cpp` compilado nativo (o la herramienta nativa `build/steprender`) antes/después sobre `tests/samples/*.stp`.

- [ ] **Step 3: Revisión final y entrega**

Revisión de toda la rama con un revisor fresco (modelo más capaz); corregir los hallazgos importantes con prueba RED→GREEN cuando sean del motor. Zip `stp-viewer-osnap.zip` con `dist/` + README, enviado con `SendUserFile`.

- [ ] **Step 4: Commit y push**

```bash
git add README.md
git commit -m "Document object snap, constraints and the new interface"
git push origin main
```
