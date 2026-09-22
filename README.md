# stp-viewer

Vista previa de archivos **STEP** (`.stp`, `.step`) en Windows: miniaturas dentro
del Explorador —como hace *DXF Thumbnails* con los DXF— y un visor 3D para
girar, acercar y mover la pieza.

Son dos programas que comparten el mismo motor:

| Programa | Que hace |
|---|---|
| `StepThumbnail.dll` | Extension de shell (`IThumbnailProvider`). El Explorador le pide la miniatura de cada `.stp` / `.step` y la dibuja en la carpeta, igual que una foto. |
| `stpviewer.exe` | Visor 3D independiente: arrastrar para girar, rueda para zoom, boton derecho para mover. |

No dependen de ningun kernel CAD externo: el lector de STEP, el mallador y el
render estan escritos en el repositorio y se compilan a binarios estaticos de
~1 MB que no necesitan .NET ni Visual C++ Redistributable.

## Instalacion

1. Descarga o compila la carpeta `dist\`.
2. Ejecuta `instalar.bat` (doble clic). No pide administrador: registra todo en
   `HKEY_CURRENT_USER`, limpia la cache de miniaturas y reinicia el Explorador.
3. Abre una carpeta con archivos STEP y pon la vista en **Iconos grandes**.

Para quitarlo: `desinstalar.bat`.

Windows mostrara un aviso de SmartScreen la primera vez porque los ejecutables
no estan firmados.

## Uso del visor

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
instalacion y dos herramientas de apoyo (`steprender`, `thumbtest`).

## Como esta hecho

```
src/engine     lector ISO 10303-21, geometria y mallado
src/render     rasterizador por software (z-buffer, luces, aristas)
src/thumbnailer  extension de shell COM
src/viewer     ventana Win32 del visor
src/tools      steprender (CLI) y thumbtest (prueba la ruta COM)
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
wine dist/thumbtest.exe dist/StepThumbnail.dll pieza.stp salida.bmp 256
```

## Licencia

MIT. Ver `LICENSE`.
