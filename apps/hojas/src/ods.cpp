// OpenDocument spreadsheets: what LibreOffice Calc writes, read into a workbook.
#include "formula.h"
#include "numfmt.h"
#include "workbook.h"
#include "xlsx.h"
#include "zip.h"

#include <QHash>
#include <QXmlStreamReader>

#include <cmath>

namespace
{
/// How much of a run of equal cells or rows is taken when it holds something:
/// past it, a run is the padding a sheet ends with.
constexpr int RunLimit = 2000;

/// «2.258cm», «0.889in», «12pt» in pixels; 0 when it says nothing.
double pixels(QStringView length)
{
    static const struct {
        QLatin1String unit;
        double factor;
    } units[] = {{QLatin1String("cm"), 96.0 / 2.54}, {QLatin1String("mm"), 96.0 / 25.4}, {QLatin1String("in"), 96.0},
                 {QLatin1String("pt"), 96.0 / 72.0}, {QLatin1String("px"), 1.0}};
    for (const auto &u : units) {
        if (length.endsWith(u.unit)) {
            bool ok = false;
            const double n = length.chopped(u.unit.size()).toDouble(&ok);
            return ok ? n * u.factor : 0;
        }
    }
    return 0;
}

/// «#rrggbb», or nothing for «transparent» and the like.
QString colour(QStringView text)
{
    return text.size() == 7 && text.startsWith(QLatin1Char('#')) ? text.toString().toLower() : QString();
}

/// An OpenFormula («of:=SUM([.A1:.B2];[$'Otra hoja'.C3])») the way ours are
/// written: plain references, commas between arguments.
QString formulaFromOds(QString text)
{
    if (text.startsWith(QLatin1String("of:"))) {
        text.remove(0, 3);
    } else if (text.startsWith(QLatin1String("oooc:"))) {
        text.remove(0, 5);
    }
    if (!text.startsWith(QLatin1Char('='))) {
        return {};
    }
    text.remove(0, 1);
    QString out;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('"')) {
            // A string, as it is.
            const int from = i;
            for (++i; i < text.size(); ++i) {
                if (text.at(i) == QLatin1Char('"')) {
                    if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                        ++i;
                        continue;
                    }
                    break;
                }
            }
            out += QStringView(text).mid(from, i - from + 1);
        } else if (c == QLatin1Char('[')) {
            const int close = text.indexOf(QLatin1Char(']'), i);
            if (close < 0) {
                return {};
            }
            // «.A1», «.A1:.B2», «$Hoja.A1:.B2», «'Otra hoja'.A1».
            const QString inside = text.mid(i + 1, close - i - 1);
            QString ref;
            bool quoted = false;
            bool first = true;
            for (int k = 0; k < inside.size(); ++k) {
                const QChar r = inside.at(k);
                if (r == QLatin1Char('\'')) {
                    quoted = !quoted;
                    ref += r;
                } else if (quoted) {
                    ref += r;
                } else if (r == QLatin1Char('.')) {
                    // The dot that ends a sheet's name is our «!»; a bare one is nothing.
                    const bool named = !ref.isEmpty() && !ref.endsWith(QLatin1Char(':'));
                    if (named && first) {
                        if (ref.startsWith(QLatin1Char('$'))) {
                            ref.remove(0, 1);
                        }
                        ref += QLatin1Char('!');
                    } else if (named) {
                        // The far corner names its sheet again: once is enough.
                        ref.truncate(ref.lastIndexOf(QLatin1Char(':')) + 1);
                    }
                } else {
                    if (r == QLatin1Char(':')) {
                        first = false;
                    }
                    ref += r;
                }
            }
            out += ref;
            i = close;
        } else if (c == QLatin1Char(';')) {
            out += QLatin1Char(',');
        } else {
            out += c;
        }
    }
    const NodePtr node = Formula::parse(out);
    return node ? Formula::print(node) : QString();
}

/// «PT12H30M05S» as the fraction of a day.
double duration(QStringView text)
{
    double seconds = 0;
    QString digits;
    for (const QChar c : text) {
        if (c.isDigit() || c == QLatin1Char('.') || c == QLatin1Char(',')) {
            digits += c == QLatin1Char(',') ? QLatin1Char('.') : c;
        } else {
            const double n = digits.toDouble();
            digits.clear();
            if (c == QLatin1Char('H')) {
                seconds += n * 3600;
            } else if (c == QLatin1Char('M')) {
                seconds += n * 60;
            } else if (c == QLatin1Char('S')) {
                seconds += n;
            }
        }
    }
    return seconds / 86400.0;
}

struct CellLook {
    Style style;
    QString data;
};

/// The number formats and the looks of content.xml (or styles.xml).
struct Looks {
    QHash<QString, QString> formats;
    QHash<QString, CellLook> cells;
    QHash<QString, double> colWidths;
    QHash<QString, double> rowHeights;
};

void readLooks(const QByteArray &bytes, Looks *looks)
{
    QXmlStreamReader xml(bytes);
    QString name, family, dataKind, literal;
    CellLook look;
    bool inLiteral = false;
    int places = 0;
    bool grouping = false, inData = false, seconds = false;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        const auto tag = xml.name();
        if (token == QXmlStreamReader::StartElement) {
            const auto a = xml.attributes();
            const auto attr = [&a](const char *n) { return a.value(QLatin1String(n)); };
            if (tag == QLatin1String("number-style") || tag == QLatin1String("currency-style") || tag == QLatin1String("percentage-style")
                || tag == QLatin1String("date-style") || tag == QLatin1String("time-style") || tag == QLatin1String("text-style")) {
                inData = true;
                dataKind = tag.toString();
                name = attr("style:name").toString();
                places = 0;
                grouping = false;
                seconds = false;
                literal.clear();
            } else if (inData && (tag == QLatin1String("text") || tag == QLatin1String("currency-symbol"))) {
                inLiteral = true;
            } else if (inData && (tag == QLatin1String("number") || tag == QLatin1String("scientific-number"))) {
                places = attr("number:decimal-places").toInt();
                grouping = attr("number:grouping") == QLatin1String("true");
                if (tag == QLatin1String("scientific-number")) {
                    dataKind = QStringLiteral("scientific");
                }
            } else if (inData && tag == QLatin1String("seconds")) {
                seconds = true;
            } else if (tag == QLatin1String("style")) {
                name = attr("style:name").toString();
                family = attr("style:family").toString();
                look = CellLook();
                look.data = attr("style:data-style-name").toString();
                // What it does not say itself, its parent says.
                const QString parent = attr("style:parent-style-name").toString();
                if (family == QLatin1String("table-cell") && looks->cells.contains(parent)) {
                    const QString own = look.data;
                    look = looks->cells.value(parent);
                    if (!own.isEmpty()) {
                        look.data = own;
                    }
                }
            } else if (tag == QLatin1String("table-column-properties")) {
                const double w = pixels(attr("style:column-width"));
                if (w > 0) {
                    looks->colWidths.insert(name, w);
                }
            } else if (tag == QLatin1String("table-row-properties")) {
                const double h = pixels(attr("style:row-height"));
                if (h > 0 && attr("style:use-optimal-row-height") != QLatin1String("true")) {
                    looks->rowHeights.insert(name, h);
                }
            } else if (family == QLatin1String("table-cell") && tag == QLatin1String("text-properties")) {
                if (a.hasAttribute(QLatin1String("fo:font-weight"))) {
                    look.style.bold = attr("fo:font-weight") == QLatin1String("bold");
                }
                if (a.hasAttribute(QLatin1String("fo:font-style"))) {
                    look.style.italic = attr("fo:font-style") == QLatin1String("italic");
                }
                if (a.hasAttribute(QLatin1String("style:text-underline-style"))) {
                    look.style.underline = attr("style:text-underline-style") != QLatin1String("none");
                }
                if (a.hasAttribute(QLatin1String("style:text-line-through-style"))) {
                    look.style.strike = attr("style:text-line-through-style") != QLatin1String("none");
                }
                if (a.hasAttribute(QLatin1String("fo:color"))) {
                    look.style.color = colour(attr("fo:color"));
                }
                if (a.hasAttribute(QLatin1String("fo:font-size"))) {
                    const double pt = attr("fo:font-size").chopped(2).toDouble();
                    look.style.size = pt > 0 && std::lround(pt) != 10 ? int(std::lround(pt)) : 0;
                }
            } else if (family == QLatin1String("table-cell") && tag == QLatin1String("table-cell-properties")) {
                if (a.hasAttribute(QLatin1String("fo:background-color"))) {
                    look.style.fill = colour(attr("fo:background-color"));
                }
                if (a.hasAttribute(QLatin1String("fo:wrap-option"))) {
                    look.style.wrap = attr("fo:wrap-option") == QLatin1String("wrap");
                }
                const auto v = attr("style:vertical-align");
                if (v == QLatin1String("top")) {
                    look.style.valign = 2;
                } else if (v == QLatin1String("middle")) {
                    look.style.valign = 1;
                }
            } else if (family == QLatin1String("table-cell") && tag == QLatin1String("paragraph-properties")) {
                const auto h = attr("fo:text-align");
                if (h == QLatin1String("start") || h == QLatin1String("left")) {
                    look.style.align = 1;
                } else if (h == QLatin1String("center")) {
                    look.style.align = 2;
                } else if (h == QLatin1String("end") || h == QLatin1String("right")) {
                    look.style.align = 3;
                }
            }
        } else if (token == QXmlStreamReader::Characters && inLiteral) {
            literal += xml.text();
        } else if (token == QXmlStreamReader::EndElement) {
            inLiteral = false;
            if (tag == QLatin1String("number-style") || tag == QLatin1String("currency-style") || tag == QLatin1String("percentage-style")
                || tag == QLatin1String("date-style") || tag == QLatin1String("time-style") || tag == QLatin1String("text-style")) {
                QString code;
                if (tag == QLatin1String("currency-style")) {
                    code = NumFmt::familyCode(QStringLiteral("currency"), places);
                } else if (tag == QLatin1String("percentage-style")) {
                    code = NumFmt::familyCode(QStringLiteral("percent"), places);
                } else if (tag == QLatin1String("date-style")) {
                    code = seconds ? QStringLiteral("dd/mm/yyyy hh:mm") : NumFmt::familyCode(QStringLiteral("date"));
                } else if (tag == QLatin1String("time-style")) {
                    code = NumFmt::familyCode(QStringLiteral("time"));
                } else if (tag == QLatin1String("text-style")) {
                    code = NumFmt::familyCode(QStringLiteral("text"));
                } else if (literal.contains(QChar(0x20AC))) {
                    // Calc writes «0 €» as a number with the sign as text after it.
                    code = NumFmt::familyCode(QStringLiteral("currency"), places);
                } else if (literal.contains(QLatin1Char('%'))) {
                    code = NumFmt::familyCode(QStringLiteral("percent"), places);
                } else if (dataKind == QLatin1String("scientific")) {
                    code = NumFmt::familyCode(QStringLiteral("scientific"), places);
                } else if (grouping) {
                    code = NumFmt::familyCode(QStringLiteral("number"), places);
                } else if (places > 0) {
                    code = QLatin1String("0.") + QString(places, QLatin1Char('0'));
                }
                looks->formats.insert(name, code);
                inData = false;
            } else if (tag == QLatin1String("style") && family == QLatin1String("table-cell")) {
                looks->cells.insert(name, look);
                family.clear();
            } else if (tag == QLatin1String("style")) {
                family.clear();
            }
        }
    }
}
}

bool Ods::load(Workbook &book, const QByteArray &bytes, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    const ZipReader zip(bytes);
    if (!zip.isValid() || !zip.contains(QStringLiteral("content.xml"))) {
        return fail(QStringLiteral("El archivo no es una hoja de cálculo OpenDocument (.ods) o está dañado."));
    }
    if (zip.contains(QStringLiteral("META-INF/manifest.xml")) && zip.file(QStringLiteral("META-INF/manifest.xml")).contains("encryption-data")) {
        return fail(QStringLiteral("La hoja de cálculo está protegida con contraseña y no se puede abrir."));
    }
    const QByteArray content = zip.file(QStringLiteral("content.xml"));
    Looks looks;
    readLooks(zip.file(QStringLiteral("styles.xml")), &looks);
    readLooks(content, &looks);

    book.clear();
    QHash<QString, int> styleIds;
    const auto styleOf = [&](const QString &name) {
        if (name.isEmpty() || !looks.cells.contains(name)) {
            return 0;
        }
        const auto known = styleIds.constFind(name);
        if (known != styleIds.constEnd()) {
            return *known;
        }
        const CellLook &look = looks.cells[name];
        Style style = look.style;
        style.format = looks.formats.value(look.data);
        const int id = book.styleId(style);
        styleIds.insert(name, id);
        return id;
    };

    QXmlStreamReader xml(content);
    int sheets = 0;
    int index = -1, row = 0, col = 0, rowRepeat = 1, tableCol = 0;
    // The cells of the row in hand, to lay it again when it repeats.
    struct Held {
        int col;
        Value value;
        QString formula;
        int style;
    };
    QList<Held> held;
    QHash<int, int> colStyles;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        const auto tag = xml.name();
        if (token == QXmlStreamReader::StartElement) {
            const auto a = xml.attributes();
            const auto attr = [&a](const char *n) { return a.value(QLatin1String(n)); };
            if (tag == QLatin1String("table") && xml.prefix() == QLatin1String("table")) {
                index = sheets++;
                if (index > 0) {
                    book.addSheet(QStringLiteral("\u0001hoja%1").arg(index));
                }
                const QString name = attr("table:name").toString();
                book.loaderSheet(index).name = name.isEmpty() ? QStringLiteral("Hoja %1").arg(index + 1) : name;
                row = 0;
                tableCol = 0;
                colStyles.clear();
            } else if (index >= 0 && tag == QLatin1String("table-column")) {
                const int repeat = qMax(1, attr("table:number-columns-repeated").toInt());
                const QString name = attr("table:style-name").toString();
                const bool hidden = attr("table:visibility") == QLatin1String("collapse");
                if (repeat < 1024) {
                    Sheet &sheet = book.loaderSheet(index);
                    for (int c = tableCol; c < qMin(tableCol + repeat, int(Limits::Cols)); ++c) {
                        if (looks.colWidths.contains(name)) {
                            sheet.colWidths.insert(c, qBound(8.0, std::round(looks.colWidths.value(name)), 1200.0));
                        }
                        if (hidden) {
                            sheet.hiddenCols.insert(c);
                        }
                    }
                }
                tableCol += repeat;
            } else if (index >= 0 && tag == QLatin1String("table-row")) {
                rowRepeat = qMax(1, attr("table:number-rows-repeated").toInt());
                col = 0;
                held.clear();
                const QString name = attr("table:style-name").toString();
                if (rowRepeat < 1024 && row < Limits::Rows) {
                    Sheet &sheet = book.loaderSheet(index);
                    for (int r = row; r < qMin(row + rowRepeat, int(Limits::Rows)); ++r) {
                        if (looks.rowHeights.contains(name)) {
                            sheet.rowHeights.insert(r, qBound(4.0, std::round(looks.rowHeights.value(name)), 600.0));
                        }
                        if (attr("table:visibility") == QLatin1String("collapse")) {
                            sheet.hiddenRows.insert(r);
                        }
                    }
                }
            } else if (index >= 0 && (tag == QLatin1String("table-cell") || tag == QLatin1String("covered-table-cell"))) {
                const int repeat = qMax(1, attr("table:number-columns-repeated").toInt());
                const auto type = attr("office:value-type");
                const QString formula = formulaFromOds(attr("table:formula").toString());
                int style = styleOf(attr("table:style-name").toString());
                const int spanCols = attr("table:number-columns-spanned").toInt();
                const int spanRows = attr("table:number-rows-spanned").toInt();
                Value value;
                bool typed = true;
                if (type == QLatin1String("float") || type == QLatin1String("currency") || type == QLatin1String("percentage")) {
                    value = Value(attr("office:value").toDouble());
                } else if (type == QLatin1String("boolean")) {
                    value = Value::boolean(attr("office:boolean-value") == QLatin1String("true"));
                } else if (type == QLatin1String("date")) {
                    const QString text = attr("office:date-value").toString();
                    const QDateTime moment = QDateTime::fromString(text.left(19), Qt::ISODate);
                    if (text.size() > 10 && moment.isValid()) {
                        value = Value(NumFmt::serial(moment));
                    } else if (const QDate date = QDate::fromString(text.left(10), Qt::ISODate); date.isValid()) {
                        value = Value(NumFmt::serial(date));
                    }
                } else if (type == QLatin1String("time")) {
                    value = Value(duration(attr("office:time-value")));
                } else {
                    typed = false;
                }
                // A date under a look that says nothing of dates would show as a bare number.
                if ((type == QLatin1String("date") || type == QLatin1String("time")) && !NumFmt::isDate(book.style(style).format)) {
                    Style dated = book.style(style);
                    const bool clock = type == QLatin1String("time") || attr("office:date-value").size() > 10;
                    dated.format = type == QLatin1String("time") ? NumFmt::familyCode(QStringLiteral("time"))
                                   : clock ? QStringLiteral("dd/mm/yyyy hh:mm") : NumFmt::familyCode(QStringLiteral("date"));
                    style = book.styleId(dated);
                }
                // The text: its paragraphs, a line each.
                QString text;
                int paragraphs = 0;
                int depth = 1;
                while (!xml.atEnd() && depth > 0) {
                    const auto inner = xml.readNext();
                    if (inner == QXmlStreamReader::StartElement) {
                        ++depth;
                        if (xml.name() == QLatin1String("p") && depth == 2) {
                            if (paragraphs++ > 0) {
                                text += QLatin1Char('\n');
                            }
                        } else if (xml.name() == QLatin1String("s")) {
                            text += QString(qMax(1, xml.attributes().value(QLatin1String("text:c")).toInt()), QLatin1Char(' '));
                        } else if (xml.name() == QLatin1String("tab")) {
                            text += QLatin1Char('\t');
                        } else if (xml.name() == QLatin1String("line-break")) {
                            text += QLatin1Char('\n');
                        } else if (xml.name() == QLatin1String("annotation")) {
                            // A comment is not what the cell holds.
                            xml.skipCurrentElement();
                            --depth;
                        }
                    } else if (inner == QXmlStreamReader::EndElement) {
                        --depth;
                    } else if (inner == QXmlStreamReader::Characters && depth >= 2) {
                        text += xml.text();
                    }
                }
                if (!typed && !text.isEmpty()) {
                    if (type == QLatin1String("string") || formula.isEmpty()) {
                        value = Value(a.hasAttribute(QLatin1String("office:string-value")) ? attr("office:string-value").toString() : text);
                    }
                    // A formula that failed says so in its text.
                    if (!formula.isEmpty() && text.startsWith(QLatin1Char('#'))) {
                        value = Value(Value::errorFromText(text));
                    }
                }
                const bool blank = value.isEmpty() && formula.isEmpty();
                // An empty run only carries a look, and a long one is the sheet's own.
                const int count = blank ? (style != 0 && repeat <= 256 ? repeat : 0) : qMin(repeat, RunLimit);
                for (int k = 0; k < count && col + k < Limits::Cols; ++k) {
                    QString f = formula;
                    if (k > 0 && !formula.isEmpty()) {
                        f = Formula::print(Formula::shifted(Formula::parse(formula), 0, k));
                    }
                    held.append({col + k, value, f, style});
                }
                if ((spanCols > 1 || spanRows > 1) && row < Limits::Rows && col < Limits::Cols) {
                    Area area;
                    area.r1 = row;
                    area.c1 = col;
                    area.r2 = qMin(row + qMax(1, spanRows) - 1, Limits::Rows - 1);
                    area.c2 = qMin(col + qMax(1, spanCols) - 1, Limits::Cols - 1);
                    book.loaderSheet(index).merges.append(area);
                }
                col += repeat;
            }
        } else if (token == QXmlStreamReader::EndElement) {
            if (index >= 0 && tag == QLatin1String("table-row")) {
                bool content = false;
                for (const Held &cell : std::as_const(held)) {
                    content = content || !cell.value.isEmpty() || !cell.formula.isEmpty();
                }
                const int count = held.isEmpty() ? 0 : (content ? qMin(rowRepeat, RunLimit) : (rowRepeat <= 256 ? rowRepeat : 0));
                for (int k = 0; k < count && row + k < Limits::Rows; ++k) {
                    for (const Held &cell : std::as_const(held)) {
                        QString f = cell.formula;
                        if (k > 0 && !f.isEmpty()) {
                            f = Formula::print(Formula::shifted(Formula::parse(f), k, 0));
                        }
                        book.loadCell(index, row + k, cell.col, cell.value, f, cell.style);
                    }
                }
                row += rowRepeat;
                held.clear();
            } else if (tag == QLatin1String("table") && xml.prefix() == QLatin1String("table")) {
                index = -1;
            }
        }
    }
    if (sheets == 0) {
        book.finishLoading();
        return fail(xml.hasError() ? QStringLiteral("La hoja de cálculo está dañada: %1").arg(xml.errorString())
                                   : QStringLiteral("El archivo no tiene ninguna hoja."));
    }
    book.finishLoading();
    return true;
}
