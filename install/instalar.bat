@echo off
setlocal
rem Instala la vista previa de archivos STEP: miniaturas en el Explorador y
rem panel de vista previa interactivo.
rem
rem Las miniaturas se registran para el usuario actual y no piden permisos.
rem El panel de vista previa solo funciona si el manejador aparece en la lista
rem PreviewHandlers de la maquina, y esa clave si necesita administrador: el
rem script la pide una sola vez.

set "DIR=%~dp0"
set "DLL=%DIR%StepShellExt.dll"
set "EXE=%DIR%stpviewer.exe"
set "PREVIEW_CLSID={B718893F-E5EC-4CD8-BF74-D02BC81308C3}"
set "PREVIEW_KEY=HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\PreviewHandlers"

if not exist "%DLL%" (
    echo No se encontro StepShellExt.dll junto a este script.
    pause
    exit /b 1
)

echo Registrando miniaturas y panel de vista previa para este usuario...
regsvr32 /s "%DLL%"
if errorlevel 1 (
    echo.
    echo Fallo el registro de la DLL.
    pause
    exit /b 1
)

reg query "%PREVIEW_KEY%" /v "%PREVIEW_CLSID%" >nul 2>&1
if errorlevel 1 (
    echo Autorizando el panel de vista previa ^(Windows pedira permiso^)...
    powershell -NoProfile -Command "Start-Process reg.exe -ArgumentList 'add','%PREVIEW_KEY%','/v','%PREVIEW_CLSID%','/d','stp-viewer STEP preview handler','/f' -Verb RunAs -Wait" >nul 2>&1
    reg query "%PREVIEW_KEY%" /v "%PREVIEW_CLSID%" >nul 2>&1
    if errorlevel 1 (
        echo.
        echo Sin ese permiso el panel de vista previa no se activa.
        echo Las miniaturas si quedan funcionando.
    )
)

if exist "%EXE%" (
    echo Registrando el visor en "Abrir con"...
    reg add "HKCU\Software\Classes\Applications\stpviewer.exe\shell\open\command" /ve /d "\"%EXE%\" \"%%1\"" /f >nul
    reg add "HKCU\Software\Classes\Applications\stpviewer.exe" /v FriendlyAppName /d "stp-viewer" /f >nul
    reg add "HKCU\Software\Classes\Applications\stpviewer.exe\SupportedTypes" /v ".stp" /d "" /f >nul
    reg add "HKCU\Software\Classes\Applications\stpviewer.exe\SupportedTypes" /v ".step" /d "" /f >nul
)

echo Limpiando la cache de miniaturas...
taskkill /f /im prevhost.exe >nul 2>&1
taskkill /f /im explorer.exe >nul 2>&1
del /f /q "%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db" >nul 2>&1
start explorer.exe

echo.
echo Listo.
echo  - Miniaturas: abre una carpeta con archivos .stp o .step en iconos grandes.
echo  - Panel de vista previa: menu Ver ^> Panel de vista previa ^(Alt+P^), y
echo    selecciona un archivo STEP. Se puede girar, acercar y mover ahi mismo.
pause
