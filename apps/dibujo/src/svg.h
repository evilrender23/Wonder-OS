#pragma once

#include "model.h"

/// SVG in and out.
namespace Svg
{
/// An area of the drawing as an SVG document. Frames are painted as plain
/// pages; text stays text; images travel inside as data URIs.
QByteArray write(const Document &doc, const QRectF &area);

/// Reads rect, circle, ellipse, line, polyline, polygon, path, text, image,
/// g and use, with their transforms, presentation attributes, `style`,
/// simple class rules and gradients (taken as two colours). Filters, masks,
/// clips, patterns and markers are ignored. `baseDir` is where linked images
/// are looked for.
bool read(const QByteArray &bytes, QList<Shape> *shapes, QMap<QString, QByteArray> *resources, QString *error = nullptr,
          const QString &baseDir = {});
}
