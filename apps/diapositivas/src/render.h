#pragma once

#include "model.h"

#include <QImage>
#include <QPainterPath>

class QPainter;

/// Paints slides: on the canvas, in the thumbnails, when presenting and into PNG and PDF.
namespace Render {

/// The room between the edge of an item and its text, in points.
constexpr double insetX = 8;
constexpr double insetY = 4;

struct Options {
    /// The editor shows what a presentation does not: prompts and empty placeholders.
    bool editing = false;
    /// The item whose text is being typed on the canvas: its text is left out.
    QString skipText;
    /// How many of the slide's builds have come in; -1 for all of them.
    int step = -1;
    /// Only this item, on nothing: the layer that an animation moves.
    QString only;
    /// Another theme than the deck's own, to try one on before taking it.
    QString theme;
};

/// Paints a slide in its own coordinates: the caller scales the painter.
void paint(QPainter &painter, const Model::Presentation &deck, int index, const Options &options = {});
/// The outline of a shape in a box of its size at the origin.
QPainterPath outline(const Model::Item &item);
/// The height the text of an item takes at its width, insets included.
double textHeight(const Model::Item &item, const Model::Look &look);
QImage image(const Model::Presentation &deck, int index, double scale, const Options &options = {});
bool savePng(const Model::Presentation &deck, int index, const QString &path, double scale = 2);
/// Every slide that is not hidden, one per page.
bool savePdf(const Model::Presentation &deck, const QString &path);

}
