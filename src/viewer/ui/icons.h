// Iconos vectoriales propios: se dibujan a cualquier DPI con un mismo trazo.
#pragma once

#include <windows.h>
#include <gdiplus.h>

#include "../../engine/measure.h"

namespace stp {
namespace ui {

enum class Icon {
    None,
    Open, Recent, Save, ExportPdf, ExportPng, About,
    Navigate, Fit, Views, Plan2d, View3d,
    Distance, Radius, Angle, Area,
    SnapEndpoint, SnapMidpoint, SnapCenter, SnapQuadrant, SnapIntersection, SnapExtension,
    SnapPerpendicular, SnapTangent, SnapNearest, Snap, Ortho, Polar,
    Highlight, Underline, Note, Rectangle, Ellipse, Cloud, Pen, Color, Undo, Redo, Delete,
    Shaded, Wireframe, Edges, Perspective, Panel, StatusBar, ViewCube,
    Home, Minimize, Maximize, Restore, Close, ChevronDown, ChevronRight, Marks, Properties, File, Folder, Check,
};

// Dibuja el icono dentro de box (cuadrado): grilla de 16 unidades, trazo 1,5.
void drawIcon(Gdiplus::Graphics& g, Icon icon, const Gdiplus::RectF& box, Gdiplus::ARGB color);
Icon iconForSnap(SnapKind kind);
SnapKind snapKindForIcon(Icon icon);  // None si no es de enganche

}  // namespace ui
}  // namespace stp
