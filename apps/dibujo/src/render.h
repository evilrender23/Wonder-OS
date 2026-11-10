#pragma once

#include "model.h"

#include <QFont>
#include <QImage>
#include <QPainter>

/// Paints a drawing with QPainter: on the screen, into images and into PDF.
namespace Render
{
struct Options {
    /// Frames get their name above them and a hairline around (the canvas);
    /// off for what is exported.
    bool frameChrome = false;
    /// Screen pixels per canvas unit: keeps the frame names a steady size.
    double zoom = 1;
    QColor chromeColor = QColor(0, 0, 0, 120);
    /// An object whose words are being typed elsewhere, so not painted here.
    QString editing;
    /// Soft shadows cost a blur each; thumbnails go without.
    bool shadows = true;
};

/// One laid-out line of the words of an object, in its own box.
struct TextLine {
    QString text;
    /// Where its baseline starts.
    QPointF origin;
    double width = 0;
};

QFont font(const Style &style);
/// The space inside an object for its words.
QRectF textRect(const Shape &shape);
QList<TextLine> layout(const Shape &shape);
/// The size its words want; `width` > 0 wraps them.
QSizeF textSize(const Shape &shape, double width);
QBrush fillBrush(const Shape &shape, const QRectF &box);
QPen strokePen(const Style &style);
QImage image(const Document &doc, const QString &resource);

void paintShape(QPainter *painter, const Shape &shape, const Document &doc, const Options &options);
/// Everything that touches `visible` (all of it when that is null).
void paint(QPainter *painter, const Document &doc, const QRectF &visible, const Options &options);
/// An area of the canvas as a picture; a transparent background shows through.
QImage picture(const Document &doc, const QRectF &area, double scale, const QColor &background, const Options &options = {});
/// The drawing fitted into a thumbnail.
/// With `pages`, only what its frames hold, cut at their edges, when it has any.
QImage thumbnail(const Document &doc, const QSize &size, const QColor &background, bool pages = false);
}
