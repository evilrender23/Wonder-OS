#include "odp.h"

#include "render.h"
#include "zip.h"

#include <QPolygonF>

#include <cmath>

using namespace Model;

namespace {

const char *namespaces =
    " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
    " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
    " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""
    " xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\""
    " xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\""
    " xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\""
    " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
    " xmlns:dc=\"http://purl.org/dc/elements/1.1/\""
    " xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\""
    " xmlns:presentation=\"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0\""
    " xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\""
    " xmlns:smil=\"urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0\""
    " xmlns:loext=\"urn:org:documentfoundation:names:experimental:office:xmlns:loext:1.0\""
    " office:version=\"1.3\"";

QString esc(const QString &text)
{
    return text.toHtmlEscaped();
}

/// A length in points, as short as it can be written.
QString pt(double value)
{
    QString number = QString::number(value, 'f', 3);
    while (number.contains(QLatin1Char('.')) && (number.endsWith(QLatin1Char('0')) || number.endsWith(QLatin1Char('.')))) {
        const bool dot = number.endsWith(QLatin1Char('.'));
        number.chop(1);
        if (dot) {
            break;
        }
    }
    if (number == QLatin1String("-0")) {
        number = QStringLiteral("0");
    }
    return number + QStringLiteral("pt");
}

QString percent(double share)
{
    return QString::number(qRound(qBound(0.0, share, 1.0) * 100)) + QLatin1Char('%');
}

class Writer
{
public:
    explicit Writer(const Presentation &deck)
        : deck(deck), look(deck.look()) {}

    const Presentation &deck;
    const Look look;
    /// The automatic styles of content.xml, each written once.
    QString styles;
    QHash<QString, QString> known;
    QHash<QString, int> counters;

    /// The name of the style with this body, made the first time it is asked for.
    QString style(const char *prefix, const QString &family, const QString &body)
    {
        const QString key = family + QLatin1Char('|') + body;
        const auto found = known.constFind(key);
        if (found != known.constEnd()) {
            return *found;
        }
        const QString name = QLatin1String(prefix) + QString::number(++counters[QLatin1String(prefix)]);
        styles += QStringLiteral("<style:style style:name=\"%1\" style:family=\"%2\">%3</style:style>").arg(name, family, body);
        known.insert(key, name);
        return name;
    }

    QString hex(const QString &token, const QColor &fallback = QColor()) const
    {
        const QColor color = look.color(token);
        return (color.isValid() ? color : fallback).name();
    }

    /// The text of a run: spaces in a row, tabs and line breaks have elements of their own.
    static QString characters(const QString &text)
    {
        QString out;
        int spaces = 0;
        const auto flush = [&]() {
            if (spaces == 1) {
                out += QLatin1Char(' ');
            } else if (spaces > 1) {
                out += QStringLiteral(" <text:s text:c=\"%1\"/>").arg(spaces - 1);
            }
            spaces = 0;
        };
        QString plain;
        const auto flushPlain = [&]() {
            out += esc(plain);
            plain.clear();
        };
        for (const QChar c : text) {
            if (c == QLatin1Char(' ')) {
                flushPlain();
                ++spaces;
                continue;
            }
            flush();
            if (c == QLatin1Char('\n') || c == QChar::LineSeparator) {
                flushPlain();
                out += QStringLiteral("<text:line-break/>");
            } else if (c == QLatin1Char('\t')) {
                flushPlain();
                out += QStringLiteral("<text:tab/>");
            } else {
                plain += c;
            }
        }
        flushPlain();
        flush();
        return out;
    }

    QString span(const Run &run, const TextDefaults &d)
    {
        const bool bold = run.bold < 0 ? d.bold : run.bold > 0;
        const QString color = run.color.isEmpty() ? d.color.name() : hex(run.color, d.color);
        const QString font = run.font.isEmpty() ? d.font : run.font;
        const double size = run.size > 0 ? run.size : d.size;
        QString props = QStringLiteral("<style:text-properties fo:font-family=\"%1\" style:font-family-asian=\"%1\" style:font-family-complex=\"%1\""
                                       " fo:font-size=\"%2\" style:font-size-asian=\"%2\" style:font-size-complex=\"%2\" fo:color=\"%3\""
                                       " fo:font-weight=\"%4\" style:font-weight-asian=\"%4\" style:font-weight-complex=\"%4\""
                                       " fo:font-style=\"%5\" style:font-style-asian=\"%5\" style:font-style-complex=\"%5\"")
            .arg(esc(font), pt(size), color, bold ? QStringLiteral("bold") : QStringLiteral("normal"),
                 run.italic ? QStringLiteral("italic") : QStringLiteral("normal"));
        props += run.underline
            ? QStringLiteral(" style:text-underline-style=\"solid\" style:text-underline-width=\"auto\" style:text-underline-color=\"font-color\"")
            : QStringLiteral(" style:text-underline-style=\"none\"");
        props += run.strike ? QStringLiteral(" style:text-line-through-style=\"solid\" style:text-line-through-type=\"single\"")
                            : QStringLiteral(" style:text-line-through-style=\"none\"");
        props += QStringLiteral("/>");
        return QStringLiteral("<text:span text:style-name=\"%1\">%2</text:span>")
            .arg(style("T", QStringLiteral("text"), props), characters(run.text));
    }

    QString paragraphs(const Item &item, const TextDefaults &d)
    {
        static const char *aligns[] = {"start", "center", "end", "justify"};
        QString out;
        QList<Paragraph> all = item.text;
        if (all.isEmpty()) {
            all << Paragraph();
        }
        for (const Paragraph &paragraph : all) {
            const int align = qBound(0, paragraph.align < 0 ? d.align : paragraph.align, 3);
            const int level = qBound(0, paragraph.level, 8);
            const QString props = QStringLiteral("<style:paragraph-properties fo:text-align=\"%1\" fo:margin-left=\"%2\" fo:margin-right=\"0pt\""
                                                 " fo:text-indent=\"0pt\" fo:margin-top=\"0pt\" fo:margin-bottom=\"%3\" fo:line-height=\"100%\"/>")
                .arg(QLatin1String(aligns[align]), pt(0), pt(d.spacing));
            QString body;
            for (const Run &run : paragraph.runs) {
                if (!run.text.isEmpty()) {
                    body += span(run, d);
                }
            }
            if (body.isEmpty()) {
                // An empty paragraph is as tall as the text that would be typed in it.
                Run empty = paragraph.runs.isEmpty() ? Run() : paragraph.runs.last();
                empty.text.clear();
                body = span(empty, d);
            }
            const QString p = QStringLiteral("<text:p text:style-name=\"%1\">%2</text:p>").arg(style("P", QStringLiteral("paragraph"), props), body);
            if (!paragraph.bullet) {
                out += p;
                continue;
            }
            out += QStringLiteral("<text:list text:style-name=\"L1\">");
            for (int i = 0; i < level; ++i) {
                out += QStringLiteral("<text:list-item><text:list>");
            }
            out += QStringLiteral("<text:list-item>") + p + QStringLiteral("</text:list-item>");
            for (int i = 0; i < level; ++i) {
                out += QStringLiteral("</text:list></text:list-item>");
            }
            out += QStringLiteral("</text:list>");
        }
        return out;
    }

    /// Where a box is: by its corner, or turned about its centre.
    static QString place(const Item &item)
    {
        QString out = QStringLiteral(" svg:width=\"%1\" svg:height=\"%2\"").arg(pt(item.w), pt(item.h));
        if (qAbs(item.rotation) < 0.01) {
            return out + QStringLiteral(" svg:x=\"%1\" svg:y=\"%2\"").arg(pt(item.x), pt(item.y));
        }
        // OpenDocument turns the other way, about the corner at the origin, and then moves.
        const double angle = -item.rotation * M_PI / 180;
        const double tx = item.center().x() - (std::cos(angle) * item.w / 2 + std::sin(angle) * item.h / 2);
        const double ty = item.center().y() - (-std::sin(angle) * item.w / 2 + std::cos(angle) * item.h / 2);
        return out + QStringLiteral(" draw:transform=\"rotate (%1) translate (%2 %3)\"").arg(QString::number(angle, 'f', 6), pt(tx), pt(ty));
    }

    QString shadow(const Item &item) const
    {
        return item.shadow
            ? QStringLiteral(" draw:shadow=\"visible\" draw:shadow-offset-x=\"0pt\" draw:shadow-offset-y=\"4pt\" draw:shadow-color=\"#000000\""
                             " draw:shadow-opacity=\"25%\" loext:shadow-blur=\"8pt\"")
            : QStringLiteral(" draw:shadow=\"hidden\"");
    }

    QString graphic(const Item &item)
    {
        static const char *anchors[] = {"top", "middle", "bottom"};
        const bool line = item.type == QLatin1String("shape") && item.shape == QLatin1String("line");
        QString props = QStringLiteral("<style:graphic-properties");
        if (line) {
            props += QStringLiteral(" draw:stroke=\"solid\" svg:stroke-color=\"%1\" svg:stroke-width=\"%2\" draw:fill=\"none\"")
                .arg(hex(item.stroke, look.text), pt(qMax(0.5, item.strokeWidth)));
            if (item.arrow) {
                const double head = qMax(10.0, item.strokeWidth * 4) * 0.8;
                props += QStringLiteral(" draw:marker-end=\"Arrow\" draw:marker-end-width=\"%1\" draw:marker-end-center=\"false\"").arg(pt(head));
            }
        } else {
            const QColor fill = look.color(item.fill);
            const bool image = item.type == QLatin1String("image");
            props += fill.isValid() && !image ? QStringLiteral(" draw:fill=\"solid\" draw:fill-color=\"%1\"").arg(fill.name()) : QStringLiteral(" draw:fill=\"none\"");
            const QColor stroke = look.color(item.stroke);
            props += stroke.isValid() && item.strokeWidth > 0
                ? QStringLiteral(" draw:stroke=\"solid\" svg:stroke-color=\"%1\" svg:stroke-width=\"%2\" draw:stroke-linejoin=\"miter\"").arg(stroke.name(), pt(item.strokeWidth))
                : QStringLiteral(" draw:stroke=\"none\"");
            props += QStringLiteral(" draw:textarea-vertical-align=\"%1\" draw:textarea-horizontal-align=\"justify\""
                                    " draw:auto-grow-height=\"false\" draw:auto-grow-width=\"false\" draw:fit-to-size=\"false\" fo:wrap-option=\"wrap\""
                                    " fo:padding-left=\"%2\" fo:padding-right=\"%2\" fo:padding-top=\"%3\" fo:padding-bottom=\"%3\"")
                .arg(QLatin1String(anchors[qBound(0, item.valign, 2)]), pt(Render::insetX), pt(Render::insetY));
            if (image && item.opacity < 0.999) {
                props += QStringLiteral(" draw:image-opacity=\"%1\"").arg(percent(item.opacity));
            }
        }
        if (item.opacity < 0.999) {
            props += QStringLiteral(" draw:opacity=\"%1\"").arg(percent(item.opacity));
        }
        props += shadow(item) + QStringLiteral("/>");
        return style("gr", QStringLiteral("graphic"), props);
    }

    QString table(const Item &item)
    {
        const TextDefaults d = Model::textDefaults(item, look);
        QColor head = look.color(item.fill);
        if (!head.isValid()) {
            head = look.accents.value(0, QColor(0x0a, 0x7a, 0xff));
        }
        const double luma = 0.299 * head.redF() + 0.587 * head.greenF() + 0.114 * head.blueF();
        const QString headInk = luma < 0.6 ? QStringLiteral("#ffffff") : QStringLiteral("#1d1d1f");
        // The rules and the bands are the ink thinned over the paper.
        const auto mixed = [&](double share) {
            const QColor paper = QColor::fromRgbF((look.bg1.redF() + look.bg2.redF()) / 2, (look.bg1.greenF() + look.bg2.greenF()) / 2,
                                                  (look.bg1.blueF() + look.bg2.blueF()) / 2);
            return QColor::fromRgbF(float(paper.redF() + (look.text.redF() - paper.redF()) * share),
                                    float(paper.greenF() + (look.text.greenF() - paper.greenF()) * share),
                                    float(paper.blueF() + (look.text.blueF() - paper.blueF()) * share)).name();
        };
        const QString border = QStringLiteral("<style:paragraph-properties fo:border=\"0.75pt solid %1\"/>").arg(mixed(0.25));
        const auto cellStyle = [&](const QString &fill, bool band = false) {
            return style("ce", QStringLiteral("table-cell"),
                         QStringLiteral("<loext:graphic-properties %1 draw:textarea-vertical-align=\"middle\" fo:padding-top=\"0pt\" fo:padding-bottom=\"0pt\""
                                        " fo:padding-left=\"%2\" fo:padding-right=\"%2\"/>%3")
                             .arg(fill.isEmpty() ? QStringLiteral("draw:fill=\"none\"")
                                      : QStringLiteral("draw:fill=\"solid\" draw:fill-color=\"%1\"%2").arg(fill, band ? QStringLiteral(" draw:opacity=\"6%\"") : QString()),
                                  pt(Render::insetX), border));
        };
        const QString column = style("co", QStringLiteral("table-column"),
                                     QStringLiteral("<style:table-column-properties style:column-width=\"%1\" style:use-optimal-column-width=\"false\"/>").arg(pt(item.w / item.cols)));
        const QString rowStyle = style("ro", QStringLiteral("table-row"),
                                       QStringLiteral("<style:table-row-properties style:row-height=\"%1\" style:use-optimal-row-height=\"false\"/>").arg(pt(item.h / item.rows)));
        Item frame = item;
        frame.rotation = 0;
        Item plain;
        plain.type = QStringLiteral("table");
        plain.shadow = item.shadow;
        plain.opacity = item.opacity;
        QString out = QStringLiteral("<draw:frame draw:style-name=\"%1\" draw:layer=\"layout\"%2><table:table>").arg(graphic(plain), place(frame));
        for (int col = 0; col < item.cols; ++col) {
            out += QStringLiteral("<table:table-column table:style-name=\"%1\"/>").arg(column);
        }
        for (int row = 0; row < item.rows; ++row) {
            out += QStringLiteral("<table:table-row table:style-name=\"%1\">").arg(rowStyle);
            const QString cell = row == 0 ? cellStyle(head.name()) : row % 2 == 0 ? cellStyle(look.text.name(), true) : cellStyle(QString());
            for (int col = 0; col < item.cols; ++col) {
                Item holder;
                holder.type = QStringLiteral("table");
                Run run;
                run.text = item.cell(row, col);
                run.bold = row == 0 ? 1 : 0;
                run.color = row == 0 ? headInk : QString();
                Paragraph paragraph;
                paragraph.runs << run;
                holder.text << paragraph;
                TextDefaults plainDefaults = d;
                plainDefaults.spacing = 0;
                out += QStringLiteral("<table:table-cell table:style-name=\"%1\">%2</table:table-cell>").arg(cell, paragraphs(holder, plainDefaults));
            }
            out += QStringLiteral("</table:table-row>");
        }
        return out + QStringLiteral("</table:table></draw:frame>");
    }

    /// The outline the painter draws, as a path in hundredths of a point.
    static QString geometry(const Item &item)
    {
        static const QHash<QString, QString> names = {
            {QStringLiteral("triangle"), QStringLiteral("isosceles-triangle")}, {QStringLiteral("diamond"), QStringLiteral("diamond")},
            {QStringLiteral("arrow"), QStringLiteral("right-arrow")}, {QStringLiteral("star"), QStringLiteral("star5")},
        };
        Item plain = item;
        plain.w = qMax(1.0, item.w);
        plain.h = qMax(1.0, item.h);
        const QPolygonF polygon = Render::outline(plain).toFillPolygon();
        QString path;
        // The polygon comes closed: its last point is its first again.
        for (int i = 0; i + 1 < polygon.size(); ++i) {
            path += QStringLiteral("%1 %2 %3 ").arg(i == 0 ? QStringLiteral("M") : i == 1 ? QStringLiteral("L") : QString())
                .arg(qRound(polygon.at(i).x() * 100)).arg(qRound(polygon.at(i).y() * 100)).simplified() + QLatin1Char(' ');
        }
        return QStringLiteral("<draw:enhanced-geometry svg:viewBox=\"0 0 %1 %2\" draw:type=\"%3\" draw:mirror-horizontal=\"%4\" draw:mirror-vertical=\"%5\""
                              " draw:enhanced-path=\"%6Z N\"/>")
            .arg(qRound(plain.w * 100)).arg(qRound(plain.h * 100)).arg(names.value(item.shape, QStringLiteral("non-primitive")),
                 item.flipH ? QStringLiteral("true") : QStringLiteral("false"), item.flipV ? QStringLiteral("true") : QStringLiteral("false"), path);
    }

    QString item(const Item &item)
    {
        if (item.type == QLatin1String("table")) {
            return item.rows > 0 && item.cols > 0 ? table(item) : QString();
        }
        const QString gr = graphic(item);
        if (item.type == QLatin1String("shape") && item.shape == QLatin1String("line")) {
            // The ends as they are once the box is turned.
            QTransform turn;
            turn.translate(item.center().x(), item.center().y());
            turn.rotate(item.rotation);
            turn.translate(-item.center().x(), -item.center().y());
            const QLineF l = turn.map(item.line());
            return QStringLiteral("<draw:line draw:style-name=\"%1\" draw:layer=\"layout\" svg:x1=\"%2\" svg:y1=\"%3\" svg:x2=\"%4\" svg:y2=\"%5\"/>")
                .arg(gr, pt(l.x1()), pt(l.y1()), pt(l.x2()), pt(l.y2()));
        }
        if (item.type == QLatin1String("image")) {
            if (item.image.isEmpty() || !deck.media.contains(item.image)) {
                return {};
            }
            return QStringLiteral("<draw:frame draw:style-name=\"%1\" draw:layer=\"layout\"%2%3><draw:image xlink:href=\"Pictures/%4\" xlink:type=\"simple\""
                                  " xlink:show=\"embed\" xlink:actuate=\"onLoad\"/></draw:frame>")
                .arg(gr, name(item), place(item), esc(item.image));
        }
        const TextDefaults d = Model::textDefaults(item, look);
        if (item.type == QLatin1String("text")) {
            QString role;
            if (item.role == QLatin1String("ctitle") || item.role == QLatin1String("title")) role = QStringLiteral("title");
            else if (item.role == QLatin1String("subtitle")) role = QStringLiteral("subtitle");
            else if (item.role == QLatin1String("body")) role = QStringLiteral("outline");
            return QStringLiteral("<draw:frame draw:style-name=\"%1\" draw:layer=\"layout\"%2%3%4><draw:text-box>%5</draw:text-box></draw:frame>")
                .arg(gr, name(item), place(item),
                     role.isEmpty() ? QString() : QStringLiteral(" presentation:class=\"%1\" presentation:user-transformed=\"true\"").arg(role),
                     paragraphs(item, d));
        }
        const QString words = item.hasText() ? paragraphs(item, d) : QString();
        if (item.shape == QLatin1String("ellipse")) {
            return QStringLiteral("<draw:ellipse draw:style-name=\"%1\" draw:layer=\"layout\"%2>%3</draw:ellipse>").arg(gr, place(item), words);
        }
        if (item.shape == QLatin1String("rect") || item.shape == QLatin1String("rounded") || item.shape.isEmpty()) {
            const QString radius = item.shape == QLatin1String("rounded") ? QStringLiteral(" draw:corner-radius=\"%1\"").arg(pt(qMin(item.w, item.h) / 6)) : QString();
            return QStringLiteral("<draw:rect draw:style-name=\"%1\" draw:layer=\"layout\"%2%3>%4</draw:rect>").arg(gr, place(item), radius, words);
        }
        return QStringLiteral("<draw:custom-shape draw:style-name=\"%1\" draw:layer=\"layout\"%2>%3%4</draw:custom-shape>")
            .arg(gr, place(item), words, geometry(item));
    }

    /// What an item is on its layout, where the format has no word for it.
    static QString name(const Item &item)
    {
        return item.role == QLatin1String("ctitle") || item.role == QLatin1String("caption") || item.role == QLatin1String("picture")
            ? QStringLiteral(" draw:name=\"%1\"").arg(item.role) : QString();
    }

    QString page(const Slide &slide, int index)
    {
        QString props = QStringLiteral("<style:drawing-page-properties presentation:background-visible=\"true\" presentation:background-objects-visible=\"true\"");
        const QColor own = look.color(slide.background);
        if (own.isValid()) {
            props += QStringLiteral(" draw:fill=\"solid\" draw:fill-color=\"%1\"").arg(own.name());
        }
        if (slide.hidden) {
            props += QStringLiteral(" presentation:visibility=\"hidden\"");
        }
        if (slide.transition == QLatin1String("dissolve")) {
            props += QStringLiteral(" presentation:transition-speed=\"medium\" smil:type=\"fade\" smil:subtype=\"crossfade\"");
        } else if (slide.transition == QLatin1String("fade")) {
            props += QStringLiteral(" presentation:transition-speed=\"medium\" smil:type=\"fade\" smil:subtype=\"fadeOverColor\" smil:fadeColor=\"#000000\"");
        } else if (slide.transition == QLatin1String("push")) {
            props += QStringLiteral(" presentation:transition-speed=\"medium\" smil:type=\"pushWipe\" smil:subtype=\"fromRight\"");
        }
        props += QStringLiteral("/>");
        QString out = QStringLiteral("<draw:page draw:name=\"page%1\" draw:style-name=\"%2\" draw:master-page-name=\"Default\">")
            .arg(index + 1).arg(style("dp", QStringLiteral("drawing-page"), props));
        // The members of a group that lie together in the stack are written as one.
        for (int i = 0; i < slide.items.size(); ++i) {
            const QString group = slide.items.at(i).group;
            int end = i;
            while (!group.isEmpty() && end + 1 < slide.items.size() && slide.items.at(end + 1).group == group) {
                ++end;
            }
            if (end == i) {
                out += item(slide.items.at(i));
                continue;
            }
            out += QStringLiteral("<draw:g>");
            for (; i <= end; ++i) {
                out += item(slide.items.at(i));
            }
            --i;
            out += QStringLiteral("</draw:g>");
        }
        if (!slide.notes.isEmpty()) {
            Item notes;
            notes.type = QStringLiteral("text");
            notes.x = 60;
            notes.y = 380;
            notes.w = 475;
            notes.h = 340;
            notes.setPlainText(slide.notes);
            TextDefaults d;
            d.font = look.bodyFont;
            d.size = 14;
            d.color = QColor(0x1d, 0x1d, 0x1f);
            Item plain;
            plain.type = QStringLiteral("text");
            out += QStringLiteral("<presentation:notes draw:style-name=\"%1\"><draw:page-thumbnail draw:layer=\"layout\" svg:width=\"475pt\" svg:height=\"%2\""
                                  " svg:x=\"60pt\" svg:y=\"60pt\" draw:page-number=\"%3\" presentation:class=\"page\"/>"
                                  "<draw:frame draw:style-name=\"%4\" draw:layer=\"layout\"%5 presentation:class=\"notes\" presentation:user-transformed=\"true\">"
                                  "<draw:text-box>%6</draw:text-box></draw:frame></presentation:notes>")
                .arg(style("dp", QStringLiteral("drawing-page"), QStringLiteral("<style:drawing-page-properties presentation:display-header=\"false\"/>")),
                     pt(475 * deck.height / deck.width))
                .arg(index + 1).arg(graphic(plain), place(notes), paragraphs(notes, d));
        }
        return out + QStringLiteral("</draw:page>");
    }

    /// styles.xml: the paper, its background and what every slide carries.
    QString shared() const
    {
        const bool gradient = look.bg1 != look.bg2;
        QString out = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-styles%1><office:styles>").arg(QLatin1String(namespaces));
        if (gradient) {
            // Straight down: the first colour at the top.
            out += QStringLiteral("<draw:gradient draw:name=\"Fondo\" draw:style=\"linear\" draw:start-color=\"%1\" draw:end-color=\"%2\""
                                  " draw:start-intensity=\"100%\" draw:end-intensity=\"100%\" draw:angle=\"0deg\" draw:border=\"0%\"/>")
                .arg(look.bg1.name(), look.bg2.name());
        }
        out += QStringLiteral("<draw:marker draw:name=\"Arrow\" svg:viewBox=\"0 0 20 30\" svg:d=\"M10 0l-10 30h20z\"/>"
                              "<style:default-style style:family=\"graphic\"><style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\"/>"
                              "<style:text-properties fo:font-family=\"%1\" fo:font-size=\"18pt\" fo:color=\"%2\"/></style:default-style>"
                              "<style:style style:name=\"standard\" style:family=\"graphic\"/>"
                              "</office:styles><office:automatic-styles>"
                              "<style:page-layout style:name=\"PM1\"><style:page-layout-properties fo:margin-top=\"0pt\" fo:margin-bottom=\"0pt\""
                              " fo:margin-left=\"0pt\" fo:margin-right=\"0pt\" fo:page-width=\"%3\" fo:page-height=\"%4\" style:print-orientation=\"landscape\"/>"
                              "</style:page-layout>"
                              "<style:page-layout style:name=\"PM2\"><style:page-layout-properties fo:page-width=\"595pt\" fo:page-height=\"842pt\""
                              " style:print-orientation=\"portrait\"/></style:page-layout>"
                              "<style:style style:name=\"Mdp1\" style:family=\"drawing-page\"><style:drawing-page-properties draw:background-size=\"border\" %5/></style:style>"
                              "<style:style style:name=\"Mgr1\" style:family=\"graphic\"><style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\""
                              " draw:textarea-vertical-align=\"middle\" draw:auto-grow-height=\"false\" fo:padding-left=\"0pt\" fo:padding-right=\"0pt\"/></style:style>"
                              "<style:style style:name=\"MP1\" style:family=\"paragraph\"><style:paragraph-properties fo:text-align=\"start\"/></style:style>"
                              "<style:style style:name=\"MP2\" style:family=\"paragraph\"><style:paragraph-properties fo:text-align=\"end\"/></style:style>"
                              "<style:style style:name=\"MT1\" style:family=\"text\"><style:text-properties fo:font-family=\"%1\" fo:font-size=\"12pt\" fo:color=\"%6\"/></style:style>"
                              "</office:automatic-styles><office:master-styles>"
                              "<draw:layer-set><draw:layer draw:name=\"layout\"/><draw:layer draw:name=\"background\"/><draw:layer draw:name=\"backgroundobjects\"/>"
                              "<draw:layer draw:name=\"controls\"/><draw:layer draw:name=\"measurelines\"/></draw:layer-set>"
                              "<style:master-page style:name=\"Default\" style:page-layout-name=\"PM1\" draw:style-name=\"Mdp1\">")
            .arg(esc(look.bodyFont), look.text.name(), pt(deck.width), pt(deck.height),
                 gradient ? QStringLiteral("draw:fill=\"gradient\" draw:fill-gradient-name=\"Fondo\" draw:fill-color=\"%1\"").arg(look.bg1.name())
                          : QStringLiteral("draw:fill=\"solid\" draw:fill-color=\"%1\"").arg(look.bg1.name()),
                 // The painter thins this ink to six tenths over the paper.
                 QColor::fromRgbF(float(look.bg2.redF() + (look.text.redF() - look.bg2.redF()) * 0.6),
                                  float(look.bg2.greenF() + (look.text.greenF() - look.bg2.greenF()) * 0.6),
                                  float(look.bg2.blueF() + (look.text.blueF() - look.bg2.blueF()) * 0.6)).name());
        const QString strip = QStringLiteral(" svg:width=\"%1\" svg:height=\"20pt\" svg:x=\"%2\" svg:y=\"%3\"").arg(pt(deck.width * 0.44), QStringLiteral("%1"), pt(deck.height - 34));
        if (!deck.master.footer.isEmpty()) {
            out += QStringLiteral("<draw:frame draw:style-name=\"Mgr1\" draw:layer=\"backgroundobjects\"%1><draw:text-box><text:p text:style-name=\"MP1\">"
                                  "<text:span text:style-name=\"MT1\">%2</text:span></text:p></draw:text-box></draw:frame>")
                .arg(strip.arg(pt(deck.width * 0.06)), esc(deck.master.footer));
        }
        if (deck.master.numbers) {
            out += QStringLiteral("<draw:frame draw:style-name=\"Mgr1\" draw:layer=\"backgroundobjects\"%1><draw:text-box><text:p text:style-name=\"MP2\">"
                                  "<text:span text:style-name=\"MT1\"><text:page-number>&lt;número&gt;</text:page-number></text:span></text:p></draw:text-box></draw:frame>")
                .arg(strip.arg(pt(deck.width * 0.5)));
        }
        out += QStringLiteral("<presentation:notes style:page-layout-name=\"PM2\"/></style:master-page></office:master-styles></office:document-styles>");
        return out;
    }
};

QString mediaType(const QString &name)
{
    const QString suffix = name.section(QLatin1Char('.'), -1).toLower();
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")) return QStringLiteral("image/jpeg");
    if (suffix == QLatin1String("svg")) return QStringLiteral("image/svg+xml");
    return QStringLiteral("image/") + suffix;
}

}

QByteArray Odp::write(const Presentation &deck)
{
    Writer w(deck);
    QString pages;
    QStringList used;
    for (int i = 0; i < deck.slides.size(); ++i) {
        pages += w.page(deck.slides.at(i), i);
        for (const Item &item : deck.slides.at(i).items) {
            if (item.type == QLatin1String("image") && deck.media.contains(item.image) && !used.contains(item.image)) {
                used << item.image;
            }
        }
    }
    QString list = QStringLiteral("<text:list-style style:name=\"L1\">");
    for (int level = 1; level <= 10; ++level) {
        // As the painter sets them: the text forty points in for each level, the bullet just before it.
        list += QStringLiteral("<text:list-level-style-bullet text:level=\"%1\" text:bullet-char=\"%2\"><style:list-level-properties text:space-before=\"%3\""
                               " text:min-label-width=\"20pt\"/><style:text-properties fo:font-family=\"Arial\" style:use-window-font-color=\"true\""
                               " fo:font-size=\"100%\"/></text:list-level-style-bullet>")
            .arg(level).arg(level % 2 ? QStringLiteral("•") : QStringLiteral("◦"), pt((level - 1) * 40.0 + 20));
    }
    list += QStringLiteral("</text:list-style>");
    const QString content = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-content%1><office:automatic-styles>%2%3"
                                           "</office:automatic-styles><office:body><office:presentation>%4</office:presentation></office:body>"
                                           "</office:document-content>")
        .arg(QLatin1String(namespaces), w.styles, list, pages);

    // What the format has no place for, kept so that the deck comes back as it was.
    const auto own = [](const char *name, const QString &value) {
        return value.isEmpty() ? QString() : QStringLiteral("<meta:user-defined meta:name=\"%1\">%2</meta:user-defined>").arg(QLatin1String(name), esc(value));
    };
    const QString meta = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-meta%1><office:meta>"
                                        "<meta:generator>Wonderland Diapositivas</meta:generator>%2</office:meta></office:document-meta>")
        .arg(QLatin1String(namespaces),
             own("WonderlandTheme", deck.theme) + own("WonderlandFooter", deck.master.footer) + own("WonderlandNumbers", deck.master.numbers ? QStringLiteral("1") : QString())
                 + own("WonderlandBackground", deck.master.background) + own("WonderlandTitleFont", deck.master.titleFont) + own("WonderlandBodyFont", deck.master.bodyFont)
                 + own("WonderlandTitleColor", deck.master.titleColor) + own("WonderlandTextColor", deck.master.textColor));

    QString manifest = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\""
                                      " manifest:version=\"1.3\"><manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.3\""
                                      " manifest:media-type=\"application/vnd.oasis.opendocument.presentation\"/>"
                                      "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
                                      "<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>"
                                      "<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>");
    for (const QString &key : std::as_const(used)) {
        manifest += QStringLiteral("<manifest:file-entry manifest:full-path=\"Pictures/%1\" manifest:media-type=\"%2\"/>").arg(esc(key), mediaType(key));
    }
    manifest += QStringLiteral("</manifest:manifest>");

    ZipWriter zip;
    // First and as it is: how the archive says what it holds.
    zip.add(QStringLiteral("mimetype"), QByteArrayLiteral("application/vnd.oasis.opendocument.presentation"), false);
    zip.add(QStringLiteral("content.xml"), content.toUtf8());
    zip.add(QStringLiteral("styles.xml"), w.shared().toUtf8());
    zip.add(QStringLiteral("meta.xml"), meta.toUtf8());
    for (const QString &key : std::as_const(used)) {
        zip.add(QStringLiteral("Pictures/") + key, deck.media.value(key), false);
    }
    zip.add(QStringLiteral("META-INF/manifest.xml"), manifest.toUtf8());
    return zip.finish();
}
