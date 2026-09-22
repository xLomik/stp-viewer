// Polygon triangulation in 2D parameter space (outer loop + holes).
#pragma once

#include <array>
#include <vector>

#include "geom.h"

namespace stp {

// loops[0] is the outer boundary, the rest are holes. Returns the triangle list
// as indices into *vertices*. Loops may be re-oriented internally, so
// sourceIndex maps every output vertex back to its position in the
// concatenation of the input loops; callers that keep parallel per-point data
// (the original 3D positions) must go through it.
// Returns false when no triangle could be produced.
bool triangulatePolygon(const std::vector<std::vector<Vec2>>& loops,
                        std::vector<Vec2>* vertices,
                        std::vector<std::array<int, 3>>* triangles,
                        std::vector<int>* sourceIndex = nullptr);

// Bridges the holes into a single closed polygon, counter-clockwise.
bool mergeLoops(const std::vector<std::vector<Vec2>>& loops, std::vector<Vec2>* polygon,
                std::vector<int>* sourceIndex = nullptr);

// Tessellates the region enclosed by an already merged polygon on a regular
// grid of uStep x vStep cells. Curved parametric surfaces need this: plain ear
// clipping would join points that are far apart in parameter space, which maps
// back to triangles cutting straight through the surface.
bool triangulateGrid(const std::vector<Vec2>& polygon, double uStep, double vStep,
                     std::vector<Vec2>* vertices, std::vector<std::array<int, 3>>* triangles);

double signedArea(const std::vector<Vec2>& loop);

}  // namespace stp
