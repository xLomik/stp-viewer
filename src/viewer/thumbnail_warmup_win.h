// Prepara la cache de miniaturas de Windows para una carpeta: pide cada miniatura
// a IThumbnailCache (WTS_EXTRACT), que llama al manejador de stp-viewer y guarda el
// resultado donde el Explorador lo busca. Corre en hilos de prioridad baja.
#pragma once

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace stp {

class ThumbnailWarmup {
public:
    struct Progress {
        int total = 0;
        int done = 0;    // generadas ahora
        int cached = 0;  // ya estaban en la cache
        int failed = 0;
        bool finished = false;
        bool available = true;  // false: Windows no ofrece la cache de miniaturas
    };

    ThumbnailWarmup() = default;
    ThumbnailWarmup(const ThumbnailWarmup&) = delete;
    ThumbnailWarmup& operator=(const ThumbnailWarmup&) = delete;
    ~ThumbnailWarmup() { cancel(); }

    // Empieza en segundo plano (cancela lo anterior). notify, si no es nulo,
    // recibe message en cada avance (como mucho cada 100 ms) y al terminar.
    void start(const std::wstring& folder, int size, HWND notify = nullptr, UINT message = 0);
    // Pide parar y espera a los hilos.
    void cancel();
    Progress progress() const;
    std::wstring folder() const;

private:
    void work();
    void notifyProgress(bool force);

    std::vector<std::thread> m_threads;
    std::vector<std::wstring> m_files;  // rutas completas
    std::wstring m_folder;
    int m_size = 256;
    HWND m_notify = nullptr;
    UINT m_message = 0;
    std::atomic<bool> m_stop{false};
    std::atomic<int> m_next{0};
    std::atomic<int> m_done{0}, m_cached{0}, m_failed{0}, m_running{0};
    std::atomic<bool> m_available{true};
    std::atomic<ULONGLONG> m_lastNotify{0};
    mutable std::mutex m_mutex;  // m_folder
};

// Ventana chica de progreso para "stpviewer.exe --miniaturas <carpeta>".
int runThumbnailWarmupWindow(HINSTANCE instance, const std::wstring& folder);

}  // namespace stp
