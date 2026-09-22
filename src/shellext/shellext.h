// Extensiones de shell para archivos STEP: miniatura del Explorador y
// panel de vista previa interactivo.
#pragma once

#include <windows.h>
#include <thumbcache.h>

// {90D4532D-A5D0-49A7-B115-800AD0042693}
extern const CLSID CLSID_StepThumbnailProvider;
// {B718893F-E5EC-4CD8-BF74-D02BC81308C3}
extern const CLSID CLSID_StepPreviewHandler;

extern long g_dllRefCount;
extern HMODULE g_shellExtModule;

HRESULT CreateStepThumbnailProvider(REFIID riid, void** ppv);
HRESULT CreateStepPreviewHandler(REFIID riid, void** ppv);

HRESULT RegisterShellExtensions(HMODULE module, bool perUser);
HRESULT UnregisterShellExtensions(bool perUser);

// Dibuja un archivo STEP en memoria como mapa de bits ARGB premultiplicado.
#include <string>

HRESULT RenderStepThumbnail(const char* data, size_t length, UINT size, HBITMAP* out,
                            const std::string& extension);
