#pragma once

#include "layout.hpp"

#include <QFont>
#include <QPainter>
#include <QPainterPath>

namespace s2a::native {

/// Font of block texts (also used to measure them for the layout).
[[nodiscard]] QFont blockFont();

/// Measure function for LayoutContext, based on blockFont().
[[nodiscard]] Measure fontMeasure();

/// Outline of a block, Scratch style (notches, hat bump, C-block mouths, round reporters,
/// hexagonal conditions). Coordinates are canvas coordinates.
[[nodiscard]] QPainterPath blockPath(const BlockGeometry& geometry);

/// Paints one block (its own shape, texts and fields; reporters it holds are separate geometries).
void paintBlock(QPainter& painter, const BlockGeometry& geometry, qreal opacity = 1.0);

} // namespace s2a::native
