#include "csv.h"

#include "numfmt.h"
#include "workbook.h"

#include <QStringDecoder>

#include <cmath>

QList<QStringList> Csv::parse(const QString &text, QChar separator)
{
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    bool any = false;
    const qsizetype n = text.size();
    for (qsizetype i = 0; i < n; ++i) {
        const QChar ch = text.at(i);
        if (quoted) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < n && text.at(i + 1) == QLatin1Char('"')) {
                    field += ch;
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += ch;
            }
            continue;
        }
        if (ch == QLatin1Char('"') && field.isEmpty()) {
            quoted = true;
            any = true;
        } else if (ch == separator) {
            row.append(field);
            field.clear();
            any = true;
        } else if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r')) {
            if (ch == QLatin1Char('\r') && i + 1 < n && text.at(i + 1) == QLatin1Char('\n')) {
                ++i;
            }
            row.append(field);
            rows.append(row);
            row.clear();
            field.clear();
            any = false;
        } else {
            field += ch;
            any = true;
        }
    }
    if (any || !field.isEmpty() || !row.isEmpty()) {
        row.append(field);
        rows.append(row);
    }
    return rows;
}

QChar Csv::detectSeparator(const QString &text)
{
    // Whichever shows most outside quotes in the first lines, and evenly.
    const QList<QChar> candidates = {QLatin1Char('\t'), QLatin1Char(';'), QLatin1Char(','), QLatin1Char('|')};
    QChar best = QLatin1Char(',');
    double bestScore = 0;
    for (const QChar candidate : candidates) {
        QList<int> counts;
        int count = 0;
        bool quoted = false;
        for (qsizetype i = 0; i < text.size() && counts.size() < 20; ++i) {
            const QChar ch = text.at(i);
            if (ch == QLatin1Char('"')) {
                quoted = !quoted;
            } else if (!quoted && ch == candidate) {
                ++count;
            } else if (!quoted && ch == QLatin1Char('\n')) {
                counts.append(count);
                count = 0;
            }
        }
        if (count > 0 || counts.isEmpty()) {
            counts.append(count);
        }
        int total = 0, same = 0;
        for (int c : std::as_const(counts)) {
            total += c;
            same += c == counts.first() && c > 0 ? 1 : 0;
        }
        const double score = total == 0 ? 0 : double(total) / counts.size() + 4.0 * same / counts.size();
        if (score > bestScore) {
            bestScore = score;
            best = candidate;
        }
    }
    return best;
}

QString Csv::decode(const QByteArray &bytes, QString *encoding)
{
    const auto named = [encoding](const char *name) {
        if (encoding) {
            *encoding = QString::fromLatin1(name);
        }
    };
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        named("UTF-8");
        return QString::fromUtf8(bytes.mid(3));
    }
    if (bytes.startsWith("\xFF\xFE") || bytes.startsWith("\xFE\xFF")) {
        named("UTF-16");
        QStringDecoder decoder(QStringConverter::Utf16);
        return decoder(bytes);
    }
    QStringDecoder utf8(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    const QString text = utf8(bytes);
    if (!utf8.hasError()) {
        named("UTF-8");
        return text;
    }
    QStringDecoder windows("windows-1252");
    if (windows.isValid()) {
        named("windows-1252");
        return windows(bytes);
    }
    named("ISO-8859-1");
    return QString::fromLatin1(bytes);
}

QString Csv::write(const QList<QStringList> &rows, QChar separator)
{
    QString out;
    for (const QStringList &row : rows) {
        for (int i = 0; i < row.size(); ++i) {
            QString field = row.at(i);
            if (field.contains(separator) || field.contains(QLatin1Char('"')) || field.contains(QLatin1Char('\n')) || field.contains(QLatin1Char('\r'))
                || field.startsWith(QLatin1Char(' ')) || field.endsWith(QLatin1Char(' '))) {
                field.replace(QLatin1Char('"'), QLatin1String("\"\""));
                field = QLatin1Char('"') + field + QLatin1Char('"');
            }
            if (i > 0) {
                out += separator;
            }
            out += field;
        }
        out += QLatin1Char('\n');
    }
    return out;
}

void Csv::load(Workbook &book, int sheet, const QByteArray &bytes, QChar separator)
{
    const QString text = decode(bytes);
    if (separator.isNull()) {
        separator = detectSeparator(text);
    }
    const QList<QStringList> rows = parse(text, separator);
    const bool english = separator == QLatin1Char(',');
    for (int r = 0; r < rows.size() && r < Limits::Rows; ++r) {
        const QStringList &row = rows.at(r);
        for (int c = 0; c < row.size() && c < Limits::Cols; ++c) {
            const QString &field = row.at(c);
            if (field.isEmpty()) {
                continue;
            }
            if (field.startsWith(QLatin1Char('=')) && field.size() > 1) {
                // A formula written out: taken if it reads as one.
                if (const NodePtr parsed = Formula::parse(field.mid(1))) {
                    book.loadCell(sheet, r, c, Value(), Formula::print(parsed), 0);
                    continue;
                }
            }
            if (english) {
                bool ok = false;
                const double number = field.trimmed().toDouble(&ok);
                if (ok && std::isfinite(number)) {
                    book.loadCell(sheet, r, c, Value(number), QString(), 0);
                    continue;
                }
            }
            QString code;
            const Value value = NumFmt::parseInput(field, &code);
            Style style;
            style.format = code;
            book.loadCell(sheet, r, c, value, QString(), code.isEmpty() ? 0 : book.styleId(style));
        }
    }
}

QByteArray Csv::save(Workbook &book, int sheet, QChar separator)
{
    int usedRows = 0, usedCols = 0;
    book.used(sheet, &usedRows, &usedCols);
    const bool english = separator == QLatin1Char(',');
    QList<QStringList> rows;
    for (int r = 0; r < usedRows; ++r) {
        QStringList row;
        int last = -1;
        for (int c = 0; c < usedCols; ++c) {
            const Value v = book.value(sheet, r, c);
            QString field;
            if (!v.isEmpty()) {
                const QString code = book.cellStyle(sheet, r, c).format;
                if (v.isNumber() && english && !NumFmt::isDate(code)) {
                    // With commas between fields the decimal mark has to be a dot.
                    field = QString::number(v.num, 'g', 15);
                } else if (v.isNumber() && !NumFmt::isDate(code)) {
                    field = NumFmt::editText(v, QString());
                } else {
                    field = book.display(sheet, r, c);
                }
                last = c;
            }
            row.append(field);
        }
        rows.append(row.mid(0, last + 1));
    }
    return write(rows, separator).toUtf8();
}
