// Shell thumbnail handler for .stp / .step files.
#pragma once

#include <windows.h>
#include <thumbcache.h>

// {90D4532D-A5D0-49A7-B115-800AD0042693}
extern const CLSID CLSID_StepThumbnailProvider;

extern long g_dllRefCount;

HRESULT CreateStepThumbnailProvider(REFIID riid, void** ppv);
HRESULT RegisterThumbnailProvider(HMODULE module, bool perUser);
HRESULT UnregisterThumbnailProvider(bool perUser);

// Renders a STEP file held in memory into a 32-bit premultiplied ARGB bitmap.
HRESULT RenderStepThumbnail(const char* data, size_t length, UINT size, HBITMAP* out);
