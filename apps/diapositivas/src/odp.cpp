#include "odp.h"

#include "xmlutil.h"
#include "zip.h"

#include <QRegularExpression>

#include <cmath>

using namespace Model;
using namespace Xml;

namespace {

/// "2.54cm", "1in", "12pt" → points.
double length(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("^\\s*(-?[0-9.]+(?:e-?[0-9]+)?)\\s*([a-z%]*)"));
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch()) {
        return 0;
    }
    const double value = match.captured(1).toDouble();
    const QString unit = match.captured(2);
    if (unit == QLatin1String("cm")) return value * 72 / 2.54;
    if (unit == QLatin1String("mm")) return value * 72 / 25.4;
    if (unit == QLatin1String("in")) return value * 72;
    if (unit == QLatin1String("px")) return value * 0.75;
    if (unit == QLatin1String("pc")) return value * 12;
    return value;
}

class Reader
{
public:
    Reader(const ZipReader &zip, Presentation &deck)
        : zip(zip), deck(deck) {}

    const ZipReader &zip;
    Presentation &deck;
    QHash<QString, QDomElement> styles;
    Look look;
    /// Written by Diapositivas itself: the names of the frames say what they are on the layout.
    bool ours = false;

    void collect(const QDomElement &holder)
    {
        for (const QDomElement &style : kids(holder, "style")) {
            styles.insert(attr(style, "family") + QLatin1Char('/') + attr(style, "name"), style);
        }
    }

    /// A property of a style, or of the nearest of its parents that has it.
    QString property(const QString &family, QString name, const char *group, const char *key) const
    {
        for (int depth = 0; depth < 12 && !name.isEmpty(); ++depth) {
            const QDomElement style = styles.value(family + QLatin1Char('/') + name);
            if (style.isNull()) {
                break;
            }
            const QString value = attr(kid(style, group), key);
            if (!value.isEmpty()) {
                return value;
            }
            name = attr(style, "parent-style-name");
        }
        return {};
    }

    void format(Run &run, const QString &family, const QString &style) const
    {
        const auto get = [&](const char *key) {
            return property(family, style, "text-properties", key);
        };
        const QString size = get("font-size");
        if (size.endsWith(QLatin1String("pt"))) run.size = length(size);
        const QString color = get("color");
        if (color.startsWith(QLatin1Char('#'))) run.color = color.toLower();
        const QString weight = get("font-weight");
        if (!weight.isEmpty()) run.bold = weight == QLatin1String("bold") || weight.toInt() >= 600;
        const QString slant = get("font-style");
        if (!slant.isEmpty()) run.italic = slant == QLatin1String("italic") || slant == QLatin1String("oblique");
        const QString under = get("text-underline-style");
        if (!under.isEmpty()) run.underline = under != QLatin1String("none");
        const QString through = get("text-line-through-style");
        if (!through.isEmpty()) run.strike = through != QLatin1String("none");
        QString font = get("font-family");
        if (font.isEmpty()) font = get("font-name");
        if (!font.isEmpty()) run.font = font.remove(QLatin1Char('\'')).section(QLatin1Char(','), 0, 0).trimmed();
    }

    void inlines(const QDomNode &node, const Run &pattern, Paragraph &paragraph) const
    {
        for (QDomNode n = node.firstChild(); !n.isNull(); n = n.nextSibling()) {
            if (n.isText()) {
                Run run = pattern;
                run.text = n.nodeValue();
                paragraph.runs << run;
                continue;
            }
            const QDomElement e = n.toElement();
            const QString kind = e.localName();
            Run run = pattern;
            if (kind == QLatin1String("span") || kind == QLatin1String("a")) {
                format(run, QStringLiteral("text"), attr(e, "style-name"));
                inlines(e, run, paragraph);
            } else if (kind == QLatin1String("s")) {
                run.text = QString(qMax(1, attr(e, "c", QStringLiteral("1")).toInt()), QLatin1Char(' '));
                paragraph.runs << run;
            } else if (kind == QLatin1String("tab")) {
                run.text = QStringLiteral("\t");
                paragraph.runs << run;
            } else if (kind == QLatin1String("line-break")) {
                run.text = QString(QChar::LineSeparator);
                paragraph.runs << run;
            }
        }
    }

    void blocks(const QDomElement &holder, const Run &pattern, bool bullet, int level, QList<Paragraph> &out) const
    {
        for (const QDomElement &e : kids(holder)) {
            const QString kind = e.localName();
            if (kind == QLatin1String("p") || kind == QLatin1String("h")) {
                Paragraph paragraph;
                paragraph.bullet = bullet;
                paragraph.level = level;
                const QString style = attr(e, "style-name");
                const QString align = property(QStringLiteral("paragraph"), style, "paragraph-properties", "text-align");
                paragraph.align = align == QLatin1String("center") ? 1 : align == QLatin1String("end") || align == QLatin1String("right") ? 2
                    : align == QLatin1String("justify") ? 3 : align == QLatin1String("start") || align == QLatin1String("left") ? 0 : -1;
                Run run = pattern;
                format(run, QStringLiteral("paragraph"), style);
                inlines(e, run, paragraph);
                out << paragraph;
            } else if (kind == QLatin1String("list")) {
                for (const QDomElement &entry : kids(e)) {
                    blocks(entry, pattern, true, bullet ? level + 1 : level, out);
                }
            }
        }
    }

    void settle(Item &item) const
    {
        const TextDefaults d = Model::textDefaults(item, look);
        for (Paragraph &paragraph : item.text) {
            if (paragraph.align == d.align) {
                paragraph.align = -1;
            }
            QList<Run> runs;
            for (Run run : std::as_const(paragraph.runs)) {
                if (run.text.isEmpty()) {
                    continue;
                }
                if (run.font == d.font) run.font.clear();
                if (qAbs(run.size - d.size) < 0.01) run.size = 0;
                if (run.bold == (d.bold ? 1 : 0)) run.bold = -1;
                if (!run.color.isEmpty() && QColor::fromString(run.color).rgb() == d.color.rgb()) run.color.clear();
                if (!runs.isEmpty() && runs.last().sameFormat(run)) {
                    runs.last().text += run.text;
                } else {
                    runs << run;
                }
            }
            paragraph.runs = runs;
        }
    }

    bool place(const QDomElement &e, Item &item) const
    {
        item.w = length(attr(e, "width"));
        item.h = length(attr(e, "height"));
        item.x = length(attr(e, "x"));
        item.y = length(attr(e, "y"));
        const QString transform = attr(e, "transform");
        if (!transform.isEmpty()) {
            // The box, with a corner at the origin, is turned and then moved.
            static const QRegularExpression turn(QStringLiteral("rotate\\s*\\(\\s*(-?[0-9.e-]+)"));
            static const QRegularExpression move(QStringLiteral("translate\\s*\\(\\s*(\\S+?)[\\s,]+(\\S+?)\\s*\\)"));
            const double angle = turn.match(transform).captured(1).toDouble();
            const QRegularExpressionMatch moved = move.match(transform);
            const double tx = length(moved.captured(1));
            const double ty = length(moved.captured(2));
            const double cx = std::cos(angle) * item.w / 2 + std::sin(angle) * item.h / 2 + tx;
            const double cy = -std::sin(angle) * item.w / 2 + std::cos(angle) * item.h / 2 + ty;
            item.x = cx - item.w / 2;
            item.y = cy - item.h / 2;
            item.rotation = std::fmod(360 - angle * 180 / M_PI + 720, 360);
        }
        return item.w > 0 || item.h > 0;
    }

    void graphic(const QString &style, Item &item, bool filled) const
    {
        const auto get = [&](const char *key) {
            QString value = property(QStringLiteral("graphic"), style, "graphic-properties", key);
            if (value.isEmpty()) value = property(QStringLiteral("presentation"), style, "graphic-properties", key);
            return value;
        };
        const QString fill = get("fill");
        if (fill == QLatin1String("solid") || (fill.isEmpty() && filled)) {
            const QString color = get("fill-color");
            item.fill = color.isEmpty() ? (filled ? QStringLiteral("#729fcf") : QString()) : color.toLower();
        }
        const QString stroke = get("stroke");
        if (stroke == QLatin1String("solid") || stroke == QLatin1String("dash") || (stroke.isEmpty() && filled)) {
            const QString color = get("stroke-color");
            item.stroke = color.isEmpty() ? QStringLiteral("#3465a4") : color.toLower();
            item.strokeWidth = qMax(1.0, length(get("stroke-width")));
        }
        const QString opacity = get("opacity");
        if (opacity.endsWith(QLatin1Char('%'))) item.opacity = qBound(0.0, opacity.chopped(1).toDouble() / 100, 1.0);
        item.shadow = get("shadow") == QLatin1String("visible");
        const QString anchor = get("textarea-vertical-align");
        if (anchor == QLatin1String("middle")) item.valign = 1;
        else if (anchor == QLatin1String("bottom")) item.valign = 2;
        else if (anchor == QLatin1String("top")) item.valign = 0;
        if (!get("marker-end").isEmpty() || !get("marker-start").isEmpty()) item.arrow = true;
    }

    void text(const QDomElement &holder, const QString &style, Item &item) const
    {
        Run pattern;
        format(pattern, QStringLiteral("graphic"), style);
        format(pattern, QStringLiteral("presentation"), style);
        blocks(holder, pattern, false, 0, item.text);
        settle(item);
    }

    void shapes(const QDomElement &holder, const QString &group, QList<Item> &out, QString *notes)
    {
        for (const QDomElement &e : kids(holder)) {
            const QString kind = e.localName();
            Item item;
            item.id = newId();
            item.group = group;
            const QString style = attr(e, "style-name");
            if (kind == QLatin1String("g")) {
                QList<Item> members;
                shapes(e, group.isEmpty() ? newId() : group, members, nullptr);
                out << members;
            } else if (kind == QLatin1String("notes") && notes) {
                for (const QDomElement &frame : kids(e, "frame")) {
                    if (attr(frame, "class") == QLatin1String("notes")) {
                        Item holderItem;
                        blocks(kid(frame, "text-box"), Run(), false, 0, holderItem.text);
                        *notes = holderItem.plainText().trimmed();
                    }
                }
            } else if (kind == QLatin1String("frame")) {
                if (!place(e, item)) {
                    continue;
                }
                const QString role = attr(e, "class");
                const QDomElement box = kid(e, "text-box");
                const QDomElement image = kid(e, "image");
                const QDomElement table = kid(e, "table");
                // A table carries a picture of itself for readers that cannot draw it: the table wins.
                if (!image.isNull() && table.isNull()) {
                    const QString href = attr(image, "href");
                    const QByteArray bytes = zip.file(href);
                    if (bytes.isEmpty()) {
                        continue;
                    }
                    item.type = QStringLiteral("image");
                    item.image = deck.addMedia(bytes, href.section(QLatin1Char('.'), -1));
                    graphic(style, item, false);
                    item.valign = 0;
                    const QString faded = property(QStringLiteral("graphic"), style, "graphic-properties", "image-opacity");
                    if (faded.endsWith(QLatin1Char('%'))) item.opacity = qBound(0.0, faded.chopped(1).toDouble() / 100, 1.0);
                    const QString name = attr(e, "name");
                    if (ours && (name == QLatin1String("picture"))) item.role = name;
                } else if (!table.isNull()) {
                    item.type = QStringLiteral("table");
                    const QList<QDomElement> rows = kids(table, "table-row");
                    item.rows = int(rows.size());
                    for (const QDomElement &row : rows) {
                        item.cols = qMax(item.cols, int(kids(row, "table-cell").size()));
                    }
                    if (item.rows < 1 || item.cols < 1) {
                        continue;
                    }
                    for (const QDomElement &row : rows) {
                        const QList<QDomElement> cells = kids(row, "table-cell");
                        for (int col = 0; col < item.cols; ++col) {
                            Item cell;
                            if (col < cells.size()) {
                                blocks(cells.at(col), Run(), false, 0, cell.text);
                            }
                            item.cells << cell.plainText();
                        }
                    }
                    item.rotation = 0;
                    item.shadow = property(QStringLiteral("graphic"), style, "graphic-properties", "shadow") == QLatin1String("visible");
                    // The colour of the heading row, when it is not the theme's own.
                    const QDomElement first = kid(rows.first(), "table-cell");
                    QString cellStyle = attr(first, "style-name");
                    if (cellStyle.isEmpty()) cellStyle = attr(rows.first(), "default-cell-style-name");
                    const QString head = property(QStringLiteral("table-cell"), cellStyle, "graphic-properties", "fill-color").toLower();
                    if (!head.isEmpty() && QColor::fromString(head).rgb() != look.accents.value(0).rgb()) item.fill = head;
                } else if (!box.isNull()) {
                    item.type = QStringLiteral("text");
                    if (role == QLatin1String("title")) item.role = QStringLiteral("title");
                    else if (role == QLatin1String("subtitle")) item.role = QStringLiteral("subtitle");
                    else if (role == QLatin1String("outline")) item.role = QStringLiteral("body");
                    const QString name = attr(e, "name");
                    if (ours && (name == QLatin1String("ctitle") || name == QLatin1String("caption"))) item.role = name;
                    if (item.role == QLatin1String("title")) item.valign = 1;
                    graphic(style, item, false);
                    text(box, style, item);
                    if (item.text.isEmpty()) {
                        item.text << Paragraph();
                    }
                } else {
                    continue;
                }
                out << item;
            } else if (kind == QLatin1String("custom-shape") || kind == QLatin1String("rect") || kind == QLatin1String("ellipse")) {
                if (!place(e, item)) {
                    continue;
                }
                item.type = QStringLiteral("shape");
                item.valign = 1;
                // Shapes that came from a PowerPoint file keep its names: "ooxml-rightArrow".
                QString type = (kind == QLatin1String("custom-shape") ? attr(kid(e, "enhanced-geometry"), "type") : kind).toLower();
                if (type.startsWith(QLatin1String("ooxml-"))) {
                    type.remove(0, 6);
                }
                type.remove(QLatin1Char('-'));
                item.shape = type == QLatin1String("ellipse") ? QStringLiteral("ellipse")
                    : type.contains(QLatin1String("round")) ? QStringLiteral("rounded")
                    : type.contains(QLatin1String("triangle")) ? QStringLiteral("triangle")
                    : type == QLatin1String("diamond") ? QStringLiteral("diamond")
                    : type.contains(QLatin1String("arrow")) ? QStringLiteral("arrow")
                    : type.startsWith(QLatin1String("star")) ? QStringLiteral("star") : QStringLiteral("rect");
                if (type == QLatin1String("leftarrow")) item.flipH = true;
                if (kind == QLatin1String("rect") && length(attr(e, "corner-radius")) > 0) item.shape = QStringLiteral("rounded");
                if (attr(kid(e, "enhanced-geometry"), "mirror-horizontal") == QLatin1String("true")) item.flipH = !item.flipH;
                if (attr(kid(e, "enhanced-geometry"), "mirror-vertical") == QLatin1String("true")) item.flipV = true;
                graphic(style, item, true);
                text(e, style, item);
                if (!item.hasText()) {
                    item.text.clear();
                } else if (item.shape == QLatin1String("rect") && item.fill.isEmpty() && (item.stroke.isEmpty() || item.strokeWidth <= 0)) {
                    // A bare rectangle with words in it is what a text box turns into.
                    item.type = QStringLiteral("text");
                    item.shape.clear();
                    item.valign = 0;
                }
                out << item;
            } else if (kind == QLatin1String("line") || kind == QLatin1String("connector")) {
                const double x1 = length(attr(e, "x1"));
                const double y1 = length(attr(e, "y1"));
                const double x2 = length(attr(e, "x2"));
                const double y2 = length(attr(e, "y2"));
                item.type = QStringLiteral("shape");
                item.shape = QStringLiteral("line");
                item.x = qMin(x1, x2);
                item.y = qMin(y1, y2);
                item.w = qAbs(x2 - x1);
                item.h = qAbs(y2 - y1);
                item.flipH = x2 < x1;
                item.flipV = y2 < y1;
                graphic(style, item, true);
                item.fill.clear();
                out << item;
            }
        }
    }
};

}

bool Odp::read(const QByteArray &bytes, Presentation &deck, QString *error)
{
    const auto fail = [&](const QString &why) {
        if (error) {
            *error = why;
        }
        return false;
    };
    const ZipReader zip(bytes);
    if (!zip.isValid() || !zip.contains(QStringLiteral("content.xml"))) {
        return fail(QStringLiteral("no es un archivo .odp"));
    }
    Presentation loaded;
    Reader r(zip, loaded);
    const QDomDocument contentDoc = parse(zip.file(QStringLiteral("content.xml")));
    const QDomDocument stylesDoc = parse(zip.file(QStringLiteral("styles.xml")));
    const QDomElement content = contentDoc.documentElement();
    const QDomElement shared = stylesDoc.documentElement();
    r.collect(kid(shared, "styles"));
    r.collect(kid(shared, "automatic-styles"));
    r.collect(kid(content, "automatic-styles"));
    const QDomElement body = kid(content, {"body", "presentation"});
    if (body.isNull()) {
        return fail(QStringLiteral("no contiene una presentación"));
    }
    // The size of the page the first master uses.
    QHash<QString, QDomElement> pageLayouts;
    for (const QDomElement &layout : kids(kid(shared, "automatic-styles"), "page-layout")) {
        pageLayouts.insert(attr(layout, "name"), layout);
    }
    QHash<QString, QDomElement> masters;
    for (const QDomElement &master : kids(kid(shared, "master-styles"), "master-page")) {
        masters.insert(attr(master, "name"), master);
    }
    const QList<QDomElement> pages = kids(body, "page");
    if (pages.isEmpty()) {
        return fail(QStringLiteral("no tiene diapositivas"));
    }
    const QDomElement firstMaster = masters.value(attr(pages.first(), "master-page-name"));
    const QDomElement pageProps = kid(pageLayouts.value(attr(firstMaster, "page-layout-name")), "page-layout-properties");
    const double width = length(attr(pageProps, "page-width"));
    const double height = length(attr(pageProps, "page-height"));
    if (width > 10 && height > 10) {
        loaded.width = width;
        loaded.height = height;
    }
    // The background the master page paints decides which of the plain themes it becomes.
    const auto pageColor = [&](const QString &style) {
        const QString fill = r.property(QStringLiteral("drawing-page"), style, "drawing-page-properties", "fill");
        if (fill == QLatin1String("gradient")) {
            // A background that fades is taken for the colour it starts from.
            const QString name = r.property(QStringLiteral("drawing-page"), style, "drawing-page-properties", "fill-gradient-name");
            for (const QDomElement &gradient : kids(kid(shared, "styles"), "gradient")) {
                if (attr(gradient, "name") == name) {
                    return attr(gradient, "start-color").toLower();
                }
            }
        }
        if (fill != QLatin1String("solid")) {
            return QString();
        }
        return r.property(QStringLiteral("drawing-page"), style, "drawing-page-properties", "fill-color").toLower();
    };
    const QString paper = pageColor(attr(firstMaster, "style-name"));
    const QColor paperColor = QColor::fromString(paper.isEmpty() ? QStringLiteral("#ffffff") : paper);
    const double luma = 0.299 * paperColor.redF() + 0.587 * paperColor.greenF() + 0.114 * paperColor.blueF();
    loaded.theme = luma > 0.5 ? QStringLiteral("blanco") : QStringLiteral("negro");
    if (paperColor.rgb() != QColor::fromString(Model::theme(loaded.theme).bg1).rgb()) {
        loaded.master.background = paperColor.name();
    }
    // What Diapositivas wrote down of its own: the theme and the master come back as they were.
    QHash<QString, QString> kept;
    if (zip.contains(QStringLiteral("meta.xml"))) {
        QList<QDomElement> entries;
        all(parse(zip.file(QStringLiteral("meta.xml"))).documentElement(), "user-defined", entries);
        for (const QDomElement &entry : std::as_const(entries)) {
            kept.insert(attr(entry, "name"), entry.text());
        }
    }
    const QString ourTheme = kept.value(QStringLiteral("WonderlandTheme"));
    if (!ourTheme.isEmpty() && Model::theme(ourTheme).id == ourTheme) {
        r.ours = true;
        loaded.theme = ourTheme;
        loaded.master = Master();
        loaded.master.background = kept.value(QStringLiteral("WonderlandBackground"));
        loaded.master.titleFont = kept.value(QStringLiteral("WonderlandTitleFont"));
        loaded.master.bodyFont = kept.value(QStringLiteral("WonderlandBodyFont"));
        loaded.master.titleColor = kept.value(QStringLiteral("WonderlandTitleColor"));
        loaded.master.textColor = kept.value(QStringLiteral("WonderlandTextColor"));
        loaded.master.footer = kept.value(QStringLiteral("WonderlandFooter"));
        loaded.master.numbers = kept.value(QStringLiteral("WonderlandNumbers")) == QLatin1String("1");
    }
    r.look = loaded.look();

    for (const QDomElement &page : pages) {
        Slide slide;
        slide.id = newId();
        const QString style = attr(page, "style-name");
        const QString own = pageColor(style);
        if (!own.isEmpty() && QColor::fromString(own).rgb() != paperColor.rgb()) {
            slide.background = own;
        }
        const QString wipe = r.property(QStringLiteral("drawing-page"), style, "drawing-page-properties", "type");
        const QString how = r.property(QStringLiteral("drawing-page"), style, "drawing-page-properties", "subtype");
        if (wipe == QLatin1String("fade")) {
            slide.transition = how == QLatin1String("crossfade") ? QStringLiteral("dissolve") : QStringLiteral("fade");
        } else if (wipe == QLatin1String("pushWipe") || wipe == QLatin1String("slideWipe")) {
            slide.transition = QStringLiteral("push");
        } else if (!wipe.isEmpty() && wipe != QLatin1String("none")) {
            slide.transition = QStringLiteral("dissolve");
        }
        slide.hidden = r.property(QStringLiteral("drawing-page"), style, "drawing-page-properties", "visibility") == QLatin1String("hidden");
        r.shapes(page, QString(), slide.items, &slide.notes);
        bool title = false;
        bool text = false;
        for (const Item &item : std::as_const(slide.items)) {
            title = title || item.role == QLatin1String("title") || item.role == QLatin1String("ctitle");
            text = text || item.role == QLatin1String("body") || item.role == QLatin1String("subtitle");
        }
        slide.layout = title && text ? QStringLiteral("content") : title ? QStringLiteral("titleOnly") : QStringLiteral("blank");
        loaded.slides << slide;
    }
    if (qAbs(loaded.height - 540) > 0.01) {
        const double k = 540 / loaded.height;
        for (Slide &slide : loaded.slides) {
            for (Item &item : slide.items) {
                item.x *= k;
                item.y *= k;
                item.w *= k;
                item.h *= k;
                item.strokeWidth *= k;
                for (Paragraph &paragraph : item.text) {
                    for (Run &run : paragraph.runs) {
                        run.size *= k;
                    }
                }
            }
        }
        loaded.width *= k;
        loaded.height = 540;
    }
    deck = loaded;
    return true;
}
