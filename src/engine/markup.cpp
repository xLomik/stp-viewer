#include "markup.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace stp {
namespace {

const char* kHeader = "stp-viewer-marcas";
constexpr int kVersion = 1;

struct KindName {
    MarkKind kind;
    const char* line;  // medida, nota, trazo, forma
    const char* type;  // valor de tipo=
};

const KindName kKinds[] = {
    {MarkKind::Distance, "medida", "distancia"},  {MarkKind::Radius, "medida", "radio"},
    {MarkKind::Angle, "medida", "angulo"},        {MarkKind::Area, "medida", "area"},
    {MarkKind::Note, "nota", ""},                 {MarkKind::Highlight, "trazo", "resaltador"},
    {MarkKind::Underline, "trazo", "subrayado"},  {MarkKind::Pen, "trazo", "lapiz"},
    {MarkKind::Rectangle, "forma", "rectangulo"}, {MarkKind::Ellipse, "forma", "elipse"},
    {MarkKind::Cloud, "forma", "nube"},
};

std::string number(double v) {
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.17g", v);
    return buffer;
}

std::string vec(const Vec3& v) { return number(v.x) + "," + number(v.y) + "," + number(v.z); }

std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out.push_back(c); break;
        }
    }
    return out + "\"";
}

std::string colorText(std::uint32_t argb) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%06X", static_cast<unsigned>(argb & 0xFFFFFF));
    return buffer;
}

// Divide "tipo clave=valor clave="texto con espacios"" en clave -> valor.
// Devuelve false si una comilla queda sin cerrar.
bool splitLine(const std::string& line, std::string* kind, std::map<std::string, std::string>* fields) {
    std::size_t i = line.find(' ');
    *kind = line.substr(0, i);
    while (i != std::string::npos && i < line.size()) {
        while (i < line.size() && line[i] == ' ') ++i;
        if (i >= line.size()) break;
        const std::size_t eq = line.find('=', i);
        if (eq == std::string::npos) return false;
        const std::string key = line.substr(i, eq - i);
        std::string value;
        i = eq + 1;
        if (i < line.size() && line[i] == '"') {
            ++i;
            bool closed = false;
            while (i < line.size()) {
                const char c = line[i++];
                if (c == '"') {
                    closed = true;
                    break;
                }
                if (c == '\\' && i < line.size()) {
                    const char e = line[i++];
                    value.push_back(e == 'n' ? '\n' : (e == 'r' ? '\r' : e));
                } else {
                    value.push_back(c);
                }
            }
            if (!closed) return false;
        } else {
            const std::size_t end = line.find(' ', i);
            value = line.substr(i, end == std::string::npos ? std::string::npos : end - i);
            i = end;
        }
        (*fields)[key] = value;
    }
    return true;
}

bool parseNumber(const std::string& text, double* out) {
    if (text.empty()) return false;
    char* end = nullptr;
    *out = std::strtod(text.c_str(), &end);
    return end && *end == '\0' && std::isfinite(*out);
}

bool parseVec(const std::string& text, Vec3* out) {
    const std::size_t a = text.find(',');
    const std::size_t b = a == std::string::npos ? a : text.find(',', a + 1);
    if (b == std::string::npos) return false;
    return parseNumber(text.substr(0, a), &out->x) && parseNumber(text.substr(a + 1, b - a - 1), &out->y) &&
           parseNumber(text.substr(b + 1), &out->z);
}

bool parsePoints(const std::string& text, std::vector<Vec3>* out) {
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(';', start);
        Vec3 p;
        if (!parseVec(text.substr(start, end == std::string::npos ? std::string::npos : end - start), &p)) return false;
        out->push_back(p);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return !out->empty();
}

bool parseColor(const std::string& text, std::uint32_t* out) {
    if (text.size() != 7 || text[0] != '#') return false;
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str() + 1, &end, 16);
    if (!end || *end != '\0') return false;
    *out = 0xFF000000u | static_cast<std::uint32_t>(value);
    return true;
}

}  // namespace

const MarkupView* MarkupDocument::findView(int id) const {
    for (const MarkupView& view : views) {
        if (view.id == id) return &view;
    }
    return nullptr;
}

bool isMeasurement(MarkKind kind) {
    return kind == MarkKind::Distance || kind == MarkKind::Radius || kind == MarkKind::Angle ||
           kind == MarkKind::Area;
}

std::string serializeMarkup(const MarkupDocument& doc) {
    std::string out = std::string(kHeader) + " " + std::to_string(kVersion) + "\n";
    out += "modelo nombre=" + quoted(doc.modelName) + " tamano=" + std::to_string(doc.modelSize) +
           " fecha=" + quoted(doc.modelDate) + "\n";
    for (const MarkupView& v : doc.views) {
        const Camera& c = v.camera;
        out += "vista id=" + std::to_string(v.id) + " nombre=" + quoted(v.name) + " objetivo=" + vec(c.target) +
               " distancia=" + number(c.distance) + " yaw=" + number(c.yaw) + " pitch=" + number(c.pitch) +
               " fov=" + number(c.fov) + " alto_orto=" + number(c.orthoHeight) + " orto=" + (c.ortho ? "1" : "0") +
               " plano=" + (c.planView ? "1" : "0") + " normal=" + vec(c.planNormal) + " derecha=" +
               vec(c.planRight) + " arriba=" + vec(c.planUp) + "\n";
    }
    for (const Mark& m : doc.marks) {
        const KindName* name = nullptr;
        for (const KindName& k : kKinds) {
            if (k.kind == m.kind) name = &k;
        }
        if (!name) continue;
        out += std::string(name->line) + " id=" + std::to_string(m.id);
        if (name->type[0]) out += std::string(" tipo=") + name->type;
        out += " vista=" + std::to_string(m.view) + " color=" + colorText(m.color) + " puntos=";
        for (std::size_t i = 0; i < m.points.size(); ++i) out += (i ? ";" : "") + vec(m.points[i]);
        if (m.kind == MarkKind::Note) out += " texto=" + quoted(m.text);
        out += "\n";
    }
    return out;
}

MarkupParseReport parseMarkup(const std::string& text, MarkupDocument* doc) {
    MarkupParseReport report;
    *doc = MarkupDocument();
    // BOM de UTF-8 (lo agrega el Bloc de notas): no es parte de la cabecera.
    std::size_t start = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    bool first = true;
    int maxId = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (first) {
            first = false;
            const std::string prefix = std::string(kHeader) + " ";
            if (line.compare(0, prefix.size(), prefix) != 0) return report;
            report.recognized = true;
            report.newerVersion = std::atoi(line.c_str() + prefix.size()) > kVersion;
            continue;
        }
        if (line.empty()) continue;

        std::string kind;
        std::map<std::string, std::string> f;
        if (!splitLine(line, &kind, &f)) {
            ++report.badLines;
            continue;
        }
        if (kind == "modelo") {
            doc->modelName = f["nombre"];
            doc->modelSize = std::strtoull(f["tamano"].c_str(), nullptr, 10);
            doc->modelDate = f["fecha"];
            continue;
        }
        if (kind == "vista") {
            MarkupView v;
            v.id = std::atoi(f["id"].c_str());
            v.name = f["nombre"];
            Camera& c = v.camera;
            const bool ok = v.id > 0 && parseVec(f["objetivo"], &c.target) && parseNumber(f["distancia"], &c.distance) &&
                            parseNumber(f["yaw"], &c.yaw) && parseNumber(f["pitch"], &c.pitch) &&
                            parseNumber(f["fov"], &c.fov) && parseNumber(f["alto_orto"], &c.orthoHeight) &&
                            parseVec(f["normal"], &c.planNormal) && parseVec(f["derecha"], &c.planRight) &&
                            parseVec(f["arriba"], &c.planUp);
            if (!ok) {
                ++report.badLines;
                continue;
            }
            c.ortho = f["orto"] == "1";
            c.planView = f["plano"] == "1";
            maxId = std::max(maxId, v.id);
            doc->views.push_back(v);
            continue;
        }
        const KindName* name = nullptr;
        for (const KindName& k : kKinds) {
            if (kind == k.line && f["tipo"] == k.type) name = &k;
        }
        Mark m;
        if (!name || !parsePoints(f["puntos"], &m.points)) {
            ++report.badLines;
            continue;
        }
        m.kind = name->kind;
        m.id = std::atoi(f["id"].c_str());
        m.view = std::atoi(f["vista"].c_str());
        if (f.count("color") && !parseColor(f["color"], &m.color)) {
            ++report.badLines;
            continue;
        }
        m.text = f["texto"];
        maxId = std::max(maxId, m.id);
        doc->marks.push_back(m);
    }
    doc->nextId = maxId + 1;
    return report;
}

bool sameView(const Camera& a, const Camera& b) {
    if (a.planView != b.planView || a.ortho != b.ortho) return false;
    if (dot(a.forward(), b.forward()) < 1.0 - 1e-6 || dot(a.up(), b.up()) < 1.0 - 1e-6) return false;
    const double height = std::max(1e-12, a.orthoHeight);
    if (std::fabs(b.orthoHeight / height - 1.0) > 0.005) return false;
    const Vec3 shift = b.target - a.target;
    const double sideways = std::hypot(dot(shift, a.right()), dot(shift, a.up()));
    return sideways <= 0.005 * height;
}

bool markVisible(const Mark& mark, const MarkupDocument& doc, const Camera& camera) {
    if (mark.view == 0) return true;
    const MarkupView* view = doc.findView(mark.view);
    return view && sameView(view->camera, camera);
}

bool modelChanged(const MarkupDocument& doc, std::uint64_t size, const std::string& date) {
    if (doc.modelSize == 0 && doc.modelDate.empty()) return false;
    return doc.modelSize != size || doc.modelDate != date;
}

}  // namespace stp
