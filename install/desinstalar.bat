@echo off
setlocal
rem Quita la vista previa de archivos STEP del usuario actual.

set "DIR=%~dp0"
set "DLL=%DIR%StepThumbnail.dll"

echo Cerrando el Explorador para liberar la DLL...
taskkill /f /im explorer.exe >nul 2>&1

if exist "%DLL%" (
    regsvr32 /s /u "%DLL%"
) else (
    rem Sin la DLL a mano, se borran las claves directamente.
    reg delete "HKCU\Software\Classes\CLSID\{90D4532D-A5D0-49A7-B115-800AD0042693}" /f >nul 2>&1
    reg delete "HKCU\Software\Classes\.stp\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}" /f >nul 2>&1
    reg delete "HKCU\Software\Classes\.step\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}" /f >nul 2>&1
    reg delete "HKCU\Software\Classes\SystemFileAssociations\.stp\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}" /f >nul 2>&1
    reg delete "HKCU\Software\Classes\SystemFileAssociations\.step\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}" /f >nul 2>&1
)

reg delete "HKCU\Software\Classes\Applications\stpviewer.exe" /f >nul 2>&1
del /f /q "%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db" >nul 2>&1
start explorer.exe

echo.
echo Desinstalado.
pause
