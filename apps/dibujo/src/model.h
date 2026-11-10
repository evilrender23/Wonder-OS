#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTransform>

/// How an object is painted and how its words are set.
struct Style {
    /// none, solid, linear or radial.
    QString fillKind = QStringLiteral("solid");
    QColor fill = QColor(QStringLiteral("#ffffff"));
    /// The far end of a gradient.
    QColor fill2 = QColor(QStringLiteral("#40c8e0"));
    double gradientAngle = 90;
    QColor stroke = QColor(QStringLiteral("#1d1d1f"));
    double strokeWidth = 2;
    /// solid, dash, dot or dashdot.
    QString dash = QStringLiteral("solid");
    /// none, arrow, triangle, circle, diamond or bar.
    QString startArrow = QStringLiteral("none");
    QString endArrow = QStringLiteral("none");
    double radius = 0;
    bool shadow = false;
    double opacity = 1;
    QString fontFamily;
    double fontSize = 16;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    QColor textColor = QColor(QStringLiteral("#1d1d1f"));
    /// left, center or right; top, middle or bottom.
    QString hAlign = QStringLiteral("center");
    QString vAlign = QStringLiteral("middle");

    QJsonObject toJson() const;
    static Style fromJson(const QJsonObject &json);
    bool operator==(const Style &other) const = default;
};

/// One thing on the canvas.
struct Shape {
    QString id;
    /// shape, line, path, text, image, note or frame.
    QString kind = QStringLiteral("shape");
    /// For a shape: rect, ellipse, diamond, triangle, polygon, star… (see Geometry::forms).
    QString form = QStringLiteral("rect");
    int sides = 5;
    QString name;
    /// The box, before turning; a turn is around its centre, in degrees.
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
    double rotation = 0;
    QString text;
    bool locked = false;
    bool hidden = false;
    /// Objects with the same group move and are picked as one.
    QString group;
    Style style;

    /// A line: its two ends, and how it gets from one to the other
    /// (straight, elbow or curve).
    QPointF p1;
    QPointF p2;
    QString route = QStringLiteral("straight");
    /// A connector end stuck to an object follows it.
    QString from;
    QString to;
    /// Worked out with the ends, not saved: an elbow or a curve leaves sideways.
    bool horizontal = true;

    /// A path: drawn in `ref`, stretched to the box.
    QPainterPath path;
    QRectF ref;
    /// A text box as wide as its words (false: the words wrap to the box).
    bool autoSize = false;
    /// An image: the name of a resource of the document.
    QString image;

    bool isLine() const { return kind == QLatin1String("line"); }
    bool isFrame() const { return kind == QLatin1String("frame"); }
    QRectF box() const { return QRectF(x, y, w, h); }
    QPointF center() const { return QPointF(x + w / 2, y + h / 2); }
    /// Local (box) coordinates to the canvas.
    QTransform transform() const;
    /// The box that holds the turned object.
    QRectF bounds() const;

    QJsonObject toJson() const;
    static Shape fromJson(const QJsonObject &json);
    bool operator==(const Shape &other) const;
};

/// A drawing: objects from the bottom to the top, on one endless canvas.
class Document
{
public:
    /// Never changes: what the versions and the recovery copy are filed under.
    QString uid;
    QString title;
    QList<Shape> shapes;
    QStringList palette;
    /// Images, by resource name.
    QMap<QString, QByteArray> resources;
    bool grid = false;
    bool snap = true;

    int indexOf(const QString &id) const;
    const Shape *find(const QString &id) const;
    Shape *find(const QString &id);
    bool contains(const QString &id) const { return indexOf(id) >= 0; }
    QString freshId();
    /// «Rectángulo 3»: the next free name of its sort.
    QString freshName(const QString &base) const;
    QList<const Shape *> frames() const;
    /// Around everything that shows; empty when nothing does.
    QRectF bounds() const;
    /// Stores an image and returns the name it goes by.
    QString addResource(const QByteArray &bytes, const QString &suffix);
    /// Forgets the images no object shows.
    void pruneResources();

    QJsonObject toJson() const;
    static Document fromJson(const QJsonObject &json);

private:
    int m_serial = 0;
};

namespace Model
{
QString colorName(const QColor &color);
QColor color(const QString &name, const QColor &fallback = QColor());
/// A path as the `d` of SVG, and back (all of the SVG path grammar).
QString pathToD(const QPainterPath &path, int decimals = 2);
QPainterPath pathFromD(const QString &d);
QString number(double value, int decimals = 2);
}
