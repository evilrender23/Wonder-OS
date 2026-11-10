#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QLineF>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>

/// The presentation as plain values: a deck of slides, each a stack of items.
/// Lengths are in points (a 16:9 slide is 960 × 540), angles in degrees.
namespace Model {

/// A stretch of text in one format. What is left unset comes from the role of
/// the item and the theme of the deck.
struct Run {
    QString text;
    /// -1 as the theme has it, 0 not bold, 1 bold.
    int bold = -1;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    /// Points; 0 as the theme has it.
    double size = 0;
    /// "#rrggbb" or a theme colour ("@accent1"); empty as the theme has it.
    QString color;
    /// Empty as the theme has it.
    QString font;

    bool sameFormat(const Run &other) const;
    bool operator==(const Run &other) const = default;
    QJsonObject toJson() const;
    static Run fromJson(const QJsonObject &object);
};

struct Paragraph {
    QList<Run> runs;
    /// -1 as the role has it, 0 left, 1 centre, 2 right, 3 justified.
    int align = -1;
    bool bullet = false;
    int level = 0;

    QString plain() const;
    bool operator==(const Paragraph &other) const = default;
    QJsonObject toJson() const;
    static Paragraph fromJson(const QJsonObject &object);
};

struct Item {
    QString id;
    /// text, shape, image or table.
    QString type;
    /// Of a shape: rect, rounded, ellipse, triangle, diamond, arrow, star or line.
    QString shape;
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
    double rotation = 0;
    bool flipH = false;
    bool flipV = false;
    /// A colour, a theme colour, or empty for none.
    QString fill;
    QString stroke;
    double strokeWidth = 0;
    bool shadow = false;
    double opacity = 1;
    /// A line that ends in an arrow head.
    bool arrow = false;
    QList<Paragraph> text;
    /// 0 top, 1 middle, 2 bottom.
    int valign = 0;
    /// What the item is on its layout: title, ctitle, subtitle, body, caption,
    /// picture, or empty for something the user put there.
    QString role;
    /// The key of the picture in the deck's media.
    QString image;
    int rows = 0;
    int cols = 0;
    QStringList cells;
    /// Items with the same group move and are selected as one.
    QString group;
    /// How it comes in when presenting: empty (it is there), appear or slide.
    QString anim;

    QRectF rect() const { return QRectF(x, y, w, h); }
    void setRect(const QRectF &r);
    QPointF center() const { return QPointF(x + w / 2, y + h / 2); }
    /// The upright box around the item as rotated.
    QRectF bounds() const;
    /// Whether a point of the slide is on the item, `slack` points around it.
    bool hit(const QPointF &point, double slack = 0) const;
    /// The two ends of a line.
    QLineF line() const;
    bool hasText() const;
    QString plainText() const;
    void setPlainText(const QString &text);
    QString cell(int row, int col) const { return cells.value(row * cols + col); }

    bool operator==(const Item &other) const = default;
    QJsonObject toJson() const;
    static Item fromJson(const QJsonObject &object);
};

struct Slide {
    QString id;
    /// title, content, columns, titleOnly, blank or picture.
    QString layout = QStringLiteral("blank");
    QList<Item> items;
    QString notes;
    bool hidden = false;
    /// A colour of its own; empty for the deck's background.
    QString background;
    /// How it arrives when presenting: empty, dissolve, push or fade.
    QString transition;

    int indexOf(const QString &itemId) const;
    /// The items that come in one by one, in the order they do.
    QStringList builds() const;
    bool operator==(const Slide &other) const = default;
    QJsonObject toJson() const;
    static Slide fromJson(const QJsonObject &object);
};

struct Theme {
    QString id;
    QString name;
    /// The background runs from `bg1` at the top to `bg2` at the bottom.
    QString bg1;
    QString bg2;
    QString title;
    QString text;
    QStringList accents;
    QString titleFont;
    QString bodyFont;
    bool titleBold = true;
};

/// The slide master: what the deck changes of its theme, and what every slide carries.
struct Master {
    QString background;
    QString titleFont;
    QString bodyFont;
    QString titleColor;
    QString textColor;
    QString footer;
    bool numbers = false;

    bool operator==(const Master &other) const = default;
    QJsonObject toJson() const;
    static Master fromJson(const QJsonObject &object);
};

/// The theme with the master's changes applied: what the painter works from.
struct Look {
    QColor bg1;
    QColor bg2;
    QColor title;
    QColor text;
    QList<QColor> accents;
    QString titleFont;
    QString bodyFont;
    bool titleBold = true;

    /// "#rrggbb", "@accent1" … "@accent6", "@text", "@title", "@bg"; invalid for empty.
    QColor color(const QString &token) const;
};

/// What a run falls back on.
struct TextDefaults {
    QString font;
    double size = 20;
    QColor color;
    int align = 0;
    bool bold = false;
    /// Points after each paragraph.
    double spacing = 0;
};

struct Presentation {
    double width = 960;
    double height = 540;
    QString theme = QStringLiteral("blanco");
    Master master;
    QList<Slide> slides;
    QHash<QString, QByteArray> media;

    Look look(const QString &themeOverride = QString()) const;
    QSizeF size() const { return QSizeF(width, height); }
    /// Keeps the bytes of a picture and gives the key to them.
    QString addMedia(const QByteArray &bytes, const QString &suffix);
    bool operator==(const Presentation &other) const = default;
};

const QList<Theme> &themes();
const Theme &theme(const QString &id);

/// The ids of the layouts, in the order of the menu.
QStringList layouts();
QString layoutName(const QString &layout);
/// The placeholders of a layout on a slide of this size, with fresh ids.
QList<Item> layoutItems(const QString &layout, const QSizeF &size);
/// Moves a slide to another layout, keeping what its placeholders hold.
void applyLayout(Slide &slide, const QString &layout, const QSizeF &size);
Slide newSlide(const QString &layout, const QSizeF &size);

TextDefaults textDefaults(const Item &item, const Look &look);
/// What an empty placeholder says in the editor.
QString prompt(const Item &item);
QString newId();

/// The upright box around several items.
QRectF bounds(const QList<Item> &items);

struct Snap {
    double dx = 0;
    double dy = 0;
    /// The lines to show: where an edge or a centre meets another.
    QList<QLineF> guides;
};
/// What to add to a moving box so that it lines up with the slide or its neighbours.
Snap snap(const QRectF &moving, const QList<QRectF> &others, const QSizeF &slide, double threshold);

/// A box dragged by a handle (nw, n, ne, e, se, s, sw, w) by `delta` points of
/// the slide; the box stays put on the side opposite the handle, rotated or not.
QRectF resized(const QRectF &rect, double rotation, const QString &handle, const QPointF &delta, bool keepAspect, double minimum = 6);

}
