# Miniaturas rápidas con muchos archivos — diseño

Fecha: 2026-09-29 · Estado: decidido sin preguntas (el usuario pidió explícitamente
"no me preguntes nada"); las decisiones y su porqué quedan aquí.

## 1. Objetivo

Con muchos archivos CAD en una carpeta, el Explorador tarda en mostrar las
miniaturas. Meta: que abrir una carpeta con miniaturas se sienta tan rápido como
sin ellas.

Criterios de éxito:
- Generar una miniatura cuesta ≤ 8 ms en 256, 768 y 1024 px para un desarrollo de
  chapa típico (hoy: 3,7 / 22 / 47 ms), medido con `thumbbench` sobre 70 archivos.
- Una carpeta ya "preparada" muestra todas sus miniaturas al abrirla, sin
  esperar a que se generen (salen de la caché de Windows).
- Nada de esto puede colgar ni frenar el Explorador: la preparación corre en su
  propio proceso, con prioridad baja, y se puede cancelar.

## 2. Mediciones (antes de decidir)

`thumbbench` (ruta de la miniatura sin Windows) y `thumbtest --lote` (ruta COM
completa bajo wine), con 70 desarrollos de chapa de 60 KB (≈2 000 segmentos cada
uno):

| Paso | 256 px | 768 px | 1024 px |
|---|---|---|---|
| Leer + cargar + detectar plano | ~1 ms | ~1 ms | ~1 ms |
| Dibujar (hoy, supermuestreo ×2) | 2,6 ms | 21 ms | 46 ms |
| Dibujar con el raster limitado a 1024 px y 1 hilo | 1,9 ms | 3,1 ms | 5,6 ms |

- El motor no es el cuello de botella en 256 px; sí lo es el dibujo cuando el
  Explorador pide miniaturas grandes (escala de pantalla alta, panel de detalles,
  panel de vista previa sin manejador): el raster interno llega a 2048×2048.
- Repartir el dibujo en varios hilos no mejora nada con archivos así (lo que
  cuesta es llenar y reducir el raster, que es secuencial) y el Explorador ya pide
  varias miniaturas en paralelo: más hilos por miniatura solo compiten entre sí.
- Por la ruta COM (wine) una miniatura de 256 px cuesta ~8,5 ms; el resto del
  tiempo que ve el usuario es del propio Explorador: arrancar el proceso aislado
  (`dllhost.exe`), pasarle el archivo, programar las extracciones de a una. Ese
  tiempo no se puede bajar desde el manejador; se evita teniendo la miniatura en
  la caché de Windows antes de abrir la carpeta.

## 3. Enfoques

- **A. Miniatura más barata** (elegido, parte 1): limitar el raster interno a
  1024 px por lado (el supermuestreo baja a ×1 en 512+ px, donde el propio tamaño
  ya da detalle) y dibujar con 1 hilo. 5–8× más rápido en tamaños grandes, sin
  cambio visible en 256.
- **B. Preparar la caché de Windows** (elegido, parte 2): usar
  `IThumbnailCache::GetThumbnail(WTS_EXTRACT)` sobre los archivos de una carpeta,
  en un proceso aparte y en segundo plano. Windows llama a nuestro manejador,
  guarda el resultado en `thumbcache_*.db` y el Explorador ya lo encuentra hecho.
- C. Quitar el aislamiento (`DisableProcessIsolation`) para ahorrar el salto a
  `dllhost.exe`: descartado; una miniatura que falle tumbaría el Explorador.
- D. Un servicio de Windows que vigile carpetas: descartado (instalación con
  administrador, consumo permanente) — YAGNI frente a B.

## 4. Diseño

### 4.1 Miniatura más barata (DLL)
- `RenderStepThumbnail`: `style.supersample = clamp(1024 / tamaño, 1, tamaño >= 256 ? 2 : 3)`
  y `style.threads = 1`.
- La regla vive en una función portable `thumbnailSupersample(int size)` en el
  motor (probada en Linux) y la usan la DLL y `thumbbench`.

### 4.2 Preparar miniaturas
- Lógica portable `src/ui/thumbnail_warmup.h/.cpp`: decide qué archivos de una
  lista son candidatos (extensiones registradas por la DLL, sin carpetas ni
  archivos vacíos, máximo 2 000 por carpeta) y en qué orden (los más recientes
  primero: son los que el usuario está por ver).
- Windows `src/viewer/thumbnail_warmup_win.h/.cpp`: recorre una carpeta
  (`FindFirstFileW`), filtra con lo anterior y reparte los archivos entre
  `min(núcleos − 1, 4)` hilos con prioridad baja
  (`THREAD_MODE_BACKGROUND_BEGIN`). Cada hilo: `CoInitializeEx` MTA,
  `CoCreateInstance(CLSID_LocalThumbnailCache)`,
  `SHCreateItemFromParsingName`, `GetThumbnail(item, tamaño, WTS_EXTRACT, …)`
  con tamaño = 256 × DPI / 96 (lo que pide "Iconos muy grandes"; Windows guarda
  también los tamaños menores). Cuenta hechos, ya en caché y fallidos;
  cancelable con una bandera atómica.
- Dos entradas:
  1. **Menú contextual de carpetas** ("Preparar miniaturas CAD"), en la carpeta
     y en el fondo de la carpeta, registrado por `instalar.bat` para el usuario
     (`HKCU\Software\Classes\Directory\shell` y `Directory\Background\shell`) y
     quitado por `desinstalar.bat`. Lanza `stpviewer.exe --miniaturas "<carpeta>"`:
     ventana chica con el tema grafito, barra de progreso, "Cancelar", y se
     cierra sola al terminar (mensaje final 2 s).
  2. **Automático desde el visor**: al abrir un archivo local (unidad fija), el
     visor prepara en segundo plano las miniaturas de esa carpeta; se cancela al
     cerrar el visor o abrir otra carpeta. No en unidades de red ni extraíbles.
- Si `CLSID_LocalThumbnailCache` no está disponible, no hace nada y no avisa
  (Windows sigue generando las miniaturas al vuelo como hoy).

### 4.3 Qué no cambia
- La caché la administra Windows (se invalida sola si el archivo cambia).
- `instalar.bat` sigue borrando la caché al instalar (el dibujo nuevo tiene que
  reemplazar al viejo).

## 5. Pruebas
- Nativas: `thumbnail_supersample_caps_raster` (regla de supermuestreo),
  `thumbnail_warmup_candidates` (extensiones, límite, orden, vacíos),
  benchmark en `thumbbench` como verificación de los criterios (no como prueba).
- Wine: `thumbtest --lote` antes/después; `stpviewer.exe --miniaturas <carpeta>`
  abre la ventana y termina sin errores (wine no implementa la caché de
  miniaturas: se verifica que la ausencia se maneja sin fallar).
- En Windows real (usuario): abrir la carpeta tras "Preparar miniaturas CAD".
