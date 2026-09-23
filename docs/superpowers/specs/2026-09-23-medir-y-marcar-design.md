# Medir y marcar — diseño

Fecha: 2026-09-23 · Estado: aprobado por secciones en conversación, pendiente de
revisión escrita.

## 1. Objetivo

Que quien abre una pieza o un plano en stp-viewer pueda **medirlo** y **marcarlo**
(resaltar, subrayar, anotar) para **compartir una revisión** con un compañero, y
que esas marcas **se guarden junto al archivo** y reaparezcan al abrirlo.

Lo pidió el usuario:
- Medir: distancia punto a punto, radio/diámetro, ángulo, área y perímetro.
- Marcar: resaltador y subrayado, nota con flecha, rectángulo/elipse/nube de
  revisión, lápiz a mano alzada.
- Las marcas se guardan junto al archivo **y** se exportan para compartir.

Supuestos (no dichos por el usuario, aceptados en el diseño):
- El trabajo de marcado, guardado y exportación ocurre en el visor
  (`stpviewer.exe`). El panel del Explorador solo mide (ver 3.4).
- Formato de exportación: PNG y PDF.

Fuera de alcance (backlog, cada uno con su propio ciclo de diseño): capas y
colores del archivo, corte de sección 3D, propiedades físicas (volumen, peso,
centro de masa), árbol de ensamble / lista de materiales.

Criterios de éxito:
- Un agujero de Ø10 en STEP o un `CIRCLE` de radio 5 en DXF miden exactamente
  10,000 / 5,000.
- Marcas hechas, guardadas, cerradas y reabiertas se ven igual.
- El PDF exportado se abre en cualquier lector y trae una página por vista
  marcada más la tabla de medidas y notas.
- El enganche no pierde fluidez: < 5 ms por movimiento del ratón en un DXF de
  1,5 millones de segmentos.
- Sin herramienta activa, el visor y el panel se comportan exactamente como hoy.

## 2. Enfoques elegidos

- **A1 — Anclaje estilo eDrawings.** Notas y medidas se anclan a un punto 3D
  del modelo y se ven desde cualquier ángulo. Trazos y formas se guardan con la
  cámara en que se dibujaron (vista marcada); al girar se ocultan y la lista
  permite volver a esa vista. En planos 2D todo se ancla al plano y se ve
  siempre.
- **B1 — Datos exactos de los lectores.** STEP, IGES y DXF registran los
  círculos, arcos, caras y contornos que ya conocen, en vez de deducirlos de
  los triángulos.

Descartados: proyectar trazos sobre la superficie (frágil en bordes y
agujeros, triple de trabajo); marcas solo en pantalla (se desalinean);
deducir radios de la malla (valores aproximados).

## 3. Arquitectura

### 3.1 Unidades nuevas

| Unidad | Responsabilidad | Depende de |
|---|---|---|
| `src/engine/features.h` | Tipos de elementos medibles guardados en `Mesh`: `CircleFeature` (centro, normal, radio, ángulo inicial y barrido), `FaceFeature` (primer triángulo, cantidad, tipo de superficie), `ContourFeature` (polilínea cerrada de un plano, con sus agujeros si se conocen). | `geom.h` |
| `src/engine/measure.{h,cpp}` | Matemática pura: rayo desde un píxel, intersección rayo–triángulo, enganche, distancia, ángulo, radio, área, perímetro, índice espacial para enganche. | `mesh.h`, `features.h`, `renderer.h` (cámara y proyección) |
| `src/engine/markup.{h,cpp}` | Modelo de datos (`Measurement`, `Note`, `Stroke`, `Shape`, `MarkupView`, `MarkupDocument`) y lectura/escritura del archivo `.marcas`. | `geom.h`, `renderer.h` (Camera) |
| `src/viewer/markup_tools.{h,cpp}` | Estado de la herramienta activa, eventos de ratón y teclado, vista previa, deshacer/rehacer, dibujo con GDI+. | Windows, GDI+, `measure`, `markup` |
| `src/viewer/toolbar.{h,cpp}` | Barra vertical de herramientas y lista lateral (F2). | Windows |
| `src/viewer/export.{h,cpp}` | PNG con GDI+; PDF con escritor propio (JPEG DCTDecode + texto). | Windows, GDI+ |

Las unidades de `src/engine` se compilan también en Linux y tienen pruebas en
`tests/unit_tests.cpp`.

### 3.2 Cambios en lo existente

- `Mesh` gana `std::vector<CircleFeature> circles`, `faces`, `contours` y
  `LengthUnit units` (mm por defecto).
- `step_model.cpp`: al muestrear una arista `CIRCLE` registra un
  `CircleFeature`; al mallar una cara registra su `FaceFeature` (rango de
  triángulos y tipo: plano, cilindro, cono, esfera, toro, otro). Lee la unidad
  de `SI_UNIT`/`CONVERSION_BASED_UNIT` (mm, m, pulgada).
- `iges.cpp`: círculos (entidad 100) y caras recortadas; unidad del campo 14
  de la sección global.
- `dxf.cpp`: `CIRCLE`, `ARC` y arcos de bulge como `CircleFeature`;
  `LWPOLYLINE`/`POLYLINE` cerradas y `CIRCLE` como `ContourFeature`; `$INSUNITS`
  (mm si falta). Los elementos dentro de bloques se transforman con el
  `INSERT`; con escala no uniforme un círculo deja de ser círculo y no se
  registra.
- STL/OBJ/PLY: sin elementos; se mide distancia, ángulo y área de triángulos
  coplanares contiguos.
- `SceneView`: si hay herramienta activa, delega ratón, teclado y dibujo en
  `MarkupTools`; si no, sin cambios. El visor la crea con barra; el panel, sin
  barra y solo con medir.

### 3.3 Flujo de datos

```
archivo -> lector -> Mesh (+ elementos medibles, unidades)
                         |
ratón -> MarkupTools -> measure (rayo, enganche, cálculo) -> MarkupDocument
                         |                                      |
                    dibujo GDI+ encima del render        <modelo>.marcas
                                                               |
                                                        export PNG / PDF
```

### 3.4 Panel del Explorador

Windows entrega al panel el contenido del archivo por `IStream`, sin ruta, así
que no puede encontrar el `.marcas`. En el panel: tecla `M` para medir, `Esc`
para salir; las medidas son temporales y no se guardan. No se muestran marcas
guardadas.

## 4. Medición

- **Enganche:** radio de 8 px. Prioridad: vértice/extremo, centro de círculo,
  punto medio de segmento, punto sobre cara. Marcador distinto por tipo.
  `Shift` desactiva el enganche. Sin candidato, punto de la superficie bajo el
  cursor (rayo contra triángulos); en planos, el punto del plano.
- **Distancia:** 2 clics. Distancia total más ΔX ΔY ΔZ (en planos, Δ en los ejes
  del plano).
- **Radio / diámetro:** 1 clic sobre una arista circular, un agujero o un
  círculo. Se busca el `CircleFeature` más cercano al punto; en una cara
  cilíndrica, el radio de la cara.
- **Ángulo:** 3 clics (vértice al medio), o clic en dos segmentos o aristas
  rectas.
- **Área / perímetro:** 1 clic. En 3D, la `FaceFeature` bajo el cursor (suma de
  sus triángulos; perímetro por aristas de borde). En planos, el
  `ContourFeature` más interno que contiene el punto, restando los contornos
  que contiene. La zona medida se sombrea.
- **Presentación:** estilo de cota (líneas, flechas, valor) y entrada en la
  lista lateral. `Supr` borra la seleccionada.
- **Formato:** unidades del archivo, 3 decimales sin ceros sobrantes, ángulos
  en grados con 2 decimales.
- **Índice espacial:** rejilla uniforme sobre puntos de enganche (extremos,
  vértices, puntos medios, centros), construida una vez al cargar, en el hilo de
  carga. En 3D se proyectan solo las celdas visibles.

## 5. Marcado

### 5.1 Herramientas

| Herramienta | Atajo | Gesto |
|---|---|---|
| Navegar | `Esc` | Comportamiento actual |
| Medir | `M`, luego `1` distancia, `2` radio, `3` ángulo, `4` área | Sección 4 |
| Resaltador | `H` | Arrastrar; 14 px, 40 % opacidad; amarillo, verde, rosa |
| Subrayado | `U` | Arrastrar; recta de 3 px; `Shift` fuerza horizontal/vertical |
| Nota con flecha | `N` | Clic en el punto, clic donde va el texto, escribir, `Enter` |
| Rectángulo / elipse / nube | `R` / `E` / `C` | Arrastrar de esquina a esquina |
| Lápiz | `L` | Trazo libre de 2 px |
| Color | `Q` | Rota rojo, amarillo, verde, azul, negro |

- Con Navegar: clic selecciona una marca, `Supr` la borra, doble clic edita una
  nota. `Ctrl+Z` / `Ctrl+Y` deshacen y rehacen (pila de 100 pasos).
- Mientras se dibuja: la rueda hace zoom y el botón derecho mueve.
- Grosores y letra en píxeles de pantalla: se leen igual a cualquier zoom.
- Los trazos se simplifican (Douglas–Peucker a 0,5 px) antes de guardar.

### 5.2 Anclaje (A1)

- Planos 2D: puntos en coordenadas del plano (u, v); siempre visibles.
- 3D: medidas y notas guardan puntos 3D; siempre visibles, la etiqueta de la
  nota se dibuja en pantalla junto a su punto.
- 3D: trazos y formas pertenecen a una `MarkupView` (cámara completa). Al
  empezar a dibujar con una cámara nueva se crea "Vista N". Solo se ven cuando
  la cámara actual coincide con la de su vista (misma orientación y encuadre,
  tolerancia 0,5 %); si no, aviso "N marcas en otras vistas — F2 para verlas".

### 5.3 Barra y lista lateral

- Barra vertical a la izquierda del visor, iconos dibujados por código (sin
  archivos de imagen), con tooltip y atajo.
- Lista lateral (`F2`): medidas, notas y vistas marcadas. Clic centra la marca o
  restaura la vista.

## 6. Guardado y exportación

### 6.1 Archivo `.marcas`

- Ruta: `<ruta del modelo>.marcas` (p. ej. `brida.dxf.marcas`).
- Texto UTF-8, una línea por elemento, clave=valor, textos entre comillas con
  escapes `\"`, `\\`, `\n`:
  ```
  stp-viewer-marcas 1
  modelo "brida.dxf" tamano=64385 fecha=2026-09-23T12:00:00
  vista id=1 nombre="Vista 1" camara=...
  medida tipo=distancia a=x,y,z b=x,y,z
  nota punto=x,y,z etiqueta=x,y,z texto="Revisar este agujero"
  trazo tipo=resaltador vista=1 color=#FFE14D puntos=x,y,z;x,y,z;...
  forma tipo=nube vista=1 color=#E03C31 a=x,y,z b=x,y,z
  ```
- Los valores medidos no se guardan: se recalculan de los puntos al abrir, así
  la medida no puede contradecir al modelo.
- Guardado automático al cerrar o cambiar de archivo, y con `Ctrl+S`. Sin marcas,
  se borra el `.marcas` existente.
- Escritura segura: se escribe `<nombre>.marcas.tmp` y se reemplaza
  (`MoveFileEx` con `MOVEFILE_REPLACE_EXISTING`).
- Modelo cambiado (tamaño o fecha distintos): se cargan igual y aparece el aviso
  "El archivo cambió desde que se marcó".
- Archivo dañado o de versión futura: se ignoran las líneas que no se entienden
  y se avisa; no se sobrescribe hasta que el usuario marque algo nuevo.
- Carpeta de solo lectura: aviso "No se pudieron guardar las marcas"; la
  exportación sigue disponible.

### 6.2 Exportar (`Ctrl+E`)

- **PNG:** vista actual con marcas, a 2× la resolución de la ventana.
- **PDF:** A4 apaisado. Página 1, vista general (en planos, el plano completo);
  una página por cada vista marcada; última página, tabla de medidas y notas.
  Pie: nombre del archivo, fecha y usuario de Windows. Imágenes en JPEG
  (`DCTDecode`, calidad 90) generadas con GDI+; texto con Helvetica estándar
  del PDF (WinAnsiEncoding, suficiente para español).

## 7. Errores y bordes

- Sin triángulos bajo el cursor y sin enganche: la herramienta no hace nada y
  el cursor muestra "no disponible".
- Radio pedido sobre un modelo sin círculos (STL): mensaje "Este formato no
  guarda círculos; usa distancia".
- Área en un plano sin contorno cerrado bajo el cursor: "No hay un contorno
  cerrado aquí".
- Modelo truncado por el tope de tiempo: se mide lo que se cargó; el aviso de
  modelo grande ya existe.

## 8. Pruebas

Nativas (`./build.sh test`, en CI), escritas antes del código (TDD):
- Elementos: Ø10 de STEP (`placa_agujero.stp`) y `CIRCLE` DXF dan radio exacto;
  cara plana con su rango de triángulos; unidades de STEP y `$INSUNITS`.
- Selección: rayo sobre caja da la cara correcta; enganche a extremo cercano
  sí, lejano no; punto medio y centro.
- Cálculo: distancia y Δ; ángulos de 90° y 45°; área de rectángulo 100×50 con
  agujero r=5 = 5000 − 25π; perímetro.
- `.marcas`: ida y vuelta idéntica; líneas dañadas y versión futura toleradas;
  textos con ñ, comillas y saltos de línea.
- PDF: tabla `xref` correcta, cantidad de páginas esperada (verificado además
  con `pdfinfo`).
- Rendimiento: enganche < 5 ms por consulta sobre el DXF de 1,5 M segmentos.

Bajo wine con capturas: cada medida en `plano_brida.dxf` y `placa_agujero.stp`;
cada marca; guardar, cerrar, reabrir; exportar PNG y PDF; medir en el panel.

## 9. Orden de entrega

Cada paso usable, probado y subido por separado:
1. Elementos medibles, selección y enganche (motor).
2. Medir en visor y panel.
3. Modelo de marcas y archivo `.marcas`.
4. Herramientas de marcado, barra y lista lateral.
5. Exportar PNG y PDF.
6. README, verificación completa y zip.
