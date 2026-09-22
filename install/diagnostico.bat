@echo off
setlocal enabledelayedexpansion
rem Revisa por que el panel de vista previa o las miniaturas no aparecen.
rem No cambia nada: solo informa.

set "DIR=%~dp0"
set "DLL=%DIR%StepShellExt.dll"
set "THUMB_CLSID={90D4532D-A5D0-49A7-B115-800AD0042693}"
set "PREVIEW_CLSID={B718893F-E5EC-4CD8-BF74-D02BC81308C3}"
set "THUMB_IID={E357FCCD-A995-4576-B01F-234630154E96}"
set "PREVIEW_IID={8895B1C6-B41F-4C1C-A562-0D564250836F}"
set "PROBLEMAS=0"

echo ============================================================
echo  Diagnostico de stp-viewer
echo ============================================================
echo.

echo [1] Archivos
if exist "%DLL%" (
    echo     OK   StepShellExt.dll presente
) else (
    echo     MAL  falta StepShellExt.dll junto a este script
    set /a PROBLEMAS+=1
)
echo.

echo [2] Servidores COM registrados ^(usuario actual^)
for %%C in ("%THUMB_CLSID%" "%PREVIEW_CLSID%") do (
    reg query "HKCU\Software\Classes\CLSID\%%~C\InprocServer32" /ve >nul 2>&1
    if errorlevel 1 (
        echo     MAL  %%~C sin registrar   ^(ejecuta instalar.bat^)
        set /a PROBLEMAS+=1
    ) else (
        for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Classes\CLSID\%%~C\InprocServer32" /ve 2^>nul ^| findstr REG_SZ') do (
            echo     OK   %%~C -^> %%B
            if not exist "%%B" (
                echo     MAL  la ruta registrada ya no existe; vuelve a ejecutar instalar.bat
                set /a PROBLEMAS+=1
            )
        )
    )
)
echo.

echo [3] Lista PreviewHandlers de la maquina ^(necesita administrador^)
reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\PreviewHandlers" /v "%PREVIEW_CLSID%" >nul 2>&1
if errorlevel 1 (
    echo     MAL  el manejador no esta autorizado: sin esto Windows nunca lo carga.
    echo          Arreglo: ejecuta instalar.bat y acepta el aviso de administrador,
    echo          o abre CMD como administrador y pega esta linea:
    echo.
    echo          reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\PreviewHandlers" /v "%PREVIEW_CLSID%" /d "stp-viewer STEP preview handler" /f
    echo.
    set /a PROBLEMAS+=1
) else (
    echo     OK   autorizado
)
echo.

echo [4] Asociacion por extension
call :revisar_extension .stp
call :revisar_extension .step
echo.

echo [5] Ajustes del Explorador
for /f "tokens=3" %%A in ('reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced" /v IconsOnly 2^>nul ^| findstr IconsOnly') do (
    if "%%A"=="0x1" (
        echo     MAL  "Mostrar siempre iconos, nunca miniaturas" esta activado.
        echo          Arreglo: Opciones del Explorador ^> Ver ^> desmarcar esa casilla.
        set /a PROBLEMAS+=1
    ) else (
        echo     OK   las miniaturas estan permitidas
    )
)
reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Policies\Explorer" /v NoPreviewPane >nul 2>&1
if not errorlevel 1 (
    echo     MAL  una directiva desactiva el panel de vista previa ^(NoPreviewPane^)
    set /a PROBLEMAS+=1
)
echo.

echo [6] Prueba directa del motor
if exist "%DIR%stpviewer.exe" (
    echo     --   si el visor abre el archivo pero el panel no lo muestra,
    echo          el problema es de registro, no del archivo.
)
echo.

goto :resumen

:revisar_extension
set "EXT=%~1"
set "PROGID="
for /f "tokens=2,*" %%A in ('reg query "HKCR\%EXT%" /ve 2^>nul ^| findstr /i REG_SZ') do set "PROGID=%%B"
if not defined PROGID (
    for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Classes\%EXT%" /ve 2^>nul ^| findstr /i REG_SZ') do set "PROGID=%%B"
)
rem Con el valor por defecto vacio, reg imprime "(value not set)" / "(valor no establecido)".
if defined PROGID if "!PROGID:~0,1!"=="(" set "PROGID="

rem Se consulta la vista combinada y tambien la del usuario: el registro por
rem usuario es el que escribe instalar.bat sin permisos de administrador.
call :existe_clave "%EXT%\ShellEx\%PREVIEW_IID%"
if errorlevel 1 (
    echo     MAL  %EXT% sin manejador de vista previa
    set /a PROBLEMAS+=1
) else (
    echo     OK   %EXT% tiene manejador de vista previa
)

call :existe_clave "%EXT%\ShellEx\%THUMB_IID%"
if errorlevel 1 (
    echo     MAL  %EXT% sin manejador de miniaturas
    set /a PROBLEMAS+=1
) else (
    echo     OK   %EXT% tiene manejador de miniaturas
)

if not defined PROGID goto :eof
echo     --   %EXT% pertenece al tipo "%PROGID%"
call :existe_clave "%PROGID%\ShellEx\%PREVIEW_IID%"
if errorlevel 1 (
    echo     MAL  ese tipo manda sobre la extension y no apunta a stp-viewer.
    echo          Pasa cuando un CAD reclama el %EXT% despues de instalar.
    echo          Arreglo: vuelve a ejecutar instalar.bat.
    set /a PROBLEMAS+=1
) else (
    echo     OK   el tipo "%PROGID%" apunta a stp-viewer
)
goto :eof

rem Devuelve errorlevel 0 si la clave existe en la vista combinada o en la del usuario.
:existe_clave
reg query "HKCR\%~1" /ve >nul 2>&1
if not errorlevel 1 exit /b 0
reg query "HKCU\Software\Classes\%~1" /ve >nul 2>&1
if not errorlevel 1 exit /b 0
exit /b 1

:resumen
echo ============================================================
if "%PROBLEMAS%"=="0" (
    echo  Todo en orden. Si aun no ves nada:
    echo   - Activa el panel: Ver ^> Panel de vista previa ^(Alt+P^).
    echo     En Windows 11: Ver ^> Mostrar ^> Panel de vista previa.
    echo   - Cierra el Explorador y vuelve a abrirlo.
    echo   - Si el archivo esta en OneDrive, descargalo primero.
) else (
    echo  Se encontraron %PROBLEMAS% problema^(s^). Mira los arreglos de arriba.
)
echo ============================================================
pause
