# stp-viewer

Vista previa de archivos 3D y planos CAD en Windows: miniaturas dentro del
Explorador —como hace *DXF Thumbnails* con los DXF—, **panel de vista previa
interactivo** y un visor 3D independiente.

| Programa | Que hace |
|---|---|
| `StepShellExt.dll` | Dos extensiones de shell en una DLL: `IThumbnailProvider` dibuja la miniatura de cada archivo en la carpeta, e `IPreviewHandler` pone la pieza en el panel de vista previa, donde se puede girar, acercar y mover sin abrir nada. |
| `stpviewer.exe` | Visor independiente con cinta, cubo de vistas, panel de marcas y enganche a objetos (OSNAP) para medir; usa la misma vista 3D que el panel. |

## Formatos

| Formato | Extensiones | Que se ve |
|---|---|---|
| STEP | `.stp` `.step` | Solido completo, mallado exacto de plano, cilindro, cono, esfera y toro |
| IGES | `.igs` `.iges` | Solidos B-rep y superficies recortadas, con sus aristas |
| DXF | `.dxf` | Planos 2D completos: lineas, polilineas con arcos, circulos, elipses, splines, bloques, cotas y textos; tambien `3DFACE` y mallas en 3D |
| Mallas | `.stl` `.obj` `.ply` | Triangulos con sombreado plano y realce de cantos |
| Cerrados | `.dwg` `.prt` `.sldprt` `.sldasm` `.ipt` `.iam` `.catpart` | La imagen de vista previa que el CAD guardo dentro del archivo |

La geometria de `.dwg`, `.prt` de NX/Creo, `.sldprt` y demas formatos nativos
esta en binarios cerrados sin especificacion publica; leerla exigiria el SDK del
fabricante o una libreria con licencia incompatible. Lo que si se puede, y es lo
que hace este programa, es sacar la imagen de vista previa que el propio CAD
dejo incrustada al grabar: sirve para reconocer el archivo en la carpeta, pero
no se puede girar.

## Planos 2D

La mayoria de los DXF son planos: todo esta en un mismo plano y, visto en
isometrica como una pieza 3D, queda un enredo de lineas oscuras sobre fondo
oscuro. Por eso cada archivo se revisa al abrirlo: si todos sus puntos (y los
textos) caben en un plano —con una tolerancia de 0,1 % del tamano del dibujo,
de modo que una arandela de 1 mm en 100 mm sigue siendo 3D— se trata como
plano:

- **Miniatura:** de frente, en una hoja blanca opaca con lineas oscuras, y con
  los textos que se alcanzan a leer. Se ve igual con el tema claro que con el
  oscuro del Explorador.
- **Visor y panel:** de frente, lineas claras sobre fondo oscuro (oscuras si el
  panel usa tema claro), la etiqueta *Plano 2D ancho x alto* y navegacion de CAD:
  arrastrar mueve, la rueda acerca hacia donde apunta el raton. `7` lo pasa a 3D
  para girarlo y `D` vuelve al plano.
- **Textos y cotas:** `TEXT`, `MTEXT` y atributos con su posicion, giro, altura y
  alineacion; `%%c`, `%%d` y `%%p` salen como Ø, ° y ±; acentos y ñ tanto en DXF
  nuevos (UTF-8) como viejos (ANSI). Las cotas se dibujan desde el bloque que
  guardo el CAD, con flechas y valor.
- Se respetan las capas apagadas o congeladas y se ignora el espacio papel
  (marco y cajetin a otra escala), salvo que el modelo este vacio.

Lo mismo vale para cualquier formato: un desarrollo de chapa en STEP tambien se
abre de frente. Los DWG siguen mostrando la imagen que guardo AutoCAD, rotulada
como plano.

No dependen de ningun kernel CAD externo: el lector de STEP, el mallador y el
render estan escritos en el repositorio y se compilan a binarios estaticos de
~1 MB que no necesitan .NET ni Visual C++ Redistributable.

## Interfaz del visor

El visor tiene la forma de un programa CAD de escritorio, en gris grafito:

- **Barra de título propia** con el nombre del archivo (`*` si hay marcas sin
  guardar) y los botones de minimizar, maximizar y cerrar. Se arrastra, se
  acopla a los bordes de la pantalla y abre el menú de la ventana con clic
  derecho, como cualquier ventana de Windows.
- **Cinta con pestañas** — *Archivo* (abrir, recientes, guardar marcas,
  exportar PDF y PNG), *Inicio* (navegar, encuadrar, vistas estándar, plano 2D),
  *Medir* (herramientas, enganche y restricciones), *Marcar* (resaltador,
  subrayado, nota, formas, lápiz, color, deshacer) y *Vista* (sombreado,
  alambre, aristas, perspectiva, panel, barra de estado, cubo). Cada botón tiene
  un tooltip con su atajo. Doble clic en una pestaña pliega la cinta; en una
  ventana angosta los grupos pasan a iconos pequeños y luego a un botón con menú.
- **Cubo de vistas** arriba a la derecha: clic en una cara, arista o esquina
  gira la pieza (animado) y la casita vuelve a la isométrica encuadrada. En los
  planos 2D es una brújula que señala el norte del dibujo.
- **Panel lateral** (`F2`): arriba las marcas agrupadas por vista (clic para
  ir a ellas), abajo las propiedades de la marca elegida —valor, color, texto de
  la nota, que se edita ahí mismo— o, sin selección, las del modelo (formato,
  unidades, tamaño, caras, triángulos). El borde izquierdo y el divisor se
  arrastran.
- **Barra de estado**: coordenadas del cursor con unidades, los interruptores
  **OSNAP**, **ORTO** y **POLAR** (clic los alterna, clic derecho abre sus
  opciones), 2D/3D, unidades, zoom y la ayuda de la herramienta activa.
- **Pantalla de inicio** sin archivo abierto: abrir, archivos recientes (los
  que ya no existen se ven en gris y se pueden quitar) y soltar un archivo.
- Todo se dibuja con vectores y escala con el DPI de cada monitor. La
  configuración (modos de enganche, pestaña, panel, tamaño de la ventana,
  recientes) se guarda en `HKCU\Software\stp-viewer`.

## Medir y marcar

Las pestañas **Medir** y **Marcar** de la cinta (o sus atajos) sirven para
**medir** y **marcar** una pieza o un plano, guardar las marcas junto al archivo
y exportar la revisión para mandarla a un compañero.

| Herramienta | Tecla | Cómo se usa |
|---|---|---|
| Distancia | `M` y `1` | Clic en dos puntos. Muestra el total y ΔX ΔY ΔZ; con Orto, solo la componente ("Vertical: 70 mm") |
| Radio y diámetro | `M` y `2` | Clic sobre un círculo, un arco o un agujero |
| Ángulo | `M` y `3` | Tres clics (vértice en el medio) o clic en dos líneas |
| Área y perímetro | `M` y `4` | Clic dentro de un contorno cerrado del plano o sobre una cara de la pieza |
| Resaltador | `H` | Arrastrar; `Q` cambia entre amarillo, verde y rosa |
| Subrayado | `U` | Arrastrar; con `Shift` u Orto sale horizontal o vertical |
| Nota con flecha | `N` | Clic en el punto a señalar, clic donde va el texto, escribir y `Enter` |
| Rectángulo, elipse, nube | `R`, `E`, `C` | Arrastrar de esquina a esquina |
| Lápiz | `L` | Trazo libre |
| Color | `Q` | Rojo, amarillo, verde, azul, negro (o el menú Color de la cinta) |

- `Esc` termina la herramienta. Clic en una marca la selecciona; `Supr` la
  borra; doble clic en una nota edita su texto. `Ctrl+Z` y `Ctrl+Y` deshacen y
  rehacen.
- En un plano 2D las marcas se ven siempre. En una pieza 3D, las medidas y
  las notas siguen a la pieza al girarla; los trazos y formas quedan en la
  vista en que se dibujaron ("Vista 1", "Vista 2"...): al girar se ocultan y
  el panel lateral lleva de vuelta a esa vista.
- Las marcas se guardan solas al cerrar o al abrir otro archivo (y con
  `Ctrl+S`) en `<archivo>.marcas`, al lado del modelo: si se copia la carpeta,
  las marcas van con ella. Si el modelo cambia después de marcarlo, se avisa.
- **Exportar PDF** (`Ctrl+E`): vista general, una página por cada vista
  marcada y la tabla de medidas y notas. **Exportar PNG**: la vista actual.
- En el **panel de vista previa** se mide con la mini barra flotante de arriba
  (o `M`); esas medidas no se guardan. Windows no le dice al panel dónde está el
  archivo, así que ahí no se ven las marcas guardadas.

## Enganche a objetos (OSNAP) y restricciones

Al medir, el cursor se engancha a los puntos notables del dibujo o de la pieza.
Un marcador verde muestra el tipo de punto y un rótulo dice su nombre.

| Modo | Punto | Marcador | Por defecto |
|---|---|---|---|
| Extremo | Extremos de rectas y arcos | cuadrado | sí |
| Punto medio | Mitad de rectas y arcos | triángulo | sí |
| Centro | Centro de círculos y arcos | círculo | sí |
| Cuadrante | 0°, 90°, 180° y 270° de círculos y arcos | rombo | sí |
| Intersección | Cruce de rectas y arcos | X | sí |
| Extensión | Cruce de las prolongaciones de dos rectas (vértice de una esquina redondeada o achaflanada) | X punteada | no |
| Perpendicular | Pie de la perpendicular desde el primer punto | escuadra | no |
| Tangente | Tangencia desde el primer punto a un círculo | círculo y tangente | no |
| Más cercano | Cualquier punto de una arista | reloj de arena | no |

- **Ejemplo, largo de una ranura:** con Cuadrante encendido, clic en la punta
  de un extremo redondeado y en la del otro: 70 mm exactos. El ancho sale de
  extremo a extremo de los arcos.
- Si hay varios candidatos bajo el cursor gana el de mayor prioridad
  (Intersección, Extremo, Centro, Cuadrante, Medio, Perpendicular, Tangente,
  Extensión); `Tab` pasa al siguiente y `Shift+Tab` al anterior.
- `F3` enciende o apaga el enganche; `Shift` lo apaga mientras se mantiene.
  Los modos se eligen en la pestaña Medir o con clic derecho en **OSNAP**.
- **Orto** (`F8`): la segunda punta de la distancia queda en horizontal o
  vertical (ejes X, Y o Z en 3D) y la cota lo dice.
- **Polar** (`F10`): la dirección se ajusta a múltiplos de 15°, 30°, 45° o 90°
  cuando el cursor pasa a menos de 3°; se ve una guía punteada y el ángulo.
  Orto y Polar se excluyen.

## Instalacion

1. Descarga o compila la carpeta `dist\`.
2. Ejecuta `instalar.bat` (doble clic): registra las extensiones, limpia la cache
   de miniaturas y reinicia el Explorador.
3. Abre una carpeta con modelos y pon la vista en **Iconos grandes**.
4. Para el panel: menu **Ver > Panel de vista previa** (`Alt+P`) y selecciona un
   archivo.

El registro de las miniaturas es por usuario y no pide permisos. El panel de
vista previa si necesita una clave de maquina (`PreviewHandlers`), asi que el
instalador pide permiso de administrador una sola vez; si lo rechazas, las
miniaturas siguen funcionando y el panel no.

Para quitarlo: `desinstalar.bat`. Para revisar que quedo mal: `diagnostico.bat`.

Windows mostrara un aviso de SmartScreen la primera vez porque los ejecutables
no estan firmados.

## Si el panel no muestra nada

Ejecuta `diagnostico.bat`: revisa el registro y dice exactamente que falta. Las
causas habituales, en orden:

1. **El panel esta cerrado.** Ver > Panel de vista previa (`Alt+P`); en Windows
   11, Ver > Mostrar > Panel de vista previa.
2. **Falta el permiso de administrador.** Windows solo carga manejadores
   listados en `HKLM\...\PreviewHandlers`. Vuelve a ejecutar `instalar.bat` y
   acepta el aviso.
3. **Un CAD reclamo la extension.** Si SolidWorks, Inventor o similar registro
   un tipo de archivo para `.stp`, ese tipo manda sobre la extension. El
   instalador tambien escribe ahi, pero si instalaste el CAD *despues*, vuelve a
   ejecutar `instalar.bat`.
4. **El Explorador tiene la DLL vieja en memoria.** `taskkill /f /im
   prevhost.exe` y cierra y abre el Explorador.
5. **Archivo en OneDrive sin descargar**, o en una unidad de red lenta.

Para las miniaturas, ademas: Opciones del Explorador > Ver > desmarca "Mostrar
siempre iconos, nunca miniaturas", y borra la cache
(`%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db`), cosa que
`instalar.bat` ya hace.

## Uso del visor y del panel

Los dos usan los mismos controles de vista.

| Accion | Control |
|---|---|
| Girar | Arrastrar con el boton izquierdo, o clic en el cubo de vistas |
| Mover | Arrastrar con el boton derecho o el central |
| Zoom | Rueda del raton |
| Encuadrar | `F` o doble clic |
| Vistas | `1` frente, `2` atras, `3` izquierda, `4` derecha, `5` superior, `6` inferior, `7` isometrica |
| Plano 2D | `D` vuelve a verlo de frente; ahi arrastrar mueve y la rueda acerca hacia el cursor |
| Alambre | `W` |
| Aristas | `A` (en el panel tambien `E`) |
| Medir | `M` (luego `1`-`4`) |
| Enganche / Orto / Polar | `F3` / `F8` / `F10`; `Tab` cambia de candidato |
| Marcar (solo visor) | `H` `U` `N` `R` `E` `C` `L`, color `Q` |
| Panel de marcas y propiedades | `F2` |
| Guardar marcas / Exportar PDF | `Ctrl+S` / `Ctrl+E` |
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
src/engine     lector ISO 10303-21, geometria, mallado, NURBS, deteccion de planos, medicion y marcas
src/formats    IGES, DXF, STL, OBJ, PLY y extraccion de vistas previas
src/render     rasterizador por software (z-buffer, luces, aristas)
src/shellext   extensiones COM: miniatura y panel de vista previa
src/export     escritor de PDF
src/viewer     SceneView (vista 3D interactiva), herramientas de medir y marcar, y el visor
src/viewer/ui  tema grafito, iconos vectoriales, cinta, barra de estado, panel, cubo de vistas
src/ui         logica de interfaz sin Windows (distribucion de la cinta, cubo, recientes)
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
- **Enganche** (`measure.cpp`): extremos, medios, centros y cuadrantes se
  indexan al cargar (BVH de puntos); intersecciones, extensiones,
  perpendiculares y tangentes se calculan al vuelo solo con las rectas y los
  círculos exactos que están bajo el cursor. Con todos los modos encendidos una
  consulta tarda ~1,5 ms en un plano de 1,5 millones de segmentos.
- **Interfaz** (`src/viewer/ui`): dibujada con GDI+ (sin Ribbon Framework ni
  WebView), escalada por DPI, con la barra de título propia por `WM_NCCALCSIZE`
  y `WM_NCHITTEST`.
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
- De los formatos cerrados solo se muestra su imagen incrustada; si el archivo
  se guardo sin vista previa, no hay nada que ensenar.
- El DXF binario no se lee; hay que guardarlo como DXF ASCII.
- Las marcas de trazo y forma en 3D pertenecen a la vista en que se dibujaron;
  no se proyectan sobre la superficie de la pieza.
- STL, OBJ y PLY no guardan círculos: en esos formatos se mide distancia,
  ángulo y área, pero no radio.
- El PDF usa Helvetica con codificación Windows: los caracteres fuera del
  alfabeto latino salen como `?`.
- En los planos no se dibujan los rellenos de sombreado (`HATCH`) ni los tipos de
  linea (trazos, ejes); todo sale con linea continua de un solo color. Los
  textos usan Arial sin negrita ni cursiva y los `MTEXT` no se ajustan al ancho
  de su recuadro.
- Archivos comprimidos `.stpz` se registran pero aun no se descomprimen.
- Hay topes de trabajo para que ninguna pieza pueda bloquear al Explorador: la
  miniatura se rinde a los 6 s y el panel a los 20 s, dibujando lo que se haya
  mallado; los archivos de mas de 64 MB no generan miniatura; y una cara con
  miles de agujeros deja de perforar cuando el contorno unido pasa de 12000
  puntos. En piezas normales ninguno de esos topes se roza.

## Rendimiento

Pensado para equipos modestos: todo el dibujo es por software y el reparto de
trabajo se ajusta solo.

**Render.** Los vertices se transforman y se sombrean una sola vez, la
rasterizacion va en `float` con funciones de borde incrementales, y la pantalla
se reparte en bandas entre los nucleos disponibles. Medido con una pieza de
27000 triangulos a 900x900:

| Calidad | Antes | Ahora | Ahora, un solo nucleo |
|---|---|---|---|
| Sin suavizado | 84 ms (12 fps) | 17 ms (59 fps) | 29 ms (34 fps) |
| Suavizado x2 | 292 ms (3 fps) | 76 ms (13 fps) | 108 ms (9 fps) |
| Suavizado x3 | 570 ms (2 fps) | 138 ms (7 fps) | 198 ms (5 fps) |

Ademas la vista mide cuanto tarda cada cuadro y elige sola la calidad: mientras
se arrastra apunta a 22 ms por cuadro, bajando el suavizado y, si hace falta, la
resolucion interna (se estira al mostrarla); al soltar el raton redibuja nitido.
En un equipo lento eso se traduce en giro fluido en vez de saltos.

**Mallado.** Esta acotado por diseno, porque el Explorador llama a estas
extensiones de forma sincrona y un archivo lento se nota como una carpeta que
no termina de abrir. Medido con una placa de 1600 agujeros (8,3 MB): 2 min 42 s
antes de acotar el puenteo de agujeros, 3,7 s ahora. Una placa de 400 agujeros
tarda 0,9 s y un soporte normal 0,3 s.

## Pruebas

```bash
./build.sh test                                # pruebas unitarias del motor
python3 tests/make_samples.py tests/samples   # genera STEP de prueba sin CAD
python3 tests/make_dxf_samples.py tests/samples  # planos DXF (pip install ezdxf)
./build.sh native                              # herramienta de linea de comandos
./build/steprender tests/samples/placa_agujero.stp salida.bmp 512
```

`tests/unit_tests.cpp` cubre el lector DXF (arcos por bulge, elipses, splines,
bloques, cotas, capas, espacio papel, textos y codificaciones), la deteccion de
planos y la camara 2D, el enganche a objetos (cuadrantes de la ranura de
`plano_brida.dxf`, intersecciones, extension, perpendicular, tangente, orden de
`Tab` y rendimiento con todos los modos), Orto y Polar, medidas y marcas, el PDF
y la logica pura de la interfaz (distribucion de la cinta, cubo de vistas,
recientes); corre tambien en cada push. `plano_brida.dxf` es un plano
con todo lo anterior y `pieza_3d.dxf` una caja de `3DFACE` que tiene que seguir
viendose en 3D.

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
