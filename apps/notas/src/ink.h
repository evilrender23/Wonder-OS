#pragma once

#include <QByteArray>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>

class QPainter;

/// One line drawn without lifting the pen.
struct Stroke {
    /// "pen" or "marker" (wide and see-through).
    QString tool = QStringLiteral("pen");
    QString color = QStringLiteral("#1d1d1f");
    qreal width = 2.5;
    QList<QPointF> points;

    bool operator==(const Stroke &other) const = default;
    /// A curve through the points: quadratic segments between their midpoints.
    QPainterPath path() const;
    QString pathData() const;
    QRectF bounds() const;
    /// Whether the line passes within `radius` of the point.
    bool near(const QPointF &point, qreal radius) const;
};

/// A drawing: strokes kept as vectors, saved as an SVG that any program shows
/// and that this one reads back stroke by stroke.
struct Drawing {
    int width = 720;
    int height = 320;
    QList<Stroke> strokes;

    bool operator==(const Drawing &other) const = default;
    bool isEmpty() const { return strokes.isEmpty(); }

    /// What the hand drew, with the jitter taken out: points too close are
    /// dropped, the rest eased towards their neighbours, and those that add
    /// nothing to the shape removed.
    static QList<QPointF> smooth(const QList<QPointF> &raw);
    /// Takes away every stroke that passes near the point; says how many.
    int eraseAt(const QPointF &point, qreal radius);
    /// Shrinks the sheet to what is drawn, with a margin around.
    void crop(int margin = 12);
    void paint(QPainter *painter) const;

    QByteArray toSvg() const;
    static Drawing fromSvg(const QByteArray &svg, bool *ok = nullptr);
};
