@echo off
setlocal
rem Quita las extensiones de shell para archivos STEP.

set "DIR=%~dp0"
set "DLL=%DIR%StepShellExt.dll"
set "THUMB_CLSID={90D4532D-A5D0-49A7-B115-800AD0042693}"
set "PREVIEW_CLSID={B718893F-E5EC-4CD8-BF74-D02BC81308C3}"
set "PREVIEW_KEY=HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\PreviewHandlers"
set "THUMB_IID={E357FCCD-A995-4576-B01F-234630154E96}"
set "PREVIEW_IID={8895B1C6-B41F-4C1C-A562-0D564250836F}"

echo Cerrando el Explorador y el anfitrion de vista previa...
taskkill /f /im prevhost.exe >nul 2>&1
taskkill /f /im explorer.exe >nul 2>&1

if exist "%DLL%" (
    regsvr32 /s /u "%DLL%"
) else (
    reg delete "HKCU\Software\Classes\CLSID\%THUMB_CLSID%" /f >nul 2>&1
    reg delete "HKCU\Software\Classes\CLSID\%PREVIEW_CLSID%" /f >nul 2>&1
    for %%E in (.stp .step .stpz) do (
        reg delete "HKCU\Software\Classes\%%E\ShellEx\%THUMB_IID%" /f >nul 2>&1
        reg delete "HKCU\Software\Classes\%%E\ShellEx\%PREVIEW_IID%" /f >nul 2>&1
        reg delete "HKCU\Software\Classes\SystemFileAssociations\%%E\ShellEx\%THUMB_IID%" /f >nul 2>&1
        reg delete "HKCU\Software\Classes\SystemFileAssociations\%%E\ShellEx\%PREVIEW_IID%" /f >nul 2>&1
    )
)

reg query "%PREVIEW_KEY%" /v "%PREVIEW_CLSID%" >nul 2>&1
if not errorlevel 1 (
    echo Quitando el manejador de la lista de la maquina ^(Windows pedira permiso^)...
    powershell -NoProfile -Command "Start-Process reg.exe -ArgumentList 'delete','%PREVIEW_KEY%','/v','%PREVIEW_CLSID%','/f' -Verb RunAs -Wait" >nul 2>&1
)

reg delete "HKCU\Software\Classes\Applications\stpviewer.exe" /f >nul 2>&1
del /f /q "%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db" >nul 2>&1
start explorer.exe

echo.
echo Desinstalado.
pause
