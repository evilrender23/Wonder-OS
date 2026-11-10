#include "search.h"

#include <algorithm>

namespace Search {

QString fold(const QString &text)
{
    QString out(text.size(), Qt::Uninitialized);
    for (qsizetype i = 0; i < text.size(); ++i) {
        QChar c = text.at(i);
        if (c.unicode() > 127 && !c.isSurrogate()) {
            // «á» is «a» and a mark: keep the letter.
            while (c.decompositionTag() == QChar::Canonical) {
                const QString parts = c.decomposition();
                if (parts.isEmpty()) {
                    break;
                }
                c = parts.at(0);
            }
            if (c.isMark()) {
                // A loose combining mark matches nothing.
                c = QChar(0x1f);
            }
        }
        const QChar lower = c.toLower();
        out[i] = lower;
    }
    return out;
}

QStringList terms(const QString &query)
{
    return fold(query).split(QChar::Space, Qt::SkipEmptyParts);
}

bool matches(const QString &text, const QString &query)
{
    const QString folded = fold(text);
    const QStringList words = terms(query);
    if (words.isEmpty()) {
        return false;
    }
    for (const QString &word : words) {
        if (!folded.contains(word)) {
            return false;
        }
    }
    return true;
}

QList<Range> find(const QString &text, const QString &query)
{
    const QString folded = fold(text);
    QList<Range> found;
    const QStringList words = terms(query);
    for (const QString &word : words) {
        for (qsizetype at = folded.indexOf(word); at >= 0; at = folded.indexOf(word, at + word.size())) {
            found << Range{int(at), int(word.size())};
        }
    }
    std::sort(found.begin(), found.end(), [](const Range &a, const Range &b) {
        return a.start < b.start || (a.start == b.start && a.length > b.length);
    });
    QList<Range> merged;
    for (const Range &range : std::as_const(found)) {
        if (!merged.isEmpty() && range.start <= merged.last().start + merged.last().length) {
            merged.last().length = qMax(merged.last().length, range.start + range.length - merged.last().start);
        } else {
            merged << range;
        }
    }
    return merged;
}

QString marked(const QString &text, const QString &query)
{
    QString out;
    int at = 0;
    const QList<Range> ranges = find(text, query);
    for (const Range &range : ranges) {
        out += text.mid(at, range.start - at).toHtmlEscaped();
        out += QStringLiteral("<b>") + text.mid(range.start, range.length).toHtmlEscaped() + QStringLiteral("</b>");
        at = range.start + range.length;
    }
    out += text.mid(at).toHtmlEscaped();
    return out;
}

QString snippet(const QString &text, const QString &query, int width)
{
    QString flat = text;
    flat.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\t'), QLatin1Char(' '));
    const QList<Range> ranges = find(flat, query);
    int from = 0;
    if (!ranges.isEmpty() && ranges.first().start > width / 3) {
        // Start a few words before the match, at the beginning of a word.
        from = ranges.first().start - width / 4;
        const int space = flat.indexOf(QLatin1Char(' '), from);
        if (space >= 0 && space < ranges.first().start) {
            from = space + 1;
        }
    }
    QString cut = flat.mid(from, width);
    if (from + width < flat.size()) {
        cut += QStringLiteral("…");
    }
    return (from > 0 ? QStringLiteral("…") : QString()) + marked(cut, query);
}

}
