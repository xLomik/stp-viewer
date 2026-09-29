# Miniaturas rápidas — plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Que una carpeta con muchos archivos CAD muestre sus miniaturas tan rápido como sin ellas: miniatura más barata y caché de Windows preparada de antemano.

**Architecture:** La DLL dibuja cada miniatura con el raster interno limitado a 1024 px y un solo hilo (regla portable en el renderer). Un preparador en `stpviewer.exe` pide las miniaturas a la caché de Windows (`IThumbnailCache`, `WTS_EXTRACT`) en segundo plano, desde el menú contextual de carpetas o automáticamente al abrir un archivo; la selección y el orden de archivos es lógica portable en `src/ui/`.

**Tech Stack:** C++17, mingw-w64, Win32 + COM (`thumbcache.h`, `shobjidl.h`), pruebas nativas `./build.sh test`, wine.

**Spec:** `docs/superpowers/specs/2026-09-29-miniaturas-rapidas-design.md`

## Global Constraints

- Raster interno de la miniatura ≤ 1024 px por lado; supermuestreo `clamp(1024 / tamaño, 1, tamaño >= 256 ? 2 : 3)`; `threads = 1`.
- Preparación: `min(núcleos − 1, 4)` hilos (mínimo 1), `THREAD_MODE_BACKGROUND_BEGIN`, máximo 2 000 archivos por carpeta, los más recientes primero, tamaño 256 × DPI / 96.
- Automático solo en unidades fijas (`DRIVE_FIXED`); nunca red ni extraíbles.
- Sin `CLSID_LocalThumbnailCache` disponible: no hace nada y no avisa.
- Menú contextual solo para el usuario (HKCU), puesto por `instalar.bat` y quitado por `desinstalar.bat`.
- Literales en ASCII con `\uXXXX`; commits con `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` y `Claude-Session: https://claude.ai/code/session_015NM5546gRGTfskkpAXPqTF`.

## Review Focus

1. Carpeta con miles de archivos o subcarpetas: solo archivos CAD de ese nivel, tope 2 000, sin recursión → prueba `thumbnail_warmup_limits_and_skips_folders` (Tarea 2).
2. Extensiones en mayúsculas (`.DXF`, `.StP`) y nombres sin extensión o con punto final → prueba `thumbnail_warmup_candidates` (Tarea 2).
3. Cerrar el visor en plena preparación: los hilos terminan antes de destruir nada (cancelar + join en el destructor) → verificación en wine (Tarea 3).
4. Tamaños extremos pedidos por el Explorador (16 px, 2560 px): nunca raster > 1024 salvo que el tamaño mismo lo exija (supermuestreo mínimo 1) → prueba `thumbnail_supersample_caps_raster` (Tarea 1).
5. Ruta con espacios o acentos en el menú contextual → comillas en el registro y `CommandLineToArgvW` (Tarea 3, verificación en wine con carpeta "con espacio ñ").

---

### Task 1: Miniatura más barata

**Files:**
- Modify: `src/render/renderer.h`, `src/render/renderer.cpp`
- Modify: `src/shellext/thumbnail_provider.cpp`, `src/tools/thumbbench.cpp`
- Test: `tests/unit_tests.cpp`

**Interfaces:**
- Produces: `int thumbnailSupersample(int size);` (namespace `stp`, en `renderer.h`).

- [ ] **Step 1: Prueba que falla**

```cpp
TEST(thumbnail_supersample_caps_raster) {
    CHECK(stp::thumbnailSupersample(16) == 3);
    CHECK(stp::thumbnailSupersample(96) == 3);
    CHECK(stp::thumbnailSupersample(256) == 2);
    CHECK(stp::thumbnailSupersample(512) == 2);
    CHECK(stp::thumbnailSupersample(768) == 1);
    CHECK(stp::thumbnailSupersample(1024) == 1);
    CHECK(stp::thumbnailSupersample(2560) == 1);
    for (int size = 16; size <= 1024; size += 8) CHECK(size * stp::thumbnailSupersample(size) <= std::max(1024, size));
}
```

- [ ] **Step 2:** `./build.sh test` → no compila (`thumbnailSupersample` no existe).

- [ ] **Step 3: Implementación**

`renderer.h` (después de `renderMesh`):

```cpp
// Supermuestreo para una miniatura de size px: el raster interno no pasa de 1024 px
// por lado (en tamanos grandes el propio tamano ya da el detalle y el Explorador
// achica la imagen al mostrarla).
int thumbnailSupersample(int size);
```

`renderer.cpp`:

```cpp
int thumbnailSupersample(int size) {
    const int most = size >= 256 ? 2 : 3;
    return std::max(1, std::min(most, 1024 / std::max(1, size)));
}
```

`thumbnail_provider.cpp`: `style.supersample = stp::thumbnailSupersample(static_cast<int>(size));` y `style.threads = 1;` con el comentario "El Explorador pide varias miniaturas a la vez: un hilo por miniatura". `thumbbench.cpp`: la misma regla y `threads = 1` (se quitan las variables de entorno de prueba).

- [ ] **Step 4:** `./build.sh test` pasa; `build/thumbbench 256|768|1024 lote/*.dxf` ≤ 8 ms por archivo; `thumbtest --lote` bajo wine.

- [ ] **Step 5: Commit** — "Make thumbnails cheaper at large sizes".

---

### Task 2: Qué archivos preparar (lógica portable)

**Files:**
- Create: `src/ui/thumbnail_warmup.h`, `src/ui/thumbnail_warmup.cpp`
- Modify: `src/shellext/dllmain.cpp` (usa la misma lista de extensiones), `build.sh`, `build.bat`
- Test: `tests/unit_tests.cpp`

**Interfaces:**
- Produces (namespace `stp::ui`):
  - `const std::vector<std::wstring>& cadExtensions();` (minúsculas, con punto; las mismas que registra la DLL)
  - `bool isCadFile(const std::wstring& name);`
  - `struct WarmupFile { std::wstring name; std::uint64_t size = 0; std::int64_t modified = 0; bool folder = false; };`
  - `std::vector<std::wstring> warmupOrder(std::vector<WarmupFile> files, std::size_t limit = 2000);`

- [ ] **Step 1: Pruebas que fallan**

```cpp
TEST(thumbnail_warmup_candidates) {
    CHECK(stp::ui::isCadFile(L"pieza.DXF"));
    CHECK(stp::ui::isCadFile(L"Ensamble.StP"));
    CHECK(stp::ui::isCadFile(L"a.b.sldprt"));
    CHECK(!stp::ui::isCadFile(L"notas.txt"));
    CHECK(!stp::ui::isCadFile(L"dxf"));
    CHECK(!stp::ui::isCadFile(L"raro."));
    CHECK(!stp::ui::isCadFile(L".dxf.bak"));
    const auto order = stp::ui::warmupOrder({{L"viejo.dxf", 10, 100}, {L"nuevo.stp", 10, 300}, {L"medio.igs", 10, 200},
                                             {L"foto.jpg", 10, 400}, {L"vacio.dxf", 0, 500}});
    CHECK((order == std::vector<std::wstring>{L"nuevo.stp", L"medio.igs", L"viejo.dxf"}));
}

TEST(thumbnail_warmup_limits_and_skips_folders) {
    std::vector<stp::ui::WarmupFile> files;
    for (int i = 0; i < 2500; ++i) files.push_back({L"p" + std::to_wstring(i) + L".dxf", 1, i});
    files.push_back({L"carpeta.dxf", 1, 99999, true});
    const auto order = stp::ui::warmupOrder(files);
    CHECK(order.size() == 2000);
    CHECK(order.front() == L"p2499.dxf");
    CHECK(std::find(order.begin(), order.end(), L"carpeta.dxf") == order.end());
    CHECK(stp::ui::warmupOrder(files, 3).size() == 3);
}
```

- [ ] **Step 2:** `./build.sh test` → no compila.

- [ ] **Step 3: Implementación**

```cpp
// thumbnail_warmup.h
// Que archivos de una carpeta conviene pasar por la cache de miniaturas y en que
// orden. Sin Windows: se prueba en Linux.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace stp {
namespace ui {

// Extensiones con miniatura de stp-viewer, en minusculas y con punto.
const std::vector<std::wstring>& cadExtensions();
bool isCadFile(const std::wstring& name);

struct WarmupFile {
    std::wstring name;
    std::uint64_t size = 0;
    std::int64_t modified = 0;  // cualquier escala creciente (FILETIME)
    bool folder = false;
};

// Archivos CAD no vacios, los mas recientes primero, como mucho limit.
std::vector<std::wstring> warmupOrder(std::vector<WarmupFile> files, std::size_t limit = 2000);

}  // namespace ui
}  // namespace stp
```

```cpp
// thumbnail_warmup.cpp
#include "thumbnail_warmup.h"

#include <algorithm>
#include <cwctype>

namespace stp {
namespace ui {

const std::vector<std::wstring>& cadExtensions() {
    static const std::vector<std::wstring> list = {L".stp", L".step", L".stpz", L".igs", L".iges", L".dxf",
                                                   L".stl", L".obj", L".ply", L".dwg", L".prt", L".sldprt",
                                                   L".sldasm", L".ipt", L".iam", L".catpart", L".catproduct"};
    return list;
}

bool isCadFile(const std::wstring& name) {
    const std::size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0 || dot + 1 >= name.size()) return false;
    std::wstring ext = name.substr(dot);
    for (wchar_t& c : ext) c = static_cast<wchar_t>(std::towlower(c));
    const auto& list = cadExtensions();
    return std::find(list.begin(), list.end(), ext) != list.end();
}

std::vector<std::wstring> warmupOrder(std::vector<WarmupFile> files, std::size_t limit) {
    files.erase(std::remove_if(files.begin(), files.end(),
                               [](const WarmupFile& f) { return f.folder || f.size == 0 || !isCadFile(f.name); }),
                files.end());
    std::stable_sort(files.begin(), files.end(),
                     [](const WarmupFile& a, const WarmupFile& b) { return a.modified > b.modified; });
    std::vector<std::wstring> names;
    for (const WarmupFile& f : files) {
        if (names.size() >= limit) break;
        names.push_back(f.name);
    }
    return names;
}

}  // namespace ui
}  // namespace stp
```

`dllmain.cpp`: el arreglo `kExtensions` se reemplaza por `stp::ui::cadExtensions()` (recorrer el vector y usar `.c_str()`), así la DLL y el preparador nunca difieren. Agregar `src/ui/thumbnail_warmup.cpp` a `ENGINE` en `build.sh` y `build.bat`.

- [ ] **Step 4:** `./build.sh test` pasa; `./build.sh` compila.

- [ ] **Step 5: Commit** — "Add portable selection of files for thumbnail warm-up".

---

### Task 3: Preparar la caché de Windows

**Files:**
- Create: `src/viewer/thumbnail_warmup_win.h`, `src/viewer/thumbnail_warmup_win.cpp`
- Modify: `src/viewer/main.cpp` (modo `--miniaturas`, automático al abrir), `install/instalar.bat`, `install/desinstalar.bat`, `build.sh`, `build.bat`, `README.md`

**Interfaces:**
- Consumes: `stp::ui::warmupOrder`, `stp::ui::WarmupFile` (Tarea 2); tema `ui::` del visor.
- Produces:

```cpp
namespace stp {
class ThumbnailWarmup {
public:
    struct Progress { int total = 0, done = 0, cached = 0, failed = 0; bool finished = false, available = true; };
    ~ThumbnailWarmup();  // cancela y espera a los hilos
    // Empieza en segundo plano; notify (puede ser nullptr) recibe message en cada avance.
    void start(const std::wstring& folder, int size, HWND notify = nullptr, UINT message = 0);
    void cancel();       // pide parar y espera
    Progress progress() const;
    const std::wstring& folder() const;
};
int runThumbnailWarmupWindow(HINSTANCE instance, const std::wstring& folder);  // modo --miniaturas
}
```

- [ ] **Step 1: `ThumbnailWarmup`**

`start`: `cancel()` previo; enumera la carpeta con `FindFirstFileExW(FindExInfoBasic, FIND_FIRST_EX_LARGE_FETCH)` a `WarmupFile` (tamaño de `nFileSizeHigh/Low`, fecha de `ftLastWriteTime`, carpeta por `FILE_ATTRIBUTE_DIRECTORY`), `warmupOrder`, y lanza `std::max(1, std::min(4, hardware_concurrency() - 1))` hilos que toman el siguiente índice con un `std::atomic<int>`. Cada hilo: `SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN)`, `CoInitializeEx(nullptr, COINIT_MULTITHREADED)`, `CoCreateInstance(kCLSID_LocalThumbnailCache {50EF4544-AC9F-4A8E-B21B-8A26180DB13F}, nullptr, CLSCTX_INPROC_SERVER, kIID_IThumbnailCache {F676C15D-596A-4CE2-8234-33996F445DB1})`; si falla, `available = false` y termina. Por archivo: `SHCreateItemFromParsingName(ruta, nullptr, IID_IShellItem)`, `cache->GetThumbnail(item, size, WTS_EXTRACT, &bitmap, &flags, nullptr)`; `WTS_CACHED` en `flags` cuenta como `cached`, `SUCCEEDED` como `done`, lo demás `failed`; `bitmap->Release()`. Aviso con `PostMessageW(notify, message, 0, 0)` como mucho cada 100 ms y al terminar. `cancel`: bandera atómica y `join` de todos.

- [ ] **Step 2: Modo `--miniaturas`**

`wWinMain`: si el primer argumento es `--miniaturas`, llamar `runThumbnailWarmupWindow(instance, argv[1])` y salir. Ventana `WS_POPUP | WS_CAPTION | WS_SYSMENU` de 460×170 escalados, centrada, con fondo `kBackground` pintado con `paintBuffered`: título "Preparando miniaturas" 12 pt, nombre de la carpeta (recortado), barra de progreso redondeada (`kPanel` y `kAccent`), línea "34 de 70 · 12 ya estaban listas", botón "Cancelar" (clic → `cancel()` y cerrar). Al terminar: "Listo: 70 miniaturas" y se cierra a los 2 s (`SetTimer`). Si `available == false`: "Esta versión de Windows no permite preparar miniaturas" y cierra a los 4 s. Tamaño: `256 * GetDeviceCaps(LOGPIXELSX) / 96`.

- [ ] **Step 3: Automático al abrir**

`Frame` gana `stp::ThumbnailWarmup warmup;`. En `openFile`, tras cargar: carpeta del archivo; si `GetDriveTypeW(raíz) == DRIVE_FIXED` y la carpeta difiere de `warmup.folder()`, `warmup.start(carpeta, 256 * dpi / 96)`. El destructor de `Frame` (o `WM_DESTROY`) llama `warmup.cancel()` antes de salir del bucle de mensajes.

- [ ] **Step 4: Menú contextual**

`instalar.bat` (dentro de `if exist "%EXE%"`):

```bat
for %%K in ("Directory" "Directory\Background") do (
    reg add "HKCU\Software\Classes\%%~K\shell\stpviewer.miniaturas" /ve /d "Preparar miniaturas CAD" /f >nul
    reg add "HKCU\Software\Classes\%%~K\shell\stpviewer.miniaturas" /v Icon /d "\"%EXE%\",0" /f >nul
)
reg add "HKCU\Software\Classes\Directory\shell\stpviewer.miniaturas\command" /ve /d "\"%EXE%\" --miniaturas \"%%1\"" /f >nul
reg add "HKCU\Software\Classes\Directory\Background\shell\stpviewer.miniaturas\command" /ve /d "\"%EXE%\" --miniaturas \"%%V\"" /f >nul
```

`desinstalar.bat`: `reg delete` de las dos claves `...\shell\stpviewer.miniaturas`.

- [ ] **Step 5: Verificación**

`./build.sh`; wine: `stpviewer.exe --miniaturas "build/lote con espacio ñ"` abre la ventana y cierra sola sin errores (wine no tiene la caché: se ve el mensaje de "no permite"); abrir un archivo del lote en el visor y cerrarlo mientras prepara: sin cuelgue. `wine reg query` de las claves del menú tras `instalar.bat` no aplica (no hay cmd completo): verificar el texto del .bat con `grep`.

- [ ] **Step 6:** README ("Miniaturas rápidas": menú "Preparar miniaturas CAD", automático al abrir, qué hace Windows con la caché) y commit "Warm the Windows thumbnail cache in the background".

---

### Task 4: Verificación, revisión y entrega

- [ ] `./build.sh test`, `./build.sh`, `thumbbench` y `thumbtest --lote` (antes/después en el README).
- [ ] Revisión final con un revisor fresco (modelo más capaz); corregir hallazgos importantes.
- [ ] Zip con `dist/` y README vía `SendUserFile`; push a `main`.
