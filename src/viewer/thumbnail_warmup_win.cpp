#include "thumbnail_warmup_win.h"

#include <windowsx.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <thumbcache.h>

#include <algorithm>

#include "../ui/thumbnail_warmup.h"
#include "ui/theme.h"

namespace stp {
namespace {

// mingw-w64 no trae estos GUID en libuuid.
const CLSID kCLSID_LocalThumbnailCache = {
    0x50EF4544, 0xAC9F, 0x4A8E, {0xB2, 0x1B, 0x8A, 0x26, 0x18, 0x0D, 0xB1, 0x3F}};
const IID kIID_IThumbnailCache = {0xF676C15D, 0x596A, 0x4CE2, {0x82, 0x34, 0x33, 0x99, 0x6F, 0x44, 0x5D, 0xB1}};
const IID kIID_IShellItem = {0x43826D1E, 0xE718, 0x42EE, {0xBC, 0x55, 0xA1, 0xE2, 0x61, 0xC3, 0x7B, 0xFE}};

std::int64_t fileTime(const FILETIME& t) {
    return static_cast<std::int64_t>((static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime);
}

}  // namespace

void ThumbnailWarmup::start(const std::wstring& folder, int size, HWND notify, UINT message) {
    cancel();
    std::wstring base = folder;
    while (base.size() > 3 && (base.back() == L'\\' || base.back() == L'/')) base.pop_back();
    if (base.empty()) {
        m_files.clear();
        m_running = 0;
        notifyProgress(true);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_folder = base;
    }
    m_size = std::max(16, size);
    m_notify = notify;
    m_message = message;
    m_stop = false;
    m_next = 0;
    m_done = m_cached = m_failed = 0;
    m_available = true;
    m_files.clear();

    // Solo este nivel de la carpeta: sin recorrer subcarpetas.
    std::vector<ui::WarmupFile> found;
    WIN32_FIND_DATAW data;
    const std::wstring pattern = base + (base.empty() || base.back() == L'\\' ? L"*" : L"\\*");
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                   FIND_FIRST_EX_LARGE_FETCH);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            ui::WarmupFile file;
            file.name = data.cFileName;
            file.size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            file.modified = fileTime(data.ftLastWriteTime);
            file.folder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            // Archivos de OneDrive sin descargar: pedir la miniatura los bajaria.
            if (data.dwFileAttributes & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)) continue;
            found.push_back(file);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    const std::wstring prefix = base + (base.back() == L'\\' ? L"" : L"\\");
    for (const std::wstring& name : ui::warmupOrder(found)) m_files.push_back(prefix + name);

    const unsigned cores = std::thread::hardware_concurrency();
    const int threads = m_files.empty() ? 0 : std::max(1, std::min(4, static_cast<int>(cores) - 1));
    m_running = threads;
    if (threads == 0) {
        notifyProgress(true);
        return;
    }
    for (int i = 0; i < threads; ++i) m_threads.emplace_back([this]() { work(); });
}

void ThumbnailWarmup::cancel() {
    m_stop = true;
    for (std::thread& thread : m_threads) {
        if (thread.joinable()) thread.join();
    }
    m_threads.clear();
    m_running = 0;
}

ThumbnailWarmup::Progress ThumbnailWarmup::progress() const {
    Progress p;
    p.total = static_cast<int>(m_files.size());
    p.done = m_done;
    p.cached = m_cached;
    p.failed = m_failed;
    p.available = m_available;
    p.finished = m_running == 0;
    return p;
}

std::wstring ThumbnailWarmup::folder() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_folder;
}

void ThumbnailWarmup::notifyProgress(bool force) {
    if (!m_notify) return;
    const ULONGLONG now = GetTickCount64();
    if (!force && now - m_lastNotify < 100) return;
    m_lastNotify = now;
    PostMessageW(m_notify, m_message, 0, 0);
}

void ThumbnailWarmup::work() {
    // Prioridad baja de CPU, disco y memoria: el usuario no tiene que notarlo.
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IThumbnailCache* cache = nullptr;
    if (FAILED(CoCreateInstance(kCLSID_LocalThumbnailCache, nullptr, CLSCTX_INPROC_SERVER, kIID_IThumbnailCache,
                                reinterpret_cast<void**>(&cache))) ||
        !cache) {
        m_available = false;  // Windows sin cache de miniaturas: el Explorador las hara al vuelo
    } else {
        for (;;) {
            if (m_stop) break;
            const int index = m_next++;
            if (index >= static_cast<int>(m_files.size())) break;
            IShellItem* item = nullptr;
            HRESULT hr = SHCreateItemFromParsingName(m_files[static_cast<std::size_t>(index)].c_str(), nullptr,
                                                     kIID_IShellItem, reinterpret_cast<void**>(&item));
            ISharedBitmap* bitmap = nullptr;
            WTS_CACHEFLAGS flags = WTS_DEFAULT;
            if (SUCCEEDED(hr)) {
                hr = cache->GetThumbnail(item, static_cast<UINT>(m_size), WTS_EXTRACT, &bitmap, &flags, nullptr);
            }
            if (bitmap) bitmap->Release();
            if (item) item->Release();
            if (FAILED(hr)) ++m_failed;
            else if (flags & WTS_CACHED) ++m_cached;
            else ++m_done;
            notifyProgress(false);
        }
        cache->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
    if (--m_running == 0) notifyProgress(true);
}

// --- Ventana de progreso -------------------------------------------------------------

namespace {

constexpr UINT kProgressMessage = WM_APP + 51;
constexpr UINT_PTR kCloseTimer = 1;

struct WarmupWindow {
    ThumbnailWarmup warmup;
    std::wstring folder;
    int dpi = 96;
    RECT cancel = {};
    bool hot = false;
    bool closing = false;
};

int scaledBy(int value, int dpi) { return MulDiv(value, dpi, 96); }

void paintWarmup(HWND hwnd, WarmupWindow& w) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    const ThumbnailWarmup::Progress p = w.warmup.progress();
    ui::paintBuffered(dc, client, [&](Gdiplus::Graphics& g) {
        const float width = static_cast<float>(client.right);
        Gdiplus::SolidBrush back(ui::color(ui::kBackground));
        g.FillRectangle(&back, 0.0f, 0.0f, width, static_cast<float>(client.bottom));
        const float pad = static_cast<float>(scaledBy(20, w.dpi));
        const auto title = ui::font(12, w.dpi, Gdiplus::FontStyleBold);
        const auto text = ui::font(9, w.dpi);
        const std::wstring heading = !p.available ? L"No se pueden preparar miniaturas"
                                     : p.finished ? L"Miniaturas listas"
                                                  : L"Preparando miniaturas";
        ui::drawText(g, heading, *title, Gdiplus::RectF(pad, pad - 4, width - 2 * pad, static_cast<float>(scaledBy(26, w.dpi))),
                     ui::kText);
        ui::drawText(g, w.folder, *text,
                     Gdiplus::RectF(pad, pad + scaledBy(24, w.dpi), width - 2 * pad, static_cast<float>(scaledBy(20, w.dpi))),
                     ui::kTextDim);
        // Barra de progreso.
        const float barY = pad + scaledBy(54, w.dpi), barH = static_cast<float>(scaledBy(6, w.dpi));
        ui::fillRound(g, Gdiplus::RectF(pad, barY, width - 2 * pad, barH), barH / 2, ui::kPanel);
        const int handled = p.done + p.cached + p.failed;
        const float share = p.total > 0 ? static_cast<float>(handled) / p.total : (p.finished ? 1.0f : 0.0f);
        if (share > 0) ui::fillRound(g, Gdiplus::RectF(pad, barY, (width - 2 * pad) * share, barH), barH / 2, ui::kAccent);
        std::wstring status;
        if (!p.available) {
            status = L"Windows no ofrece la cach\u00e9 de miniaturas; el Explorador las har\u00e1 al abrir la carpeta.";
        } else if (p.total == 0 && p.finished) {
            status = L"No hay archivos CAD en esta carpeta.";
        } else {
            status = std::to_wstring(handled) + L" de " + std::to_wstring(p.total);
            if (p.cached > 0) status += L"  \u00b7  " + std::to_wstring(p.cached) + L" ya estaban listas";
            if (p.failed > 0) status += L"  \u00b7  " + std::to_wstring(p.failed) + L" sin miniatura";
        }
        // Hasta dos renglones: el aviso de "no disponible" es largo.
        Gdiplus::SolidBrush dim(ui::color(ui::kTextDim));
        g.DrawString(status.c_str(), -1, text.get(),
                     Gdiplus::RectF(pad, barY + scaledBy(12, w.dpi), width - 2 * pad, static_cast<float>(scaledBy(36, w.dpi))),
                     nullptr, &dim);
        // Boton Cancelar / Cerrar.
        const float bw = static_cast<float>(scaledBy(96, w.dpi)), bh = static_cast<float>(scaledBy(30, w.dpi));
        const Gdiplus::RectF button(width - pad - bw, static_cast<float>(client.bottom) - pad - bh + 4, bw, bh);
        w.cancel = RECT{static_cast<LONG>(button.X), static_cast<LONG>(button.Y), static_cast<LONG>(button.X + bw),
                        static_cast<LONG>(button.Y + bh)};
        ui::fillRound(g, button, 4, w.hot ? ui::kPressed : ui::kPanel);
        ui::strokeRound(g, button, 4, ui::kBorder, 1.0f);
        ui::drawText(g, p.finished ? L"Cerrar" : L"Cancelar", *text, button, ui::kText, 1);
    });
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK warmupProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* w = reinterpret_cast<WarmupWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
            break;
        case WM_PAINT:
            if (w) {
                paintWarmup(hwnd, *w);
                return 0;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case kProgressMessage:
            if (w) {
                InvalidateRect(hwnd, nullptr, FALSE);
                const ThumbnailWarmup::Progress p = w->warmup.progress();
                if (p.finished && !w->closing) {
                    w->closing = true;
                    SetTimer(hwnd, kCloseTimer, p.available && p.failed == 0 ? 2000 : 5000, nullptr);
                }
            }
            return 0;
        case WM_TIMER:
            if (wparam == kCloseTimer) DestroyWindow(hwnd);
            return 0;
        case WM_MOUSEMOVE:
            if (w) {
                const POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                const bool hot = PtInRect(&w->cancel, pt) != FALSE;
                if (hot != w->hot) {
                    w->hot = hot;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        case WM_LBUTTONUP:
            if (w) {
                const POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                if (PtInRect(&w->cancel, pt)) DestroyWindow(hwnd);
            }
            return 0;
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (w) w->warmup.cancel();  // los hilos terminan antes de salir
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

int runThumbnailWarmupWindow(HINSTANCE instance, const std::wstring& folder) {
    WarmupWindow state;
    state.folder = folder;
    HDC screen = GetDC(nullptr);
    state.dpi = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
    if (screen) ReleaseDC(nullptr, screen);

    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = &warmupProc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    cls.hIconSm = cls.hIcon;
    cls.lpszClassName = L"StpViewerThumbnailWarmup";
    RegisterClassExW(&cls);

    const int width = scaledBy(480, state.dpi), height = scaledBy(190, state.dpi);
    RECT work = {0, 0, 1024, 768};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    HWND hwnd = CreateWindowExW(0, cls.lpszClassName, L"Visor STP \u2014 miniaturas", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                work.left + (work.right - work.left - width) / 2,
                                work.top + (work.bottom - work.top - height) / 2, width, height, nullptr, nullptr, instance,
                                &state);
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    state.warmup.start(folder, 256 * state.dpi / 96, hwnd, kProgressMessage);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

}  // namespace stp
