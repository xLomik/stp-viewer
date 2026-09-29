// Prepara la cache de miniaturas de Windows para una carpeta: pide cada miniatura
// a IThumbnailCache (WTS_EXTRACT), que llama al manejador de stp-viewer y guarda el
// resultado donde el Explorador lo busca. Corre en hilos de prioridad baja.
#pragma once

#include <windows.h>

#include <atomic>
#include <memory>
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
        int cached = 0;  // ya estaban en la cache (o el Explorador las estaba haciendo)
        int failed = 0;
        bool listed = false;    // ya se leyo la carpeta
        bool finished = false;
        bool available = true;  // false: Windows no ofrece la cache de miniaturas
    };

    ThumbnailWarmup() = default;
    ThumbnailWarmup(const ThumbnailWarmup&) = delete;
    ThumbnailWarmup& operator=(const ThumbnailWarmup&) = delete;
    ~ThumbnailWarmup() { cancel(true); }

    // Empieza en segundo plano y vuelve enseguida (la carpeta se lee en otro hilo);
    // cancela lo anterior sin esperarlo. notify, si no es nulo, recibe message en
    // cada avance (como mucho cada 100 ms) y al terminar. threads: extracciones a la vez.
    void start(const std::wstring& folder, int size, int threads, HWND notify = nullptr, UINT message = 0);
    // Pide parar. wait = true espera a los hilos (hacerlo con la ventana ya oculta:
    // una miniatura grande puede tardar segundos en soltar).
    void cancel(bool wait);
    Progress progress() const;
    std::wstring folder() const;

private:
    struct Job;
    static void run(std::shared_ptr<Job> job);
    static void work(std::shared_ptr<Job> job);

    std::shared_ptr<Job> m_job;
    std::thread m_thread;  // coordinador: lista la carpeta y reparte
    mutable std::mutex m_mutex;
};

// Ventana chica de progreso para "stpviewer.exe --miniaturas <carpeta>".
int runThumbnailWarmupWindow(HINSTANCE instance, const std::wstring& folder);

}  // namespace stp
