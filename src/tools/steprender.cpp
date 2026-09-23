// Command line renderer used for development and regression checks.
// Builds on Linux and on Windows; writes a 32-bit BMP.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cctype>

#include "../formats/formats.h"
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
        std::printf("uso: steprender <archivo.stp> <salida.bmp> [tamano] [--persp] [--no-edges]"
                    " [--wire] [--view=iso|3d|top|front|right]\n");
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

    std::FILE* file = std::fopen(input.c_str(), "rb");
    if (!file) {
        std::printf("error: no se pudo abrir %s\n", input.c_str());
        return 1;
    }
    std::fseek(file, 0, SEEK_END);
    const long fileSize = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    std::string bytes(static_cast<std::size_t>(std::max(0L, fileSize)), '\0');
    const std::size_t got = std::fread(&bytes[0], 1, bytes.size(), file);
    std::fclose(file);
    bytes.resize(got);

    std::string extension;
    const std::size_t dot = input.find_last_of('.');
    if (dot != std::string::npos) {
        extension = input.substr(dot);
        for (char& c : extension) c = static_cast<char>(std::tolower(c));
    }

    stp::Mesh mesh;
    stp::LoadStats stats;
    std::string error;
    if (!stp::loadModel(bytes.data(), bytes.size(), extension, &mesh, &error, &stats, 0.0012)) {
        std::printf("error: %s\n", error.c_str());
        std::vector<std::uint8_t> preview;
        if (stp::extractEmbeddedPreview(bytes.data(), bytes.size(), &preview)) {
            std::printf("lleva una vista previa incrustada de %zu bytes\n", preview.size());
        }
        return 1;
    }

    const stp::Vec3 s = mesh.bounds.size();
    std::printf("esquema : %s\n", stats.schema.c_str());
    std::printf("solidos : %d  caras: %d (fallidas %d)\n", stats.solids, stats.faces,
                stats.facesFailed);
    std::printf("triangulos: %zu  aristas: %zu\n", mesh.triangleCount(), mesh.edgeLines.size() / 2);
    std::printf("tamano  : %.3f x %.3f x %.3f\n", s.x, s.y, s.z);
    const stp::PlanarInfo planar = stp::detectPlanar(mesh);
    if (planar.planar) {
        std::printf("plano 2D: %.3f x %.3f  (normal %.3f %.3f %.3f)  textos: %zu\n", planar.width,
                    planar.height, planar.normal.x, planar.normal.y, planar.normal.z,
                    mesh.texts.size());
    } else {
        std::printf("plano 2D: no\n");
    }

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
    if (planar.planar && view == "iso") {
        // Igual que la miniatura: de frente al plano, en hoja blanca.
        cam.fitPlanar(planar, 1.0, 1.06);
        style.backgroundTop = style.backgroundBottom = 0xFFFFFFFF;
        style.edgeColor = 0xFF1A2027;
        style.faceColor = 0xFFD3D9DF;
        style.edgeWidth = std::min(2.5, std::max(1.2, size / 170.0));
    }

    stp::Framebuffer fb;
    stp::renderMesh(mesh, cam, style, size, size, &fb);
    if (!writeBmp(output, fb)) {
        std::printf("error: no se pudo escribir %s\n", output.c_str());
        return 1;
    }
    std::printf("escrito : %s (%dx%d)\n", output.c_str(), size, size);
    return 0;
}
