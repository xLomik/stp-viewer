// Escritor de PDF minimo: paginas con una imagen JPEG y lineas de texto en
// Helvetica. Suficiente para exportar una revision; sin dependencias.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace stp {

struct PdfText {
    double x = 0.0, y = 0.0;  // puntos, origen abajo a la izquierda
    double size = 10.0;
    std::string text;         // UTF-8; lo que no entra en WinAnsi sale como '?'
    bool bold = false;
};

class PdfWriter {
public:
    explicit PdfWriter(double width = 842.0, double height = 595.0);  // A4 apaisado

    // jpeg vacio: pagina solo con texto.
    void addPage(const std::vector<std::uint8_t>& jpeg, int pixelWidth, int pixelHeight, double x, double y,
                 double w, double h, const std::vector<PdfText>& texts);
    std::size_t pageCount() const { return m_pages.size(); }
    std::string finish() const;

private:
    struct Page {
        std::vector<std::uint8_t> jpeg;
        int pixelWidth = 0, pixelHeight = 0;
        double x = 0, y = 0, w = 0, h = 0;
        std::vector<PdfText> texts;
    };
    double m_width, m_height;
    std::vector<Page> m_pages;
};

std::string utf8ToWinAnsi(const std::string& utf8);

}  // namespace stp
