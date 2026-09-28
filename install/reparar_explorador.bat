@echo off
setlocal
rem Deja el Explorador limpio despues de cuelgues o reinicios:
rem  - cierra el Explorador y los procesos que tengan cargada StepShellExt.dll,
rem  - borra las caches de miniaturas e iconos (Windows las vuelve a crear),
rem  - si se acepta, restablece como se ve cada carpeta (diseno, orden, columnas).
rem No toca archivos del usuario.

echo ============================================================
echo  Reparar el Explorador
echo ============================================================
echo.
echo Si las vistas de carpeta quedaron trabadas ^(no deja elegir el diseno
echo de iconos^), conviene restablecerlas. Se pierden los ajustes de vista
echo guardados por carpeta; los archivos no se tocan.
echo.
choice /c SN /m "Restablecer tambien las vistas de todas las carpetas"
set "RESTABLECER=%errorlevel%"

echo.
echo Cerrando el Explorador y los anfitriones de vista previa y miniaturas...
taskkill /f /fi "MODULES eq StepShellExt.dll" >nul 2>&1
taskkill /f /im prevhost.exe >nul 2>&1
taskkill /f /im explorer.exe >nul 2>&1
timeout /t 2 /nobreak >nul

echo Borrando las caches de miniaturas e iconos...
del /f /q /a "%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db" >nul 2>&1
del /f /q /a "%LOCALAPPDATA%\Microsoft\Windows\Explorer\iconcache_*.db" >nul 2>&1
del /f /q /a "%LOCALAPPDATA%\IconCache.db" >nul 2>&1

if "%RESTABLECER%"=="1" (
    echo Restableciendo las vistas de carpeta...
    reg delete "HKCU\Software\Classes\Local Settings\Software\Microsoft\Windows\Shell\Bags" /f >nul 2>&1
    reg delete "HKCU\Software\Classes\Local Settings\Software\Microsoft\Windows\Shell\BagMRU" /f >nul 2>&1
    reg delete "HKCU\Software\Microsoft\Windows\Shell\Bags" /f >nul 2>&1
    reg delete "HKCU\Software\Microsoft\Windows\Shell\BagMRU" /f >nul 2>&1
)

start explorer.exe
echo.
echo Listo. Abre de nuevo la carpeta; las miniaturas se generan otra vez.
pause
