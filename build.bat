@echo off
rem Compilacion en Windows con MinGW-w64 (MSYS2: pacman -S mingw-w64-x86_64-gcc).
rem Ejecutar desde la raiz del repositorio.
setlocal

if not exist build mkdir build
if not exist dist mkdir dist

set ENGINE=src\export\pdf_writer.cpp src\engine\features.cpp src\engine\markup.cpp src\engine\measure.cpp src\engine\planar.cpp src\engine\step_file.cpp src\engine\step_model.cpp src\engine\surfaces.cpp src\engine\tessellate.cpp src\render\renderer.cpp src\formats\model_loader.cpp src\formats\mesh_formats.cpp src\formats\dxf.cpp src\formats\iges.cpp src\formats\embedded_preview.cpp src\ui\ribbon_layout.cpp src\ui\view_cube_math.cpp src\ui\recent_files.cpp
set FLAGS=-std=c++17 -O2 -pthread
set VIEWER_UI=src\viewer\ui\theme.cpp src\viewer\ui\icons.cpp src\viewer\ui\commands.cpp src\viewer\ui\tooltip.cpp src\viewer\ui\popup_menu.cpp src\viewer\ui\ribbon.cpp src\viewer\ui\status_bar.cpp src\viewer\ui\side_panel.cpp src\viewer\ui\start_page.cpp src\viewer\ui\view_cube.cpp
set STATIC=-static -static-libgcc -static-libstdc++

echo [1/3] Manejador de miniaturas...
g++ %FLAGS% -shared -o dist\StepShellExt.dll src\shellext\dllmain.cpp src\shellext\thumbnail_provider.cpp src\shellext\preview_handler.cpp src\viewer\scene_view.cpp src\viewer\image_view.cpp src\viewer\text_overlay.cpp src\viewer\markup_tools.cpp src\viewer\settings.cpp %VIEWER_UI% %ENGINE% src\shellext\shellext.def %STATIC% -lole32 -loleaut32 -luuid -lshlwapi -lshell32 -ladvapi32 -lgdi32 -lgdiplus
if errorlevel 1 goto :error

echo [2/3] Visor 3D...
windres src\viewer\viewer.rc -O coff -o build\viewer.res
if errorlevel 1 goto :error
g++ %FLAGS% -municode -o dist\stpviewer.exe src\viewer\main.cpp src\viewer\export.cpp src\viewer\scene_view.cpp src\viewer\image_view.cpp src\viewer\text_overlay.cpp src\viewer\markup_tools.cpp src\viewer\settings.cpp %VIEWER_UI% %ENGINE% build\viewer.res -mwindows %STATIC% -lcomctl32 -lshlwapi -lole32 -loleaut32 -luuid -lgdi32 -luser32 -lshell32 -lcomdlg32 -ladvapi32 -lgdiplus
if errorlevel 1 goto :error

echo [3/3] Herramientas...
g++ %FLAGS% -o dist\thumbtest.exe src\tools\thumbtest.cpp %STATIC% -lole32 -loleaut32 -luuid -lshlwapi -lgdi32 -luser32
g++ %FLAGS% -o dist\previewtest.exe src\tools\previewtest.cpp %STATIC% -lole32 -loleaut32 -luuid -lshlwapi -lgdi32 -luser32
g++ %FLAGS% -o dist\steprender.exe %ENGINE% src\tools\steprender.cpp %STATIC%

copy /y install\instalar.bat dist\ >nul
copy /y install\desinstalar.bat dist\ >nul
copy /y install\diagnostico.bat dist\ >nul

echo.
echo Compilado en dist\
exit /b 0

:error
echo.
echo Fallo la compilacion.
exit /b 1
