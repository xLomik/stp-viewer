#include "commands.h"

namespace stp {
namespace ui {
namespace {

// Herramientas: kCmdTool + indice de Tool (Navigate 0, Distance 1 ... Pen 11).
const CommandInfo kCommands[] = {
    {kCmdOpen, Icon::Open, L"Abrir", L"Ctrl+O", L"Abre un modelo o un plano (STEP, IGES, DXF, STL, OBJ...)."},
    {kCmdRecent, Icon::Recent, L"Recientes", nullptr, L"Los \u00faltimos archivos abiertos."},
    {kCmdSaveMarks, Icon::Save, L"Guardar marcas", L"Ctrl+S", L"Guarda medidas y marcas junto al archivo (.marcas)."},
    {kCmdExportPdf, Icon::ExportPdf, L"Exportar PDF", L"Ctrl+E", L"Una p\u00e1gina por vista guardada, con sus marcas."},
    {kCmdExportPng, Icon::ExportPng, L"Exportar PNG", nullptr, L"Guarda la vista actual como imagen."},
    {kCmdAbout, Icon::About, L"Acerca de", nullptr, L"Versi\u00f3n y formatos admitidos."},

    {kCmdNavigate, Icon::Navigate, L"Navegar", L"Esc", L"Girar, mover y hacer zoom; clic para elegir una marca."},
    {kCmdFit, Icon::Fit, L"Encuadrar", L"F", L"Ajusta la vista para ver todo el modelo."},
    {kCmdViews, Icon::Views, L"Vistas", nullptr, L"Frente, atr\u00e1s, lados, superior, inferior e isom\u00e9trica."},
    {kCmdPlan2d, Icon::Plan2d, L"Plano 2D", L"D", L"Mira el plano de frente y navega como en un CAD."},
    {kCmdViewFront, Icon::Views, L"Frente", L"1", L"Vista frontal."},
    {kCmdViewBack, Icon::Views, L"Atr\u00e1s", L"2", L"Vista posterior."},
    {kCmdViewLeft, Icon::Views, L"Izquierda", L"3", L"Vista lateral izquierda."},
    {kCmdViewRight, Icon::Views, L"Derecha", L"4", L"Vista lateral derecha."},
    {kCmdViewTop, Icon::Views, L"Superior", L"5", L"Vista desde arriba."},
    {kCmdViewBottom, Icon::Views, L"Inferior", L"6", L"Vista desde abajo."},
    {kCmdViewIso, Icon::View3d, L"Isom\u00e9trica", L"7", L"Vista isom\u00e9trica en 3D."},

    {kCmdTool + 1, Icon::Distance, L"Distancia", L"M", L"Dos puntos. Con Orto (F8) mide solo en horizontal o vertical."},
    {kCmdTool + 2, Icon::Radius, L"Radio", nullptr, L"Clic sobre un c\u00edrculo, un arco o un agujero."},
    {kCmdTool + 3, Icon::Angle, L"\u00c1ngulo", nullptr, L"Tres puntos con el v\u00e9rtice al medio, o dos l\u00edneas."},
    {kCmdTool + 4, Icon::Area, L"\u00c1rea", nullptr, L"Clic dentro de un contorno cerrado o sobre una cara."},
    {kCmdTool + 5, Icon::Highlight, L"Resaltador", L"H", L"Trazo ancho y transl\u00facido para destacar."},
    {kCmdTool + 6, Icon::Underline, L"Subrayado", L"U", L"L\u00ednea recta; con Shift u Orto, horizontal o vertical."},
    {kCmdTool + 7, Icon::Note, L"Nota", L"N", L"Texto con flecha hacia un punto del modelo."},
    {kCmdTool + 8, Icon::Rectangle, L"Rect\u00e1ngulo", L"R", L"Recuadro arrastrando de esquina a esquina."},
    {kCmdTool + 9, Icon::Ellipse, L"Elipse", L"E", L"Elipse arrastrando de esquina a esquina."},
    {kCmdTool + 10, Icon::Cloud, L"Nube", L"C", L"Nube de revisi\u00f3n alrededor de una zona."},
    {kCmdTool + 11, Icon::Pen, L"L\u00e1piz", L"L", L"Trazo libre."},

    {kCmdSnapToggle, Icon::Snap, L"OSNAP", L"F3", L"Enciende o apaga el enganche a objetos. Shift lo apaga mientras se mantiene."},
    {kCmdSnapMode + 0, Icon::SnapEndpoint, L"Extremo", nullptr, L"Extremos de rectas y de arcos."},
    {kCmdSnapMode + 1, Icon::SnapMidpoint, L"Punto medio", nullptr, L"Mitad de rectas y de arcos."},
    {kCmdSnapMode + 2, Icon::SnapCenter, L"Centro", nullptr, L"Centro de c\u00edrculos y arcos."},
    {kCmdSnapMode + 3, Icon::SnapQuadrant, L"Cuadrante", nullptr, L"0\u00b0, 90\u00b0, 180\u00b0 y 270\u00b0 de c\u00edrculos y arcos: largo de ranuras."},
    {kCmdSnapMode + 4, Icon::SnapIntersection, L"Intersecci\u00f3n", nullptr, L"Cruce de dos rectas, recta y arco o dos arcos."},
    {kCmdSnapMode + 5, Icon::SnapExtension, L"Extensi\u00f3n", nullptr, L"V\u00e9rtice de una esquina redondeada o achaflanada."},
    {kCmdSnapMode + 6, Icon::SnapPerpendicular, L"Perpendicular", nullptr, L"Pie de la perpendicular desde el primer punto: espesores."},
    {kCmdSnapMode + 7, Icon::SnapTangent, L"Tangente", nullptr, L"Punto de tangencia desde el primer punto."},
    {kCmdSnapMode + 8, Icon::SnapNearest, L"M\u00e1s cercano", nullptr, L"Cualquier punto de una arista."},
    {kCmdOrtho, Icon::Ortho, L"Orto", L"F8", L"Fija la medida a horizontal o vertical (X, Y o Z en 3D)."},
    {kCmdPolar, Icon::Polar, L"Polar", L"F10", L"Fija la direcci\u00f3n a m\u00faltiplos del \u00e1ngulo elegido."},
    {kCmdPolarMenu, Icon::None, L"\u00c1ngulo polar", nullptr, L"Paso del ajuste polar."},
    {kCmdPolarStep + 15, Icon::Polar, L"15\u00b0", nullptr, L"Paso polar de 15 grados."},
    {kCmdPolarStep + 30, Icon::Polar, L"30\u00b0", nullptr, L"Paso polar de 30 grados."},
    {kCmdPolarStep + 45, Icon::Polar, L"45\u00b0", nullptr, L"Paso polar de 45 grados."},
    {kCmdPolarStep + 90, Icon::Polar, L"90\u00b0", nullptr, L"Paso polar de 90 grados."},

    {kCmdColor, Icon::Color, L"Color", L"Q", L"Color de la pr\u00f3xima marca."},
    {kCmdColorMenu, Icon::Color, L"Color", nullptr, L"Elegir el color de las marcas."},
    {kCmdUndo, Icon::Undo, L"Deshacer", L"Ctrl+Z", L"Deshace el \u00faltimo cambio de marcas."},
    {kCmdRedo, Icon::Redo, L"Rehacer", L"Ctrl+Y", L"Rehace el cambio deshecho."},
    {kCmdDelete, Icon::Delete, L"Borrar", L"Supr", L"Borra la marca seleccionada."},

    {kCmdShaded, Icon::Shaded, L"Sombreado", nullptr, L"Caras sombreadas."},
    {kCmdWireframe, Icon::Wireframe, L"Alambre", L"W", L"Solo aristas, sin caras."},
    {kCmdEdges, Icon::Edges, L"Aristas", L"A", L"Muestra u oculta las aristas."},
    {kCmdPerspective, Icon::Perspective, L"Perspectiva", L"P", L"Alterna entre perspectiva y ortogr\u00e1fica."},
    {kCmdPanel, Icon::Panel, L"Panel", L"F2", L"Panel de marcas y propiedades."},
    {kCmdStatusBar, Icon::StatusBar, L"Barra de estado", nullptr, L"Coordenadas, OSNAP, ORTO, POLAR y zoom."},
    {kCmdViewCube, Icon::ViewCube, L"Cubo de vistas", nullptr, L"Cubo para girar a las vistas est\u00e1ndar."},
    {kCmdRibbonToggle, Icon::ChevronDown, L"Plegar cinta", nullptr, L"Doble clic en una pesta\u00f1a tambi\u00e9n la pliega."},
};

}  // namespace

const CommandInfo* commandInfo(int id) {
    for (const CommandInfo& info : kCommands) {
        if (info.id == id) return &info;
    }
    return nullptr;
}

}  // namespace ui
}  // namespace stp
