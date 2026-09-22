// Mide el coste del render: cuadros por segundo a varios tamanos y calidades.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../engine/step_model.h"
#include "../render/renderer.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("uso: benchrender <archivo> [tamano] [cuadros] [hilos]\n");
        return 2;
    }
    const int size = argc > 2 ? std::atoi(argv[2]) : 900;
    const int frames = argc > 3 ? std::atoi(argv[3]) : 30;
    const int threads = argc > 4 ? std::atoi(argv[4]) : 0;

    stp::Mesh mesh;
    stp::LoadStats stats;
    std::string error;
    if (!stp::loadStepFile(argv[1], &mesh, &error, &stats, 0.0008)) {
        std::printf("error: %s\n", error.c_str());
        return 1;
    }

    stp::Camera camera;
    camera.ortho = true;
    camera.fit(mesh.bounds, 1.0);

    for (int ss : {1, 2, 3}) {
        stp::RenderStyle style;
        style.supersample = ss;
        style.threads = threads;
        stp::Framebuffer frame;
        renderMesh(mesh, camera, style, size, size, &frame);  // calentamiento

        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; ++i) {
            camera.yaw += 0.01;
            renderMesh(mesh, camera, style, size, size, &frame);
        }
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - start).count() / frames;
        std::printf("%dx%d  ss=%d  %7.1f ms/cuadro  %5.1f fps  (%zu triangulos)\n", size, size, ss,
                    ms, 1000.0 / ms, mesh.triangleCount());
    }
    return 0;
}
