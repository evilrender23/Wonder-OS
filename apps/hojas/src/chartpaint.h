#pragma once

#include "workbook.h"

#include <QRectF>

class QPainter;

/// Draws a chart: on the sheet, and on the pages of a PDF.
namespace ChartPaint
{
/// The colours of the series, in order.
QStringList palette();
void paint(QPainter *painter, const QRectF &rect, const Chart &chart, const ChartData &data, bool dark);
}
