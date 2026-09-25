// Pruebas unitarias del motor. Sin dependencias: se compilan y corren con
//     ./build.sh test
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "../src/engine/markup.h"
#include "../src/export/pdf_writer.h"
#include "../src/engine/measure.h"
#include "../src/engine/nurbs.h"
#include "../src/formats/formats.h"
#include "../src/render/renderer.h"
#include "../src/ui/recent_files.h"
#include "../src/ui/ribbon_layout.h"
#include "../src/ui/view_cube_math.h"

namespace {

struct TestCase {
    const char* name;
    void (*run)();
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

int g_failures = 0;

struct Registrar {
    Registrar(const char* name, void (*run)()) { registry().push_back({name, run}); }
};

#define TEST(name)                                   \
    static void name();                              \
    static Registrar registrar_##name(#name, name);  \
    static void name()

#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            std::printf("    FALLO %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++g_failures;                                                         \
        }                                                                         \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                          \
    do {                                                                                 \
        const double a_ = (actual), e_ = (expected);                                     \
        if (std::fabs(a_ - e_) > (tolerance)) {                                          \
            std::printf("    FALLO %s:%d: %s = %g, se esperaba %g\n", __FILE__, __LINE__, \
                        #actual, a_, e_);                                                \
            ++g_failures;                                                                \
        }                                                                                \
    } while (0)

// Arma un DXF ASCII a partir de pares (codigo, valor).
std::string dxf(const std::vector<std::pair<int, std::string>>& pairs) {
    std::string out;
    for (const auto& pair : pairs) {
        out += std::to_string(pair.first) + "\n" + pair.second + "\n";
    }
    return out;
}

std::string entities(const std::vector<std::pair<int, std::string>>& body) {
    std::vector<std::pair<int, std::string>> all = {{0, "SECTION"}, {2, "ENTITIES"}};
    all.insert(all.end(), body.begin(), body.end());
    all.push_back({0, "ENDSEC"});
    all.push_back({0, "EOF"});
    return dxf(all);
}

// Documento DXF completo con capas, bloques y entidades.
using Pairs = std::vector<std::pair<int, std::string>>;

std::string document(const Pairs& layers, const Pairs& blocks, const Pairs& body) {
    Pairs all;
    if (!layers.empty()) {
        all.insert(all.end(), {{0, "SECTION"}, {2, "TABLES"}, {0, "TABLE"}, {2, "LAYER"}});
        all.insert(all.end(), layers.begin(), layers.end());
        all.insert(all.end(), {{0, "ENDTAB"}, {0, "ENDSEC"}});
    }
    if (!blocks.empty()) {
        all.insert(all.end(), {{0, "SECTION"}, {2, "BLOCKS"}});
        all.insert(all.end(), blocks.begin(), blocks.end());
        all.push_back({0, "ENDSEC"});
    }
    all.insert(all.end(), {{0, "SECTION"}, {2, "ENTITIES"}});
    all.insert(all.end(), body.begin(), body.end());
    all.insert(all.end(), {{0, "ENDSEC"}, {0, "EOF"}});
    return dxf(all);
}

// Distancia maxima de los extremos de segmento a un punto.
double farthestFrom(const stp::Mesh& mesh, const stp::Vec3& center) {
    double worst = 0;
    for (const stp::Vec3& p : mesh.edgeLines) worst = std::max(worst, stp::distance(p, center));
    return worst;
}

double nearestTo(const stp::Mesh& mesh, const stp::Vec3& center) {
    double best = 1e300;
    for (const stp::Vec3& p : mesh.edgeLines) best = std::min(best, stp::distance(p, center));
    return best;
}

bool loadDxfText(const std::string& text, stp::Mesh* mesh) {
    std::string error;
    return stp::loadModel(text.data(), text.size(), ".dxf", mesh, &error);
}

}  // namespace

// --- DXF: lo que ya funcionaba ------------------------------------------------

TEST(dxf_line_becomes_one_segment) {
    const std::string text = entities({{0, "LINE"}, {10, "0"}, {20, "0"}, {30, "0"},
                                       {11, "10"}, {21, "0"}, {31, "0"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(mesh.edgeLines.size() == 2);
    CHECK_NEAR(mesh.bounds.size().x, 10.0, 1e-9);
}

// --- DXF: geometria de planos 2D --------------------------------------------------

TEST(dxf_lwpolyline_bulge_draws_arc) {
    // Bulge 1 = media vuelta en sentido antihorario de (0,0) a (10,0): pasa por (5,-5).
    const std::string text = entities({{0, "LWPOLYLINE"}, {90, "2"}, {70, "0"},
                                       {10, "0"}, {20, "0"}, {42, "1"},
                                       {10, "10"}, {20, "0"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(mesh.edgeLines.size() > 20);
    CHECK_NEAR(farthestFrom(mesh, stp::Vec3(5, 0, 0)), 5.0, 1e-9);
    CHECK_NEAR(mesh.bounds.lo.y, -5.0, 1e-2);
    CHECK_NEAR(mesh.bounds.hi.y, 0.0, 1e-9);
}

TEST(dxf_closed_lwpolyline_bulge_on_closing_segment) {
    // Cuadrado con el lado de cierre (0,10)->(0,0) abombado hacia afuera.
    const std::string text = entities({{0, "LWPOLYLINE"}, {90, "4"}, {70, "1"},
                                       {10, "0"}, {20, "0"}, {10, "10"}, {20, "0"},
                                       {10, "10"}, {20, "10"}, {10, "0"}, {20, "10"}, {42, "1"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK_NEAR(mesh.bounds.lo.x, -5.0, 1e-2);
}

TEST(dxf_polyline_vertex_bulge_draws_arc) {
    const std::string text = entities({{0, "POLYLINE"}, {66, "1"}, {70, "0"},
                                       {0, "VERTEX"}, {10, "0"}, {20, "0"}, {42, "-1"},
                                       {0, "VERTEX"}, {10, "10"}, {20, "0"},
                                       {0, "SEQEND"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK_NEAR(farthestFrom(mesh, stp::Vec3(5, 0, 0)), 5.0, 1e-9);
    CHECK_NEAR(mesh.bounds.hi.y, 5.0, 1e-2);  // bulge negativo: horario, por arriba
}

TEST(dxf_polyline_spline_frame_points_are_not_drawn) {
    const std::string text = entities({{0, "POLYLINE"}, {66, "1"}, {70, "4"},
                                       {0, "VERTEX"}, {10, "0"}, {20, "100"}, {70, "16"},
                                       {0, "VERTEX"}, {10, "0"}, {20, "0"}, {70, "8"},
                                       {0, "VERTEX"}, {10, "10"}, {20, "0"}, {70, "8"},
                                       {0, "SEQEND"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK_NEAR(mesh.bounds.hi.y, 0.0, 1e-9);
}

TEST(dxf_full_ellipse) {
    const std::string text = entities({{0, "ELLIPSE"}, {10, "0"}, {20, "0"}, {30, "0"},
                                       {11, "10"}, {21, "0"}, {31, "0"}, {40, "0.5"},
                                       {41, "0"}, {42, "6.283185307179586"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK_NEAR(mesh.bounds.size().x, 20.0, 1e-6);
    CHECK_NEAR(mesh.bounds.size().y, 10.0, 1e-2);
}

TEST(dxf_half_ellipse) {
    const std::string text = entities({{0, "ELLIPSE"}, {10, "0"}, {20, "0"}, {30, "0"},
                                       {11, "10"}, {21, "0"}, {31, "0"}, {40, "0.5"},
                                       {41, "0"}, {42, "3.141592653589793"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK_NEAR(mesh.bounds.lo.y, 0.0, 1e-9);
    CHECK_NEAR(mesh.bounds.hi.y, 5.0, 1e-2);
}

TEST(dxf_spline_with_control_points) {
    // Bezier cuadratica (0,0) (1,1) (2,0): la cima vale y = 0,5.
    const std::string text = entities({{0, "SPLINE"}, {70, "8"}, {71, "2"}, {72, "6"}, {73, "3"},
                                       {40, "0"}, {40, "0"}, {40, "0"}, {40, "1"}, {40, "1"}, {40, "1"},
                                       {10, "0"}, {20, "0"}, {30, "0"},
                                       {10, "1"}, {20, "1"}, {30, "0"},
                                       {10, "2"}, {20, "0"}, {30, "0"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK_NEAR(mesh.bounds.hi.y, 0.5, 1e-3);
    CHECK_NEAR(mesh.bounds.size().x, 2.0, 1e-9);
}

TEST(dxf_rational_spline_uses_weights) {
    const double w = std::sqrt(0.5);
    const std::string text = entities({{0, "SPLINE"}, {70, "12"}, {71, "2"}, {72, "6"}, {73, "3"},
                                       {40, "0"}, {40, "0"}, {40, "0"}, {40, "1"}, {40, "1"}, {40, "1"},
                                       {10, "1"}, {20, "0"}, {30, "0"}, {41, "1"},
                                       {10, "1"}, {20, "1"}, {30, "0"}, {41, std::to_string(w)},
                                       {10, "0"}, {20, "1"}, {30, "0"}, {41, "1"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK_NEAR(farthestFrom(mesh, stp::Vec3(0, 0, 0)), 1.0, 1e-6);
    CHECK_NEAR(nearestTo(mesh, stp::Vec3(0, 0, 0)), 1.0, 1e-6);
}

TEST(dxf_spline_with_fit_points_only) {
    const std::string text = entities({{0, "SPLINE"}, {70, "8"}, {71, "3"}, {74, "3"},
                                       {11, "0"}, {21, "0"}, {31, "0"},
                                       {11, "5"}, {21, "5"}, {31, "0"},
                                       {11, "10"}, {21, "0"}, {31, "0"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK_NEAR(nearestTo(mesh, stp::Vec3(5, 5, 0)), 0.0, 1e-9);
    CHECK_NEAR(mesh.bounds.size().x, 10.0, 1e-9);
}

TEST(dxf_insert_honors_block_base_point) {
    const std::string text = document(
        {}, {{0, "BLOCK"}, {2, "PIEZA"}, {70, "0"}, {10, "5"}, {20, "5"}, {30, "0"},
             {0, "LINE"}, {10, "5"}, {20, "5"}, {30, "0"}, {11, "6"}, {21, "5"}, {31, "0"},
             {0, "ENDBLK"}},
        {{0, "INSERT"}, {2, "PIEZA"}, {10, "100"}, {20, "0"}, {30, "0"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK_NEAR(mesh.bounds.lo.x, 100.0, 1e-9);
    CHECK_NEAR(mesh.bounds.hi.x, 101.0, 1e-9);
    CHECK_NEAR(mesh.bounds.lo.y, 0.0, 1e-9);
}

TEST(dxf_mirrored_insert_uses_extrusion) {
    // Bloque espejado: extrusion (0,0,-1) invierte el eje X del objeto.
    const std::string text = document(
        {}, {{0, "BLOCK"}, {2, "B"}, {70, "0"}, {10, "0"}, {20, "0"}, {30, "0"},
             {0, "LINE"}, {10, "0"}, {20, "0"}, {30, "0"}, {11, "1"}, {21, "0"}, {31, "0"},
             {0, "ENDBLK"}},
        {{0, "INSERT"}, {2, "B"}, {10, "10"}, {20, "0"}, {30, "0"},
         {210, "0"}, {220, "0"}, {230, "-1"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK_NEAR(mesh.bounds.lo.x, -11.0, 1e-9);
    CHECK_NEAR(mesh.bounds.hi.x, -10.0, 1e-9);
}

TEST(dxf_minsert_repeats_block) {
    const std::string text = document(
        {}, {{0, "BLOCK"}, {2, "B"}, {70, "0"}, {10, "0"}, {20, "0"}, {30, "0"},
             {0, "LINE"}, {10, "0"}, {20, "0"}, {30, "0"}, {11, "1"}, {21, "0"}, {31, "0"},
             {0, "ENDBLK"}},
        {{0, "INSERT"}, {2, "B"}, {10, "0"}, {20, "0"}, {30, "0"},
         {70, "3"}, {71, "2"}, {44, "10"}, {45, "20"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(mesh.edgeLines.size() == 12);
    CHECK_NEAR(mesh.bounds.hi.x, 21.0, 1e-9);
    CHECK_NEAR(mesh.bounds.hi.y, 20.0, 1e-9);
}

TEST(dxf_dimension_draws_its_block) {
    const std::string text = document(
        {}, {{0, "BLOCK"}, {2, "*D1"}, {70, "1"}, {10, "0"}, {20, "0"}, {30, "0"},
             {0, "LINE"}, {10, "0"}, {20, "-3"}, {30, "0"}, {11, "10"}, {21, "-3"}, {31, "0"},
             {0, "ENDBLK"}},
        {{0, "DIMENSION"}, {2, "*D1"}, {10, "10"}, {20, "-3"}, {30, "0"}, {70, "32"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK_NEAR(mesh.bounds.lo.y, -3.0, 1e-9);
    CHECK_NEAR(mesh.bounds.size().x, 10.0, 1e-9);
}

TEST(dxf_paper_space_ignored_when_model_space_has_geometry) {
    const std::string text = entities({{0, "LINE"}, {10, "0"}, {20, "0"}, {11, "10"}, {21, "0"},
                                       {0, "LINE"}, {67, "1"}, {10, "500"}, {20, "500"},
                                       {11, "900"}, {21, "500"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK_NEAR(mesh.bounds.hi.x, 10.0, 1e-9);
}

TEST(dxf_paper_space_used_when_model_space_is_empty) {
    const std::string text = entities({{0, "LINE"}, {67, "1"}, {10, "500"}, {20, "500"},
                                       {11, "900"}, {21, "500"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK_NEAR(mesh.bounds.hi.x, 900.0, 1e-9);
}

TEST(dxf_frozen_and_off_layers_are_hidden) {
    const std::string text = document(
        {{0, "LAYER"}, {2, "CONGELADA"}, {70, "1"}, {62, "7"},
         {0, "LAYER"}, {2, "APAGADA"}, {70, "0"}, {62, "-7"},
         {0, "LAYER"}, {2, "VISIBLE"}, {70, "0"}, {62, "7"}},
        {},
        {{0, "LINE"}, {8, "VISIBLE"}, {10, "0"}, {20, "0"}, {11, "10"}, {21, "0"},
         {0, "LINE"}, {8, "CONGELADA"}, {10, "0"}, {20, "0"}, {11, "50"}, {21, "0"},
         {0, "LINE"}, {8, "APAGADA"}, {10, "0"}, {20, "0"}, {11, "0"}, {21, "50"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(mesh.edgeLines.size() == 2);
}

// --- DXF: textos ----------------------------------------------------------------

namespace {

const stp::MeshText* onlyText(const stp::Mesh& mesh) {
    return mesh.texts.size() == 1 ? &mesh.texts[0] : nullptr;
}

}  // namespace

TEST(dxf_text_basic_placement) {
    const std::string text = entities({{0, "TEXT"}, {1, "HOLA"}, {10, "1"}, {20, "2"}, {30, "0"},
                                       {40, "3.5"}, {50, "90"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    const stp::MeshText* t = onlyText(mesh);
    CHECK(t != nullptr);
    if (!t) return;
    CHECK(t->text == "HOLA");
    CHECK_NEAR(t->position.x, 1.0, 1e-9);
    CHECK_NEAR(t->position.y, 2.0, 1e-9);
    CHECK_NEAR(t->direction.y, 1.0, 1e-9);
    CHECK_NEAR(t->up.x, -1.0, 1e-9);
    CHECK_NEAR(t->height, 3.5, 1e-9);
    CHECK(t->halign == 0 && t->valign == 0);
}

TEST(dxf_text_alignment_uses_second_point) {
    const std::string text = entities({{0, "TEXT"}, {1, "A"}, {10, "0"}, {20, "0"}, {40, "1"},
                                       {11, "50"}, {21, "5"}, {72, "1"}, {73, "2"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    const stp::MeshText* t = onlyText(mesh);
    CHECK(t != nullptr);
    if (!t) return;
    CHECK_NEAR(t->position.x, 50.0, 1e-9);
    CHECK_NEAR(t->position.y, 5.0, 1e-9);
    CHECK(t->halign == 1 && t->valign == 2);
}

TEST(dxf_text_special_codes) {
    const std::string text = entities({{0, "TEXT"}, {1, "%%c10 %%p0.1 45%%d 100%%% %%uok"},
                                       {10, "0"}, {20, "0"}, {40, "1"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    const stp::MeshText* t = onlyText(mesh);
    CHECK(t && t->text == "\xC3\x98" "10 \xC2\xB1" "0.1 45\xC2\xB0 100% ok");
}

TEST(dxf_text_unicode_escape) {
    const std::string text = entities({{0, "TEXT"}, {1, "Espa\\U+00F1a"}, {10, "0"}, {20, "0"},
                                       {40, "1"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    const stp::MeshText* t = onlyText(mesh);
    CHECK(t && t->text == "Espa\xC3\xB1" "a");
}

TEST(dxf_text_ansi_bytes_become_utf8) {
    const std::string text = entities({{0, "TEXT"}, {1, "Cami\xF3n"}, {10, "0"}, {20, "0"},
                                       {40, "1"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    const stp::MeshText* t = onlyText(mesh);
    CHECK(t && t->text == "Cami\xC3\xB3n");
}

TEST(dxf_text_utf8_is_kept) {
    const std::string text = entities({{0, "TEXT"}, {1, "Cami\xC3\xB3n"}, {10, "0"}, {20, "0"},
                                       {40, "1"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    const stp::MeshText* t = onlyText(mesh);
    CHECK(t && t->text == "Cami\xC3\xB3n");
}

TEST(dxf_mtext_formatting_is_cleaned) {
    const std::string text = entities(
        {{0, "MTEXT"}, {10, "0"}, {20, "0"}, {30, "0"}, {40, "2"}, {71, "5"},
         {3, "{\\fArial|b1;Linea 1}\\PLin"},
         {1, "ea \\C1;2\\~fin \\S1^2; \\\\x\\{y\\}"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    const stp::MeshText* t = onlyText(mesh);
    CHECK(t != nullptr);
    if (!t) return;
    CHECK(t->text == "Linea 1\nLinea 2 fin 1/2 \\x{y}");
    CHECK(t->halign == 1 && t->valign == 2);
    CHECK_NEAR(t->height, 2.0, 1e-9);
}

TEST(dxf_mtext_direction_vector_and_rotation) {
    const std::string text = entities(
        {{0, "MTEXT"}, {10, "0"}, {20, "0"}, {40, "1"}, {71, "7"}, {1, "V"},
         {11, "0"}, {21, "2"}, {31, "0"},
         {0, "MTEXT"}, {10, "0"}, {20, "0"}, {40, "1"}, {71, "1"}, {1, "R"},
         {50, "180"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(mesh.texts.size() == 2);
    if (mesh.texts.size() != 2) return;
    CHECK_NEAR(mesh.texts[0].direction.y, 1.0, 1e-9);
    CHECK(mesh.texts[0].valign == 1 && mesh.texts[0].halign == 0);
    CHECK_NEAR(mesh.texts[1].direction.x, -1.0, 1e-9);
    CHECK(mesh.texts[1].valign == 3);
}

TEST(dxf_text_in_block_follows_insert) {
    const std::string text = document(
        {}, {{0, "BLOCK"}, {2, "R"}, {70, "0"}, {10, "0"}, {20, "0"}, {30, "0"},
             {0, "TEXT"}, {1, "X"}, {10, "1"}, {20, "0"}, {30, "0"}, {40, "1"},
             {0, "ENDBLK"}},
        {{0, "INSERT"}, {2, "R"}, {10, "10"}, {20, "0"}, {30, "0"}, {41, "2"}, {42, "2"},
         {50, "90"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    const stp::MeshText* t = onlyText(mesh);
    CHECK(t != nullptr);
    if (!t) return;
    CHECK_NEAR(t->position.x, 10.0, 1e-9);
    CHECK_NEAR(t->position.y, 2.0, 1e-9);
    CHECK_NEAR(t->direction.y, 1.0, 1e-9);
    CHECK_NEAR(t->height, 2.0, 1e-9);
}

TEST(dxf_attributes_after_insert) {
    const std::string text = document(
        {}, {{0, "BLOCK"}, {2, "R"}, {70, "2"}, {10, "0"}, {20, "0"}, {30, "0"},
             {0, "ATTDEF"}, {1, "PLANTILLA"}, {2, "TAG"}, {10, "0"}, {20, "0"}, {40, "1"},
             {0, "LINE"}, {10, "0"}, {20, "0"}, {11, "1"}, {21, "0"},
             {0, "ENDBLK"}},
        {{0, "INSERT"}, {66, "1"}, {2, "R"}, {10, "0"}, {20, "0"}, {30, "0"},
         {0, "ATTRIB"}, {1, "P-01"}, {2, "TAG"}, {10, "5"}, {20, "5"}, {40, "1"}, {70, "0"},
         {0, "ATTRIB"}, {1, "OCULTO"}, {2, "TAG2"}, {10, "5"}, {20, "5"}, {40, "1"}, {70, "1"},
         {0, "SEQEND"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    const stp::MeshText* t = onlyText(mesh);
    CHECK(t && t->text == "P-01");
}

TEST(dxf_drawing_with_only_text_loads) {
    const std::string text = entities({{0, "TEXT"}, {1, "NOTA"}, {10, "0"}, {20, "0"},
                                       {40, "1"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(!mesh.empty());
}

TEST(dxf_dimension_text_comes_from_its_block) {
    const std::string text = document(
        {}, {{0, "BLOCK"}, {2, "*D2"}, {70, "1"}, {10, "0"}, {20, "0"}, {30, "0"},
             {0, "LINE"}, {10, "0"}, {20, "0"}, {11, "10"}, {21, "0"},
             {0, "MTEXT"}, {10, "5"}, {20, "1"}, {40, "2.5"}, {71, "8"}, {1, "10"},
             {0, "ENDBLK"}},
        {{0, "DIMENSION"}, {2, "*D2"}, {70, "32"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    const stp::MeshText* t = onlyText(mesh);
    CHECK(t && t->text == "10");
}

// --- Elementos medibles ---------------------------------------------------------

namespace {

std::string headerSection(const Pairs& header) {
    Pairs all = {{0, "SECTION"}, {2, "HEADER"}};
    all.insert(all.end(), header.begin(), header.end());
    all.push_back({0, "ENDSEC"});
    return dxf(all);
}

}  // namespace

TEST(features_dxf_circle_is_exact) {
    const std::string text = entities({{0, "CIRCLE"}, {10, "10"}, {20, "20"}, {30, "0"}, {40, "5"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(mesh.features.circles.size() == 1);
    CHECK(mesh.features.contours.size() == 1);
    if (mesh.features.circles.size() != 1) return;
    const stp::CircleFeature& c = mesh.features.circles[0];
    CHECK_NEAR(c.radius, 5.0, 1e-12);
    CHECK_NEAR(c.center.x, 10.0, 1e-12);
    CHECK_NEAR(c.center.y, 20.0, 1e-12);
    CHECK(c.full());
    CHECK(mesh.units == stp::LengthUnit::Millimeter);  // DXF sin $INSUNITS: mm
}

TEST(features_dxf_arc_keeps_its_range) {
    const std::string text = entities({{0, "ARC"}, {10, "0"}, {20, "0"}, {40, "4"},
                                       {50, "0"}, {51, "90"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(mesh.features.circles.size() == 1);
    CHECK(mesh.features.contours.empty());
    if (mesh.features.circles.empty()) return;
    const stp::CircleFeature& c = mesh.features.circles[0];
    CHECK_NEAR(c.startAngle, 0.0, 1e-12);
    CHECK_NEAR(c.sweep, stp::kPi / 2, 1e-12);
    CHECK(!c.full());
    CHECK_NEAR(c.pointAt(c.startAngle + c.sweep).y, 4.0, 1e-12);
}

TEST(features_dxf_bulge_polyline_registers_arc_and_contour) {
    // Cuadrado 10x10 con el lado de cierre abombado (semicirculo de radio 5).
    const std::string text = entities({{0, "LWPOLYLINE"}, {90, "4"}, {70, "1"},
                                       {10, "0"}, {20, "0"}, {10, "10"}, {20, "0"},
                                       {10, "10"}, {20, "10"}, {10, "0"}, {20, "10"}, {42, "1"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(mesh.features.circles.size() == 1);
    CHECK(mesh.features.contours.size() == 1);
    if (mesh.features.circles.empty() || mesh.features.contours.empty()) return;
    CHECK_NEAR(mesh.features.circles[0].radius, 5.0, 1e-12);
    CHECK(mesh.features.contours[0].points.size() == 4);
    CHECK_NEAR(mesh.features.contours[0].bulges[3], 1.0, 1e-12);
}

TEST(features_dxf_segments_are_flagged_straight_or_curved) {
    const std::string text = entities({{0, "LINE"}, {10, "0"}, {20, "0"}, {11, "10"}, {21, "0"},
                                       {0, "CIRCLE"}, {10, "0"}, {20, "0"}, {40, "1"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(mesh.edgeCurve.size() == mesh.edgeLines.size() / 2);
    if (mesh.edgeCurve.size() < 2) return;
    CHECK(mesh.edgeCurve[0] == 0);
    CHECK(mesh.edgeCurve[1] == 1);
    CHECK(mesh.edgeCurve.back() == 1);
}

TEST(features_mirrored_insert_keeps_radius) {
    const std::string text = document(
        {}, {{0, "BLOCK"}, {2, "B"}, {70, "0"}, {10, "0"}, {20, "0"}, {30, "0"},
             {0, "CIRCLE"}, {10, "2"}, {20, "0"}, {30, "0"}, {40, "3"},
             {0, "ENDBLK"}},
        {{0, "INSERT"}, {2, "B"}, {10, "10"}, {20, "0"}, {30, "0"},
         {210, "0"}, {220, "0"}, {230, "-1"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(mesh.features.circles.size() == 1);
    if (mesh.features.circles.empty()) return;
    CHECK_NEAR(mesh.features.circles[0].radius, 3.0, 1e-12);
    CHECK_NEAR(mesh.features.circles[0].center.x, -12.0, 1e-12);
}

TEST(features_nonuniform_insert_drops_circle) {
    const std::string text = document(
        {}, {{0, "BLOCK"}, {2, "B"}, {70, "0"}, {10, "0"}, {20, "0"}, {30, "0"},
             {0, "CIRCLE"}, {10, "0"}, {20, "0"}, {30, "0"}, {40, "3"},
             {0, "ENDBLK"}},
        {{0, "INSERT"}, {2, "B"}, {10, "0"}, {20, "0"}, {30, "0"}, {41, "2"}, {42, "1"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(mesh.features.circles.empty());
    CHECK(mesh.features.contours.empty());
    CHECK(!mesh.edgeLines.empty());  // se sigue dibujando, como elipse
}

TEST(features_dxf_units_from_insunits) {
    stp::Mesh inches;
    CHECK(loadDxfText(headerSection({{9, "$INSUNITS"}, {70, "1"}}) +
                          entities({{0, "LINE"}, {10, "0"}, {20, "0"}, {11, "1"}, {21, "0"}}),
                      &inches));
    CHECK(inches.units == stp::LengthUnit::Inch);
    stp::Mesh meters;
    CHECK(loadDxfText(headerSection({{9, "$INSUNITS"}, {70, "6"}}) +
                          entities({{0, "LINE"}, {10, "0"}, {20, "0"}, {11, "1"}, {21, "0"}}),
                      &meters));
    CHECK(meters.units == stp::LengthUnit::Meter);
}

namespace {

std::string readText(const char* path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool loadStepText(const std::string& text, stp::Mesh* mesh) {
    std::string error;
    return stp::loadModel(text.data(), text.size(), ".stp", mesh, &error);
}

bool loadModelFile(const char* path, stp::Mesh* mesh) {
    const std::string bytes = readText(path);
    const char* dot = std::strrchr(path, '.');
    std::string error;
    return stp::loadModel(bytes.data(), bytes.size(), dot ? dot : "", mesh, &error);
}

}  // namespace

TEST(features_step_hole_is_exact) {
    stp::Mesh mesh;
    CHECK(loadStepText(readText("tests/samples/placa_agujero.stp"), &mesh));
    CHECK(mesh.units == stp::LengthUnit::Millimeter);
    int holeEdges = 0;
    for (const stp::CircleFeature& c : mesh.features.circles) {
        if (std::fabs(c.radius - 10.0) < 1e-9) ++holeEdges;
    }
    CHECK(holeEdges >= 2);  // borde de arriba y de abajo del agujero
    bool cylinder = false;
    for (const stp::FaceFeature& f : mesh.features.faces) {
        if (f.kind == stp::SurfaceKind::Cylinder && std::fabs(f.radius - 10.0) < 1e-9) cylinder = true;
    }
    CHECK(cylinder);
}

TEST(features_step_faces_cover_every_triangle_once) {
    stp::Mesh mesh;
    CHECK(loadStepText(readText("tests/samples/placa_agujero.stp"), &mesh));
    std::uint32_t next = 0;
    bool ordered = true;
    for (const stp::FaceFeature& f : mesh.features.faces) {
        if (f.firstTriangle != next || f.triangleCount == 0) ordered = false;
        next = f.firstTriangle + f.triangleCount;
    }
    CHECK(ordered);
    CHECK(next == mesh.triangleCount());
}

TEST(features_step_straight_edges_are_not_curves) {
    stp::Mesh mesh;
    CHECK(loadStepText(readText("tests/samples/caja.stp"), &mesh));
    CHECK(mesh.edgeCurve.size() == mesh.edgeLines.size() / 2);
    bool anyCurve = false;
    for (const std::uint8_t c : mesh.edgeCurve) anyCurve = anyCurve || c != 0;
    CHECK(!anyCurve);  // una caja solo tiene rectas
}

TEST(features_step_repeated_instance_keeps_circles_and_edges) {
    // La misma pieza colocada dos veces (dos solidos sobre el mismo cascaron):
    // cada copia tiene que tener sus circulos y aristas para medir.
    std::string text = readText("tests/samples/placa_agujero.stp");
    stp::Mesh single;
    CHECK(loadStepText(text, &single));
    const std::string rep = "(#228,#219),#224);";
    const std::size_t at = text.find(rep);
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    text.replace(at, rep.size(), "(#228,#219,#999998),#224);");
    const std::size_t data = text.find("ENDSEC;", text.find("DATA;"));
    text.insert(data, "#999998=MANIFOLD_SOLID_BREP('',#218);\n");
    stp::Mesh twice;
    CHECK(loadStepText(text, &twice));
    CHECK(twice.features.circles.size() == 2 * single.features.circles.size());
    CHECK(twice.edgeLines.size() == 2 * single.edgeLines.size());
}

TEST(features_step_units_inch) {
    std::string text = readText("tests/samples/caja.stp");
    const std::string mm = "( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) )";
    const std::size_t at = text.find(mm);
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    text.replace(at, mm.size(), "( CONVERSION_BASED_UNIT('INCH',#999999) LENGTH_UNIT() NAMED_UNIT(*) )");
    stp::Mesh mesh;
    CHECK(loadStepText(text, &mesh));
    CHECK(mesh.units == stp::LengthUnit::Inch);
}

namespace {

// Linea IGES de 80 columnas: 72 de datos, la letra de seccion y el numero.
std::string igesLine(const std::string& data, char section, int sequence) {
    std::string line = data;
    line.resize(72, ' ');
    char number[16];
    std::snprintf(number, sizeof(number), "%7d", sequence);
    return line + section + number + "\n";
}

// Archivo IGES con un solo circulo (entidad 100) de radio 5 en (10, 20).
std::string igesCircle(int unitFlag) {
    std::string out = igesLine("stp-viewer prueba", 'S', 1);
    const std::string global = "1H,,1H;,4Hprod,8Hfile.igs,3Hsys,3H1.0,32,38,6,308,15,4Hprod,1.," +
                               std::to_string(unitFlag) +
                               ",2HMM,1,0.1,15H20260923.120000,0.001,100.,4Huser,3Horg,11,0;";
    int g = 0;
    for (std::size_t i = 0; i < global.size(); i += 72) out += igesLine(global.substr(i, 72), 'G', ++g);
    char d1[96], d2[96];
    std::snprintf(d1, sizeof(d1), "%8d%8d%8d%8d%8d%8d%8d%8d%8s", 100, 1, 0, 1, 0, 0, 0, 0, "00000000");
    std::snprintf(d2, sizeof(d2), "%8d%8d%8d%8d%8d%8s%8s%8s%8d", 100, 0, 0, 1, 0, "", "", "", 0);
    out += igesLine(d1, 'D', 1) + igesLine(d2, 'D', 2);
    std::string p = "100,0.,10.,20.,15.,20.,15.,20.;";
    p.resize(64, ' ');
    p += "       1";  // columnas 65-72: puntero al directorio
    out += igesLine(p, 'P', 1);
    out += igesLine("S      1G      1D      2P      1", 'T', 1);
    return out;
}

bool loadIgesText(const std::string& text, stp::Mesh* mesh) {
    std::string error;
    return stp::loadModel(text.data(), text.size(), ".igs", mesh, &error);
}

}  // namespace

TEST(features_iges_arc_is_exact) {
    stp::Mesh mesh;
    CHECK(loadIgesText(igesCircle(2), &mesh));
    CHECK(mesh.features.circles.size() == 1);
    if (mesh.features.circles.empty()) return;
    CHECK_NEAR(mesh.features.circles[0].radius, 5.0, 1e-12);
    CHECK_NEAR(mesh.features.circles[0].center.y, 20.0, 1e-12);
    CHECK(mesh.features.circles[0].full());
    CHECK(mesh.units == stp::LengthUnit::Millimeter);
    CHECK(!mesh.edgeCurve.empty() && mesh.edgeCurve[0] == 1);
}

TEST(features_iges_units) {
    stp::Mesh inch, meter;
    CHECK(loadIgesText(igesCircle(1), &inch));
    CHECK(loadIgesText(igesCircle(6), &meter));
    CHECK(inch.units == stp::LengthUnit::Inch);
    CHECK(meter.units == stp::LengthUnit::Meter);
}

// --- Planaridad ----------------------------------------------------------------

namespace {

void addRectangle(stp::Mesh* mesh, const stp::Vec3& origin, const stp::Vec3& u,
                  const stp::Vec3& v) {
    const stp::Vec3 a = origin, b = origin + u, c = origin + u + v, d = origin + v;
    mesh->addSegment(a, b);
    mesh->addSegment(b, c);
    mesh->addSegment(c, d);
    mesh->addSegment(d, a);
}

}  // namespace

TEST(planar_xy_rectangle_is_2d_seen_from_top) {
    stp::Mesh mesh;
    addRectangle(&mesh, stp::Vec3(0, 0, 0), stp::Vec3(20, 0, 0), stp::Vec3(0, 10, 0));
    const stp::PlanarInfo info = stp::detectPlanar(mesh);
    CHECK(info.planar);
    CHECK_NEAR(info.normal.z, 1.0, 1e-9);
    CHECK_NEAR(info.width, 20.0, 1e-9);
    CHECK_NEAR(info.height, 10.0, 1e-9);
}

TEST(planar_tilted_plane_is_2d) {
    stp::Mesh mesh;
    const double angle = 30.0 * stp::kPi / 180.0;
    const stp::Vec3 v(0, 10 * std::cos(angle), 10 * std::sin(angle));
    addRectangle(&mesh, stp::Vec3(5, 5, 5), stp::Vec3(20, 0, 0), v);
    const stp::PlanarInfo info = stp::detectPlanar(mesh);
    CHECK(info.planar);
    const stp::Vec3 expected(0, -std::sin(angle), std::cos(angle));
    CHECK_NEAR(std::fabs(stp::dot(info.normal, expected)), 1.0, 1e-9);
    CHECK_NEAR(info.width, 20.0, 1e-6);
    CHECK_NEAR(info.height, 10.0, 1e-6);
}

TEST(planar_thin_washer_is_not_2d) {
    // Una pieza de 1 mm de espesor sobre 100 mm sigue siendo 3D.
    stp::Mesh mesh;
    addRectangle(&mesh, stp::Vec3(0, 0, 0), stp::Vec3(100, 0, 0), stp::Vec3(0, 100, 0));
    addRectangle(&mesh, stp::Vec3(0, 0, 1), stp::Vec3(100, 0, 0), stp::Vec3(0, 100, 0));
    CHECK(!stp::detectPlanar(mesh).planar);
}

TEST(planar_degenerate_inputs_are_not_2d) {
    stp::Mesh empty;
    CHECK(!stp::detectPlanar(empty).planar);
    stp::Mesh point;
    point.addSegment(stp::Vec3(1, 1, 1), stp::Vec3(1, 1, 1));
    CHECK(!stp::detectPlanar(point).planar);
}

TEST(planar_triangles_count_as_geometry) {
    stp::Mesh mesh;
    const auto a = mesh.addVertex(stp::Vec3(0, 0, 0), stp::Vec3(0, 0, 1));
    const auto b = mesh.addVertex(stp::Vec3(10, 0, 0), stp::Vec3(0, 0, 1));
    const auto c = mesh.addVertex(stp::Vec3(0, 10, 0), stp::Vec3(0, 0, 1));
    const auto d = mesh.addVertex(stp::Vec3(0, 0, 10), stp::Vec3(0, 0, 1));
    mesh.addTriangle(a, b, c);
    mesh.addTriangle(a, b, d);
    CHECK(!stp::detectPlanar(mesh).planar);
}

TEST(planar_vertical_drawing_is_seen_from_front) {
    stp::Mesh mesh;
    addRectangle(&mesh, stp::Vec3(0, 3, 0), stp::Vec3(40, 0, 0), stp::Vec3(0, 0, 15));
    const stp::PlanarInfo info = stp::detectPlanar(mesh);
    CHECK(info.planar);
    CHECK_NEAR(info.u.x, 1.0, 1e-9);
    CHECK_NEAR(info.v.z, 1.0, 1e-9);
    CHECK_NEAR(info.normal.y, -1.0, 1e-9);
    CHECK_NEAR(info.width, 40.0, 1e-9);
    CHECK_NEAR(info.height, 15.0, 1e-9);
    CHECK_NEAR(info.center.x, 20.0, 1e-9);
    CHECK_NEAR(info.center.z, 7.5, 1e-9);
}

TEST(planar_single_line_is_2d) {
    stp::Mesh mesh;
    mesh.addSegment(stp::Vec3(0, 0, 0), stp::Vec3(10, 10, 0));
    const stp::PlanarInfo info = stp::detectPlanar(mesh);
    CHECK(info.planar);
    CHECK_NEAR(info.normal.z, 1.0, 1e-9);
}

// --- NURBS -----------------------------------------------------------------------

TEST(nurbs_rational_quarter_circle_stays_on_circle) {
    stp::NurbsCurve curve;
    curve.degree = 2;
    curve.knots = {0, 0, 0, 1, 1, 1};
    curve.controls = {stp::Vec3(1, 0, 0), stp::Vec3(1, 1, 0), stp::Vec3(0, 1, 0)};
    curve.weights = {1, std::sqrt(0.5), 1};
    CHECK(curve.valid());
    for (int i = 0; i <= 10; ++i) {
        const stp::Vec3 p = curve.eval(i / 10.0);
        CHECK_NEAR(stp::length(p), 1.0, 1e-12);
    }
    CHECK_NEAR(curve.eval(1.0).y, 1.0, 1e-12);
}

TEST(planar_counts_text_anchors) {
    stp::Mesh mesh;
    addRectangle(&mesh, stp::Vec3(0, 0, 0), stp::Vec3(10, 0, 0), stp::Vec3(0, 10, 0));
    stp::MeshText text;
    text.text = "Z";
    text.position = stp::Vec3(5, 5, 50);
    mesh.addText(text);
    CHECK(!stp::detectPlanar(mesh).planar);
}

TEST(text_box_is_counted_in_bounds_and_planarity) {
    const std::string text = entities({{0, "TEXT"}, {1, "ABCD"}, {10, "0"}, {20, "0"},
                                       {40, "2"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    CHECK(mesh.bounds.hi.x > 4.0);
    CHECK_NEAR(mesh.bounds.hi.y, 2.0, 1e-9);
    const stp::PlanarInfo info = stp::detectPlanar(mesh);
    CHECK(info.planar);
    CHECK(info.width > 4.0);
}

TEST(text_box_follows_alignment) {
    stp::MeshText text;
    text.text = "AB\nCD";
    text.height = 1.0;
    text.halign = 2;
    text.valign = 3;
    stp::Vec3 corners[4];
    stp::textCorners(text, corners);
    double loX = 1e9, hiX = -1e9, loY = 1e9, hiY = -1e9;
    for (const stp::Vec3& c : corners) {
        loX = std::min(loX, c.x); hiX = std::max(hiX, c.x);
        loY = std::min(loY, c.y); hiY = std::max(hiY, c.y);
    }
    CHECK_NEAR(hiX, 0.0, 1e-12);   // alineado a la derecha
    CHECK(loX < -1.0);
    CHECK_NEAR(hiY, 0.0, 1e-12);   // colgado desde arriba
    CHECK(loY < -2.0);             // dos renglones
}

// --- Camara 2D ------------------------------------------------------------------------

TEST(camera_plan_view_faces_vertical_drawing) {
    stp::Mesh mesh;
    addRectangle(&mesh, stp::Vec3(0, 3, 0), stp::Vec3(40, 0, 0), stp::Vec3(0, 0, 15));
    stp::Camera camera;
    camera.fitPlanar(stp::detectPlanar(mesh), 2.0);
    CHECK(camera.planView);
    CHECK(camera.ortho);
    double x0, y0, x1, y1;
    CHECK(stp::projectPoint(camera, 400, 200, stp::Vec3(0, 3, 0), &x0, &y0));
    CHECK(stp::projectPoint(camera, 400, 200, stp::Vec3(40, 3, 15), &x1, &y1));
    CHECK(x0 < x1);  // X hacia la derecha
    CHECK(y0 > y1);  // Z hacia arriba (y de pantalla crece hacia abajo)
    CHECK(x0 > 0 && x1 < 400 && y1 > 0 && y0 < 200);
    // Encuadre ajustado: el dibujo ocupa casi todo el ancho.
    CHECK(x1 - x0 > 400 * 0.85);
    CHECK_NEAR(stp::dot(camera.forward(), stp::Vec3(0, 1, 0)), 1.0, 1e-12);
}

TEST(camera_zoom_keeps_point_under_cursor) {
    stp::Mesh mesh;
    addRectangle(&mesh, stp::Vec3(0, 0, 0), stp::Vec3(100, 0, 0), stp::Vec3(0, 50, 0));
    stp::Camera camera;
    camera.fitPlanar(stp::detectPlanar(mesh), 1.5);
    const stp::Vec3 world(80, 10, 0);
    double sx, sy;
    CHECK(stp::projectPoint(camera, 300, 200, world, &sx, &sy));
    const double before = camera.orthoHeight;
    camera.zoomAt(0.5, sx, sy, 300, 200);
    CHECK_NEAR(camera.orthoHeight, before * 0.5, 1e-12);
    double ax, ay;
    CHECK(stp::projectPoint(camera, 300, 200, world, &ax, &ay));
    CHECK_NEAR(ax, sx, 1e-6);
    CHECK_NEAR(ay, sy, 1e-6);
}

TEST(camera_leaving_plan_view_restores_orbit) {
    stp::Mesh mesh;
    addRectangle(&mesh, stp::Vec3(0, 0, 0), stp::Vec3(10, 0, 0), stp::Vec3(0, 10, 0));
    stp::Camera camera;
    camera.fitPlanar(stp::detectPlanar(mesh), 1.0);
    camera.planView = false;
    camera.yaw = 0.0;
    camera.pitch = 0.0;
    CHECK_NEAR(camera.forward().x, -1.0, 1e-12);
}

// --- Seleccion y enganche --------------------------------------------------------

namespace {

struct PlanScene {
    stp::Mesh mesh;
    stp::PlanarInfo plane;
    stp::Camera camera;
    stp::PickIndex pick;
    int width = 800, height = 600;

    void finish() {
        plane = stp::detectPlanar(mesh);
        camera.fitPlanar(plane, static_cast<double>(width) / height);
        pick.build(mesh);
    }
    stp::SnapResult snapNear(const stp::Vec3& world, double dx, double dy, bool snapping = true,
                             unsigned modes = stp::kSnapDefaultModes, const stp::Vec3* from = nullptr) {
        double sx = 0, sy = 0;
        stp::projectPoint(camera, width, height, world, &sx, &sy);
        stp::SnapOptions options;
        options.snapping = snapping;
        options.modes = modes;
        options.from = from;
        options.plane = &plane;
        return pick.snap(mesh, camera, width, height, sx + dx, sy + dy, options);
    }
    std::vector<stp::SnapResult> snapAllNear(const stp::Vec3& world, double dx, double dy,
                                             unsigned modes = stp::kSnapDefaultModes,
                                             const stp::Vec3* from = nullptr) {
        double sx = 0, sy = 0;
        stp::projectPoint(camera, width, height, world, &sx, &sy);
        stp::SnapOptions options;
        options.modes = modes;
        options.from = from;
        options.plane = &plane;
        return pick.snapAll(mesh, camera, width, height, sx + dx, sy + dy, options);
    }
};

PlanScene lineAndCircle() {
    PlanScene scene;
    const std::string text = entities({{0, "LINE"}, {10, "0"}, {20, "0"}, {11, "100"}, {21, "0"},
                                       {0, "CIRCLE"}, {10, "50"}, {20, "40"}, {40, "10"}});
    loadDxfText(text, &scene.mesh);
    scene.finish();
    return scene;
}

PlanScene sceneFrom(const std::string& text) {
    PlanScene scene;
    CHECK(loadDxfText(text, &scene.mesh));
    scene.finish();
    return scene;
}

bool hasKind(const std::vector<stp::SnapResult>& all, stp::SnapKind kind) {
    for (const stp::SnapResult& s : all) {
        if (s.kind == kind) return true;
    }
    return false;
}

}  // namespace

TEST(snap_endpoint_near_cursor) {
    PlanScene scene = lineAndCircle();
    const stp::SnapResult near = scene.snapNear(stp::Vec3(100, 0, 0), 5, -3);
    CHECK(near.kind == stp::SnapKind::Endpoint);
    CHECK_NEAR(near.point.x, 100.0, 1e-9);
    const stp::SnapResult far = scene.snapNear(stp::Vec3(100, 0, 0), 20, 0);
    CHECK(far.kind == stp::SnapKind::OnPlane);
}

TEST(snap_midpoint_and_center) {
    PlanScene scene = lineAndCircle();
    const stp::SnapResult mid = scene.snapNear(stp::Vec3(50, 0, 0), 2, 2);
    CHECK(mid.kind == stp::SnapKind::Midpoint);
    CHECK_NEAR(mid.point.x, 50.0, 1e-9);
    const stp::SnapResult center = scene.snapNear(stp::Vec3(50, 40, 0), -3, 1);
    CHECK(center.kind == stp::SnapKind::Center);
    CHECK(center.circle == 0);
}

TEST(snap_on_circle_edge_has_no_fake_endpoints) {
    PlanScene scene = lineAndCircle();
    // Un punto del circulo a 30 grados: solo puede ser "mas cercano".
    const stp::Vec3 p(50 + 10 * std::cos(stp::kPi / 6), 40 + 10 * std::sin(stp::kPi / 6), 0);
    const stp::SnapResult edge = scene.snapNear(p, 0, 1, true, stp::kSnapDefaultModes | stp::kSnapNearest);
    CHECK(edge.kind == stp::SnapKind::OnEdge);
    CHECK(edge.segment >= 1);
    CHECK_NEAR(stp::distance(edge.point, stp::Vec3(50, 40, 0)), 10.0, 0.05);
    // Sin "mas cercano" no se ofrece nada: cae en el plano.
    CHECK(scene.snapNear(p, 0, 1).kind == stp::SnapKind::OnPlane);
}


TEST(snap_can_be_disabled) {
    PlanScene scene = lineAndCircle();
    const stp::SnapResult free = scene.snapNear(stp::Vec3(100, 0, 0), 0, 0, false);
    CHECK(free.kind == stp::SnapKind::OnPlane);
    CHECK_NEAR(free.point.x, 100.0, 1e-6);
    CHECK_NEAR(free.point.z, 0.0, 1e-9);
}

TEST(snap_slot_length_and_width_from_quadrants) {
    PlanScene scene;
    CHECK(loadModelFile("tests/samples/plano_brida.dxf", &scene.mesh));
    scene.finish();
    const stp::SnapResult bottom = scene.snapNear(stp::Vec3(170, 35, 0), 2, 1);
    const stp::SnapResult top = scene.snapNear(stp::Vec3(170, 105, 0), -1, 2);
    CHECK(bottom.kind == stp::SnapKind::Quadrant);
    CHECK(top.kind == stp::SnapKind::Quadrant);
    CHECK_NEAR(stp::distance(bottom.point, top.point), 70.0, 1e-9);
    const stp::SnapResult left = scene.snapNear(stp::Vec3(160, 45, 0), 1, 1);
    const stp::SnapResult right = scene.snapNear(stp::Vec3(180, 45, 0), -1, 1);
    CHECK(left.kind == stp::SnapKind::Endpoint);
    CHECK(right.kind == stp::SnapKind::Endpoint);
    CHECK_NEAR(stp::distance(left.point, right.point), 20.0, 1e-9);
}

TEST(snap_tab_order_puts_endpoint_before_quadrant) {
    PlanScene scene;
    CHECK(loadModelFile("tests/samples/plano_brida.dxf", &scene.mesh));
    scene.finish();
    const std::vector<stp::SnapResult> all = scene.snapAllNear(stp::Vec3(160, 45, 0), 0, 0);
    CHECK(all.size() >= 2);
    if (all.size() >= 2) {
        CHECK(all[0].kind == stp::SnapKind::Endpoint);
        CHECK(all[1].kind == stp::SnapKind::Quadrant);
        CHECK_NEAR(stp::distance(all[1].point, stp::Vec3(160, 45, 0)), 0.0, 1e-9);
    }
    CHECK(!hasKind(all, stp::SnapKind::Intersection));
}

TEST(snap_arc_midpoint) {
    PlanScene scene = sceneFrom(entities({{0, "ARC"}, {10, "0"}, {20, "0"}, {40, "10"}, {50, "0"}, {51, "90"}}));
    const stp::Vec3 mid(10 * std::cos(stp::kPi / 4), 10 * std::sin(stp::kPi / 4), 0);
    const stp::SnapResult r = scene.snapNear(mid, 1, -1);
    CHECK(r.kind == stp::SnapKind::Midpoint);
    CHECK_NEAR(stp::distance(r.point, mid), 0.0, 1e-9);
}

TEST(snap_quadrants_respect_negative_sweep) {
    // Arco de 0 a 90 grados recorrido al reves (normal -Z): sigue cubriendo el
    // primer cuadrante del dibujo y no ofrece el cuadrante de 180 grados.
    PlanScene scene = sceneFrom(entities({{0, "ARC"}, {10, "0"}, {20, "0"}, {40, "10"}, {50, "90"},
                                          {51, "180"}, {210, "0"}, {220, "0"}, {230, "-1"}}));
    CHECK(scene.mesh.features.circles.size() == 1);
    // Con normal -Z, x del OCS es -X: el arco va de (0,10) a (10,0) por el primer cuadrante.
    const std::vector<stp::SnapResult> right = scene.snapAllNear(stp::Vec3(10, 0, 0), 0, 0, stp::kSnapQuadrant);
    CHECK(hasKind(right, stp::SnapKind::Quadrant));
    const std::vector<stp::SnapResult> left = scene.snapAllNear(stp::Vec3(-10, 0, 0), 0, 0, stp::kSnapQuadrant);
    CHECK(left.empty());
    const stp::SnapResult mid = scene.snapNear(stp::Vec3(7.0710678, 7.0710678, 0), 0, 0, true, stp::kSnapMidpoint);
    CHECK(mid.kind == stp::SnapKind::Midpoint);
    CHECK_NEAR(mid.point.x, 7.0710678, 1e-6);
}

TEST(snap_line_line_and_line_circle_intersections) {
    PlanScene scene = sceneFrom(entities({{0, "LINE"}, {10, "0"}, {20, "0"}, {11, "100"}, {21, "100"},
                                          {0, "LINE"}, {10, "0"}, {20, "100"}, {11, "100"}, {21, "0"},
                                          {0, "CIRCLE"}, {10, "50"}, {20, "140"}, {40, "10"},
                                          {0, "LINE"}, {10, "0"}, {20, "145"}, {11, "100"}, {21, "145"}}));
    const stp::SnapResult cross = scene.snapNear(stp::Vec3(50, 50, 0), 2, 1);
    CHECK(cross.kind == stp::SnapKind::Intersection);
    CHECK_NEAR(stp::distance(cross.point, stp::Vec3(50, 50, 0)), 0.0, 1e-9);
    const stp::Vec3 hit(50 + std::sqrt(75.0), 145, 0);
    const stp::SnapResult lc = scene.snapNear(hit, -1, 1);
    CHECK(lc.kind == stp::SnapKind::Intersection);
    CHECK_NEAR(stp::distance(lc.point, hit), 0.0, 1e-9);
}

TEST(snap_circle_circle_intersection) {
    PlanScene scene = sceneFrom(entities({{0, "CIRCLE"}, {10, "0"}, {20, "0"}, {40, "10"},
                                          {0, "CIRCLE"}, {10, "12"}, {20, "0"}, {40, "10"}}));
    const stp::Vec3 hit(6, 8, 0);
    const stp::SnapResult r = scene.snapNear(hit, 1, 1);
    CHECK(r.kind == stp::SnapKind::Intersection);
    CHECK_NEAR(stp::distance(r.point, hit), 0.0, 1e-9);
}

TEST(snap_polyline_corner_is_not_intersection) {
    PlanScene scene = sceneFrom(entities({{0, "LWPOLYLINE"}, {90, "4"}, {70, "1"},
                                          {10, "0"}, {20, "0"}, {10, "100"}, {20, "0"},
                                          {10, "100"}, {20, "50"}, {10, "0"}, {20, "50"}}));
    const std::vector<stp::SnapResult> all = scene.snapAllNear(stp::Vec3(100, 0, 0), 1, 1);
    CHECK(!all.empty() && all[0].kind == stp::SnapKind::Endpoint);
    CHECK(!hasKind(all, stp::SnapKind::Intersection));
}

TEST(snap_tangent_joint_is_not_intersection) {
    // Recta que sigue en un arco tangente (esquina redondeada): la union es un extremo.
    const double b = std::tan(stp::kPi / 8);
    PlanScene scene = sceneFrom(entities({{0, "LWPOLYLINE"}, {90, "3"}, {70, "0"},
                                          {10, "0"}, {20, "0"}, {10, "90"}, {20, "0"}, {42, std::to_string(b)},
                                          {10, "100"}, {20, "10"}}));
    const std::vector<stp::SnapResult> all = scene.snapAllNear(stp::Vec3(90, 0, 0), 0, 0);
    CHECK(!all.empty() && all[0].kind == stp::SnapKind::Endpoint);
    CHECK(!hasKind(all, stp::SnapKind::Intersection));
}

TEST(snap_sampled_arc_pieces_do_not_intersect) {
    PlanScene scene = sceneFrom(entities({{0, "CIRCLE"}, {10, "0"}, {20, "0"}, {40, "10"}}));
    const stp::Vec3 p(10 * std::cos(0.5), 10 * std::sin(0.5), 0);
    CHECK(scene.snapAllNear(p, 0, 0, stp::kSnapIntersection).empty());
}

TEST(snap_extension_finds_rounded_corner_vertex) {
    const double b = std::tan(stp::kPi / 8);
    PlanScene scene = sceneFrom(entities({{0, "LWPOLYLINE"}, {90, "4"}, {70, "0"},
                                          {10, "0"}, {20, "0"}, {10, "90"}, {20, "0"}, {42, std::to_string(b)},
                                          {10, "100"}, {20, "10"}, {10, "100"}, {20, "50"}}));
    const stp::SnapResult off = scene.snapNear(stp::Vec3(100, 0, 0), 1, 1);
    CHECK(off.kind == stp::SnapKind::OnPlane);  // Extension esta apagada por defecto
    const stp::SnapResult r = scene.snapNear(stp::Vec3(100, 0, 0), 1, 1, true,
                                             stp::kSnapDefaultModes | stp::kSnapExtension);
    CHECK(r.kind == stp::SnapKind::Extension);
    CHECK_NEAR(stp::distance(r.point, stp::Vec3(100, 0, 0)), 0.0, 1e-9);
}

TEST(snap_perpendicular_to_line_and_circle) {
    PlanScene scene = sceneFrom(entities({{0, "LINE"}, {10, "30"}, {20, "-50"}, {11, "30"}, {21, "50"},
                                          {0, "CIRCLE"}, {10, "80"}, {20, "0"}, {40, "10"}}));
    const stp::Vec3 from(10, 0, 0);
    const stp::SnapResult none = scene.snapNear(stp::Vec3(30, 20, 0), 0, 0, true, stp::kSnapPerpendicular);
    CHECK(none.kind == stp::SnapKind::OnPlane);  // sin primer punto no hay perpendicular
    const stp::SnapResult line = scene.snapNear(stp::Vec3(30, 20, 0), 1, 0, true, stp::kSnapPerpendicular, &from);
    CHECK(line.kind == stp::SnapKind::Perpendicular);
    CHECK_NEAR(stp::distance(line.point, stp::Vec3(30, 0, 0)), 0.0, 1e-9);
    const stp::Vec3 onCircle(80 - 10 * std::cos(0.6), 10 * std::sin(0.6), 0);
    const stp::SnapResult circle = scene.snapNear(onCircle, 0, 0, true, stp::kSnapPerpendicular, &from);
    CHECK(circle.kind == stp::SnapKind::Perpendicular);
    CHECK_NEAR(stp::distance(circle.point, stp::Vec3(70, 0, 0)), 0.0, 1e-9);
}

TEST(snap_tangent_from_first_point) {
    PlanScene scene = sceneFrom(entities({{0, "CIRCLE"}, {10, "0"}, {20, "0"}, {40, "5"},
                                          {0, "POINT"}, {10, "10"}, {20, "0"}}));
    const stp::Vec3 from(10, 0, 0);
    const stp::Vec3 tangent(2.5, 5 * std::sin(stp::kPi / 3), 0);
    const stp::SnapResult r = scene.snapNear(tangent, 1, 1, true, stp::kSnapTangent, &from);
    CHECK(r.kind == stp::SnapKind::Tangent);
    CHECK_NEAR(r.point.x, 2.5, 1e-9);
    CHECK_NEAR(r.point.y, 4.330127019, 1e-6);
}

TEST(snap_disabled_modes_are_not_offered) {
    PlanScene scene = lineAndCircle();
    CHECK(scene.snapAllNear(stp::Vec3(100, 0, 0), 0, 0, stp::kSnapMidpoint).empty());
    CHECK(scene.snapAllNear(stp::Vec3(50, 40, 0), 0, 0, stp::kSnapEndpoint).empty());
    CHECK(scene.snapNear(stp::Vec3(100, 0, 0), 0, 0, true, 0).kind == stp::SnapKind::OnPlane);
    CHECK(std::string(stp::snapKindName(stp::SnapKind::Quadrant)) == "Cuadrante");
    CHECK(stp::snapModeOf(stp::SnapKind::OnEdge) == stp::kSnapNearest);
}

TEST(raycast_matches_brute_force) {
    stp::Mesh mesh;
    CHECK(loadStepText(readText("tests/samples/placa_agujero.stp"), &mesh));
    stp::Camera camera;
    camera.fit(mesh.bounds, 1.0);
    stp::PickIndex pick;
    pick.build(mesh);
    for (int i = 0; i < 25; ++i) {
        const stp::Ray ray = stp::pixelRay(camera, 500, 500, 100 + 12 * i, 180 + 6 * i);
        double t = 0;
        int triangle = -1;
        const bool hit = pick.raycast(mesh, ray, &t, &triangle);
        double best = 1e300;
        for (std::size_t k = 0; k < mesh.triangleCount(); ++k) {
            double tk;
            if (stp::rayTriangle(ray, mesh.positions[mesh.indices[3 * k]],
                                 mesh.positions[mesh.indices[3 * k + 1]],
                                 mesh.positions[mesh.indices[3 * k + 2]], &tk)) {
                best = std::min(best, tk);
            }
        }
        CHECK(hit == (best < 1e299));
        if (hit) CHECK_NEAR(t, best, 1e-9);
    }
}

TEST(snap_ignores_points_hidden_behind_faces) {
    stp::Mesh mesh;
    CHECK(loadStepText(readText("tests/samples/caja.stp"), &mesh));
    stp::Camera camera;  // isometrica por defecto: el ojo esta en +X, -Y, +Z
    camera.fit(mesh.bounds, 1.0);
    stp::PickIndex pick;
    pick.build(mesh);
    const stp::Vec3 hidden(mesh.bounds.lo.x, mesh.bounds.hi.y, mesh.bounds.lo.z);
    const stp::Vec3 front(mesh.bounds.hi.x, mesh.bounds.lo.y, mesh.bounds.hi.z);
    double sx, sy;
    stp::SnapOptions options;
    stp::projectPoint(camera, 500, 500, hidden, &sx, &sy);
    const stp::SnapResult behind = pick.snap(mesh, camera, 500, 500, sx, sy, options);
    CHECK(!(behind.kind == stp::SnapKind::Endpoint && stp::distance(behind.point, hidden) < 1e-9));
    stp::projectPoint(camera, 500, 500, front, &sx, &sy);
    const stp::SnapResult visible = pick.snap(mesh, camera, 500, 500, sx + 2, sy - 2, options);
    CHECK(visible.kind == stp::SnapKind::Endpoint);
    CHECK_NEAR(stp::distance(visible.point, front), 0.0, 1e-9);
}

TEST(snap_visible_silhouette_corner_from_any_side) {
    // Esquina visible en el borde de la silueta: desde cualquier lado a 4 px
    // tiene que engancharse, aunque el rayo del cursor toque una cara casi de canto.
    // Caso real del panel: placa de 80x50x8 en 812x606, esquina izquierda de arriba.
    stp::Mesh mesh;
    CHECK(loadStepText(readText("tests/samples/placa_agujero.stp"), &mesh));
    const int w = 812, h = 606;
    stp::Camera camera;
    camera.fit(mesh.bounds, static_cast<double>(w) / h);
    stp::PickIndex pick;
    pick.build(mesh);
    stp::SnapOptions options;
    const stp::Vec3 corners[] = {stp::Vec3(mesh.bounds.lo.x, mesh.bounds.lo.y, mesh.bounds.hi.z),
                                 stp::Vec3(mesh.bounds.hi.x, mesh.bounds.lo.y, mesh.bounds.hi.z)};
    int misses = 0;
    for (const stp::Vec3& corner : corners) {
        double sx, sy;
        stp::projectPoint(camera, w, h, corner, &sx, &sy);
        for (int k = 0; k < 16; ++k) {
            const double a = 2 * stp::kPi * k / 16;
            const stp::SnapResult r = pick.snap(mesh, camera, w, h, sx + 4 * std::cos(a), sy + 4 * std::sin(a), options);
            if (r.kind != stp::SnapKind::Endpoint || stp::distance(r.point, corner) > 1e-9) ++misses;
        }
    }
    CHECK(misses == 0);
}

namespace {

// 20 000 circulos de 72 tramos y 20 000 rectangulos: 1,52 millones de segmentos.
PlanScene hugeScene(bool finish = true) {
    PlanScene scene;
    std::mt19937 random(7);
    std::uniform_real_distribution<double> x(0, 5000), y(0, 3000), r(2, 30);
    for (int i = 0; i < 20000; ++i) {
        const stp::Vec3 c(x(random), y(random), 0);
        const double radius = r(random);
        for (int k = 0; k < 72; ++k) {
            const double a0 = 2 * stp::kPi * k / 72, a1 = 2 * stp::kPi * (k + 1) / 72;
            scene.mesh.addSegment(c + stp::Vec3(std::cos(a0), std::sin(a0), 0) * radius,
                                  c + stp::Vec3(std::cos(a1), std::sin(a1), 0) * radius, true);
        }
        stp::CircleFeature circle;
        circle.center = c;
        circle.radius = radius;
        scene.mesh.features.circles.push_back(circle);
        const stp::Vec3 o(x(random), y(random), 0);
        addRectangle(&scene.mesh, o, stp::Vec3(40, 0, 0), stp::Vec3(0, 25, 0));
    }
    if (finish) scene.finish();
    return scene;
}

}  // namespace

TEST(snap_is_fast_on_huge_drawing) {
    PlanScene scene = hugeScene(false);
    std::mt19937 random(7);
    const auto t0 = std::chrono::steady_clock::now();
    scene.finish();
    const auto t1 = std::chrono::steady_clock::now();
    std::uniform_real_distribution<double> px(0, scene.width), py(0, scene.height);
    stp::SnapOptions options;
    options.plane = &scene.plane;
    const int queries = 300;
    for (int i = 0; i < queries; ++i) {
        scene.pick.snap(scene.mesh, scene.camera, scene.width, scene.height, px(random), py(random), options);
    }
    const auto t2 = std::chrono::steady_clock::now();
    const double buildMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double queryMs = std::chrono::duration<double, std::milli>(t2 - t1).count() / queries;
    std::printf("    indice %.0f ms, consulta %.3f ms\n", buildMs, queryMs);
    CHECK(queryMs < 5.0);
    CHECK(buildMs < 3000.0);
}

TEST(snap_all_modes_is_fast_on_huge_drawing) {
    PlanScene scene = hugeScene();
    std::mt19937 random(11);
    std::uniform_real_distribution<double> px(0, scene.width), py(0, scene.height);
    const stp::Vec3 from(2500, 1500, 0);
    stp::SnapOptions options;
    options.plane = &scene.plane;
    options.modes = stp::kSnapAllModes;
    options.from = &from;
    const int queries = 300;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < queries; ++i) {
        scene.pick.snap(scene.mesh, scene.camera, scene.width, scene.height, px(random), py(random), options);
    }
    // Tambien con zoom: 20 veces mas cerca hay menos elementos pero mas grandes.
    scene.camera.orthoHeight /= 20;
    for (int i = 0; i < queries; ++i) {
        scene.pick.snap(scene.mesh, scene.camera, scene.width, scene.height, px(random), py(random), options);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double queryMs = std::chrono::duration<double, std::milli>(t1 - t0).count() / (2 * queries);
    std::printf("    todos los modos: consulta %.3f ms\n", queryMs);
    CHECK(queryMs < 5.0);
}

// --- Restricciones -------------------------------------------------------------------

namespace {
stp::PlanarInfo xyPlane() {
    stp::PlanarInfo plane;
    plane.planar = true;
    return plane;  // u = X, v = Y, normal = Z
}
}  // namespace

TEST(constraint_ortho_in_plan_is_horizontal_or_vertical) {
    const stp::PlanarInfo plane = xyPlane();
    stp::Camera camera;
    camera.fitPlanar(plane, 1.0);
    stp::SnapConstraint ortho;
    ortho.kind = stp::ConstraintKind::Ortho;
    const stp::ConstrainedPoint h = stp::applyConstraint(stp::Vec3(0, 0, 0), stp::Vec3(10, 3, 0), ortho, camera, &plane);
    CHECK(h.applied);
    CHECK_NEAR(stp::distance(h.point, stp::Vec3(10, 0, 0)), 0.0, 1e-12);
    CHECK(std::string(h.axisName) == "Horizontal");
    const stp::ConstrainedPoint v = stp::applyConstraint(stp::Vec3(1, 1, 0), stp::Vec3(3, 10, 0), ortho, camera, &plane);
    CHECK_NEAR(stp::distance(v.point, stp::Vec3(1, 10, 0)), 0.0, 1e-12);
    CHECK(std::string(v.axisName) == "Vertical");
}

TEST(constraint_ortho_in_3d_follows_screen_axis) {
    stp::Camera camera;  // isometrica: Z hacia arriba en pantalla
    stp::SnapConstraint ortho;
    ortho.kind = stp::ConstraintKind::Ortho;
    const stp::ConstrainedPoint z = stp::applyConstraint(stp::Vec3(0, 0, 0), stp::Vec3(0.2, 0.1, 5), ortho, camera, nullptr);
    CHECK(z.applied);
    CHECK(std::string(z.axisName) == "Z");
    CHECK_NEAR(stp::distance(z.point, stp::Vec3(0, 0, 5)), 0.0, 1e-12);
    const stp::ConstrainedPoint x = stp::applyConstraint(stp::Vec3(0, 0, 0), stp::Vec3(6, 0.3, 0.2), ortho, camera, nullptr);
    CHECK(std::string(x.axisName) == "X");
    CHECK_NEAR(stp::distance(x.point, stp::Vec3(6, 0, 0)), 0.0, 1e-12);
}

TEST(constraint_polar_snaps_near_multiples) {
    const stp::PlanarInfo plane = xyPlane();
    stp::Camera camera;
    camera.fitPlanar(plane, 1.0);
    stp::SnapConstraint polar;
    polar.kind = stp::ConstraintKind::Polar;
    polar.polarStepDegrees = 45;
    const stp::ConstrainedPoint p = stp::applyConstraint(stp::Vec3(0, 0, 0), stp::Vec3(10, 9.5, 0), polar, camera, &plane);
    CHECK(p.applied);
    CHECK_NEAR(p.angleDegrees, 45.0, 1e-9);
    CHECK_NEAR(p.point.x, p.point.y, 1e-9);
    CHECK_NEAR(p.point.x, (10 + 9.5) / std::sqrt(2.0) / std::sqrt(2.0), 1e-9);
    const stp::ConstrainedPoint free = stp::applyConstraint(stp::Vec3(0, 0, 0), stp::Vec3(10, 6, 0), polar, camera, &plane);
    CHECK(!free.applied);
    CHECK_NEAR(stp::distance(free.point, stp::Vec3(10, 6, 0)), 0.0, 1e-12);
    polar.polarStepDegrees = 30;
    const stp::ConstrainedPoint down = stp::applyConstraint(stp::Vec3(0, 0, 0), stp::Vec3(-10, -0.3, 0), polar, camera, &plane);
    CHECK(down.applied);
    CHECK_NEAR(down.angleDegrees, 180.0, 1e-9);
}

TEST(constraint_degenerate_inputs) {
    const stp::PlanarInfo plane = xyPlane();
    stp::Camera camera;
    camera.fitPlanar(plane, 1.0);
    stp::SnapConstraint c;
    c.kind = stp::ConstraintKind::Ortho;
    const stp::ConstrainedPoint same = stp::applyConstraint(stp::Vec3(1, 1, 0), stp::Vec3(1, 1, 0), c, camera, &plane);
    CHECK(!same.applied);
    CHECK(std::isfinite(same.point.x) && std::isfinite(same.point.y));
    c.kind = stp::ConstraintKind::Polar;
    c.polarStepDegrees = 0;  // angulo invalido: no aplica
    CHECK(!stp::applyConstraint(stp::Vec3(0, 0, 0), stp::Vec3(5, 5, 0), c, camera, &plane).applied);
    c.kind = stp::ConstraintKind::None;
    CHECK(!stp::applyConstraint(stp::Vec3(0, 0, 0), stp::Vec3(5, 1, 0), c, camera, &plane).applied);
}

TEST(distance_axis_name_for_saved_marks) {
    const stp::PlanarInfo plane = xyPlane();
    CHECK(std::string(stp::distanceAxisName(stp::Vec3(0, 0, 0), stp::Vec3(7, 0, 0), &plane, 1e-9)) == "Horizontal");
    CHECK(std::string(stp::distanceAxisName(stp::Vec3(0, 0, 0), stp::Vec3(0, 0, 7), nullptr, 1e-9)) == "Z");
    CHECK(stp::distanceAxisName(stp::Vec3(0, 0, 0), stp::Vec3(3, 4, 0), &plane, 1e-9) == nullptr);
    CHECK(stp::distanceAxisName(stp::Vec3(0, 0, 0), stp::Vec3(0, 0, 0), &plane, 1e-9) == nullptr);
}

// --- Calculos de medida ---------------------------------------------------------------

TEST(measure_distance_and_deltas) {
    const stp::DistanceResult d = stp::measureDistance(stp::Vec3(1, 2, 3), stp::Vec3(4, 6, 3), nullptr);
    CHECK_NEAR(d.total, 5.0, 1e-12);
    CHECK_NEAR(d.delta.x, 3.0, 1e-12);
    CHECK_NEAR(d.delta.y, 4.0, 1e-12);
    stp::Mesh mesh;
    addRectangle(&mesh, stp::Vec3(0, 3, 0), stp::Vec3(40, 0, 0), stp::Vec3(0, 0, 15));
    const stp::PlanarInfo plane = stp::detectPlanar(mesh);  // plano XZ visto de frente
    const stp::DistanceResult p = stp::measureDistance(stp::Vec3(0, 3, 0), stp::Vec3(40, 3, 15), &plane);
    CHECK_NEAR(p.delta.x, 40.0, 1e-9);  // a lo largo de u (X)
    CHECK_NEAR(p.delta.y, 15.0, 1e-9);  // a lo largo de v (Z)
    CHECK_NEAR(p.delta.z, 0.0, 1e-12);
}

TEST(measure_angles) {
    CHECK_NEAR(stp::angleAt(stp::Vec3(1, 0, 0), stp::Vec3(0, 0, 0), stp::Vec3(0, 5, 0)), 90.0, 1e-9);
    CHECK_NEAR(stp::angleAt(stp::Vec3(1, 0, 0), stp::Vec3(0, 0, 0), stp::Vec3(3, 3, 0)), 45.0, 1e-9);
    // Dos rectas que se cortan en (10,0): el angulo depende de donde se hizo clic.
    const double sharp = stp::angleBetweenLines(stp::Vec3(0, 0, 0), stp::Vec3(20, 0, 0), stp::Vec3(15, 0, 0),
                                                stp::Vec3(10, 0, 0), stp::Vec3(20, 10, 0), stp::Vec3(15, 5, 0));
    CHECK_NEAR(sharp, 45.0, 1e-9);
    const double obtuse = stp::angleBetweenLines(stp::Vec3(0, 0, 0), stp::Vec3(20, 0, 0), stp::Vec3(2, 0, 0),
                                                 stp::Vec3(10, 0, 0), stp::Vec3(20, 10, 0), stp::Vec3(15, 5, 0));
    CHECK_NEAR(obtuse, 135.0, 1e-9);
}

TEST(measure_radius_from_circle_and_cylinder) {
    stp::Mesh drawing;
    CHECK(loadDxfText(entities({{0, "CIRCLE"}, {10, "0"}, {20, "0"}, {40, "5"}}), &drawing));
    const stp::RadiusResult onEdge = stp::measureRadius(drawing, stp::Vec3(0, 5.01, 0), -1, 0.1);
    CHECK(onEdge.ok);
    CHECK_NEAR(onEdge.radius, 5.0, 1e-12);
    const stp::RadiusResult atCenter = stp::measureRadius(drawing, stp::Vec3(0.02, 0, 0), -1, 0.1);
    CHECK(atCenter.ok);
    const stp::RadiusResult nothing = stp::measureRadius(drawing, stp::Vec3(20, 20, 0), -1, 0.1);
    CHECK(!nothing.ok);
    CHECK(nothing.error == "No hay un circulo aqui");

    stp::Mesh part;
    CHECK(loadStepText(readText("tests/samples/placa_agujero.stp"), &part));
    int cylinderTriangle = -1;
    for (const stp::FaceFeature& f : part.features.faces) {
        if (f.kind == stp::SurfaceKind::Cylinder) cylinderTriangle = static_cast<int>(f.firstTriangle);
    }
    CHECK(cylinderTriangle >= 0);
    const std::uint32_t* tri = &part.indices[3 * cylinderTriangle];
    const stp::Vec3 inside = (part.positions[tri[0]] + part.positions[tri[1]] + part.positions[tri[2]]) * (1.0 / 3);
    // Lejos de cualquier arista: tiene que salir de la cara cilindrica.
    const stp::RadiusResult face = stp::measureRadius(part, inside, cylinderTriangle, 1e-6);
    CHECK(face.ok);
    CHECK_NEAR(face.radius, 10.0, 1e-9);

    stp::Mesh stl;
    const std::uint32_t a = stl.addVertex(stp::Vec3(0, 0, 0), stp::Vec3(0, 0, 1));
    const std::uint32_t b = stl.addVertex(stp::Vec3(1, 0, 0), stp::Vec3(0, 0, 1));
    const std::uint32_t c = stl.addVertex(stp::Vec3(0, 1, 0), stp::Vec3(0, 0, 1));
    stl.addTriangle(a, b, c);
    const stp::RadiusResult none = stp::measureRadius(stl, stp::Vec3(0.2, 0.2, 0), 0, 0.1);
    CHECK(!none.ok);
    CHECK(none.error == "Este formato no guarda circulos; usa distancia");
}

TEST(measure_area_of_plate_with_hole) {
    const std::string text = entities({{0, "LWPOLYLINE"}, {90, "4"}, {70, "1"},
                                       {10, "0"}, {20, "0"}, {10, "100"}, {20, "0"},
                                       {10, "100"}, {20, "50"}, {10, "0"}, {20, "50"},
                                       {0, "CIRCLE"}, {10, "50"}, {20, "25"}, {40, "5"}});
    stp::Mesh mesh;
    CHECK(loadDxfText(text, &mesh));
    const stp::PlanarInfo plane = stp::detectPlanar(mesh);
    const stp::AreaResult plate = stp::measureArea(mesh, stp::Vec3(10, 10, 0), -1, &plane);
    CHECK(plate.ok);
    CHECK_NEAR(plate.area, 5000.0 - 25.0 * stp::kPi, 1e-9);
    CHECK_NEAR(plate.perimeter, 300.0 + 10.0 * stp::kPi, 1e-9);
    CHECK(plate.holes.size() == 1);
    const stp::AreaResult hole = stp::measureArea(mesh, stp::Vec3(50, 25, 0), -1, &plane);
    CHECK(hole.ok);
    CHECK_NEAR(hole.area, 25.0 * stp::kPi, 1e-9);
    const stp::AreaResult outside = stp::measureArea(mesh, stp::Vec3(200, 25, 0), -1, &plane);
    CHECK(!outside.ok);
    CHECK(outside.error == "No hay un contorno cerrado aqui");
}

TEST(measure_area_of_bulged_contour_is_exact) {
    // Cuadrado 10x10 con el lado izquierdo abombado hacia afuera: 100 + media circunferencia de r=5.
    stp::Mesh mesh;
    CHECK(loadDxfText(entities({{0, "LWPOLYLINE"}, {90, "4"}, {70, "1"},
                                {10, "0"}, {20, "0"}, {10, "10"}, {20, "0"},
                                {10, "10"}, {20, "10"}, {10, "0"}, {20, "10"}, {42, "1"}}),
                      &mesh));
    CHECK(mesh.features.contours.size() == 1);
    if (mesh.features.contours.empty()) return;
    CHECK_NEAR(stp::contourArea(mesh.features.contours[0]), 100.0 + 12.5 * stp::kPi, 1e-9);
    CHECK_NEAR(stp::contourPerimeter(mesh.features.contours[0]), 30.0 + 5.0 * stp::kPi, 1e-9);
    CHECK(stp::contourContains(mesh.features.contours[0], stp::Vec3(-3, 5, 0)));
    CHECK(!stp::contourContains(mesh.features.contours[0], stp::Vec3(-6, 5, 0)));
}

TEST(measure_area_of_solid_face) {
    stp::Mesh part;
    CHECK(loadStepText(readText("tests/samples/placa_agujero.stp"), &part));
    for (const stp::FaceFeature& f : part.features.faces) {
        if (f.kind != stp::SurfaceKind::Cylinder) continue;
        const std::uint32_t* tri = &part.indices[3 * f.firstTriangle];
        const stp::Vec3 p = (part.positions[tri[0]] + part.positions[tri[1]] + part.positions[tri[2]]) * (1.0 / 3);
        const stp::AreaResult r = stp::measureArea(part, p, static_cast<int>(f.firstTriangle), nullptr);
        CHECK(r.ok);
        // El agujero de la muestra esta partido en dos medias caras cilindricas:
        // cada una mide pi*r*h y su borde son dos medias circunferencias y dos rectas.
        const double height = part.bounds.size().z;
        CHECK(std::fabs(r.area - stp::kPi * 10.0 * height) < 0.01 * r.area);
        CHECK(std::fabs(r.perimeter - (2 * stp::kPi * 10.0 + 2 * height)) < 0.01 * r.perimeter);
        CHECK(r.triangles.size() == f.triangleCount);
    }
}

TEST(measure_area_floods_coplanar_triangles_without_faces) {
    // Malla sin caras (como un STL): dos triangulos coplanares y uno doblado.
    stp::Mesh mesh;
    const stp::Vec3 n(0, 0, 1);
    const std::uint32_t a = mesh.addVertex(stp::Vec3(0, 0, 0), n), b = mesh.addVertex(stp::Vec3(10, 0, 0), n);
    const std::uint32_t c = mesh.addVertex(stp::Vec3(10, 10, 0), n), d = mesh.addVertex(stp::Vec3(0, 10, 0), n);
    const std::uint32_t e = mesh.addVertex(stp::Vec3(10, 10, 5), n);
    mesh.addTriangle(a, b, c);
    mesh.addTriangle(a, c, d);
    mesh.addTriangle(b, e, c);
    const stp::AreaResult r = stp::measureArea(mesh, stp::Vec3(2, 1, 0), 0, nullptr);
    CHECK(r.ok);
    CHECK_NEAR(r.area, 100.0, 1e-9);
    CHECK_NEAR(r.perimeter, 40.0, 1e-9);
}

TEST(pick_finds_triangle_under_saved_point) {
    stp::Mesh part;
    CHECK(loadStepText(readText("tests/samples/placa_agujero.stp"), &part));
    stp::PickIndex pick;
    pick.build(part);
    const std::uint32_t* tri = &part.indices[3 * 7];
    const stp::Vec3 p = (part.positions[tri[0]] + part.positions[tri[1]] + part.positions[tri[2]]) * (1.0 / 3);
    const int found = pick.triangleAt(part, p, 1e-6);
    CHECK(found >= 0);
    if (found >= 0) {
        const std::uint32_t* f = &part.indices[3 * found];
        const stp::Vec3 n = stp::cross(part.positions[f[1]] - part.positions[f[0]], part.positions[f[2]] - part.positions[f[0]]);
        CHECK(std::fabs(stp::dot(p - part.positions[f[0]], stp::normalize(n))) < 1e-6);
    }
    CHECK(pick.triangleAt(part, part.bounds.hi + stp::Vec3(50, 50, 50), 1e-6) == -1);
}

TEST(measure_radius_anchor_lies_on_circle) {
    // El clic cae sobre la cuerda del circulo muestreado, no sobre el circulo:
    // el punto que se guarda tiene que quedar sobre el circulo exacto.
    stp::Mesh drawing;
    CHECK(loadDxfText(entities({{0, "CIRCLE"}, {10, "3"}, {20, "4"}, {40, "5"}}), &drawing));
    const stp::Vec3 onChord(3 + 4.99, 4 + 0.05, 0);
    const stp::RadiusResult r = stp::measureRadius(drawing, onChord, -1, 0.1);
    CHECK(r.ok);
    const stp::Vec3 anchor = stp::radiusAnchor(r, onChord);
    CHECK_NEAR(stp::distance(anchor, stp::Vec3(3, 4, 0)), 5.0, 1e-12);
    const stp::RadiusResult again = stp::measureRadius(drawing, anchor, -1, 1e-9);
    CHECK(again.ok);
    CHECK_NEAR(again.radius, 5.0, 1e-12);
}

TEST(measure_formatting) {
    CHECK(stp::formatNumber(220.0, 3) == "220");
    CHECK(stp::formatNumber(12.5, 3) == "12.5");
    CHECK(stp::formatNumber(1.0 / 3.0, 3) == "0.333");
    CHECK(stp::formatNumber(-0.0001, 3) == "0");
    CHECK(stp::formatLength(220.0, stp::LengthUnit::Millimeter) == "220 mm");
    CHECK(stp::formatLength(3.25, stp::LengthUnit::Unknown) == "3.25");
    CHECK(stp::formatArea(78.5398, stp::LengthUnit::Millimeter) == "78.54 mm\xC2\xB2");
    CHECK(stp::formatAngle(45.0) == "45\xC2\xB0");
    CHECK(stp::formatAngle(33.3333) == "33.33\xC2\xB0");
}

// --- Marcas ------------------------------------------------------------------------------

namespace {

stp::MarkupDocument sampleMarkup() {
    stp::MarkupDocument doc;
    doc.modelName = "brida.dxf";
    doc.modelSize = 64385;
    doc.modelDate = "2026-09-23T12:00:00";
    stp::MarkupView view;
    view.id = doc.newId();
    view.name = "Vista 1";
    view.camera.target = stp::Vec3(1.5, -2.25, 3);
    view.camera.yaw = 0.3;
    view.camera.pitch = -0.2;
    view.camera.orthoHeight = 123.456;
    view.camera.planView = true;
    view.camera.planNormal = stp::Vec3(0, -1, 0);
    view.camera.planRight = stp::Vec3(1, 0, 0);
    view.camera.planUp = stp::Vec3(0, 0, 1);
    doc.views.push_back(view);
    const stp::MarkKind kinds[] = {stp::MarkKind::Distance, stp::MarkKind::Radius, stp::MarkKind::Angle,
                                   stp::MarkKind::Area, stp::MarkKind::Note, stp::MarkKind::Highlight,
                                   stp::MarkKind::Underline, stp::MarkKind::Pen, stp::MarkKind::Rectangle,
                                   stp::MarkKind::Ellipse, stp::MarkKind::Cloud};
    for (stp::MarkKind kind : kinds) {
        stp::Mark mark;
        mark.id = doc.newId();
        mark.kind = kind;
        mark.points = {stp::Vec3(0.1, 0.2, 0.3), stp::Vec3(1.0 / 3.0, 1e-9, -7)};
        mark.color = stp::kMarkBlue;
        mark.view = stp::isMeasurement(kind) || kind == stp::MarkKind::Note ? 0 : view.id;
        if (kind == stp::MarkKind::Note) mark.text = "Revisar este agujero";
        doc.marks.push_back(mark);
    }
    return doc;
}

}  // namespace

TEST(markup_roundtrip_is_identical) {
    const stp::MarkupDocument doc = sampleMarkup();
    const std::string text = stp::serializeMarkup(doc);
    CHECK(text.compare(0, 20, "stp-viewer-marcas 1\n") == 0);
    stp::MarkupDocument back;
    const stp::MarkupParseReport report = stp::parseMarkup(text, &back);
    CHECK(report.recognized);
    CHECK(report.badLines == 0);
    CHECK(!report.newerVersion);
    CHECK(stp::serializeMarkup(back) == text);
    CHECK(back.marks.size() == doc.marks.size());
    CHECK(back.views.size() == 1);
    CHECK(back.nextId == doc.nextId);
    if (back.views.size() == 1) {
        CHECK(back.views[0].camera.planView);
        CHECK_NEAR(back.views[0].camera.orthoHeight, 123.456, 0);
    }
    if (!back.marks.empty()) CHECK(back.marks[0].points[1].x == 1.0 / 3.0);
}

TEST(markup_roundtrip_hostile_text) {
    stp::MarkupDocument doc;
    stp::Mark note;
    note.id = doc.newId();
    note.kind = stp::MarkKind::Note;
    note.points = {stp::Vec3(), stp::Vec3(1, 1, 0)};
    note.text = "Ca\xC3\xB1" "o \"interior\" \\ ruta C:\\piezas\nsegunda linea\ttab = x;y";
    note.text += std::string(10000, 'x');
    doc.marks.push_back(note);
    stp::MarkupDocument back;
    stp::parseMarkup(stp::serializeMarkup(doc), &back);
    CHECK(back.marks.size() == 1);
    if (!back.marks.empty()) CHECK(back.marks[0].text == note.text);
    // Una linea por elemento: el salto de linea del texto va escapado.
    const std::string text = stp::serializeMarkup(doc);
    CHECK(std::count(text.begin(), text.end(), '\n') == 3);  // cabecera, modelo, nota
}

TEST(markup_tolerates_damaged_and_future_files) {
    std::string text = stp::serializeMarkup(sampleMarkup());
    text += "medida id=99 tipo=distancia puntos=1,2\n";      // punto incompleto
    text += "garabato id=100 color=#FF0000\n";                // tipo desconocido
    text += "nota id=101 puntos=0,0,0;1,1,1 texto=\"sin cerrar\n";
    stp::MarkupDocument back;
    const stp::MarkupParseReport damaged = stp::parseMarkup(text, &back);
    CHECK(damaged.recognized);
    CHECK(damaged.badLines == 3);
    CHECK(back.marks.size() == sampleMarkup().marks.size());

    std::string future = stp::serializeMarkup(sampleMarkup());
    future.replace(0, 19, "stp-viewer-marcas 7");
    future += "medida id=200 tipo=distancia vista=0 puntos=0,0,0;1,0,0 nueva_clave=1\n";
    stp::MarkupDocument later;
    const stp::MarkupParseReport newer = stp::parseMarkup(future, &later);
    CHECK(newer.newerVersion);
    CHECK(newer.badLines == 0);  // las claves nuevas se ignoran, la linea se entiende
    CHECK(later.marks.size() == sampleMarkup().marks.size() + 1);

    stp::MarkupDocument other;
    CHECK(!stp::parseMarkup("hola\nmundo\n", &other).recognized);
    CHECK(other.marks.empty());
}

TEST(markup_accepts_utf8_bom) {
    // El Bloc de notas guarda UTF-8 con BOM: el archivo sigue siendo de marcas.
    const std::string text = "\xEF\xBB\xBF" + stp::serializeMarkup(sampleMarkup());
    stp::MarkupDocument back;
    const stp::MarkupParseReport report = stp::parseMarkup(text, &back);
    CHECK(report.recognized);
    CHECK(report.badLines == 0);
    CHECK(back.marks.size() == sampleMarkup().marks.size());
}

TEST(markup_detects_changed_model) {
    const stp::MarkupDocument doc = sampleMarkup();
    CHECK(!stp::modelChanged(doc, 64385, "2026-09-23T12:00:00"));
    CHECK(stp::modelChanged(doc, 64386, "2026-09-23T12:00:00"));
    CHECK(stp::modelChanged(doc, 64385, "2026-09-24T08:00:00"));
    stp::MarkupDocument fresh;  // sin datos del modelo: nada que comparar
    CHECK(!stp::modelChanged(fresh, 1, "x"));
}

TEST(markup_views_match_within_half_percent) {
    stp::Camera a;
    a.orthoHeight = 100.0;
    stp::Camera b = a;
    CHECK(stp::sameView(a, b));
    b.orthoHeight = 100.4;
    CHECK(stp::sameView(a, b));
    b.orthoHeight = 101.0;
    CHECK(!stp::sameView(a, b));
    b = a;
    b.target = a.target + a.right() * 0.4;
    CHECK(stp::sameView(a, b));
    b.target = a.target + a.right() * 1.0;
    CHECK(!stp::sameView(a, b));
    b = a;
    b.yaw += 0.01;
    CHECK(!stp::sameView(a, b));

    const stp::MarkupDocument doc = sampleMarkup();
    const stp::Mark& stroke = doc.marks[5];  // resaltador, en la vista 1
    CHECK(stp::markVisible(stroke, doc, doc.views[0].camera));
    CHECK(!stp::markVisible(stroke, doc, a));
    CHECK(stp::markVisible(doc.marks[0], doc, a));  // las medidas se ven siempre
}

// --- PDF --------------------------------------------------------------------------------

TEST(pdf_structure_has_valid_xref) {
    stp::PdfWriter pdf;
    const std::vector<std::uint8_t> fakeJpeg = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0xFF, 0xD9};
    pdf.addPage(fakeJpeg, 4, 3, 36, 48, 770, 500, {{36, 570, 14, "Vista general", true}});
    pdf.addPage({}, 0, 0, 0, 0, 0, 0, {{36, 560, 10, "Medidas y notas", false}});
    CHECK(pdf.pageCount() == 2);
    const std::string file = pdf.finish();
    CHECK(file.compare(0, 8, "%PDF-1.4") == 0);
    CHECK(file.find("/Count 2") != std::string::npos);
    CHECK(file.find("/Filter /DCTDecode") != std::string::npos);

    const std::size_t startxref = file.rfind("startxref");
    CHECK(startxref != std::string::npos);
    if (startxref == std::string::npos) return;
    const std::size_t xref = std::strtoul(file.c_str() + startxref + 10, nullptr, 10);
    CHECK(file.compare(xref, 4, "xref") == 0);
    // "xref\n0 N\n" y una entrada de 20 bytes por objeto.
    const std::size_t countAt = file.find('\n', xref) + 1;
    const int objects = std::atoi(file.c_str() + countAt + 2);
    CHECK(objects >= 8);  // catalogo, paginas, 2 fuentes, 2 paginas, 2 contenidos, 1 imagen
    const std::size_t entries = file.find('\n', countAt) + 1;
    for (int k = 1; k < objects; ++k) {
        const std::size_t offset = std::strtoul(file.c_str() + entries + 20 * k, nullptr, 10);
        CHECK(file.compare(offset, std::to_string(k).size() + 6, std::to_string(k) + " 0 obj") == 0);
    }
}

TEST(pdf_text_is_winansi_and_escaped) {
    CHECK(stp::utf8ToWinAnsi("\xC3\x98 \xC3\xB1 \xE2\x82\xAC \xE2\x80\x94 \xE4\xB8\xAD") == "\xD8 \xF1 \x80 \x97 ?");
    stp::PdfWriter pdf;
    pdf.addPage({}, 0, 0, 0, 0, 0, 0, {{10, 10, 10, "a(b)c\\", false}});
    CHECK(pdf.finish().find("(a\\(b\\)c\\\\) Tj") != std::string::npos);
}

// --- Interfaz (logica pura) ------------------------------------------------------------

namespace {
bool sizesAre(const std::vector<stp::ui::GroupSize>& got, std::initializer_list<stp::ui::GroupSize> want) {
    return got == std::vector<stp::ui::GroupSize>(want);
}
}  // namespace

TEST(ribbon_layout_shrinks_right_to_left) {
    using stp::ui::GroupSize;
    const std::vector<stp::ui::RibbonGroupSpec> groups(3, stp::ui::RibbonGroupSpec{200, 120, 50});
    const GroupSize L = GroupSize::Large, S = GroupSize::Small, C = GroupSize::Collapsed;
    CHECK(sizesAre(stp::ui::layoutRibbon(groups, 700, 4, false), {L, L, L}));
    CHECK(sizesAre(stp::ui::layoutRibbon(groups, 540, 4, false), {L, L, S}));
    CHECK(sizesAre(stp::ui::layoutRibbon(groups, 400, 4, false), {S, S, S}));
    CHECK(sizesAre(stp::ui::layoutRibbon(groups, 300, 4, false), {S, S, C}));
    CHECK(sizesAre(stp::ui::layoutRibbon(groups, 100, 4, false), {C, C, C}));
    CHECK(sizesAre(stp::ui::layoutRibbon(groups, 5000, 4, true), {S, S, S}));
    CHECK(stp::ui::ribbonWidth(groups, {L, L, S}, 4) == 528);
}

TEST(ribbon_layout_never_overflows_when_possible) {
    std::mt19937 random(3);
    std::uniform_int_distribution<int> large(80, 260), count(1, 7), width(100, 1600);
    for (int round = 0; round < 500; ++round) {
        std::vector<stp::ui::RibbonGroupSpec> groups(static_cast<std::size_t>(count(random)));
        for (auto& g : groups) {
            g.large = large(random);
            g.small = g.large * 2 / 3;
            g.collapsed = 48;
        }
        const int available = width(random);
        const auto sizes = stp::ui::layoutRibbon(groups, available, 6, false);
        CHECK(sizes.size() == groups.size());
        const std::vector<stp::ui::GroupSize> tiny(groups.size(), stp::ui::GroupSize::Collapsed);
        if (stp::ui::ribbonWidth(groups, tiny, 6) <= available) {
            CHECK(stp::ui::ribbonWidth(groups, sizes, 6) <= available);
        }
    }
}

TEST(view_cube_region_under_cursor) {
    stp::Camera front;
    front.yaw = -stp::kPi / 2;
    front.pitch = 0.0;
    CHECK((stp::ui::cubeRegionAt(front, 0, 0, 50) == stp::ui::CubeRegion{0, -1, 0}));
    CHECK((stp::ui::cubeRegionAt(front, 45, 0, 50) == stp::ui::CubeRegion{1, -1, 0}));
    CHECK((stp::ui::cubeRegionAt(front, 45, -45, 50) == stp::ui::CubeRegion{1, -1, 1}));
    CHECK((stp::ui::cubeRegionAt(front, 0, 45, 50) == stp::ui::CubeRegion{0, -1, -1}));
    CHECK(!stp::ui::cubeRegionAt(front, 60, 0, 50).valid());
    stp::Camera iso;  // isometrica: se ve la esquina (+X, -Y, +Z) en el centro
    CHECK((stp::ui::cubeRegionAt(iso, 0, 0, 50) == stp::ui::CubeRegion{1, -1, 1}));
    CHECK(std::string(stp::ui::cubeFaceName(0, 0, 1)) == "Superior");
    CHECK(std::string(stp::ui::cubeFaceName(0, -1, 0)) == "Frente");
    CHECK(stp::ui::cubeFaceName(1, -1, 0) == nullptr);
}

TEST(view_cube_orientation_per_region) {
    double yaw = 0, pitch = 0;
    stp::ui::cubeOrientation({0, -1, 0}, &yaw, &pitch);
    CHECK_NEAR(yaw, -stp::kPi / 2, 1e-12);
    CHECK_NEAR(pitch, 0.0, 1e-12);
    stp::ui::cubeOrientation({0, 0, 1}, &yaw, &pitch);
    CHECK_NEAR(pitch, 1.5533430, 1e-7);  // limite de la orbita, como la tecla 5
    CHECK_NEAR(yaw, -stp::kPi / 2, 1e-12);
    stp::ui::cubeOrientation({1, -1, 1}, &yaw, &pitch);
    CHECK_NEAR(yaw, -stp::kPi / 4, 1e-12);
    CHECK_NEAR(pitch, std::asin(1 / std::sqrt(3.0)), 1e-12);
    stp::ui::cubeOrientation({-1, 0, 0}, &yaw, &pitch);
    CHECK_NEAR(std::fabs(yaw), stp::kPi, 1e-12);
    // La animacion gira por el camino corto.
    stp::ui::interpolateOrientation(3.0, 0.0, -3.0, 0.0, 0.5, &yaw, &pitch);
    CHECK_NEAR(std::fabs(yaw), stp::kPi, 1e-3);
    stp::ui::interpolateOrientation(0.0, 0.0, 1.0, 0.5, 1.0, &yaw, &pitch);
    CHECK_NEAR(yaw, 1.0, 1e-12);
    CHECK_NEAR(pitch, 0.5, 1e-12);
}

TEST(recent_files_dedupe_case_insensitive) {
    stp::ui::RecentFiles recent;
    recent.add(L"C:\\Planos\\a.dxf");
    recent.add(L"C:\\Planos\\b.stp");
    recent.add(L"c:/planos/A.DXF");
    CHECK(recent.items().size() == 2);
    CHECK(recent.items()[0] == L"c:/planos/A.DXF");
    CHECK(recent.items()[1] == L"C:\\Planos\\b.stp");
    for (int i = 0; i < 15; ++i) recent.add(L"C:\\x\\" + std::to_wstring(i) + L".stp");
    CHECK(recent.items().size() == stp::ui::RecentFiles::kLimit);
    CHECK(recent.items()[0] == L"C:\\x\\14.stp");
    CHECK(recent.remove(L"c:\\X\\14.STP"));
    CHECK(!recent.remove(L"C:\\nada.stp"));
    CHECK(recent.items()[0] == L"C:\\x\\13.stp");
    recent.setItems({L"a", L"A", L"", L"b"});
    CHECK(recent.items().size() == 2);
    CHECK(stp::ui::samePath(L"C:\\A\\b.dxf", L"c:/a/B.DXF"));
}

int main() {
    int failedTests = 0;
    for (const TestCase& test : registry()) {
        const int before = g_failures;
        test.run();
        const bool ok = g_failures == before;
        if (!ok) ++failedTests;
        std::printf("%s  %s\n", ok ? "ok   " : "FALLA", test.name);
    }
    std::printf("\n%zu pruebas, %d fallidas\n", registry().size(), failedTests);
    return failedTests == 0 ? 0 : 1;
}
