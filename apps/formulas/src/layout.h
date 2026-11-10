#pragma once

#include "latex.h"

#include <QColor>
#include <QImage>
#include <QList>
#include <QPainterPath>
#include <QSizeF>

class QPainter;

/// One thing to fill: a glyph, a rule, a piece of a stretched delimiter.
struct Item {
    QPainterPath path;
    /// Invalid: the colour of the formula.
    QColor color;
    /// Where in the source it comes from.
    int start = 0;
    int end = 0;
    /// A hole to fill in, drawn faint.
    bool ghost = false;
};

/// A laid out piece of formula, TeX's box: the baseline is y = 0 and y grows downwards.
struct Box {
    qreal width = 0;
    qreal height = 0;
    qreal depth = 0;
    /// How far the top of the last glyph leans past its advance.
    qreal italic = 0;
    /// Where an accent goes over it, or negative for the middle.
    qreal accent = -1;
    /// It is one glyph, which scripts hug closer than a built box.
    bool single = false;
    QList<Item> items;

    void shift(qreal dx, qreal dy);
    /// Adds the items of another box, moved; the metrics are the caller's business.
    void add(const Box &other, qreal dx = 0, qreal dy = 0);
    bool isEmpty() const { return items.isEmpty(); }
};

struct LayoutOptions {
    /// The math font family; empty for the best one installed.
    QString family;
    /// The size of the text, in pixels to the em.
    qreal size = 32;
    /// False sets it as TeX does inside a paragraph.
    bool display = true;
};

/// A formula ready to draw.
struct Layout {
    Box box;
    QList<ParseError> errors;
    NodePtr root;
    QString family;
};

namespace Formula {

Layout layout(const QString &latex, const LayoutOptions &options = {});
Box layout(const NodePtr &root, const LayoutOptions &options = {});

/// The size of the picture: the box and `padding` around it.
QSizeF size(const Box &box, qreal padding = 0);
/// Draws with the baseline's left end at `origin`.
void paint(QPainter *painter, const Box &box, const QPointF &origin, const QColor &color);
/// A picture of it, `scale` pixels to the unit, on `background` (transparent by default).
QImage image(const Box &box, const QColor &color, qreal scale = 1, qreal padding = 0, const QColor &background = Qt::transparent);
/// The same as an SVG document, all outlines: it needs no font where it is opened.
QByteArray svg(const Box &box, const QColor &color, qreal padding = 0);

/// The position in the source of what is drawn at `point` (relative to the baseline's left end), or -1.
int sourceAt(const Box &box, const QPointF &point);
/// The rectangle of what the source position belongs to; empty when nothing is drawn for it.
QRectF rectOf(const Box &box, int position);

}
