#include "pdf_writer.h"

#include <cstdio>

namespace stp {
namespace {

std::string fixed(double v) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", v);
    return buffer;
}

std::string escaped(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '(' || c == ')' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

// Codigos 0x80-0x9F de Windows-1252 y su caracter Unicode.
const unsigned kHigh[32] = {0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                            0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
                            0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                            0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};

}  // namespace

std::string utf8ToWinAnsi(const std::string& utf8) {
    std::string out;
    std::size_t i = 0;
    while (i < utf8.size()) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        unsigned cp = c;
        int extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : -1;
        if (extra < 0 || i + extra >= utf8.size() + (extra == 0)) {
            out.push_back('?');
            ++i;
            continue;
        }
        if (extra > 0) {
            cp = c & (0x3F >> extra);
            for (int k = 1; k <= extra; ++k) cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
        }
        i += extra + 1;
        if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF)) {
            out.push_back(static_cast<char>(cp));
            continue;
        }
        char mapped = '?';
        for (unsigned k = 0; k < 32; ++k) {
            if (kHigh[k] && kHigh[k] == cp) mapped = static_cast<char>(0x80 + k);
        }
        out.push_back(mapped);
    }
    return out;
}

PdfWriter::PdfWriter(double width, double height) : m_width(width), m_height(height) {}

void PdfWriter::addPage(const std::vector<std::uint8_t>& jpeg, int pixelWidth, int pixelHeight, double x, double y,
                        double w, double h, const std::vector<PdfText>& texts) {
    Page page;
    page.jpeg = jpeg;
    page.pixelWidth = pixelWidth;
    page.pixelHeight = pixelHeight;
    page.x = x;
    page.y = y;
    page.w = w;
    page.h = h;
    page.texts = texts;
    m_pages.push_back(std::move(page));
}

std::string PdfWriter::finish() const {
    // Numeracion: 1 catalogo, 2 paginas, 3 y 4 fuentes, luego por pagina: pagina, contenido [, imagen].
    std::vector<int> pageIds, contentIds, imageIds;
    int next = 5;
    for (const Page& page : m_pages) {
        pageIds.push_back(next++);
        contentIds.push_back(next++);
        imageIds.push_back(page.jpeg.empty() ? 0 : next++);
    }

    std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets(static_cast<std::size_t>(next), 0);
    auto begin = [&](int id) {
        offsets[static_cast<std::size_t>(id)] = out.size();
        out += std::to_string(id) + " 0 obj\n";
    };

    begin(1);
    out += "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
    begin(2);
    out += "<< /Type /Pages /Kids [";
    for (const int id : pageIds) out += " " + std::to_string(id) + " 0 R";
    out += " ] /Count " + std::to_string(m_pages.size()) + " >>\nendobj\n";
    begin(3);
    out += "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>\nendobj\n";
    begin(4);
    out += "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>\nendobj\n";

    for (std::size_t i = 0; i < m_pages.size(); ++i) {
        const Page& page = m_pages[i];
        begin(pageIds[i]);
        out += "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + fixed(m_width) + " " + fixed(m_height) +
               "] /Resources << /Font << /F1 3 0 R /F2 4 0 R >>";
        if (imageIds[i]) out += " /XObject << /Im0 " + std::to_string(imageIds[i]) + " 0 R >>";
        out += " >> /Contents " + std::to_string(contentIds[i]) + " 0 R >>\nendobj\n";

        std::string content;
        if (imageIds[i]) {
            content += "q " + fixed(page.w) + " 0 0 " + fixed(page.h) + " " + fixed(page.x) + " " + fixed(page.y) +
                       " cm /Im0 Do Q\n";
        }
        for (const PdfText& text : page.texts) {
            content += "BT /" + std::string(text.bold ? "F2 " : "F1 ") + fixed(text.size) + " Tf " + fixed(text.x) +
                       " " + fixed(text.y) + " Td (" + escaped(utf8ToWinAnsi(text.text)) + ") Tj ET\n";
        }
        begin(contentIds[i]);
        out += "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream\nendobj\n";

        if (imageIds[i]) {
            begin(imageIds[i]);
            out += "<< /Type /XObject /Subtype /Image /Width " + std::to_string(page.pixelWidth) + " /Height " +
                   std::to_string(page.pixelHeight) +
                   " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length " +
                   std::to_string(page.jpeg.size()) + " >>\nstream\n";
            out.append(reinterpret_cast<const char*>(page.jpeg.data()), page.jpeg.size());
            out += "\nendstream\nendobj\n";
        }
    }

    const std::size_t xref = out.size();
    out += "xref\n0 " + std::to_string(next) + "\n0000000000 65535 f \n";
    for (int id = 1; id < next; ++id) {
        char line[32];
        std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offsets[static_cast<std::size_t>(id)]);
        out += line;
    }
    out += "trailer\n<< /Size " + std::to_string(next) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) +
           "\n%%EOF\n";
    return out;
}

}  // namespace stp
