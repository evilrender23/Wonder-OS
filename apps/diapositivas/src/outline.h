#pragma once

#include "model.h"

/// The deck as an outline in Markdown, and back: «# » starts the title of a
/// slide, «- » a bullet (two spaces deeper per level), «> » a line of the
/// speaker's notes, any other line a paragraph, and «---» the next slide.
namespace Outline {

struct Line {
    QString text;
    bool bullet = false;
    int level = 0;
    bool operator==(const Line &other) const = default;
};

struct Block {
    QString title;
    QList<Line> lines;
    QString notes;
    bool operator==(const Block &other) const = default;
};

QList<Block> parse(const QString &markdown);
QString write(const QList<Block> &blocks);
/// What the outline of a deck says: the title and the first text of each slide.
QList<Block> blocks(const Model::Presentation &deck);
QString fromDeck(const Model::Presentation &deck);
/// Makes the deck follow the outline: slide n takes block n, slides are added
/// for the blocks beyond the last and dropped past the last block. Text whose
/// words did not change keeps its formatting; what is not in the outline
/// (shapes, pictures, the second column) is left alone.
void apply(Model::Presentation &deck, const QString &markdown);

}
