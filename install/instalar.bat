@echo off
setlocal enabledelayedexpansion
rem Instala la vista previa de archivos STEP para el usuario actual.
rem No necesita permisos de administrador: todo se escribe en HKEY_CURRENT_USER.

set "DIR=%~dp0"
set "DLL=%DIR%StepThumbnail.dll"
set "EXE=%DIR%stpviewer.exe"

if not exist "%DLL%" (
    echo No se encontro StepThumbnail.dll junto a este script.
    pause
    exit /b 1
)

echo Registrando el generador de miniaturas...
regsvr32 /s "%DLL%"
if errorlevel 1 (
    echo.
    echo Fallo el registro. Prueba a ejecutar este archivo como administrador.
    pause
    exit /b 1
)

if exist "%EXE%" (
    echo Registrando el visor en "Abrir con"...
    reg add "HKCU\Software\Classes\Applications\stpviewer.exe\shell\open\command" /ve /d "\"%EXE%\" \"%%1\"" /f >nul
    reg add "HKCU\Software\Classes\Applications\stpviewer.exe" /v FriendlyAppName /d "stp-viewer" /f >nul
    reg add "HKCU\Software\Classes\Applications\stpviewer.exe\SupportedTypes" /v ".stp" /d "" /f >nul
    reg add "HKCU\Software\Classes\Applications\stpviewer.exe\SupportedTypes" /v ".step" /d "" /f >nul
)

echo Limpiando la cache de miniaturas...
taskkill /f /im explorer.exe >nul 2>&1
del /f /q "%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db" >nul 2>&1
start explorer.exe

echo.
echo Listo. Abre una carpeta con archivos .stp o .step y activa la vista de
echo iconos grandes. Para abrir el visor: doble clic derecho ^> Abrir con ^> stp-viewer.
pause
