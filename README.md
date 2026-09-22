# stp-viewer

Vista previa de archivos **STEP** (`.stp`, `.step`) en Windows: miniaturas dentro
del Explorador —como hace *DXF Thumbnails* con los DXF—, **panel de vista previa
interactivo** y un visor 3D independiente.

| Programa | Que hace |
|---|---|
| `StepShellExt.dll` | Dos extensiones de shell en una DLL: `IThumbnailProvider` dibuja la miniatura de cada `.stp` / `.step` en la carpeta, e `IPreviewHandler` pone la pieza en el panel de vista previa, donde se puede girar, acercar y mover sin abrir nada. |
| `stpviewer.exe` | Visor 3D independiente, con la misma vista 3D que usa el panel. |

No dependen de ningun kernel CAD externo: el lector de STEP, el mallador y el
render estan escritos en el repositorio y se compilan a binarios estaticos de
~1 MB que no necesitan .NET ni Visual C++ Redistributable.

## Instalacion

1. Descarga o compila la carpeta `dist\`.
2. Ejecuta `instalar.bat` (doble clic): registra las extensiones, limpia la cache
   de miniaturas y reinicia el Explorador.
3. Abre una carpeta con archivos STEP y pon la vista en **Iconos grandes**.
4. Para el panel: menu **Ver > Panel de vista previa** (`Alt+P`) y selecciona un
   archivo STEP.

El registro de las miniaturas es por usuario y no pide permisos. El panel de
vista previa si necesita una clave de maquina (`PreviewHandlers`), asi que el
instalador pide permiso de administrador una sola vez; si lo rechazas, las
miniaturas siguen funcionando y el panel no.

Para quitarlo: `desinstalar.bat`.

Windows mostrara un aviso de SmartScreen la primera vez porque los ejecutables
no estan firmados.

## Uso del visor y del panel

Los dos usan los mismos controles; el panel muestra una barra de ayuda mas corta.

| Accion | Control |
|---|---|
| Girar | Arrastrar con el boton izquierdo |
| Mover | Arrastrar con el boton derecho o el central |
| Zoom | Rueda del raton |
| Encuadrar | `F` o doble clic |
| Vistas | `1` frente, `2` atras, `3` izquierda, `4` derecha, `5` superior, `6` inferior, `7` isometrica |
| Alambre | `W` |
| Aristas | `E` |
| Perspectiva / ortografica | `P` |
| Abrir archivo | `Ctrl+O`, o arrastrar el archivo a la ventana |

Tambien acepta la ruta como argumento: `stpviewer.exe pieza.stp`.

## Compilacion

**Desde Linux (compilacion cruzada, es lo que usa este repositorio):**

```bash
sudo apt-get install -y g++-mingw-w64-x86-64 mingw-w64-tools
./build.sh
```

**Desde Windows con MinGW-w64 (MSYS2):**

```bat
build.bat
```

Ambos dejan el resultado en `dist\`: la DLL, el visor, los scripts de
instalacion y tres herramientas de apoyo (`steprender`, `thumbtest`,
`previewtest`).

## Como esta hecho

```
src/engine     lector ISO 10303-21, geometria y mallado
src/render     rasterizador por software (z-buffer, luces, aristas)
src/shellext   extensiones COM: miniatura y panel de vista previa
src/viewer     SceneView (vista 3D interactiva) y el visor independiente
src/tools      steprender (CLI), thumbtest y previewtest (prueban la ruta COM)
```

- **Lector STEP** (`step_file.cpp`): analiza la seccion `DATA` completa,
  incluidas las entidades complejas `#1=(A(..) B(..))` y los escapes de texto.
- **Geometria** (`step_model.cpp`): recorre solidos, cascarones y caras; muestrea
  aristas (rectas, circulos, elipses, B-splines racionales, curvas compuestas y
  recortadas) y resuelve los ensambles siguiendo
  `REPRESENTATION_RELATIONSHIP` y `MAPPED_ITEM`, de modo que cada pieza queda en
  su sitio.
- **Mallado**: las caras se proyectan al espacio de parametros de su superficie.
  Las planas se triangulan con *ear clipping* (con puentes para los agujeros);
  las curvas —cilindros, conos, esferas, toros— se mallan sobre una rejilla
  recortada, con el paso calculado a partir de la flecha maxima admitida. Asi un
  agujero de 10 mm y una pieza de 2 m reciben la densidad que les toca.
- **Vista 3D** (`scene_view.cpp`): una ventana hija Win32 con orbita, zoom,
  desplazamiento y carga en segundo plano. El visor la mete en su ventana
  principal y el manejador de vista previa la crea dentro de la ventana que le
  entrega `prevhost.exe`, de modo que el panel del Explorador es interactivo de
  verdad, no una imagen.
- **Render** (`renderer.cpp`): rasterizador propio con z-buffer, dos luces en
  espacio de camara, supermuestreo y aristas superpuestas con sesgo de
  profundidad. Sin GPU: el proceso que genera miniaturas en Windows es de baja
  prioridad y no conviene abrir contextos graficos ahi.

## Limitaciones conocidas

- Las superficies **B-spline** y las de revolucion/extrusion se aproximan por el
  plano de su contorno. La silueta queda bien; el abombamiento interior de una
  cara libre, no. Las superficies analiticas (plano, cilindro, cono, esfera,
  toro), que son la mayoria en piezas mecanicas, si se mallan exactas.
- No se leen colores ni materiales del archivo: todo se dibuja en gris acero.
- Archivos comprimidos `.stpz` se registran pero aun no se descomprimen.

## Pruebas

```bash
python3 tests/make_samples.py tests/samples   # genera STEP de prueba sin CAD
./build.sh native                              # herramienta de linea de comandos
./build/steprender tests/samples/placa_agujero.stp salida.bmp 512
```

`tests/samples` trae una caja, una placa con agujero y un eje, escritos a mano
por el generador para cubrir caras planas con contorno interno, caras
cilindricas con costura y arcos. Para probar la ruta COM tal como la usa el
Explorador:

```bash
wine dist/thumbtest.exe dist/StepShellExt.dll pieza.stp salida.bmp 256
wine dist/previewtest.exe dist/StepShellExt.dll pieza.stp
```

`previewtest` hospeda el manejador igual que el Explorador (`SetWindow`,
`SetRect`, `DoPreview`), asi que sirve para probar el panel sin registrar nada.

## Licencia

MIT. Ver `LICENSE`.
