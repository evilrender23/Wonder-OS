#include "xlsx.h"

#include "numfmt.h"
#include "workbook.h"
#include "zip.h"

#include <QBuffer>
#include <QHash>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <cmath>

namespace
{
const char *const MainNs = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
const char *const RelNs = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
const char *const PkgRelNs = "http://schemas.openxmlformats.org/package/2006/relationships";
const char *const DrawNs = "http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing";
const char *const ANs = "http://schemas.openxmlformats.org/drawingml/2006/main";
const char *const ChartNs = "http://schemas.openxmlformats.org/drawingml/2006/chart";

/// Pixels per unit of column width (the width of a digit in the default font) and its padding.
const double CharWidth = 7.0;
const double CharPadding = 5.0;

/// Functions Excel learnt after 2007 and wants prefixed in files.
const QStringList &futureFunctions()
{
    static const QStringList names = {
        QStringLiteral("XLOOKUP"), QStringLiteral("IFS"), QStringLiteral("SWITCH"), QStringLiteral("TEXTJOIN"), QStringLiteral("CONCAT"), QStringLiteral("MAXIFS"),
        QStringLiteral("MINIFS"), QStringLiteral("IFNA"), QStringLiteral("STDEV.S"), QStringLiteral("STDEV.P"), QStringLiteral("VAR.S"), QStringLiteral("VAR.P"),
        QStringLiteral("RANK.EQ"), QStringLiteral("MODE.SNGL"), QStringLiteral("PERCENTILE.INC"), QStringLiteral("QUARTILE.INC"), QStringLiteral("ISOWEEKNUM"),
        QStringLiteral("DAYS"), QStringLiteral("XOR"), QStringLiteral("TEXTBEFORE"), QStringLiteral("TEXTAFTER"), QStringLiteral("NUMBERVALUE"),
    };
    return names;
}

/// A formula as files hold it: the newer functions with their prefix.
QString formulaForFile(const QString &canonical)
{
    static const QRegularExpression name(QStringLiteral("\"(?:[^\"]|\"\")*\"|'(?:[^']|'')*'|([A-Z][A-Z0-9.]*)(?=\\()"));
    QString out;
    qsizetype last = 0;
    auto it = name.globalMatch(canonical);
    while (it.hasNext()) {
        const auto m = it.next();
        if (m.capturedStart(1) < 0 || !futureFunctions().contains(m.captured(1))) {
            continue;
        }
        out += canonical.mid(last, m.capturedStart(1) - last) + QStringLiteral("_xlfn.");
        last = m.capturedStart(1);
    }
    return out + canonical.mid(last);
}

QString formulaFromFile(QString text)
{
    text.remove(QStringLiteral("_xlfn._xlws."));
    text.remove(QStringLiteral("_xlfn."));
    text.remove(QStringLiteral("_xlws."));
    return text;
}

/// A format code without what only Excel means by it: «[$€-C0A]» is «"€"».
QString cleanCode(QString code)
{
    static const QRegularExpression locale(QStringLiteral("\\[\\$([^\\]-]*)(-[0-9A-Fa-f]+)?\\]"));
    qsizetype from = 0;
    for (auto m = locale.match(code, from); m.hasMatch(); m = locale.match(code, from)) {
        const QString sign = m.captured(1);
        const QString with = sign.isEmpty() ? QString() : QLatin1Char('"') + sign + QLatin1Char('"');
        code.replace(m.capturedStart(), m.capturedLength(), with);
        from = m.capturedStart() + with.size();
    }
    return code == QLatin1String("General") || code == QLatin1String("GENERAL") ? QString() : code;
}

QString hex(int r, int g, int b)
{
    return QStringLiteral("#%1%2%3").arg(qBound(0, r, 255), 2, 16, QLatin1Char('0')).arg(qBound(0, g, 255), 2, 16, QLatin1Char('0')).arg(qBound(0, b, 255), 2, 16, QLatin1Char('0'));
}

/// The colour of a <color>, <fgColor>… element as #rrggbb; empty when it is the automatic one.
QString readColor(const QXmlStreamAttributes &a, const QStringList &theme)
{
    if (a.hasAttribute(QLatin1String("rgb"))) {
        const QString rgb = a.value(QLatin1String("rgb")).toString();
        return rgb.size() >= 6 ? QLatin1Char('#') + rgb.right(6).toLower() : QString();
    }
    if (a.hasAttribute(QLatin1String("theme"))) {
        const int index = a.value(QLatin1String("theme")).toInt();
        if (index < 0 || index >= theme.size()) {
            return QString();
        }
        const QString base = theme.at(index);
        const double tint = a.value(QLatin1String("tint")).toDouble();
        int c[3];
        for (int i = 0; i < 3; ++i) {
            const int v = base.mid(1 + i * 2, 2).toInt(nullptr, 16);
            c[i] = tint < 0 ? int(std::lround(v * (1 + tint))) : int(std::lround(v + (255 - v) * tint));
        }
        return hex(c[0], c[1], c[2]);
    }
    if (a.hasAttribute(QLatin1String("indexed"))) {
        static const char *const basic[] = {"#000000", "#ffffff", "#ff0000", "#00ff00", "#0000ff", "#ffff00", "#ff00ff", "#00ffff"};
        const int index = a.value(QLatin1String("indexed")).toInt();
        if (index >= 0 && index < 16) {
            return QString::fromLatin1(basic[index % 8]);
        }
    }
    return QString();
}

QString argb(const QString &color)
{
    return QStringLiteral("FF") + color.mid(1).toUpper();
}

/// Text XML can hold: control characters other than tab and line ends go.
QString clean(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c.unicode() >= 0x20 || c == QLatin1Char('\t') || c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            out.append(c);
        }
    }
    return out;
}

QString number(double v)
{
    return QString::number(v, 'g', 17).contains(QLatin1Char('e')) || QString::number(v, 'g', 15).toDouble() != v ? QString::number(v, 'g', 17) : QString::number(v, 'g', 15);
}

/// The target of a relationship as a path inside the package.
QString resolve(const QString &base, const QString &target)
{
    if (target.startsWith(QLatin1Char('/'))) {
        return target.mid(1);
    }
    QStringList parts = base.split(QLatin1Char('/'));
    parts.removeLast();
    for (const QString &part : target.split(QLatin1Char('/'))) {
        if (part == QLatin1String("..")) {
            if (!parts.isEmpty()) {
                parts.removeLast();
            }
        } else if (part != QLatin1String(".")) {
            parts.append(part);
        }
    }
    return parts.join(QLatin1Char('/'));
}

QString relsPath(const QString &part)
{
    const qsizetype slash = part.lastIndexOf(QLatin1Char('/'));
    return part.left(slash + 1) + QStringLiteral("_rels/") + part.mid(slash + 1) + QStringLiteral(".rels");
}

struct Rel {
    QString type;
    QString target;
};

QHash<QString, Rel> readRels(const ZipReader &zip, const QString &part)
{
    QHash<QString, Rel> rels;
    QXmlStreamReader xml(zip.file(relsPath(part)));
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == QLatin1String("Relationship")) {
            const auto a = xml.attributes();
            if (a.value(QLatin1String("TargetMode")) != QLatin1String("External")) {
                rels.insert(a.value(QLatin1String("Id")).toString(), {a.value(QLatin1String("Type")).toString(), resolve(part, a.value(QLatin1String("Target")).toString())});
            }
        }
    }
    return rels;
}

// --- Reading ----------------------------------------------------------------------

QStringList readSharedStrings(const QByteArray &bytes)
{
    QStringList strings;
    QXmlStreamReader xml(bytes);
    QString current;
    bool inItem = false;
    int phonetic = 0;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("si")) {
                inItem = true;
                current.clear();
            } else if (xml.name() == QLatin1String("rPh")) {
                ++phonetic;
            } else if (xml.name() == QLatin1String("t") && inItem && phonetic == 0) {
                current += xml.readElementText();
            }
        } else if (token == QXmlStreamReader::EndElement) {
            if (xml.name() == QLatin1String("si")) {
                strings.append(current);
                inItem = false;
            } else if (xml.name() == QLatin1String("rPh")) {
                --phonetic;
            }
        }
    }
    return strings;
}

QStringList readTheme(const QByteArray &bytes)
{
    // The order styles count them in: light 1, dark 1, light 2, dark 2, the accents.
    QStringList colors = {QStringLiteral("#ffffff"), QStringLiteral("#000000"), QStringLiteral("#e7e6e6"), QStringLiteral("#44546a"), QStringLiteral("#4472c4"),
                          QStringLiteral("#ed7d31"), QStringLiteral("#a5a5a5"), QStringLiteral("#ffc000"), QStringLiteral("#5b9bd5"), QStringLiteral("#70ad47"),
                          QStringLiteral("#0563c1"), QStringLiteral("#954f72")};
    if (bytes.isEmpty()) {
        return colors;
    }
    static const QHash<QString, int> themeSlots = {{QStringLiteral("lt1"), 0}, {QStringLiteral("dk1"), 1}, {QStringLiteral("lt2"), 2}, {QStringLiteral("dk2"), 3},
                                              {QStringLiteral("accent1"), 4}, {QStringLiteral("accent2"), 5}, {QStringLiteral("accent3"), 6}, {QStringLiteral("accent4"), 7},
                                              {QStringLiteral("accent5"), 8}, {QStringLiteral("accent6"), 9}, {QStringLiteral("hlink"), 10}, {QStringLiteral("folHlink"), 11}};
    QXmlStreamReader xml(bytes);
    int slot = -1;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString name = xml.name().toString();
            if (themeSlots.contains(name)) {
                slot = themeSlots.value(name);
            } else if (slot >= 0 && name == QLatin1String("srgbClr")) {
                colors[slot] = QLatin1Char('#') + xml.attributes().value(QLatin1String("val")).toString().toLower();
            } else if (slot >= 0 && name == QLatin1String("sysClr")) {
                const QString last = xml.attributes().value(QLatin1String("lastClr")).toString();
                if (last.size() == 6) {
                    colors[slot] = QLatin1Char('#') + last.toLower();
                }
            }
        } else if (token == QXmlStreamReader::EndElement && themeSlots.contains(xml.name().toString())) {
            slot = -1;
        } else if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("clrScheme")) {
            break;
        }
    }
    return colors;
}

struct FileStyles {
    /// The workbook's style for each cell format of the file.
    QList<int> cells;
    /// What each differential format (of the conditional rules) changes.
    QList<Style> diffs;
};

FileStyles readStyles(Workbook &book, const QByteArray &bytes, const QStringList &theme)
{
    struct Font {
        bool bold = false, italic = false, underline = false, strike = false;
        QString color;
        int size = 0;
    };
    struct Border {
        int bits = 0;
        QString color;
    };
    FileStyles out;
    QHash<int, QString> formats;
    QList<Font> fonts;
    QList<QString> fills;
    QList<Border> borders;
    QXmlStreamReader xml(bytes);
    QStringList path;
    Font font;
    QString fill;
    Border border;
    Style diff;
    Style xf;
    int side = 0;
    bool inCellXfs = false;
    const auto flag = [&xml] {
        const auto v = xml.attributes().value(QLatin1String("val"));
        return v.isEmpty() || (v != QLatin1String("0") && v != QLatin1String("false") && v != QLatin1String("none"));
    };
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString name = xml.name().toString();
            const QString parent = path.isEmpty() ? QString() : path.last();
            const auto a = xml.attributes();
            const bool inDxf = path.contains(QLatin1String("dxf"));
            if (name == QLatin1String("numFmt") && !inDxf) {
                formats.insert(a.value(QLatin1String("numFmtId")).toInt(), cleanCode(a.value(QLatin1String("formatCode")).toString()));
            } else if (name == QLatin1String("font")) {
                font = Font();
            } else if (parent == QLatin1String("font")) {
                if (name == QLatin1String("b")) {
                    font.bold = flag();
                } else if (name == QLatin1String("i")) {
                    font.italic = flag();
                } else if (name == QLatin1String("u")) {
                    font.underline = flag();
                } else if (name == QLatin1String("strike")) {
                    font.strike = flag();
                } else if (name == QLatin1String("sz")) {
                    const int size = int(std::lround(a.value(QLatin1String("val")).toDouble()));
                    font.size = size == 11 || size == 10 ? 0 : size;
                } else if (name == QLatin1String("color")) {
                    font.color = readColor(a, theme);
                    // Plain black is the text colour, whatever the theme of the window.
                    if (font.color == QLatin1String("#000000")) {
                        font.color.clear();
                    }
                }
            } else if (name == QLatin1String("fill")) {
                fill.clear();
            } else if (name == QLatin1String("patternFill")) {
                if (a.value(QLatin1String("patternType")) == QLatin1String("none")) {
                    fill = QStringLiteral("none");
                }
            } else if (parent == QLatin1String("patternFill") && (name == QLatin1String("fgColor") || (name == QLatin1String("bgColor") && inDxf))) {
                if (fill != QLatin1String("none")) {
                    const QString color = readColor(a, theme);
                    if (!color.isEmpty()) {
                        fill = color;
                    }
                }
            } else if (name == QLatin1String("border")) {
                border = Border();
            } else if (parent == QLatin1String("border")) {
                side = name == QLatin1String("left") || name == QLatin1String("start") ? 1
                     : name == QLatin1String("top") ? 2
                     : name == QLatin1String("right") || name == QLatin1String("end") ? 4
                     : name == QLatin1String("bottom") ? 8 : 0;
                const auto style = a.value(QLatin1String("style"));
                if (side && !style.isEmpty() && style != QLatin1String("none")) {
                    border.bits |= side;
                } else {
                    side = 0;
                }
            } else if (name == QLatin1String("color") && path.contains(QLatin1String("border")) && side) {
                const QString color = readColor(a, theme);
                if (!color.isEmpty() && color != QLatin1String("#000000")) {
                    border.color = color;
                }
            } else if (name == QLatin1String("cellXfs")) {
                inCellXfs = true;
            } else if (name == QLatin1String("xf") && inCellXfs) {
                xf = Style();
                const int format = a.value(QLatin1String("numFmtId")).toInt();
                xf.format = formats.contains(format) ? formats.value(format) : cleanCode(NumFmt::builtin(format));
                const int f = a.value(QLatin1String("fontId")).toInt();
                if (f >= 0 && f < fonts.size()) {
                    xf.bold = fonts.at(f).bold;
                    xf.italic = fonts.at(f).italic;
                    xf.underline = fonts.at(f).underline;
                    xf.strike = fonts.at(f).strike;
                    xf.color = fonts.at(f).color;
                    xf.size = fonts.at(f).size;
                }
                const int fi = a.value(QLatin1String("fillId")).toInt();
                if (fi >= 0 && fi < fills.size()) {
                    xf.fill = fills.at(fi);
                }
                const int b = a.value(QLatin1String("borderId")).toInt();
                if (b >= 0 && b < borders.size()) {
                    xf.borders = borders.at(b).bits;
                    xf.borderColor = borders.at(b).color;
                }
            } else if (name == QLatin1String("alignment") && inCellXfs) {
                const auto h = a.value(QLatin1String("horizontal"));
                xf.align = h == QLatin1String("left") ? 1 : h == QLatin1String("center") || h == QLatin1String("centerContinuous") ? 2 : h == QLatin1String("right") ? 3 : 0;
                const auto v = a.value(QLatin1String("vertical"));
                xf.valign = v == QLatin1String("center") ? 1 : v == QLatin1String("top") ? 2 : 0;
                const auto wrap = a.value(QLatin1String("wrapText"));
                xf.wrap = wrap == QLatin1String("1") || wrap == QLatin1String("true");
            } else if (name == QLatin1String("dxf")) {
                diff = Style();
            }
            path.append(name);
        } else if (token == QXmlStreamReader::EndElement) {
            const QString name = xml.name().toString();
            if (!path.isEmpty()) {
                path.removeLast();
            }
            const bool inDxf = path.contains(QLatin1String("dxf"));
            if (name == QLatin1String("font")) {
                if (inDxf) {
                    diff.bold = font.bold;
                    diff.color = font.color;
                } else if (!path.isEmpty() && path.last() == QLatin1String("fonts")) {
                    fonts.append(font);
                }
            } else if (name == QLatin1String("fill")) {
                const QString solid = fill == QLatin1String("none") ? QString() : fill;
                if (inDxf) {
                    diff.fill = solid;
                } else if (!path.isEmpty() && path.last() == QLatin1String("fills")) {
                    fills.append(solid);
                }
            } else if (name == QLatin1String("border") && !inDxf && !path.isEmpty() && path.last() == QLatin1String("borders")) {
                borders.append(border);
            } else if (name == QLatin1String("xf") && inCellXfs) {
                out.cells.append(book.styleId(xf));
            } else if (name == QLatin1String("cellXfs")) {
                inCellXfs = false;
            } else if (name == QLatin1String("dxf")) {
                out.diffs.append(diff);
            }
        }
    }
    return out;
}

bool parseSqref(const QString &sqref, Area *area)
{
    const QString first = sqref.split(QLatin1Char(' '), Qt::SkipEmptyParts).value(0).remove(QLatin1Char('$'));
    return Formula::parseArea(first, &area->r1, &area->c1, &area->r2, &area->c2);
}

/// «25» out of a rule's formula, or the text out of «"abc"»; numbers the Spanish way.
QString operand(QString text)
{
    text = text.trimmed();
    if (text.size() >= 2 && text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"'))) {
        return text.mid(1, text.size() - 2).replace(QStringLiteral("\"\""), QStringLiteral("\""));
    }
    bool ok = false;
    const double value = text.toDouble(&ok);
    return ok ? NumFmt::general(value) : text;
}

QString operandForFile(const QString &text)
{
    const double value = NumFmt::parseNumber(text);
    if (!std::isnan(value)) {
        return number(value);
    }
    return QLatin1Char('"') + QString(text).replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"');
}

struct ChartPart {
    QString path;
    int row = 0;
    int col = 0;
    double width = 480;
    double height = 300;
};

/// The anchors of a drawing: where each chart sits.
QList<ChartPart> readDrawing(const ZipReader &zip, const QString &part)
{
    QList<ChartPart> charts;
    const auto rels = readRels(zip, part);
    QXmlStreamReader xml(zip.file(part));
    ChartPart current;
    QString within;
    int toRow = -1, toCol = -1;
    bool anchor = false;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const auto name = xml.name();
            if (name == QLatin1String("twoCellAnchor") || name == QLatin1String("oneCellAnchor")) {
                current = ChartPart();
                anchor = true;
                toRow = toCol = -1;
            } else if (name == QLatin1String("from") || name == QLatin1String("to")) {
                within = name.toString();
            } else if (anchor && name == QLatin1String("col") && !within.isEmpty()) {
                (within == QLatin1String("from") ? current.col : toCol) = xml.readElementText().toInt();
            } else if (anchor && name == QLatin1String("row") && !within.isEmpty()) {
                (within == QLatin1String("from") ? current.row : toRow) = xml.readElementText().toInt();
            } else if (anchor && name == QLatin1String("ext") && xml.attributes().hasAttribute(QLatin1String("cx"))) {
                // English Metric Units: 9525 to the pixel.
                current.width = xml.attributes().value(QLatin1String("cx")).toDouble() / 9525.0;
                current.height = xml.attributes().value(QLatin1String("cy")).toDouble() / 9525.0;
            } else if (anchor && name == QLatin1String("chart") && xml.namespaceUri() == QLatin1String(ChartNs)) {
                const QString id = xml.attributes().value(QLatin1String(RelNs), QLatin1String("id")).toString();
                current.path = rels.value(id).target;
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const auto name = xml.name();
            if (name == QLatin1String("from") || name == QLatin1String("to")) {
                within.clear();
            } else if (name == QLatin1String("twoCellAnchor") || name == QLatin1String("oneCellAnchor")) {
                if (!current.path.isEmpty()) {
                    if (toRow > current.row && toCol > current.col && current.width == 480 && current.height == 300) {
                        current.width = (toCol - current.col) * Workbook::DefaultColWidth;
                        current.height = (toRow - current.row) * Workbook::DefaultRowHeight;
                    }
                    charts.append(current);
                }
                anchor = false;
            }
        }
    }
    return charts;
}

/// «Hoja!$B$2:$B$5» as an area; false when it is on another sheet's terms we cannot read.
bool parseChartRef(QString ref, Area *area)
{
    const qsizetype bang = ref.lastIndexOf(QLatin1Char('!'));
    ref = ref.mid(bang + 1).remove(QLatin1Char('$'));
    return Formula::parseArea(ref, &area->r1, &area->c1, &area->r2, &area->c2);
}

bool readChart(const ZipReader &zip, const ChartPart &part, Chart *chart)
{
    QXmlStreamReader xml(zip.file(part.path));
    QStringList path;
    QString type;
    QString title;
    bool horizontal = false;
    bool legend = false;
    QList<Area> names, cats, vals;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString name = xml.name().toString();
            if (type.isEmpty() && name.endsWith(QLatin1String("Chart")) && path.contains(QLatin1String("plotArea"))) {
                type = name;
            } else if (name == QLatin1String("barDir")) {
                horizontal = xml.attributes().value(QLatin1String("val")) == QLatin1String("bar");
            } else if (name == QLatin1String("legend")) {
                legend = true;
            } else if (name == QLatin1String("t") && path.contains(QLatin1String("title")) && !path.contains(QLatin1String("plotArea"))) {
                title += xml.readElementText();
                continue;
            } else if (name == QLatin1String("f") && path.contains(QLatin1String("ser"))) {
                Area area;
                const bool ok = parseChartRef(xml.readElementText(), &area);
                if (ok) {
                    if (path.contains(QLatin1String("tx"))) {
                        names.append(area);
                    } else if (path.contains(QLatin1String("cat")) || path.contains(QLatin1String("xVal"))) {
                        cats.append(area);
                    } else if (path.contains(QLatin1String("val")) || path.contains(QLatin1String("yVal"))) {
                        vals.append(area);
                    }
                }
                continue;
            }
            path.append(name);
        } else if (token == QXmlStreamReader::EndElement && !path.isEmpty()) {
            path.removeLast();
        }
    }
    if (vals.isEmpty()) {
        return false;
    }
    chart->type = type == QLatin1String("lineChart") ? QStringLiteral("line")
                : type == QLatin1String("areaChart") ? QStringLiteral("area")
                : type == QLatin1String("pieChart") || type == QLatin1String("doughnutChart") || type == QLatin1String("pie3DChart") ? QStringLiteral("pie")
                : type == QLatin1String("scatterChart") ? QStringLiteral("scatter")
                : horizontal ? QStringLiteral("bar") : QStringLiteral("column");
    chart->title = title;
    chart->legend = legend;
    // The series side by side, their names above and the categories to the left.
    Area all = vals.first();
    for (const Area &v : std::as_const(vals)) {
        all.r1 = qMin(all.r1, v.r1);
        all.c1 = qMin(all.c1, v.c1);
        all.r2 = qMax(all.r2, v.r2);
        all.c2 = qMax(all.c2, v.c2);
    }
    chart->headerCol = !cats.isEmpty() && cats.first().c2 < all.c1;
    if (chart->headerCol) {
        all.c1 = cats.first().c1;
    }
    chart->headerRow = !names.isEmpty() && names.first().r2 < all.r1;
    if (chart->headerRow) {
        all.r1 = names.first().r1;
    }
    chart->area = all;
    chart->row = part.row;
    chart->col = part.col;
    chart->width = qBound(160.0, part.width, 2000.0);
    chart->height = qBound(120.0, part.height, 2000.0);
    return true;
}

void readSheet(Workbook &book, int index, const ZipReader &zip, const QString &part, const QStringList &strings, const FileStyles &styles)
{
    QXmlStreamReader xml(zip.file(part));
    Sheet &sheet = book.loaderSheet(index);
    struct Shared {
        NodePtr node;
        int row = 0;
        int col = 0;
    };
    QHash<int, Shared> shared;
    int row = -1, col = -1;
    int nextRow = 0;
    // Conditional formatting and validations, as they come.
    Area ruleArea;
    bool ruleOk = false;
    CondFormat rule;
    QStringList ruleFormulas;
    bool inRule = false;
    Validation validation;
    bool inValidation = false;
    bool validationOk = false;
    QString validationOp;
    QString drawing;
    QStringList tableParts;

    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const auto name = xml.name();
            const auto a = xml.attributes();
            if (name == QLatin1String("row")) {
                row = a.hasAttribute(QLatin1String("r")) ? a.value(QLatin1String("r")).toInt() - 1 : nextRow;
                nextRow = row + 1;
                col = -1;
                if (row >= 0 && row < Limits::Rows) {
                    if (a.hasAttribute(QLatin1String("ht")) && a.value(QLatin1String("customHeight")) == QLatin1String("1")) {
                        // Points to pixels.
                        sheet.rowHeights.insert(row, qBound(4.0, std::round(a.value(QLatin1String("ht")).toDouble() * 96.0 / 72.0), 600.0));
                    }
                    const auto hidden = a.value(QLatin1String("hidden"));
                    if (hidden == QLatin1String("1") || hidden == QLatin1String("true")) {
                        sheet.hiddenRows.insert(row);
                    }
                }
            } else if (name == QLatin1String("c")) {
                CellRef ref;
                if (a.hasAttribute(QLatin1String("r")) && Formula::parseCell(a.value(QLatin1String("r")).toString(), &ref)) {
                    col = ref.col;
                } else {
                    ++col;
                }
                const QString type = a.value(QLatin1String("t")).toString();
                const int s = a.value(QLatin1String("s")).toInt();
                const int style = s >= 0 && s < styles.cells.size() ? styles.cells.at(s) : 0;
                QString v, f, inlineText;
                bool hasV = false;
                bool isShared = false;
                int sharedId = -1;
                // To the end of the cell.
                while (!xml.atEnd()) {
                    const auto inner = xml.readNext();
                    if (inner == QXmlStreamReader::EndElement && xml.name() == QLatin1String("c")) {
                        break;
                    }
                    if (inner != QXmlStreamReader::StartElement) {
                        continue;
                    }
                    if (xml.name() == QLatin1String("v")) {
                        v = xml.readElementText();
                        hasV = true;
                    } else if (xml.name() == QLatin1String("f")) {
                        const auto fa = xml.attributes();
                        if (fa.value(QLatin1String("t")) == QLatin1String("shared")) {
                            isShared = true;
                            sharedId = fa.value(QLatin1String("si")).toInt();
                        }
                        f = formulaFromFile(xml.readElementText());
                    } else if (xml.name() == QLatin1String("t")) {
                        inlineText += xml.readElementText();
                    }
                }
                Value value;
                if (type == QLatin1String("s")) {
                    const int i = v.toInt();
                    if (hasV && i >= 0 && i < strings.size() && !strings.at(i).isEmpty()) {
                        value = Value(strings.at(i));
                    }
                } else if (type == QLatin1String("inlineStr")) {
                    if (!inlineText.isEmpty()) {
                        value = Value(inlineText);
                    }
                } else if (type == QLatin1String("str")) {
                    if (hasV && !(v.isEmpty() && f.isEmpty())) {
                        value = Value(v);
                    }
                } else if (type == QLatin1String("b")) {
                    if (hasV) {
                        value = Value::boolean(v == QLatin1String("1") || v.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0);
                    }
                } else if (type == QLatin1String("e")) {
                    value = Value(Value::errorFromText(v));
                } else if (hasV) {
                    bool ok = false;
                    const double n = v.toDouble(&ok);
                    if (ok && std::isfinite(n)) {
                        value = Value(n);
                    } else if (!v.isEmpty()) {
                        value = Value(v);
                    }
                }
                if (isShared) {
                    if (!f.isEmpty()) {
                        shared.insert(sharedId, {Formula::parse(f), row, col});
                    } else if (shared.contains(sharedId) && shared.value(sharedId).node) {
                        const Shared &master = shared[sharedId];
                        f = Formula::print(Formula::shifted(master.node, row - master.row, col - master.col));
                    }
                }
                book.loadCell(index, row, col, value, f, style);
            } else if (name == QLatin1String("col")) {
                const int min = a.value(QLatin1String("min")).toInt() - 1;
                const int max = qMin(a.value(QLatin1String("max")).toInt() - 1, Limits::Cols - 1);
                // A run to the end of the sheet is its default, not a size somebody gave.
                if (min >= 0 && max >= min && max - min < 1024) {
                    const auto hidden = a.value(QLatin1String("hidden"));
                    const bool hide = hidden == QLatin1String("1") || hidden == QLatin1String("true");
                    const auto custom = a.value(QLatin1String("customWidth"));
                    const bool sized = a.hasAttribute(QLatin1String("width")) && (custom == QLatin1String("1") || custom == QLatin1String("true"));
                    const double width = qBound(8.0, std::round(a.value(QLatin1String("width")).toDouble() * CharWidth + CharPadding), 1200.0);
                    for (int c = min; c <= max; ++c) {
                        if (sized) {
                            sheet.colWidths.insert(c, width);
                        }
                        if (hide) {
                            sheet.hiddenCols.insert(c);
                        }
                    }
                }
            } else if (name == QLatin1String("pane")) {
                const auto state = a.value(QLatin1String("state"));
                if (state == QLatin1String("frozen") || state == QLatin1String("frozenSplit")) {
                    sheet.freezeCols = qBound(0, int(a.value(QLatin1String("xSplit")).toDouble()), 50);
                    sheet.freezeRows = qBound(0, int(a.value(QLatin1String("ySplit")).toDouble()), 200);
                }
            } else if (name == QLatin1String("mergeCell")) {
                Area area;
                if (parseSqref(a.value(QLatin1String("ref")).toString(), &area) && (area.rows() > 1 || area.cols() > 1)) {
                    sheet.merges.append(area);
                }
            } else if (name == QLatin1String("autoFilter")) {
                Area area;
                if (parseSqref(a.value(QLatin1String("ref")).toString(), &area)) {
                    sheet.hasFilter = true;
                    sheet.filterArea = area;
                }
            } else if (name == QLatin1String("conditionalFormatting")) {
                ruleOk = parseSqref(a.value(QLatin1String("sqref")).toString(), &ruleArea);
            } else if (name == QLatin1String("cfRule")) {
                inRule = true;
                rule = CondFormat();
                ruleFormulas.clear();
                rule.area = ruleArea;
                const QString type = a.value(QLatin1String("type")).toString();
                const QString op = a.value(QLatin1String("operator")).toString();
                static const QHash<QString, QString> ops = {{QStringLiteral("greaterThan"), QStringLiteral(">")}, {QStringLiteral("lessThan"), QStringLiteral("<")},
                                                            {QStringLiteral("greaterThanOrEqual"), QStringLiteral(">=")}, {QStringLiteral("lessThanOrEqual"), QStringLiteral("<=")},
                                                            {QStringLiteral("equal"), QStringLiteral("=")}, {QStringLiteral("notEqual"), QStringLiteral("<>")},
                                                            {QStringLiteral("between"), QStringLiteral("between")}};
                if (type == QLatin1String("cellIs")) {
                    rule.op = ops.value(op);
                } else if (type == QLatin1String("containsText")) {
                    rule.op = QStringLiteral("contains");
                    rule.a = a.value(QLatin1String("text")).toString();
                } else if (type == QLatin1String("duplicateValues")) {
                    rule.op = QStringLiteral("duplicate");
                } else if (type == QLatin1String("top10") && a.value(QLatin1String("percent")) != QLatin1String("1")) {
                    rule.op = a.value(QLatin1String("bottom")) == QLatin1String("1") ? QStringLiteral("bottom") : QStringLiteral("top");
                    rule.a = a.value(QLatin1String("rank")).toString();
                }
                const int dxf = a.hasAttribute(QLatin1String("dxfId")) ? a.value(QLatin1String("dxfId")).toInt() : -1;
                if (dxf >= 0 && dxf < styles.diffs.size()) {
                    rule.fill = styles.diffs.at(dxf).fill;
                    rule.color = styles.diffs.at(dxf).color;
                    rule.bold = styles.diffs.at(dxf).bold;
                }
            } else if (name == QLatin1String("formula") && inRule) {
                ruleFormulas.append(xml.readElementText());
            } else if (name == QLatin1String("dataValidation")) {
                inValidation = true;
                validation = Validation();
                validationOk = parseSqref(a.value(QLatin1String("sqref")).toString(), &validation.area);
                const QString type = a.value(QLatin1String("type")).toString();
                validation.kind = type == QLatin1String("list") ? QStringLiteral("list")
                                : type == QLatin1String("whole") ? QStringLiteral("integer")
                                : type == QLatin1String("decimal") ? QStringLiteral("number")
                                : type == QLatin1String("date") ? QStringLiteral("date")
                                : type == QLatin1String("textLength") ? QStringLiteral("length") : QString();
                validation.message = a.value(QLatin1String("error")).toString();
                validationOp = a.value(QLatin1String("operator")).toString();
            } else if (inValidation && (name == QLatin1String("formula1") || name == QLatin1String("formula2"))) {
                const bool first = name == QLatin1String("formula1");
                const QString text = xml.readElementText();
                if (validation.kind == QLatin1String("list")) {
                    if (text.startsWith(QLatin1Char('"'))) {
                        for (const QString &item : operand(text).split(QLatin1Char(','))) {
                            validation.items.append(item.trimmed());
                        }
                    } else {
                        // A range of cells to choose from is not something we keep.
                        validationOk = false;
                    }
                } else {
                    QString bound = operand(text);
                    if (validation.kind == QLatin1String("date")) {
                        bool ok = false;
                        const double serial = text.toDouble(&ok);
                        bound = ok ? NumFmt::format(Value(serial), QStringLiteral("dd/mm/yyyy")) : QString();
                    }
                    const bool upperOnly = validationOp == QLatin1String("lessThanOrEqual") || validationOp == QLatin1String("lessThan");
                    if (first && upperOnly) {
                        validation.max = bound;
                    } else if (first) {
                        validation.min = bound;
                        if (validationOp == QLatin1String("equal")) {
                            validation.max = bound;
                        }
                    } else {
                        validation.max = bound;
                    }
                }
            } else if (name == QLatin1String("drawing")) {
                drawing = a.value(QLatin1String(RelNs), QLatin1String("id")).toString();
            } else if (name == QLatin1String("tablePart")) {
                tableParts.append(a.value(QLatin1String(RelNs), QLatin1String("id")).toString());
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const auto name = xml.name();
            if (name == QLatin1String("cfRule")) {
                inRule = false;
                if (rule.op != QLatin1String("contains") && rule.op != QLatin1String("top") && rule.op != QLatin1String("bottom")) {
                    rule.a = operand(ruleFormulas.value(0));
                    rule.b = operand(ruleFormulas.value(1));
                }
                if (ruleOk && !rule.op.isEmpty() && (!rule.fill.isEmpty() || !rule.color.isEmpty() || rule.bold)) {
                    sheet.conditionals.append(rule);
                }
            } else if (name == QLatin1String("dataValidation")) {
                inValidation = false;
                if (validationOk && !validation.kind.isEmpty()) {
                    sheet.validations.append(validation);
                }
            }
        }
    }

    if (!tableParts.isEmpty()) {
        const auto rels = readRels(zip, part);
        for (const QString &id : std::as_const(tableParts)) {
            QXmlStreamReader table(zip.file(rels.value(id).target));
            while (!table.atEnd()) {
                if (table.readNext() != QXmlStreamReader::StartElement) {
                    continue;
                }
                const auto t = table.attributes();
                if (table.name() == QLatin1String("table")) {
                    Table found;
                    found.totals = t.value(QLatin1String("totalsRowCount")).toInt() > 0;
                    bool free = parseSqref(t.value(QLatin1String("ref")).toString(), &found.area) && found.area.rows() > 1;
                    for (const Table &other : std::as_const(sheet.tables)) {
                        free = free && !other.area.intersects(found.area);
                    }
                    if (!free) {
                        break;
                    }
                    sheet.tables.append(found);
                } else if (table.name() == QLatin1String("autoFilter") && !sheet.hasFilter && !sheet.tables.isEmpty()) {
                    // The filter of a table is its own in the file; the sheet has one.
                    sheet.hasFilter = true;
                    sheet.filterArea = sheet.tables.constLast().area;
                    if (sheet.tables.constLast().totals) {
                        sheet.filterArea.r2 -= 1;
                    }
                }
            }
        }
    }

    if (!drawing.isEmpty()) {
        const auto rels = readRels(zip, part);
        int id = 1;
        for (const ChartPart &chartPart : readDrawing(zip, rels.value(drawing).target)) {
            Chart chart;
            if (readChart(zip, chartPart, &chart)) {
                // Ids are only told apart inside the workbook; finishLoading() moves the counter on.
                chart.id = (index + 1) * 1000 + id++;
                sheet.charts.append(chart);
            }
        }
    }
}
}

bool Xlsx::load(Workbook &book, const QByteArray &bytes, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    const ZipReader zip(bytes);
    if (!zip.isValid()) {
        return fail(QStringLiteral("El archivo no es un libro de Excel (.xlsx) o está dañado."));
    }
    // The workbook part is whatever the package says it is.
    QString workbookPart = QStringLiteral("xl/workbook.xml");
    for (const Rel &rel : readRels(zip, QString())) {
        if (rel.type.endsWith(QLatin1String("/officeDocument"))) {
            workbookPart = rel.target;
        }
    }
    if (!zip.contains(workbookPart)) {
        return fail(zip.contains(QStringLiteral("EncryptedPackage")) ? QStringLiteral("El libro está protegido con contraseña y no se puede abrir.")
                                                                     : QStringLiteral("El archivo no contiene un libro de Excel."));
    }
    const auto rels = readRels(zip, workbookPart);
    QString stringsPart, stylesPart, themePart;
    for (const Rel &rel : rels) {
        if (rel.type.endsWith(QLatin1String("/sharedStrings"))) {
            stringsPart = rel.target;
        } else if (rel.type.endsWith(QLatin1String("/styles"))) {
            stylesPart = rel.target;
        } else if (rel.type.endsWith(QLatin1String("/theme"))) {
            themePart = rel.target;
        }
    }
    struct Entry {
        QString name;
        QString part;
    };
    QList<Entry> entries;
    {
        QXmlStreamReader xml(zip.file(workbookPart));
        while (!xml.atEnd()) {
            if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == QLatin1String("sheet")) {
                const auto a = xml.attributes();
                const QString id = a.value(QLatin1String(RelNs), QLatin1String("id")).toString();
                // Only worksheets: chart sheets and macro sheets have nothing for us.
                if (rels.contains(id) && rels.value(id).type.endsWith(QLatin1String("/worksheet"))) {
                    entries.append({a.value(QLatin1String("name")).toString(), rels.value(id).target});
                }
            }
        }
        if (xml.hasError() && entries.isEmpty()) {
            return fail(QStringLiteral("El libro está dañado: %1").arg(xml.errorString()));
        }
    }
    if (entries.isEmpty()) {
        return fail(QStringLiteral("El libro no tiene ninguna hoja."));
    }

    book.clear();
    const QStringList strings = readSharedStrings(zip.file(stringsPart));
    const FileStyles styles = readStyles(book, zip.file(stylesPart), readTheme(zip.file(themePart)));
    for (int i = 0; i < entries.size(); ++i) {
        if (i > 0) {
            book.addSheet(QStringLiteral("\u0001hoja%1").arg(i));
        }
    }
    for (int i = 0; i < entries.size(); ++i) {
        // Named straight, so that any name a file brings is kept as it is.
        book.loaderSheet(i).name = entries.at(i).name.isEmpty() ? QStringLiteral("Hoja %1").arg(i + 1) : entries.at(i).name;
    }
    for (int i = 0; i < entries.size(); ++i) {
        readSheet(book, i, zip, entries.at(i).part, strings, styles);
    }
    book.finishLoading();
    return true;
}

// --- Writing ----------------------------------------------------------------------

namespace
{
class Part
{
public:
    Part()
    {
        m_buffer.open(QIODevice::WriteOnly);
        xml.setDevice(&m_buffer);
        xml.writeStartDocument(QStringLiteral("1.0"), true);
    }
    QByteArray done()
    {
        xml.writeEndDocument();
        m_buffer.close();
        return m_buffer.data();
    }
    void attr(const char *name, const QString &value) { xml.writeAttribute(QLatin1String(name), value); }
    void attr(const char *name, int value) { xml.writeAttribute(QLatin1String(name), QString::number(value)); }
    void start(const char *name) { xml.writeStartElement(QLatin1String(name)); }
    void end() { xml.writeEndElement(); }
    void empty(const char *name) { xml.writeEmptyElement(QLatin1String(name)); }
    /// <name val="value"/>
    void val(const char *name, const QString &value)
    {
        xml.writeEmptyElement(QLatin1String(name));
        xml.writeAttribute(QLatin1String("val"), value);
    }
    void text(const char *name, const QString &value)
    {
        xml.writeStartElement(QLatin1String(name));
        if (value.startsWith(QLatin1Char(' ')) || value.endsWith(QLatin1Char(' ')) || value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\t'))) {
            xml.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
        }
        xml.writeCharacters(clean(value));
        xml.writeEndElement();
    }

    QXmlStreamWriter xml;

private:
    QBuffer m_buffer;
};

/// The styles of the workbook as the file's tables of fonts, fills, borders and formats.
class StyleTable
{
public:
    explicit StyleTable(Workbook &book)
        : m_book(book)
    {
        m_fonts.append(fontKey(Style()));
        m_borders.append(QStringLiteral("0|"));
        index(0);
    }

    /// The file's number for a style of the workbook.
    int index(int style)
    {
        const auto found = m_index.constFind(style);
        if (found != m_index.constEnd()) {
            return *found;
        }
        const Style &s = m_book.style(style);
        Xf xf;
        xf.style = s;
        xf.font = slot(m_fonts, fontKey(s));
        xf.fill = s.fill.isEmpty() ? 0 : slot(m_fills, s.fill) + 2;
        xf.border = slot(m_borders, QString::number(s.borders) + QLatin1Char('|') + (s.borders ? s.borderColor : QString()));
        const QString code = s.format;
        const int builtin = NumFmt::builtinId(code);
        xf.format = builtin >= 0 ? builtin : slot(m_formats, code) + 164;
        m_xfs.append(xf);
        m_index.insert(style, m_xfs.size() - 1);
        return m_xfs.size() - 1;
    }

    int diff(const CondFormat &rule)
    {
        m_diffs.append(rule);
        return m_diffs.size() - 1;
    }

    QByteArray xml()
    {
        Part p;
        p.start("styleSheet");
        p.attr("xmlns", QLatin1String(MainNs));
        if (!m_formats.isEmpty()) {
            p.start("numFmts");
            p.attr("count", m_formats.size());
            for (int i = 0; i < m_formats.size(); ++i) {
                p.empty("numFmt");
                p.attr("numFmtId", 164 + i);
                p.attr("formatCode", m_formats.at(i));
            }
            p.end();
        }
        p.start("fonts");
        p.attr("count", m_fonts.size());
        for (const QString &key : std::as_const(m_fonts)) {
            const QStringList f = key.split(QLatin1Char('|'));
            p.start("font");
            if (f.at(0) == QLatin1String("1")) {
                p.empty("b");
            }
            if (f.at(1) == QLatin1String("1")) {
                p.empty("i");
            }
            if (f.at(3) == QLatin1String("1")) {
                p.empty("strike");
            }
            if (f.at(2) == QLatin1String("1")) {
                p.empty("u");
            }
            p.val("sz", f.at(5) == QLatin1String("0") ? QStringLiteral("11") : f.at(5));
            p.empty("color");
            if (f.at(4).isEmpty()) {
                p.attr("theme", 1);
            } else {
                p.attr("rgb", argb(f.at(4)));
            }
            p.val("name", QStringLiteral("Calibri"));
            p.val("family", QStringLiteral("2"));
            p.end();
        }
        p.end();
        p.start("fills");
        p.attr("count", m_fills.size() + 2);
        for (const char *pattern : {"none", "gray125"}) {
            p.start("fill");
            p.empty("patternFill");
            p.attr("patternType", QLatin1String(pattern));
            p.end();
        }
        for (const QString &fill : std::as_const(m_fills)) {
            p.start("fill");
            p.start("patternFill");
            p.attr("patternType", QStringLiteral("solid"));
            p.empty("fgColor");
            p.attr("rgb", argb(fill));
            p.empty("bgColor");
            p.attr("indexed", 64);
            p.end();
            p.end();
        }
        p.end();
        p.start("borders");
        p.attr("count", m_borders.size());
        for (const QString &key : std::as_const(m_borders)) {
            const int bits = key.section(QLatin1Char('|'), 0, 0).toInt();
            const QString color = key.section(QLatin1Char('|'), 1);
            p.start("border");
            const struct {
                const char *name;
                int bit;
            } sides[] = {{"left", 1}, {"right", 4}, {"top", 2}, {"bottom", 8}};
            for (const auto &side : sides) {
                if (bits & side.bit) {
                    p.start(side.name);
                    p.attr("style", QStringLiteral("thin"));
                    p.empty("color");
                    if (color.isEmpty()) {
                        p.attr("auto", 1);
                    } else {
                        p.attr("rgb", argb(color));
                    }
                    p.end();
                } else {
                    p.empty(side.name);
                }
            }
            p.empty("diagonal");
            p.end();
        }
        p.end();
        p.start("cellStyleXfs");
        p.attr("count", 1);
        p.empty("xf");
        p.attr("numFmtId", 0);
        p.attr("fontId", 0);
        p.attr("fillId", 0);
        p.attr("borderId", 0);
        p.end();
        p.start("cellXfs");
        p.attr("count", m_xfs.size());
        for (const Xf &xf : std::as_const(m_xfs)) {
            const Style &s = xf.style;
            const bool aligned = s.align != 0 || s.valign != 0 || s.wrap;
            if (aligned) {
                p.start("xf");
            } else {
                p.empty("xf");
            }
            p.attr("numFmtId", xf.format);
            p.attr("fontId", xf.font);
            p.attr("fillId", xf.fill);
            p.attr("borderId", xf.border);
            p.attr("xfId", 0);
            if (xf.format != 0) {
                p.attr("applyNumberFormat", 1);
            }
            if (xf.font != 0) {
                p.attr("applyFont", 1);
            }
            if (xf.fill != 0) {
                p.attr("applyFill", 1);
            }
            if (xf.border != 0) {
                p.attr("applyBorder", 1);
            }
            if (aligned) {
                p.attr("applyAlignment", 1);
                p.empty("alignment");
                if (s.align != 0) {
                    p.attr("horizontal", s.align == 1 ? QStringLiteral("left") : s.align == 2 ? QStringLiteral("center") : QStringLiteral("right"));
                }
                // Bottom is the default of the format, and ours too.
                if (s.valign != 0) {
                    p.attr("vertical", s.valign == 1 ? QStringLiteral("center") : QStringLiteral("top"));
                }
                if (s.wrap) {
                    p.attr("wrapText", 1);
                }
                p.end();
            }
        }
        p.end();
        p.start("cellStyles");
        p.attr("count", 1);
        p.empty("cellStyle");
        p.attr("name", QStringLiteral("Normal"));
        p.attr("xfId", 0);
        p.attr("builtinId", 0);
        p.end();
        p.start("dxfs");
        p.attr("count", m_diffs.size());
        for (const CondFormat &rule : std::as_const(m_diffs)) {
            p.start("dxf");
            if (rule.bold || !rule.color.isEmpty()) {
                p.start("font");
                if (rule.bold) {
                    p.empty("b");
                }
                if (!rule.color.isEmpty()) {
                    p.empty("color");
                    p.attr("rgb", argb(rule.color));
                }
                p.end();
            }
            if (!rule.fill.isEmpty()) {
                p.start("fill");
                p.start("patternFill");
                p.empty("bgColor");
                p.attr("rgb", argb(rule.fill));
                p.end();
                p.end();
            }
            p.end();
        }
        p.end();
        p.end();
        return p.done();
    }

private:
    struct Xf {
        Style style;
        int font = 0;
        int fill = 0;
        int border = 0;
        int format = 0;
    };
    static QString fontKey(const Style &s)
    {
        return QStringLiteral("%1|%2|%3|%4|%5|%6").arg(s.bold).arg(s.italic).arg(s.underline).arg(s.strike).arg(s.color).arg(s.size);
    }
    static int slot(QStringList &list, const QString &key)
    {
        int at = list.indexOf(key);
        if (at < 0) {
            list.append(key);
            at = list.size() - 1;
        }
        return at;
    }

    Workbook &m_book;
    QHash<int, int> m_index;
    QList<Xf> m_xfs;
    QStringList m_fonts;
    QStringList m_fills;
    QStringList m_borders;
    QStringList m_formats;
    QList<CondFormat> m_diffs;
};

QString absoluteRef(const QString &sheet, int r1, int c1, int r2, int c2)
{
    const auto cell = [](int r, int c) {
        return QLatin1Char('$') + Formula::columnName(c) + QLatin1Char('$') + QString::number(r + 1);
    };
    return Formula::quoteSheet(sheet) + QLatin1Char('!') + cell(r1, c1) + (r1 == r2 && c1 == c2 ? QString() : QLatin1Char(':') + cell(r2, c2));
}

/// A chart as DrawingML: its series point at the cells and carry the values they had.
QByteArray chartXml(Workbook &book, int sheetIndex, const Chart &chart)
{
    const QString sheet = book.sheetName(sheetIndex);
    const ChartData data = book.chartData(sheetIndex, chart);
    const Area a = chart.area;
    const int firstRow = a.r1 + (chart.headerRow ? 1 : 0);
    const int firstCol = a.c1 + (chart.headerCol ? 1 : 0);
    const bool scatter = chart.type == QLatin1String("scatter");
    const bool pie = chart.type == QLatin1String("pie");
    static const char *const palette[] = {"1F9D55", "0A84FF", "FF9F0A", "FF453A", "BF5AF2", "64D2FF", "FFD60A", "AC8E68"};

    Part p;
    p.xml.writeNamespace(QLatin1String(ChartNs), QStringLiteral("c"));
    p.xml.writeNamespace(QLatin1String(ANs), QStringLiteral("a"));
    p.xml.writeNamespace(QLatin1String(RelNs), QStringLiteral("r"));
    p.start("c:chartSpace");
    p.start("c:chart");
    if (!chart.title.isEmpty()) {
        p.start("c:title");
        p.start("c:tx");
        p.start("c:rich");
        p.empty("a:bodyPr");
        p.start("a:p");
        p.start("a:r");
        p.text("a:t", chart.title);
        p.end();
        p.end();
        p.end();
        p.end();
        p.val("c:overlay", QStringLiteral("0"));
        p.end();
    }
    p.val("c:autoTitleDeleted", chart.title.isEmpty() ? QStringLiteral("1") : QStringLiteral("0"));
    p.start("c:plotArea");
    p.empty("c:layout");
    const char *element = chart.type == QLatin1String("line") ? "c:lineChart"
                        : chart.type == QLatin1String("area") ? "c:areaChart"
                        : pie ? "c:pieChart"
                        : scatter ? "c:scatterChart" : "c:barChart";
    p.start(element);
    if (chart.type == QLatin1String("column") || chart.type == QLatin1String("bar")) {
        p.val("c:barDir", chart.type == QLatin1String("bar") ? QStringLiteral("bar") : QStringLiteral("col"));
        p.val("c:grouping", QStringLiteral("clustered"));
    } else if (chart.type == QLatin1String("line") || chart.type == QLatin1String("area")) {
        p.val("c:grouping", QStringLiteral("standard"));
    } else if (scatter) {
        p.val("c:scatterStyle", QStringLiteral("lineMarker"));
    }
    p.val("c:varyColors", pie ? QStringLiteral("1") : QStringLiteral("0"));
    const int count = pie ? qMin(1, int(data.series.size())) : int(data.series.size());
    for (int s = 0; s < count; ++s) {
        const int col = firstCol + s;
        p.start("c:ser");
        p.val("c:idx", QString::number(s));
        p.val("c:order", QString::number(s));
        if (chart.headerRow) {
            p.start("c:tx");
            p.start("c:strRef");
            p.text("c:f", absoluteRef(sheet, a.r1, col, a.r1, col));
            p.start("c:strCache");
            p.val("c:ptCount", QStringLiteral("1"));
            p.start("c:pt");
            p.attr("idx", 0);
            p.text("c:v", data.seriesNames.value(s));
            p.end();
            p.end();
            p.end();
            p.end();
        }
        if (!pie) {
            p.start("c:spPr");
            if (chart.type == QLatin1String("line") || scatter) {
                p.start("a:ln");
                p.attr("w", scatter ? QStringLiteral("19050") : QStringLiteral("28575"));
                if (scatter) {
                    p.empty("a:noFill");
                } else {
                    p.start("a:solidFill");
                    p.val("a:srgbClr", QLatin1String(palette[s % 8]));
                    p.end();
                }
                p.end();
            } else {
                p.start("a:solidFill");
                p.val("a:srgbClr", QLatin1String(palette[s % 8]));
                p.end();
            }
            p.end();
        }
        if (chart.type == QLatin1String("line")) {
            p.start("c:marker");
            p.val("c:symbol", QStringLiteral("none"));
            p.end();
        }
        if (chart.headerCol) {
            p.start(scatter ? "c:xVal" : "c:cat");
            const bool numeric = scatter;
            p.start(numeric ? "c:numRef" : "c:strRef");
            p.text("c:f", absoluteRef(sheet, firstRow, a.c1, a.r2, a.c1));
            p.start(numeric ? "c:numCache" : "c:strCache");
            if (numeric) {
                p.text("c:formatCode", QStringLiteral("General"));
            }
            p.val("c:ptCount", QString::number(data.categories.size()));
            for (int i = 0; i < data.categories.size(); ++i) {
                if (numeric && (i >= data.xs.size() || std::isnan(data.xs.at(i)))) {
                    continue;
                }
                p.start("c:pt");
                p.attr("idx", i);
                p.text("c:v", numeric ? number(data.xs.at(i)) : data.categories.at(i));
                p.end();
            }
            p.end();
            p.end();
            p.end();
        }
        p.start(scatter ? "c:yVal" : "c:val");
        p.start("c:numRef");
        p.text("c:f", absoluteRef(sheet, firstRow, col, a.r2, col));
        p.start("c:numCache");
        p.text("c:formatCode", QStringLiteral("General"));
        const QList<double> values = data.series.at(s);
        p.val("c:ptCount", QString::number(values.size()));
        for (int i = 0; i < values.size(); ++i) {
            if (std::isnan(values.at(i))) {
                continue;
            }
            p.start("c:pt");
            p.attr("idx", i);
            p.text("c:v", number(values.at(i)));
            p.end();
        }
        p.end();
        p.end();
        p.end();
        p.end();
    }
    if (!pie) {
        if (chart.type == QLatin1String("column") || chart.type == QLatin1String("bar")) {
            p.val("c:gapWidth", QStringLiteral("80"));
        }
        p.val("c:axId", QStringLiteral("111111111"));
        p.val("c:axId", QStringLiteral("222222222"));
    }
    p.end();
    if (!pie) {
        const bool flipped = chart.type == QLatin1String("bar");
        const auto axis = [&p](const char *name, const char *id, const char *cross, const char *position, bool grid) {
            p.start(name);
            p.val("c:axId", QLatin1String(id));
            p.start("c:scaling");
            p.val("c:orientation", QStringLiteral("minMax"));
            p.end();
            p.val("c:delete", QStringLiteral("0"));
            p.val("c:axPos", QLatin1String(position));
            if (grid) {
                p.empty("c:majorGridlines");
            }
            p.val("c:tickLblPos", QStringLiteral("nextTo"));
            p.val("c:crossAx", QLatin1String(cross));
            p.val("c:crosses", QStringLiteral("autoZero"));
            p.end();
        };
        axis(scatter ? "c:valAx" : "c:catAx", "111111111", "222222222", flipped ? "l" : "b", false);
        axis("c:valAx", "222222222", "111111111", flipped ? "b" : "l", true);
    }
    p.end();
    if (chart.legend) {
        p.start("c:legend");
        p.val("c:legendPos", QStringLiteral("r"));
        p.val("c:overlay", QStringLiteral("0"));
        p.end();
    }
    p.val("c:plotVisOnly", QStringLiteral("1"));
    p.end();
    p.end();
    return p.done();
}

QByteArray relsXml(const QList<QPair<QString, QString>> &rels)
{
    Part p;
    p.start("Relationships");
    p.attr("xmlns", QLatin1String(PkgRelNs));
    for (int i = 0; i < rels.size(); ++i) {
        p.empty("Relationship");
        p.attr("Id", QStringLiteral("rId%1").arg(i + 1));
        p.attr("Type", QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/") + rels.at(i).first);
        p.attr("Target", rels.at(i).second);
    }
    p.end();
    return p.done();
}
}

QByteArray Xlsx::save(Workbook &book)
{
    book.recalc(false);
    ZipWriter zip;
    StyleTable styles(book);
    QStringList strings;
    QHash<QString, int> stringIndex;
    int stringUses = 0;
    QList<QByteArray> sheetParts;
    QList<QPair<QString, QByteArray>> extra;
    QStringList overrides;
    int chartCount = 0;
    int drawingCount = 0;
    int tableCount = 0;

    for (int index = 0; index < book.sheetCount(); ++index) {
        const Sheet &sheet = book.sheet(index);
        int usedRows = 0, usedCols = 0;
        book.used(index, &usedRows, &usedCols);
        Part p;
        p.xml.writeDefaultNamespace(QLatin1String(MainNs));
        p.xml.writeNamespace(QLatin1String(RelNs), QStringLiteral("r"));
        p.start("worksheet");
        p.empty("dimension");
        p.attr("ref", Formula::areaName(0, 0, qMax(usedRows - 1, 0), qMax(usedCols - 1, 0)));
        p.start("sheetViews");
        p.start("sheetView");
        if (index == 0) {
            p.attr("tabSelected", 1);
        }
        p.attr("workbookViewId", 0);
        if (sheet.freezeRows > 0 || sheet.freezeCols > 0) {
            p.empty("pane");
            if (sheet.freezeCols > 0) {
                p.attr("xSplit", sheet.freezeCols);
            }
            if (sheet.freezeRows > 0) {
                p.attr("ySplit", sheet.freezeRows);
            }
            p.attr("topLeftCell", Formula::cellName(sheet.freezeRows, sheet.freezeCols));
            p.attr("activePane", sheet.freezeRows > 0 && sheet.freezeCols > 0 ? QStringLiteral("bottomRight") : sheet.freezeRows > 0 ? QStringLiteral("bottomLeft") : QStringLiteral("topRight"));
            p.attr("state", QStringLiteral("frozen"));
        }
        p.end();
        p.end();
        p.empty("sheetFormatPr");
        p.attr("defaultColWidth", number((Workbook::DefaultColWidth - CharPadding) / CharWidth));
        p.attr("defaultRowHeight", number(Workbook::DefaultRowHeight * 72.0 / 96.0));
        p.attr("customHeight", 1);

        // Columns with a size of their own, or hidden; runs of equals in one element.
        QMap<int, QPair<double, bool>> cols;
        for (auto it = sheet.colWidths.constBegin(); it != sheet.colWidths.constEnd(); ++it) {
            cols[it.key()] = qMakePair(it.value(), false);
        }
        for (int c : sheet.hiddenCols) {
            cols[c] = qMakePair(sheet.colWidths.value(c, -1.0), true);
        }
        if (!cols.isEmpty()) {
            p.start("cols");
            for (auto it = cols.constBegin(); it != cols.constEnd();) {
                int last = it.key();
                auto next = std::next(it);
                while (next != cols.constEnd() && next.key() == last + 1 && next.value() == it.value()) {
                    last = next.key();
                    ++next;
                }
                p.empty("col");
                p.attr("min", it.key() + 1);
                p.attr("max", last + 1);
                const double width = it.value().first > 0 ? it.value().first : Workbook::DefaultColWidth;
                p.attr("width", number(qMax(0.0, (width - CharPadding) / CharWidth)));
                if (it.value().first > 0) {
                    p.attr("customWidth", 1);
                }
                if (it.value().second) {
                    p.attr("hidden", 1);
                }
                it = next;
            }
            p.end();
        }

        p.start("sheetData");
        QSet<int> special;
        for (auto it = sheet.rowHeights.constBegin(); it != sheet.rowHeights.constEnd(); ++it) {
            special.insert(it.key());
        }
        special.unite(sheet.hiddenRows);
        special.unite(sheet.filteredRows);
        QList<int> rowNumbers = sheet.rows.keys();
        for (int r : std::as_const(special)) {
            if (!sheet.rows.contains(r)) {
                rowNumbers.append(r);
            }
        }
        std::sort(rowNumbers.begin(), rowNumbers.end());
        for (int r : std::as_const(rowNumbers)) {
            p.start("row");
            p.attr("r", r + 1);
            if (sheet.rowHeights.contains(r)) {
                p.attr("ht", number(sheet.rowHeights.value(r) * 72.0 / 96.0));
                p.attr("customHeight", 1);
            }
            if (sheet.hiddenRows.contains(r) || sheet.filteredRows.contains(r)) {
                p.attr("hidden", 1);
            }
            const auto found = sheet.rows.constFind(r);
            if (found != sheet.rows.constEnd()) {
                for (auto it = found->constBegin(); it != found->constEnd(); ++it) {
                    const Cell &cell = it.value();
                    const Value &v = cell.value;
                    p.start("c");
                    p.attr("r", Formula::cellName(r, it.key()));
                    if (cell.style != 0) {
                        p.attr("s", styles.index(cell.style));
                    }
                    const bool formula = bool(cell.formula);
                    if (v.isText()) {
                        p.attr("t", formula ? QStringLiteral("str") : QStringLiteral("s"));
                    } else if (v.isBool()) {
                        p.attr("t", QStringLiteral("b"));
                    } else if (v.isError()) {
                        p.attr("t", QStringLiteral("e"));
                    }
                    if (formula) {
                        p.text("f", formulaForFile(Formula::print(cell.formula)));
                    }
                    if (v.isText() && !formula) {
                        auto at = stringIndex.constFind(v.text);
                        if (at == stringIndex.constEnd()) {
                            strings.append(v.text);
                            at = stringIndex.insert(v.text, strings.size() - 1);
                        }
                        ++stringUses;
                        p.text("v", QString::number(*at));
                    } else if (v.isText()) {
                        p.text("v", v.text);
                    } else if (v.isBool()) {
                        p.text("v", v.num != 0 ? QStringLiteral("1") : QStringLiteral("0"));
                    } else if (v.isError()) {
                        // A circle is our own word; files have none for it.
                        p.text("v", v.err == Err::Cycle ? QStringLiteral("#VALUE!") : Value::errorText(v.err));
                    } else if (v.isNumber()) {
                        p.text("v", number(v.num));
                    }
                    p.end();
                }
            }
            p.end();
        }
        p.end();

        // The filter over a table goes in the table's own part: twice is a file Excel repairs.
        int filterTable = -1;
        for (int t = 0; t < sheet.tables.size() && sheet.hasFilter; ++t) {
            if (sheet.tables.at(t).area.intersects(sheet.filterArea)) {
                filterTable = t;
            }
        }
        if (sheet.hasFilter && filterTable < 0) {
            p.empty("autoFilter");
            p.attr("ref", Formula::areaName(sheet.filterArea.r1, sheet.filterArea.c1, sheet.filterArea.r2, sheet.filterArea.c2));
        }
        if (!sheet.merges.isEmpty()) {
            p.start("mergeCells");
            p.attr("count", sheet.merges.size());
            for (const Area &m : sheet.merges) {
                p.empty("mergeCell");
                p.attr("ref", Formula::areaName(m.r1, m.c1, m.r2, m.c2));
            }
            p.end();
        }
        int priority = 1;
        for (const CondFormat &rule : sheet.conditionals) {
            static const QHash<QString, QString> ops = {{QStringLiteral(">"), QStringLiteral("greaterThan")}, {QStringLiteral("<"), QStringLiteral("lessThan")},
                                                        {QStringLiteral(">="), QStringLiteral("greaterThanOrEqual")}, {QStringLiteral("<="), QStringLiteral("lessThanOrEqual")},
                                                        {QStringLiteral("="), QStringLiteral("equal")}, {QStringLiteral("<>"), QStringLiteral("notEqual")},
                                                        {QStringLiteral("between"), QStringLiteral("between")}};
            const Area clippedArea = book.clipped(index, rule.area);
            Q_UNUSED(clippedArea)
            p.start("conditionalFormatting");
            p.attr("sqref", Formula::areaName(rule.area.r1, rule.area.c1, rule.area.r2, rule.area.c2));
            p.start("cfRule");
            const int dxf = styles.diff(rule);
            if (ops.contains(rule.op)) {
                p.attr("type", QStringLiteral("cellIs"));
                p.attr("dxfId", dxf);
                p.attr("priority", priority++);
                p.attr("operator", ops.value(rule.op));
                p.text("formula", operandForFile(rule.a));
                if (rule.op == QLatin1String("between")) {
                    p.text("formula", operandForFile(rule.b));
                }
            } else if (rule.op == QLatin1String("contains")) {
                p.attr("type", QStringLiteral("containsText"));
                p.attr("dxfId", dxf);
                p.attr("priority", priority++);
                p.attr("operator", QStringLiteral("containsText"));
                p.attr("text", rule.a);
                p.text("formula", QStringLiteral("NOT(ISERROR(SEARCH(%1,%2)))").arg(operandForFile(QLatin1Char('\'') + rule.a).replace(QStringLiteral("\"'"), QStringLiteral("\"")),
                                                                                  Formula::cellName(rule.area.r1, rule.area.c1)));
            } else if (rule.op == QLatin1String("duplicate")) {
                p.attr("type", QStringLiteral("duplicateValues"));
                p.attr("dxfId", dxf);
                p.attr("priority", priority++);
            } else {
                p.attr("type", QStringLiteral("top10"));
                p.attr("dxfId", dxf);
                p.attr("priority", priority++);
                if (rule.op == QLatin1String("bottom")) {
                    p.attr("bottom", 1);
                }
                p.attr("rank", qMax(1, int(NumFmt::parseNumber(rule.a))));
            }
            p.end();
            p.end();
        }
        if (!sheet.validations.isEmpty()) {
            p.start("dataValidations");
            p.attr("count", sheet.validations.size());
            for (const Validation &rule : sheet.validations) {
                p.start("dataValidation");
                const bool list = rule.kind == QLatin1String("list");
                p.attr("type", list ? QStringLiteral("list")
                             : rule.kind == QLatin1String("integer") ? QStringLiteral("whole")
                             : rule.kind == QLatin1String("date") ? QStringLiteral("date")
                             : rule.kind == QLatin1String("length") ? QStringLiteral("textLength") : QStringLiteral("decimal"));
                p.attr("allowBlank", 1);
                p.attr("showErrorMessage", 1);
                const auto bound = [&rule](const QString &text) {
                    if (rule.kind == QLatin1String("date")) {
                        const Value v = NumFmt::parseInput(text);
                        return v.isNumber() ? number(v.num) : QStringLiteral("0");
                    }
                    const double n = NumFmt::parseNumber(text);
                    return std::isnan(n) ? QStringLiteral("0") : number(n);
                };
                if (!list) {
                    const bool low = !rule.min.isEmpty(), high = !rule.max.isEmpty();
                    p.attr("operator", low && high ? QStringLiteral("between") : high ? QStringLiteral("lessThanOrEqual") : QStringLiteral("greaterThanOrEqual"));
                }
                if (!rule.message.isEmpty()) {
                    p.attr("errorTitle", QStringLiteral("Hojas"));
                    p.attr("error", rule.message);
                }
                p.attr("sqref", Formula::areaName(rule.area.r1, rule.area.c1, rule.area.r2, rule.area.c2));
                if (list) {
                    p.text("formula1", QLatin1Char('"') + rule.items.join(QLatin1Char(',')).replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"'));
                } else {
                    const bool low = !rule.min.isEmpty(), high = !rule.max.isEmpty();
                    p.text("formula1", bound(low ? rule.min : high ? rule.max : QStringLiteral("0")));
                    if (low && high) {
                        p.text("formula2", bound(rule.max));
                    }
                }
                p.end();
            }
            p.end();
        }
        p.empty("pageMargins");
        p.attr("left", QStringLiteral("0.7"));
        p.attr("right", QStringLiteral("0.7"));
        p.attr("top", QStringLiteral("0.75"));
        p.attr("bottom", QStringLiteral("0.75"));
        p.attr("header", QStringLiteral("0.3"));
        p.attr("footer", QStringLiteral("0.3"));

        QList<QPair<QString, QString>> sheetRels;
        if (!sheet.charts.isEmpty()) {
            ++drawingCount;
            p.empty("drawing");
            p.xml.writeAttribute(QLatin1String(RelNs), QStringLiteral("id"), QStringLiteral("rId1"));
            sheetRels.append(qMakePair(QStringLiteral("drawing"), QStringLiteral("../drawings/drawing%1.xml").arg(drawingCount)));
            overrides.append(QStringLiteral("/xl/drawings/drawing%1.xml|application/vnd.openxmlformats-officedocument.drawing+xml").arg(drawingCount));
            Part d;
            d.xml.writeNamespace(QLatin1String(DrawNs), QStringLiteral("xdr"));
            d.xml.writeNamespace(QLatin1String(ANs), QStringLiteral("a"));
            d.xml.writeNamespace(QLatin1String(RelNs), QStringLiteral("r"));
            d.xml.writeNamespace(QLatin1String(ChartNs), QStringLiteral("c"));
            d.start("xdr:wsDr");
            QList<QPair<QString, QString>> drawingRels;
            for (const Chart &chart : sheet.charts) {
                ++chartCount;
                drawingRels.append(qMakePair(QStringLiteral("chart"), QStringLiteral("../charts/chart%1.xml").arg(chartCount)));
                extra.append(qMakePair(QStringLiteral("xl/charts/chart%1.xml").arg(chartCount), chartXml(book, index, chart)));
                overrides.append(QStringLiteral("/xl/charts/chart%1.xml|application/vnd.openxmlformats-officedocument.drawingml.chart+xml").arg(chartCount));
                d.start("xdr:oneCellAnchor");
                d.start("xdr:from");
                d.text("xdr:col", QString::number(chart.col));
                d.text("xdr:colOff", QStringLiteral("0"));
                d.text("xdr:row", QString::number(chart.row));
                d.text("xdr:rowOff", QStringLiteral("0"));
                d.end();
                d.empty("xdr:ext");
                d.attr("cx", QString::number(qint64(chart.width * 9525)));
                d.attr("cy", QString::number(qint64(chart.height * 9525)));
                d.start("xdr:graphicFrame");
                d.attr("macro", QString());
                d.start("xdr:nvGraphicFramePr");
                d.empty("xdr:cNvPr");
                d.attr("id", drawingRels.size() + 1);
                d.attr("name", QStringLiteral("Gráfico %1").arg(drawingRels.size()));
                d.empty("xdr:cNvGraphicFramePr");
                d.end();
                d.start("xdr:xfrm");
                d.empty("a:off");
                d.attr("x", 0);
                d.attr("y", 0);
                d.empty("a:ext");
                d.attr("cx", 0);
                d.attr("cy", 0);
                d.end();
                d.start("a:graphic");
                d.start("a:graphicData");
                d.attr("uri", QLatin1String(ChartNs));
                d.empty("c:chart");
                d.xml.writeAttribute(QLatin1String(RelNs), QStringLiteral("id"), QStringLiteral("rId%1").arg(drawingRels.size()));
                d.end();
                d.end();
                d.end();
                d.empty("xdr:clientData");
                d.end();
            }
            d.end();
            extra.append(qMakePair(QStringLiteral("xl/drawings/drawing%1.xml").arg(drawingCount), d.done()));
            extra.append(qMakePair(QStringLiteral("xl/drawings/_rels/drawing%1.xml.rels").arg(drawingCount), relsXml(drawingRels)));
        }
        if (!sheet.tables.isEmpty()) {
            p.start("tableParts");
            p.attr("count", sheet.tables.size());
            for (int t = 0; t < sheet.tables.size(); ++t) {
                const Table &table = sheet.tables.at(t);
                ++tableCount;
                sheetRels.append(qMakePair(QStringLiteral("table"), QStringLiteral("../tables/table%1.xml").arg(tableCount)));
                p.empty("tablePart");
                p.xml.writeAttribute(QLatin1String(RelNs), QStringLiteral("id"), QStringLiteral("rId%1").arg(sheetRels.size()));
                overrides.append(QStringLiteral("/xl/tables/table%1.xml|application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml").arg(tableCount));

                const Area &area = table.area;
                Part x;
                x.xml.writeDefaultNamespace(QLatin1String(MainNs));
                x.start("table");
                x.attr("id", tableCount);
                x.attr("name", QStringLiteral("Tabla%1").arg(tableCount));
                x.attr("displayName", QStringLiteral("Tabla%1").arg(tableCount));
                x.attr("ref", Formula::areaName(area.r1, area.c1, area.r2, area.c2));
                if (table.totals) {
                    x.attr("totalsRowCount", 1);
                } else {
                    x.attr("totalsRowShown", 0);
                }
                if (t == filterTable) {
                    x.empty("autoFilter");
                    x.attr("ref", Formula::areaName(area.r1, area.c1, area.r2 - (table.totals ? 1 : 0), area.c2));
                }
                x.start("tableColumns");
                x.attr("count", area.cols());
                // A column is named by its header, and no two alike.
                QSet<QString> taken;
                for (int c = area.c1; c <= area.c2; ++c) {
                    QString header = book.display(index, area.r1, c).simplified();
                    if (header.isEmpty()) {
                        header = QStringLiteral("Columna%1").arg(c - area.c1 + 1);
                    }
                    QString unique = header;
                    for (int n = 2; taken.contains(unique.toLower()); ++n) {
                        unique = header + QString::number(n);
                    }
                    taken.insert(unique.toLower());
                    x.empty("tableColumn");
                    x.attr("id", c - area.c1 + 1);
                    x.attr("name", unique);
                    if (table.totals) {
                        const QString total = book.input(index, area.r2, c);
                        if (total.startsWith(QLatin1String("=SUBTOTALES(109"))) {
                            x.attr("totalsRowFunction", QStringLiteral("sum"));
                        } else if (!total.isEmpty() && !total.startsWith(QLatin1Char('='))) {
                            x.attr("totalsRowLabel", total);
                        }
                    }
                }
                x.end();
                x.empty("tableStyleInfo");
                x.attr("name", QStringLiteral("TableStyleMedium7"));
                x.attr("showFirstColumn", 0);
                x.attr("showLastColumn", 0);
                x.attr("showRowStripes", 1);
                x.attr("showColumnStripes", 0);
                x.end();
                extra.append(qMakePair(QStringLiteral("xl/tables/table%1.xml").arg(tableCount), x.done()));
            }
            p.end();
        }
        if (!sheetRels.isEmpty()) {
            extra.append(qMakePair(QStringLiteral("xl/worksheets/_rels/sheet%1.xml.rels").arg(index + 1), relsXml(sheetRels)));
        }
        p.end();
        sheetParts.append(p.done());
    }

    // [Content_Types].xml
    {
        Part p;
        p.start("Types");
        p.attr("xmlns", QStringLiteral("http://schemas.openxmlformats.org/package/2006/content-types"));
        p.empty("Default");
        p.attr("Extension", QStringLiteral("rels"));
        p.attr("ContentType", QStringLiteral("application/vnd.openxmlformats-package.relationships+xml"));
        p.empty("Default");
        p.attr("Extension", QStringLiteral("xml"));
        p.attr("ContentType", QStringLiteral("application/xml"));
        const auto override = [&p](const QString &part, const QString &type) {
            p.empty("Override");
            p.attr("PartName", part);
            p.attr("ContentType", type);
        };
        override(QStringLiteral("/xl/workbook.xml"), QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"));
        for (int i = 0; i < sheetParts.size(); ++i) {
            override(QStringLiteral("/xl/worksheets/sheet%1.xml").arg(i + 1), QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"));
        }
        override(QStringLiteral("/xl/styles.xml"), QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"));
        override(QStringLiteral("/xl/sharedStrings.xml"), QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"));
        override(QStringLiteral("/docProps/app.xml"), QStringLiteral("application/vnd.openxmlformats-officedocument.extended-properties+xml"));
        for (const QString &entry : std::as_const(overrides)) {
            override(entry.section(QLatin1Char('|'), 0, 0), entry.section(QLatin1Char('|'), 1));
        }
        p.end();
        zip.add(QStringLiteral("[Content_Types].xml"), p.done());
    }
    zip.add(QStringLiteral("_rels/.rels"), relsXml({qMakePair(QStringLiteral("officeDocument"), QStringLiteral("xl/workbook.xml")),
                                                    qMakePair(QStringLiteral("extended-properties"), QStringLiteral("docProps/app.xml"))}));
    {
        Part p;
        p.start("Properties");
        p.attr("xmlns", QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/extended-properties"));
        p.text("Application", QStringLiteral("Wonderland Hojas"));
        p.end();
        zip.add(QStringLiteral("docProps/app.xml"), p.done());
    }
    {
        Part p;
        p.xml.writeDefaultNamespace(QLatin1String(MainNs));
        p.xml.writeNamespace(QLatin1String(RelNs), QStringLiteral("r"));
        p.start("workbook");
        p.start("bookViews");
        p.empty("workbookView");
        p.end();
        p.start("sheets");
        for (int i = 0; i < book.sheetCount(); ++i) {
            p.empty("sheet");
            p.attr("name", clean(book.sheetName(i)));
            p.attr("sheetId", i + 1);
            p.xml.writeAttribute(QLatin1String(RelNs), QStringLiteral("id"), QStringLiteral("rId%1").arg(i + 1));
        }
        p.end();
        // Excel names the filter's range; without it the arrows do not come back there.
        bool anyFilter = false;
        for (int i = 0; i < book.sheetCount(); ++i) {
            anyFilter = anyFilter || book.sheet(i).hasFilter;
        }
        if (anyFilter) {
            p.start("definedNames");
            for (int i = 0; i < book.sheetCount(); ++i) {
                if (book.sheet(i).hasFilter) {
                    const Area f = book.sheet(i).filterArea;
                    p.start("definedName");
                    p.attr("name", QStringLiteral("_xlnm._FilterDatabase"));
                    p.attr("localSheetId", i);
                    p.attr("hidden", 1);
                    p.xml.writeCharacters(absoluteRef(book.sheetName(i), f.r1, f.c1, f.r2, f.c2));
                    p.end();
                }
            }
            p.end();
        }
        p.empty("calcPr");
        p.attr("calcId", 191029);
        p.attr("fullCalcOnLoad", 1);
        p.end();
        zip.add(QStringLiteral("xl/workbook.xml"), p.done());
    }
    {
        QList<QPair<QString, QString>> rels;
        for (int i = 0; i < sheetParts.size(); ++i) {
            rels.append(qMakePair(QStringLiteral("worksheet"), QStringLiteral("worksheets/sheet%1.xml").arg(i + 1)));
        }
        rels.append(qMakePair(QStringLiteral("styles"), QStringLiteral("styles.xml")));
        rels.append(qMakePair(QStringLiteral("sharedStrings"), QStringLiteral("sharedStrings.xml")));
        zip.add(QStringLiteral("xl/_rels/workbook.xml.rels"), relsXml(rels));
    }
    for (int i = 0; i < sheetParts.size(); ++i) {
        zip.add(QStringLiteral("xl/worksheets/sheet%1.xml").arg(i + 1), sheetParts.at(i));
    }
    zip.add(QStringLiteral("xl/styles.xml"), styles.xml());
    {
        Part p;
        p.start("sst");
        p.attr("xmlns", QLatin1String(MainNs));
        p.attr("count", stringUses);
        p.attr("uniqueCount", strings.size());
        for (const QString &s : std::as_const(strings)) {
            p.start("si");
            p.text("t", s);
            p.end();
        }
        p.end();
        zip.add(QStringLiteral("xl/sharedStrings.xml"), p.done());
    }
    for (const auto &part : std::as_const(extra)) {
        zip.add(part.first, part.second);
    }
    return zip.finish();
}
