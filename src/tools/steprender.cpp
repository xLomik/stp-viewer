// Command line renderer used for development and regression checks.
// Builds on Linux and on Windows; writes a 32-bit BMP.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../engine/step_model.h"
#include "../render/renderer.h"

namespace {

bool writeBmp(const std::string& path, const stp::Framebuffer& fb) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    const int w = fb.width;
    const int h = fb.height;
    const std::uint32_t pixelBytes = static_cast<std::uint32_t>(w) * h * 4;
    const std::uint32_t headerSize = 14 + 40;

    auto u16 = [&](unsigned v) {
        std::fputc(v & 0xFF, f);
        std::fputc((v >> 8) & 0xFF, f);
    };
    auto u32 = [&](std::uint32_t v) {
        std::fputc(v & 0xFF, f);
        std::fputc((v >> 8) & 0xFF, f);
        std::fputc((v >> 16) & 0xFF, f);
        std::fputc((v >> 24) & 0xFF, f);
    };

    std::fputc('B', f);
    std::fputc('M', f);
    u32(headerSize + pixelBytes);
    u16(0);
    u16(0);
    u32(headerSize);

    u32(40);
    u32(static_cast<std::uint32_t>(w));
    u32(static_cast<std::uint32_t>(h));
    u16(1);
    u16(32);
    u32(0);
    u32(pixelBytes);
    u32(2835);
    u32(2835);
    u32(0);
    u32(0);

    for (int y = h - 1; y >= 0; --y) {
        std::fwrite(&fb.pixels[static_cast<std::size_t>(y) * w], 4, w, f);
    }
    std::fclose(f);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("uso: steprender <archivo.stp> <salida.bmp> [tamano] [--persp] [--no-edges]\n");
        return 2;
    }
    const std::string input = argv[1];
    const std::string output = argv[2];
    int size = argc > 3 ? std::atoi(argv[3]) : 512;
    if (size < 16) size = 16;

    bool persp = false;
    bool edges = true;
    bool faces = true;
    std::string view = "iso";
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "--persp") == 0) persp = true;
        else if (std::strcmp(argv[i], "--no-edges") == 0) edges = false;
        else if (std::strcmp(argv[i], "--wire") == 0) faces = false;
        else if (std::strncmp(argv[i], "--view=", 7) == 0) view = argv[i] + 7;
    }

    stp::Mesh mesh;
    stp::LoadStats stats;
    std::string error;
    if (!stp::loadStepFile(input, &mesh, &error, &stats, 0.0012)) {
        std::printf("error: %s\n", error.c_str());
        return 1;
    }

    const stp::Vec3 s = mesh.bounds.size();
    std::printf("esquema : %s\n", stats.schema.c_str());
    std::printf("solidos : %d  caras: %d (fallidas %d)\n", stats.solids, stats.faces,
                stats.facesFailed);
    std::printf("triangulos: %zu  aristas: %zu\n", mesh.triangleCount(), mesh.edgeLines.size() / 2);
    std::printf("tamano  : %.3f x %.3f x %.3f\n", s.x, s.y, s.z);

    stp::Camera cam;
    cam.ortho = !persp;
    if (view == "top") { cam.yaw = -1.5707963; cam.pitch = 1.5533430; }
    else if (view == "front") { cam.yaw = -1.5707963; cam.pitch = 0.0; }
    else if (view == "right") { cam.yaw = 0.0; cam.pitch = 0.0; }
    cam.fit(mesh.bounds, 1.0);

    stp::RenderStyle style;
    style.drawEdges = edges;
    style.drawFaces = faces;
    style.supersample = 3;

    stp::Framebuffer fb;
    stp::renderMesh(mesh, cam, style, size, size, &fb);
    if (!writeBmp(output, fb)) {
        std::printf("error: no se pudo escribir %s\n", output.c_str());
        return 1;
    }
    std::printf("escrito : %s (%dx%d)\n", output.c_str(), size, size);
    return 0;
}
