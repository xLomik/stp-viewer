@echo off
rem Compilacion en Windows con MinGW-w64 (MSYS2: pacman -S mingw-w64-x86_64-gcc).
rem Ejecutar desde la raiz del repositorio.
setlocal

if not exist build mkdir build
if not exist dist mkdir dist

set ENGINE=src\engine\step_file.cpp src\engine\step_model.cpp src\engine\surfaces.cpp src\engine\tessellate.cpp src\render\renderer.cpp
set FLAGS=-std=c++17 -O2
set STATIC=-static -static-libgcc -static-libstdc++

echo [1/3] Manejador de miniaturas...
g++ %FLAGS% -shared -o dist\StepThumbnail.dll src\thumbnailer\dllmain.cpp src\thumbnailer\provider.cpp %ENGINE% src\thumbnailer\thumbnailer.def %STATIC% -lole32 -loleaut32 -luuid -lshlwapi -lshell32 -ladvapi32 -lgdi32
if errorlevel 1 goto :error

echo [2/3] Visor 3D...
windres src\viewer\viewer.rc -O coff -o build\viewer.res
if errorlevel 1 goto :error
g++ %FLAGS% -municode -o dist\stpviewer.exe src\viewer\main.cpp %ENGINE% build\viewer.res -mwindows %STATIC% -lcomctl32 -lshlwapi -lole32 -loleaut32 -luuid -lgdi32 -luser32 -lshell32 -lcomdlg32 -ladvapi32
if errorlevel 1 goto :error

echo [3/3] Herramientas...
g++ %FLAGS% -o dist\thumbtest.exe src\tools\thumbtest.cpp %STATIC% -lole32 -loleaut32 -luuid -lshlwapi -lgdi32 -luser32
g++ %FLAGS% -o dist\steprender.exe %ENGINE% src\tools\steprender.cpp %STATIC%

copy /y install\instalar.bat dist\ >nul
copy /y install\desinstalar.bat dist\ >nul

echo.
echo Compilado en dist\
exit /b 0

:error
echo.
echo Fallo la compilacion.
exit /b 1
