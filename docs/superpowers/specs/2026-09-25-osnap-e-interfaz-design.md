# OSNAP y restricciones, e interfaz profesional — diseño

Fecha: 2026-09-25 · Estado: aprobado por secciones en conversación (el usuario
aprobó de antemano las secciones restantes), pendiente de revisión escrita.

## 1. Objetivo

Dos cosas que pidió el usuario:

1. **Medir sin pelear con el enganche.** Ejemplo concreto: el largo total de una
   ranura (slot) hoy no se puede medir, porque sus puntas son la mitad de un arco
   y no existe un punto de enganche ahí. Se resuelve con **enganche a objetos
   (OSNAP)** por modos que se encienden y apagan, y **restricciones de acotado**
   (Orto y Polar), como en AutoCAD.
2. **Interfaz de producto profesional**, "como si la hubiera hecho una gran
   empresa": estilo de visor CAD de Autodesk (cinta con pestañas, cubo de vistas,
   panel lateral de marcas y propiedades, barra de estado con OSNAP/ORTO/POLAR),
   tema gris grafito.

Medidas que el usuario marcó como frecuentes (todas entran): largo y ancho de
ranuras, horizontal/vertical exacta, perpendicular y paralelas, esquinas e
intersecciones.

Criterios de éxito:
- Largo de la ranura de `plano_brida.dxf` en 2 clics (cuadrante a cuadrante):
  70 mm exactos. Ancho: 20 mm exactos.
- Espesor de una pared y distancia entre paralelas en 2 clics con Perpendicular.
- Vértice de una esquina redondeada o con chaflán enganchable con Extensión.
- Con Orto, la cota es horizontal o vertical aunque los puntos no estén alineados.
- Enganche con todos los modos encendidos: < 5 ms por movimiento del ratón en el
  plano de 1,5 millones de segmentos.
- El visor se ve como un producto terminado a 100 % y 150 % de escala de
  pantalla, en ventana angosta y ancha; nada de texto de ayuda suelto sobre la vista.
- El panel del Explorador sigue funcionando igual o mejor y respeta el tema del
  Explorador.

Fuera de alcance: medir "entre elementos" estilo SolidWorks (centro-centro,
mín/máx), temas claros adicionales, personalizar la cinta, idiomas distintos
del español.

## 2. Enfoques elegidos

- **A1 — OSNAP por modos + restricciones (Orto `F8`, Polar `F10`, enganche
  `F3`, `Tab` para recorrer candidatos).** Descartados: medir entre elementos
  (útil pero no cubre perpendicular ni intersección; puede venir después) y
  precalcular todas las intersecciones (minutos y memoria en planos grandes).
- **B1 — Interfaz propia dibujada con GDI+.** Descartados: Windows Ribbon
  Framework (requiere `uicc.exe` de Microsoft, no disponible en mingw) y WebView2
  (runtime extra, contradice un programa liviano).

## 3. Enganche (OSNAP)

### 3.1 Modos

| Modo | Qué ofrece | Marcador | Por defecto |
|---|---|---|---|
| Extremo | Extremos de rectas y de arcos | cuadrado | sí |
| Punto medio | Mitad de rectas y mitad de arcos | triángulo | sí |
| Centro | Centro de círculos y arcos | círculo | sí |
| Cuadrante | 0°, 90°, 180°, 270° de círculos y arcos (dentro del arco), medidos desde el eje X del mundo proyectado al plano del círculo (o el Y si el X es perpendicular) | rombo | sí |
| Intersección | Cruce real de dos elementos cercanos (recta-recta, recta-arco, arco-arco) | X | sí |
| Extensión | Cruce de las prolongaciones de dos rectas cercanas (vértice de una esquina redondeada o achaflanada) | X punteada | no |
| Perpendicular | Pie de la perpendicular desde el primer punto a una recta (su recta completa) o a un círculo | escuadra | no |
| Tangente | Punto de tangencia desde el primer punto a un círculo o arco | círculo con tangente | no |
| Más cercano | Punto de una arista más cercano al cursor | reloj de arena | no |

- Perpendicular y Tangente solo existen cuando ya hay un primer punto.
- Los "tramos de un mismo arco muestreado" no generan intersecciones entre sí
  (se identifican por pertenecer a la misma curva).
- En 3D los modos trabajan sobre las aristas visibles y respetan la oclusión
  actual (rayo por el propio punto).

### 3.2 Elección del candidato

- Radio de captura: 8 px (escalado por DPI).
- Entre los candidatos a menos de 1,5 px del más cercano gana el de mayor
  prioridad: Intersección, Extremo, Centro, Cuadrante, Medio, Perpendicular,
  Tangente, Extensión. "Más cercano" solo se ofrece si no hay ningún otro.
- `Tab` recorre los candidatos bajo el cursor en ese orden; `Shift+Tab` al revés.
- Junto al marcador, un rótulo con el nombre del modo ("Cuadrante").
- Sin candidato: punto de la cara (3D) o del plano (2D), como hoy.

### 3.3 Restricciones

- **Orto (`F8`)**: el segundo punto de distancia se proyecta sobre el eje más
  cercano a la dirección del cursor: ejes u/v del plano en 2D; X/Y/Z del mundo en
  3D. La cota muestra solo esa componente y la rotula "Horizontal" / "Vertical"
  (2D) o "X" / "Y" / "Z" (3D).
- **Polar (`F10`)**: dirección fijada a múltiplos del ángulo elegido (15°, 30°,
  45°, 90°; por defecto 45°) cuando el cursor está a menos de 3° de uno; línea
  guía punteada y ángulo junto al cursor.
- Orto y Polar se excluyen: encender uno apaga el otro.
- El enganche se resuelve primero y la restricción se aplica al punto enganchado.
- `F3` enciende/apaga todo el enganche; `Shift` lo apaga mientras se mantiene.
- Las restricciones aplican al segundo clic de Distancia y de Ángulo, y a los
  trazos de Subrayado (Orto reemplaza a `Shift`, que se mantiene).

### 3.4 Cálculo

- Puntos fijos (Extremo, Centro, Cuadrante, Medio de recta y de arco) en el índice
  de puntos existente (`PickIndex`), construido en el hilo de carga.
- Dinámicos (Intersección, Extensión, Perpendicular, Tangente, Más cercano): se
  consultan los segmentos y círculos con caja a menos del radio de captura del
  cursor (Extensión: a menos de 12× el radio, porque en una esquina redondeada
  las rectas terminan lejos del vértice) y se calculan al vuelo.
- Intersección y Extensión usan rectas de verdad (`edgeCurve == 0`) y círculos
  exactos (`features.circles`), no los tramos muestreados de las curvas; dos
  rectas que comparten un extremo (esquina de una polilínea) no generan
  intersección: ese punto ya es un Extremo.
- API nueva de motor: modos `kSnap*` (máscara de bits), `SnapConstraint`
  (ninguna/orto/polar con ángulo), `PickIndex::snapAll` (lista ordenada de
  candidatos) y `applyConstraint(first, point, constraint, camera, plane)`.

## 4. Interfaz del visor

### 4.1 Estructura

De arriba abajo:
1. **Barra de título propia** (grafito): icono, nombre de archivo, botones
   minimizar/maximizar/cerrar. Mantiene arrastre, doble clic, Aero Snap y el menú
   del sistema (`WM_NCHITTEST` y extensión del marco con DWM cuando está
   disponible; en Windows 7 sin Aero, barra propia igual).
2. **Cinta con pestañas** (botones grandes con icono y texto, grupos rotulados):
   - Archivo: Abrir, Recientes (10), Guardar marcas, Exportar PDF, Exportar PNG,
     Acerca de.
   - Inicio: Navegar, Encuadrar, Vistas estándar (menú), Plano 2D / 3D.
   - Medir: Distancia, Radio, Ángulo, Área · grupo Enganche (9 casillas) ·
     Orto, Polar (con ángulo).
   - Marcar: Resaltador, Subrayado, Nota, Rectángulo, Elipse, Nube, Lápiz ·
     Color · Deshacer, Rehacer, Borrar.
   - Vista: Sombreado, Alambre, Aristas, Perspectiva · Panel, Barra de estado,
     Cubo de vistas.
   - Doble clic en una pestaña pliega/despliega la cinta. En ventanas angostas
     los grupos se reducen a iconos pequeños y luego a un botón desplegable.
3. **Área de trabajo**: la vista actual; **cubo de vistas** arriba a la derecha
   (caras, aristas y esquinas clicables, giro animado de 250 ms, botón Inicio);
   en planos 2D se reduce a brújula con la flecha "arriba" del dibujo.
4. **Panel lateral derecho** (acoplado, plegable con `F2`, ancho ajustable):
   - Marcas: árbol Vistas → marcas con iconos por tipo; clic centra/selecciona.
   - Propiedades: de la marca seleccionada (valor, color, texto de nota
     editable) o, sin selección, del modelo (formato, unidades, tamaño, caras,
     triángulos, 2D/3D).
5. **Barra de estado**: coordenadas del cursor en vivo con unidades · OSNAP ·
   ORTO · POLAR (acento azul encendido, gris apagado; clic alterna, clic derecho
   abre el menú de modos / ángulos) · 2D/3D · unidades · zoom · mensajes (los
   avisos que hoy salen en la parte baja de la vista).
6. **Pantalla de inicio** (sin archivo abierto): logo, "Abrir archivo",
   recientes y "arrastra un archivo aquí".

### 4.2 Lenguaje visual

- Colores: fondo `#1F2329`, paneles `#262B32`, bordes `#363C45`, texto
  `#E6E9ED`, secundario `#9AA3AD`, acento `#2F80ED`, hover `#2D333B`,
  presionado `#343B45`.
- Fuente Segoe UI (respaldo sans-serif genérica), tamaños 9/10/12 pt escalados.
- Iconos vectoriales propios, trazo 1,5 px a 100 %, dibujados en tiempo real a
  cualquier DPI; un solo estilo para cinta, panel, barra de estado y menús.
- Hover con transición de 120 ms. Tooltips enriquecidos: título, atajo, una
  línea de ayuda.
- La vista 3D conserva su fondo degradado; los planos 2D, su fondo oscuro.
- Todo escalado por DPI del monitor (manifiesto con DPI awareness por monitor).

### 4.3 Panel del Explorador

Sin barra de título ni cinta. Mini barra flotante semitransparente arriba al
centro (Medir, Distancia/Radio/Ángulo/Área, OSNAP, ORTO), marcadores de enganche
con rótulo y cubo pequeño. Respeta los colores del tema del Explorador como hoy.

### 4.4 Qué desaparece

La barra vertical de la izquierda (`toolbar.cpp`), la lista de marcas
(`LISTBOX`) y los textos de ayuda sobre la vista (pasan a tooltips y barra de
estado).

## 5. Configuración

- Se guardan en `HKCU\Software\stp-viewer`: modos OSNAP, OSNAP/ORTO/POLAR
  encendidos, ángulo polar, cinta plegada, panel visible y su ancho, pestaña
  activa, tamaño y posición de la ventana, lista de recientes (10).
- Si el registro no se puede leer o escribir, se usan los valores por defecto sin
  avisar (no es un error del usuario).
- El panel del Explorador lee los mismos modos OSNAP pero no escribe.

## 6. Errores y bordes

- Recientes que ya no existen: se muestran en gris y al hacer clic se ofrece
  quitarlos.
- Ventana muy angosta (< 640 px): la cinta pasa a iconos pequeños; el panel se
  oculta solo y se abre con `F2` como flotante.
- Tab sin candidatos: no hace nada.
- Polar con un solo punto (sin primer punto): no aplica.
- Monitores con DPI distinto: al moverse la ventana se reescala (`WM_DPICHANGED`).

## 7. Pruebas

Nativas (`./build.sh test`, escritas antes del código):
- Enganche: ranura de `plano_brida.dxf` = 70 mm (cuadrantes) y 20 mm de ancho;
  medio de arco; intersección recta-recta y recta-arco; extensión en esquina
  redondeada; perpendicular a recta y a círculo; tangente; modos apagados no se
  ofrecen; `Tab` recorre en orden; tramos del mismo arco no se cruzan;
  rendimiento < 5 ms con todos los modos.
- Restricciones: Orto horizontal/vertical en 2D, X/Y/Z en 3D; Polar a 45°.
- Interfaz (lógica pura): distribución de la cinta según ancho, cara del cubo
  bajo el cursor, orientación de cámara por cara/arista/esquina, lista de
  recientes (orden, límite, quitar).

Bajo wine con capturas: cada pestaña, tooltips, cubo, barra de estado con
OSNAP/ORTO/POLAR y su menú, pantalla de inicio, 100 % y 150 %, ventana angosta
y ancha, medida de la ranura, panel del Explorador; benchmark de render sin
regresión.

## 8. Orden de entrega

1. Motor de enganche: modos, candidatos, `Tab`, restricciones.
2. Enganche conectado a las herramientas (visor y panel) con los marcadores
   nuevos.
3. Tema, iconos y primitivas de interfaz.
4. Barra de título, cinta y barra de estado; retiro de la barra vertical.
5. Panel lateral (marcas y propiedades) y pantalla de inicio.
6. Cubo de vistas.
7. Configuración en el registro, recientes, DPI.
8. Panel del Explorador con la mini barra.
9. README, verificación completa, zip.
