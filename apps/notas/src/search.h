#pragma once

#include <QList>
#include <QString>
#include <QStringList>

/// Finding text without minding case or accents.
namespace Search {

struct Range {
    int start = 0;
    int length = 0;
    bool operator==(const Range &other) const = default;
};

/// Lower case and without accents, one character out for each one in: «Canción» → «cancion».
QString fold(const QString &text);
/// The words of a query, folded.
QStringList terms(const QString &query);
/// Every word of the query is somewhere in the text.
bool matches(const QString &text, const QString &query);
/// Where the words of the query are in the text, in order and without overlaps.
QList<Range> find(const QString &text, const QString &query);
/// A stretch of the text around the first match, as HTML with the matches in <b>.
QString snippet(const QString &text, const QString &query, int width = 120);
/// The whole text as HTML with the matches in <b>.
QString marked(const QString &text, const QString &query);

}
