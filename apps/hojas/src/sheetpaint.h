#pragma once

#include "workbook.h"

#include <QColor>
#include <QRectF>
#include <functional>

class QPainter;

/// Draws the cells of a sheet: on the window, and on the pages of a PDF.
namespace SheetPaint
{
struct Look {
    QColor paper = QColor(0xff, 0xff, 0xff);
    QColor ink = QColor(0x1d, 0x1d, 0x1f);
    QColor line = QColor(0xe8, 0xe8, 0xea);
    QColor tint = QColor(0x1f, 0x9d, 0x55);
    QColor error = QColor(0xd7, 0x00, 0x15);
    QColor dim = QColor(0x6e, 0x6e, 0x73);
    bool dark = false;
    /// The lines between cells; a PDF goes without.
    bool grid = true;
    /// What is there to be clicked, like the buttons of a filter: not on paper.
    bool controls = true;
    double zoom = 1.0;
};

/// The fill a table gives a cell: 1 its headers, 2 every other row, 3 its totals.
int band(const Workbook &book, int sheet, int row, int col);

/// Paints rows r1..r2 and columns c1..c2. `colPos` and `rowPos` give where a
/// column or a row starts, in the painter's units (the one after the last
/// included, so that a size is a difference); hidden ones take no room.
void paint(QPainter *painter, Workbook &book, int sheet, int r1, int c1, int r2, int c2, const std::function<double(int)> &colPos,
           const std::function<double(int)> &rowPos, const Look &look);
}
