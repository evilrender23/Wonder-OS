#pragma once

#include "model.h"

/// A drawing out to files other programs read.
namespace Exporter
{
/// What goes out: the frame with this id, or everything with a margin.
QRectF area(const Document &doc, const QString &frameId);
bool png(const Document &doc, const QRectF &area, double scale, bool transparent, const QString &path, QString *error = nullptr);
bool svg(const Document &doc, const QRectF &area, const QString &path, QString *error = nullptr);
/// One page per area, each the size of its area (96 units to the inch), as vectors.
bool pdf(const Document &doc, const QList<QRectF> &areas, const QString &path, QString *error = nullptr);
}
