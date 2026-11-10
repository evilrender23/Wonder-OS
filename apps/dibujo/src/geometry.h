#pragma once

#include "model.h"

#include <QLineF>
#include <QPolygonF>
#include <QVariantList>

/// The outlines of things, where lines run, and what snaps to what.
namespace Geometry
{
/// The forms of the library: {form, name, section, sides, radius}.
QVariantList forms();
/// The Spanish name a new object of this sort gets («Rectángulo»).
QString baseName(const Shape &shape);

/// The outline of an object in its own box (0, 0, w, h).
QPainterPath outline(const Shape &shape);
/// The same on the canvas, turned.
QPainterPath worldOutline(const Shape &shape);

/// Where a line runs, from p1 to p2, by its route.
QPainterPath linePath(const Shape &line);
/// The head at one end of a line: the path, and whether it is filled.
QPainterPath arrowHead(const Shape &line, bool atEnd, bool *filled);
/// Moves the ends of every stuck line to the objects they are stuck to, and
/// lets go of the ones whose object is gone. True if anything moved.
bool updateConnectors(Document &doc);

/// Whether a point on the canvas touches the object; `slack` widens thin things.
bool hit(const Shape &shape, const QPointF &point, double slack);

/// A hand-drawn stroke made smooth: fewer points, joined by curves.
QPainterPath smooth(const QList<QPointF> &points, double tolerance);
QList<QPointF> simplify(const QList<QPointF> &points, double tolerance);

struct Guide {
    QLineF line;
    /// A distance to write beside it, when it marks equal gaps.
    QString label;
    bool operator==(const Guide &other) const = default;
};
struct Snap {
    QPointF delta;
    QList<Guide> guides;
};
/// How far to push a moving box so that its edges or centre line up with
/// the others, or so that the gaps between neighbours come out equal.
Snap snapMove(const QRectF &moving, const QList<QRectF> &others, double tolerance);
/// The same for one point (an end of a line, a corner being dragged).
Snap snapPoint(const QPointF &point, const QList<QRectF> &others, double tolerance);

/// Sizes of the frames on offer: {key, name, w, h}.
QVariantList framePresets();
}
