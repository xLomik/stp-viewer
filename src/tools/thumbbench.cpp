// Mide la ruta de la miniatura sin Windows: leer, cargar, detectar plano y dibujar.
//     thumbbench [tamano] archivo...
// Imprime el tiempo de cada fase por archivo y el total, para comparar cambios.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "../engine/planar.h"
#include "../formats/formats.h"
#include "../render/renderer.h"

namespace {
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

double qualityForSize(size_t bytes) {  // igual que thumbnail_provider.cpp
    if (bytes > 40u * 1024 * 1024) return 0.006;
    if (bytes > 8u * 1024 * 1024) return 0.004;
    return 0.0025;
}
}  // namespace

int main(int argc, char** argv) {
    int first = 1, size = 256;
    if (argc > 1 && std::atoi(argv[1]) > 0) {
        size = std::atoi(argv[1]);
        first = 2;
    }
    double total[4] = {0, 0, 0, 0};
    int files = 0;
    for (int i = first; i < argc; ++i) {
        const auto t0 = Clock::now();
        std::ifstream in(argv[i], std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        const std::string data = buffer.str();
        const char* dot = std::strrchr(argv[i], '.');
        std::string ext = dot ? dot : "";
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto t1 = Clock::now();
        stp::Mesh mesh;
        std::string error;
        const bool ok = stp::loadModel(data.data(), data.size(), ext, &mesh, &error, nullptr, qualityForSize(data.size()), 6000);
        const auto t2 = Clock::now();
        const stp::PlanarInfo planar = ok ? stp::detectPlanar(mesh) : stp::PlanarInfo();
        const auto t3 = Clock::now();
        if (ok && !mesh.empty()) {
            stp::Camera camera;
            stp::RenderStyle style;
            style.supersample = stp::thumbnailSupersample(size);  // igual que la DLL
            style.threads = 1;
            if (planar.planar) camera.fitPlanar(planar, 1.0, 1.06);
            else camera.fit(mesh.bounds, 1.0);
            stp::Framebuffer frame;
            stp::renderMesh(mesh, camera, style, size, size, &frame);
        }
        const auto t4 = Clock::now();
        const double phase[4] = {ms(t0, t1), ms(t1, t2), ms(t2, t3), ms(t3, t4)};
        for (int k = 0; k < 4; ++k) total[k] += phase[k];
        ++files;
        if (argc - first <= 12) {
            std::printf("%-28s leer %6.2f  cargar %7.2f  plano %6.2f  dibujar %7.2f ms  (%zu seg, %zu tri)\n",
                        std::strrchr(argv[i], '/') ? std::strrchr(argv[i], '/') + 1 : argv[i], phase[0], phase[1], phase[2],
                        phase[3], mesh.edgeLines.size() / 2, mesh.triangleCount());
        }
    }
    const double sum = total[0] + total[1] + total[2] + total[3];
    std::printf("%d archivos: leer %.1f  cargar %.1f  plano %.1f  dibujar %.1f  -> total %.1f ms (%.2f ms por archivo)\n", files,
                total[0], total[1], total[2], total[3], sum, files ? sum / files : 0.0);
    return 0;
}
