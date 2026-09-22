#!/usr/bin/env bash
# Compilacion cruzada desde Linux hacia Windows x64 con mingw-w64.
#   ./build.sh            -> todo (DLL, visor, herramientas)
#   ./build.sh native     -> solo la herramienta de linea de comandos para Linux
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p build dist

CXX_WIN=${CXX_WIN:-x86_64-w64-mingw32-g++}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
STRIP=${STRIP:-x86_64-w64-mingw32-strip}

ENGINE="src/engine/step_file.cpp src/engine/step_model.cpp src/engine/surfaces.cpp src/engine/tessellate.cpp src/render/renderer.cpp"
FLAGS="-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-delete-non-virtual-dtor"
STATIC="-static -static-libgcc -static-libstdc++"

if [ "${1:-all}" = "native" ]; then
    g++ $FLAGS $ENGINE src/tools/steprender.cpp -o build/steprender
    echo "build/steprender"
    exit 0
fi

# Herramienta nativa: util para revisar el mallado sin Windows de por medio.
g++ $FLAGS $ENGINE src/tools/steprender.cpp -o build/steprender

# Manejador de miniaturas del Explorador (DLL COM).
$CXX_WIN $FLAGS -shared -o dist/StepShellExt.dll \
    src/shellext/dllmain.cpp src/shellext/thumbnail_provider.cpp \
    src/shellext/preview_handler.cpp src/viewer/scene_view.cpp $ENGINE \
    src/shellext/shellext.def \
    $STATIC -lole32 -loleaut32 -luuid -lshlwapi -lshell32 -ladvapi32 -lgdi32

# Visor 3D.
$WINDRES src/viewer/viewer.rc -O coff -o build/viewer.res
$CXX_WIN $FLAGS -municode -o dist/stpviewer.exe \
    src/viewer/main.cpp src/viewer/scene_view.cpp $ENGINE build/viewer.res \
    -mwindows $STATIC -lcomctl32 -lshlwapi -lole32 -loleaut32 -luuid -lgdi32 -luser32 \
    -lshell32 -lcomdlg32 -ladvapi32

# Bancos de pruebas de las rutas COM (opcionales, no se instalan).
$CXX_WIN $FLAGS -o dist/thumbtest.exe src/tools/thumbtest.cpp \
    $STATIC -lole32 -loleaut32 -luuid -lshlwapi -lgdi32 -luser32
$CXX_WIN $FLAGS -o dist/previewtest.exe src/tools/previewtest.cpp \
    $STATIC -lole32 -loleaut32 -luuid -lshlwapi -lgdi32 -luser32

$STRIP dist/StepShellExt.dll dist/stpviewer.exe dist/thumbtest.exe dist/previewtest.exe || true

cp -f install/instalar.bat install/desinstalar.bat dist/ 2>/dev/null || true
ls -la dist
