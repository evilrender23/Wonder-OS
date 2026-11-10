#include "pptx.h"

#include "xmlutil.h"
#include "zip.h"

#include <QTransform>

#include <cmath>

using namespace Model;
using namespace Xml;

namespace {

double points(const QString &emu)
{
    return emu.toDouble() / 12700.0;
}

/// "ppt/slides/slide1.xml" + "../media/a.png" → "ppt/media/a.png".
QString resolve(const QString &part, const QString &target)
{
    if (target.startsWith(QLatin1Char('/'))) {
        return target.mid(1);
    }
    QStringList path = part.split(QLatin1Char('/'));
    path.removeLast();
    for (const QString &piece : target.split(QLatin1Char('/'))) {
        if (piece == QLatin1String("..")) {
            if (!path.isEmpty()) {
                path.removeLast();
            }
        } else if (piece != QLatin1String(".") && !piece.isEmpty()) {
            path << piece;
        }
    }
    return path.join(QLatin1Char('/'));
}

struct Rels {
    QHash<QString, QString> byId;
    QHash<QString, QStringList> byType;
};

struct Placeholder {
    QString type;
    QString idx;
    QDomElement element;
};

class Reader
{
public:
    Reader(const ZipReader &zip, Presentation &deck)
        : zip(zip), deck(deck) {}

    const ZipReader &zip;
    Presentation &deck;
    /// Written by this app: its colours and fonts map back onto the theme.
    bool ours = false;
    Look look;
    QHash<QString, QColor> scheme;
    QHash<QString, QString> colorMap;
    QHash<QString, QDomDocument> documents;
    QHash<QString, QString> shapeItems;
    QString footer;
    bool numbers = false;

    QDomElement root(const QString &part)
    {
        if (!documents.contains(part)) {
            documents.insert(part, parse(zip.file(part)));
        }
        return documents.value(part).documentElement();
    }

    Rels rels(const QString &part)
    {
        Rels out;
        const int slash = part.lastIndexOf(QLatin1Char('/'));
        const QString name = part.left(slash + 1) + QStringLiteral("_rels/") + part.mid(slash + 1) + QStringLiteral(".rels");
        const QDomElement top = parse(zip.file(name)).documentElement();
        for (const QDomElement &rel : kids(top, "Relationship")) {
            if (rel.attribute(QStringLiteral("TargetMode")) == QLatin1String("External")) {
                continue;
            }
            const QString target = resolve(part, rel.attribute(QStringLiteral("Target")));
            out.byId.insert(rel.attribute(QStringLiteral("Id")), target);
            out.byType[rel.attribute(QStringLiteral("Type")).section(QLatin1Char('/'), -1)] << target;
        }
        return out;
    }

    QColor schemeColor(QString name) const
    {
        name = colorMap.value(name, name);
        return scheme.value(name, name.startsWith(QLatin1String("lt")) || name.startsWith(QLatin1String("bg")) ? QColor(Qt::white) : QColor(Qt::black));
    }

    /// The colour inside a fill: a token of the theme when the deck is ours and
    /// the colour is used as it is, "#rrggbb" otherwise.
    QString color(const QDomElement &holder, double *alpha = nullptr) const
    {
        for (const QDomElement &e : kids(holder)) {
            const QString kind = e.localName();
            QColor base;
            QString token;
            if (kind == QLatin1String("srgbClr")) {
                base = QColor::fromString(QLatin1Char('#') + e.attribute(QStringLiteral("val")));
            } else if (kind == QLatin1String("sysClr")) {
                base = QColor::fromString(QLatin1Char('#') + e.attribute(QStringLiteral("lastClr"), QStringLiteral("000000")));
            } else if (kind == QLatin1String("prstClr")) {
                base = QColor::fromString(e.attribute(QStringLiteral("val")));
            } else if (kind == QLatin1String("schemeClr")) {
                const QString name = e.attribute(QStringLiteral("val"));
                base = schemeColor(name);
                if (ours) {
                    if (name == QLatin1String("tx1") || name == QLatin1String("dk1")) token = QStringLiteral("@text");
                    else if (name == QLatin1String("tx2") || name == QLatin1String("dk2")) token = QStringLiteral("@title");
                    else if (name == QLatin1String("bg1") || name == QLatin1String("lt1")) token = QStringLiteral("@bg");
                    else if (name.startsWith(QLatin1String("accent"))) token = QLatin1Char('@') + name;
                }
            } else {
                continue;
            }
            if (!base.isValid()) {
                base = QColor(Qt::black);
            }
            bool changed = false;
            for (const QDomElement &m : kids(e)) {
                const double v = m.attribute(QStringLiteral("val")).toDouble() / 100000.0;
                const QString how = m.localName();
                if (how == QLatin1String("alpha")) {
                    if (alpha) *alpha = v;
                    continue;
                }
                float h, s, l;
                base.getHslF(&h, &s, &l);
                if (how == QLatin1String("lumMod")) base = QColor::fromHslF(h, s, qBound(0.0, l * v, 1.0));
                else if (how == QLatin1String("lumOff")) base = QColor::fromHslF(h, s, qBound(0.0, l + v, 1.0));
                else if (how == QLatin1String("tint")) base = QColor::fromRgbF(base.redF() * v + (1 - v), base.greenF() * v + (1 - v), base.blueF() * v + (1 - v));
                else if (how == QLatin1String("shade")) base = QColor::fromRgbF(base.redF() * v, base.greenF() * v, base.blueF() * v);
                else continue;
                changed = true;
            }
            return token.isEmpty() || changed ? base.name() : token;
        }
        return {};
    }

    /// The fill of a shape or a line: empty for none, `found` false when nothing says.
    QString fill(const QDomElement &props, double *alpha, bool *found) const
    {
        *found = true;
        const QDomElement plain = kid(props, "solidFill");
        if (!plain.isNull()) {
            return color(plain, alpha);
        }
        const QDomElement gradient = kid(props, "gradFill");
        if (!gradient.isNull()) {
            // A gradient becomes its first colour.
            return color(kid(gradient, {"gsLst", "gs"}), alpha);
        }
        if (!kid(props, "noFill").isNull() || !kid(props, "blipFill").isNull() || !kid(props, "pattFill").isNull()) {
            return {};
        }
        *found = false;
        return {};
    }

    static QString shapeOf(const QString &preset, bool *flip)
    {
        if (preset == QLatin1String("roundRect") || preset.startsWith(QLatin1String("round")) || preset.startsWith(QLatin1String("snip"))) return QStringLiteral("rounded");
        if (preset == QLatin1String("ellipse")) return QStringLiteral("ellipse");
        if (preset == QLatin1String("triangle") || preset == QLatin1String("rtTriangle")) return QStringLiteral("triangle");
        if (preset == QLatin1String("diamond")) return QStringLiteral("diamond");
        if (preset == QLatin1String("rightArrow")) return QStringLiteral("arrow");
        if (preset == QLatin1String("leftArrow")) {
            *flip = true;
            return QStringLiteral("arrow");
        }
        if (preset.startsWith(QLatin1String("star"))) return QStringLiteral("star");
        if (preset == QLatin1String("line") || preset.contains(QLatin1String("Connector"))) return QStringLiteral("line");
        return QStringLiteral("rect");
    }

    static bool geometry(const QDomElement &xfrm, Item &item)
    {
        const QDomElement off = kid(xfrm, "off");
        const QDomElement ext = kid(xfrm, "ext");
        if (off.isNull() || ext.isNull()) {
            return false;
        }
        item.x = points(off.attribute(QStringLiteral("x")));
        item.y = points(off.attribute(QStringLiteral("y")));
        item.w = points(ext.attribute(QStringLiteral("cx")));
        item.h = points(ext.attribute(QStringLiteral("cy")));
        item.rotation = xfrm.attribute(QStringLiteral("rot")).toDouble() / 60000.0;
        item.flipH = xfrm.attribute(QStringLiteral("flipH")) == QLatin1String("1") || xfrm.attribute(QStringLiteral("flipH")) == QLatin1String("true");
        item.flipV = xfrm.attribute(QStringLiteral("flipV")) == QLatin1String("1") || xfrm.attribute(QStringLiteral("flipV")) == QLatin1String("true");
        return true;
    }

    QString fontOf(const QString &face) const
    {
        if (face.startsWith(QLatin1String("+mj"))) return look.titleFont;
        if (face.startsWith(QLatin1String("+mn"))) return look.bodyFont;
        return face;
    }

    Run runOf(const QDomElement &props, double *alpha = nullptr) const
    {
        Run run;
        if (props.isNull()) {
            return run;
        }
        if (props.hasAttribute(QStringLiteral("sz"))) run.size = props.attribute(QStringLiteral("sz")).toDouble() / 100.0;
        if (props.hasAttribute(QStringLiteral("b"))) run.bold = props.attribute(QStringLiteral("b")) == QLatin1String("1") || props.attribute(QStringLiteral("b")) == QLatin1String("true");
        const QString italic = props.attribute(QStringLiteral("i"));
        run.italic = italic == QLatin1String("1") || italic == QLatin1String("true");
        const QString underline = props.attribute(QStringLiteral("u"));
        run.underline = !underline.isEmpty() && underline != QLatin1String("none");
        const QString strike = props.attribute(QStringLiteral("strike"));
        run.strike = !strike.isEmpty() && strike != QLatin1String("noStrike");
        run.color = color(kid(props, "solidFill"), alpha);
        run.font = fontOf(kid(props, "latin").attribute(QStringLiteral("typeface")));
        return run;
    }

    QList<Paragraph> paragraphs(const QDomElement &body, bool bulletsByDefault, double *alpha = nullptr) const
    {
        QList<Paragraph> out;
        for (const QDomElement &p : kids(body, "p")) {
            Paragraph paragraph;
            const QDomElement props = kid(p, "pPr");
            const QString align = props.attribute(QStringLiteral("algn"));
            paragraph.align = align == QLatin1String("l") ? 0 : align == QLatin1String("ctr") ? 1 : align == QLatin1String("r") ? 2
                : align == QLatin1String("just") || align == QLatin1String("dist") ? 3 : -1;
            paragraph.level = props.attribute(QStringLiteral("lvl")).toInt();
            paragraph.bullet = !kid(props, "buChar").isNull() || !kid(props, "buAutoNum").isNull()
                || (bulletsByDefault && kid(props, "buNone").isNull());
            for (const QDomElement &e : kids(p)) {
                const QString kind = e.localName();
                if (kind == QLatin1String("r") || kind == QLatin1String("fld")) {
                    Run run = runOf(kid(e, "rPr"), alpha);
                    run.text = kid(e, "t").text();
                    paragraph.runs << run;
                } else if (kind == QLatin1String("br")) {
                    Run run = runOf(kid(e, "rPr"));
                    run.text = QString(QChar::LineSeparator);
                    paragraph.runs << run;
                }
            }
            out << paragraph;
        }
        return out;
    }

    /// What matches the defaults of the item goes back to following them.
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
                const QColor ink = look.color(run.color);
                if (ink.isValid() && ink.rgb() == d.color.rgb()) run.color.clear();
                if (!runs.isEmpty() && runs.last().sameFormat(run)) {
                    runs.last().text += run.text;
                } else {
                    runs << run;
                }
            }
            paragraph.runs = runs;
        }
    }

    static Placeholder placeholderOf(const QDomElement &shape)
    {
        Placeholder out;
        for (const QDomElement &nv : kids(shape)) {
            if (nv.localName().startsWith(QLatin1String("nv"))) {
                const QDomElement ph = kid(nv, {"nvPr", "ph"});
                if (!ph.isNull()) {
                    out.element = shape;
                    out.type = ph.attribute(QStringLiteral("type"), QStringLiteral("obj"));
                    out.idx = ph.attribute(QStringLiteral("idx"));
                }
            }
        }
        return out;
    }

    /// The placeholder of the layout or the master a slide's placeholder stands on.
    static QDomElement parentOf(const Placeholder &wanted, const QDomElement &sheet)
    {
        const auto family = [](const QString &type) {
            if (type == QLatin1String("ctrTitle")) return QStringLiteral("title");
            if (type == QLatin1String("subTitle") || type == QLatin1String("obj")) return QStringLiteral("body");
            return type;
        };
        QDomElement byType;
        for (const QDomElement &shape : kids(kid(sheet, {"cSld", "spTree"}))) {
            const Placeholder ph = placeholderOf(shape);
            if (ph.element.isNull()) {
                continue;
            }
            if (!wanted.idx.isEmpty() && ph.idx == wanted.idx) {
                return shape;
            }
            if (byType.isNull() && family(ph.type) == family(wanted.type)) {
                byType = shape;
            }
        }
        return byType;
    }

    void readShapes(const QDomElement &tree, const QString &part, const Rels &links, const QList<QDomElement> &sheets,
                    const QString &layout, const QString &group, QList<Item> &out)
    {
        for (const QDomElement &e : kids(tree)) {
            const QString kind = e.localName();
            if (kind == QLatin1String("AlternateContent")) {
                QDomElement branch = kid(e, "Fallback");
                if (branch.isNull()) {
                    branch = kid(e, "Choice");
                }
                readShapes(branch, part, links, sheets, layout, group, out);
                continue;
            }
            if (kind == QLatin1String("grpSp")) {
                QList<Item> members;
                const QString name = group.isEmpty() ? newId() : group;
                readShapes(e, part, links, sheets, layout, name, members);
                // Out of the group's own coordinates into those of what holds it.
                const QDomElement xfrm = kid(e, {"grpSpPr", "xfrm"});
                Item frame;
                const QDomElement chOff = kid(xfrm, "chOff");
                const QDomElement chExt = kid(xfrm, "chExt");
                if (geometry(xfrm, frame) && !chOff.isNull() && !chExt.isNull()) {
                    const double cx = points(chOff.attribute(QStringLiteral("x")));
                    const double cy = points(chOff.attribute(QStringLiteral("y")));
                    const double cw = points(chExt.attribute(QStringLiteral("cx")));
                    const double ch = points(chExt.attribute(QStringLiteral("cy")));
                    const double sx = cw > 0 ? frame.w / cw : 1;
                    const double sy = ch > 0 ? frame.h / ch : 1;
                    QTransform turn;
                    turn.translate(frame.center().x(), frame.center().y());
                    turn.rotate(frame.rotation);
                    turn.translate(-frame.center().x(), -frame.center().y());
                    for (Item &member : members) {
                        QPointF center(frame.x + (member.center().x() - cx) * sx, frame.y + (member.center().y() - cy) * sy);
                        center = turn.map(center);
                        member.w *= sx;
                        member.h *= sy;
                        member.x = center.x() - member.w / 2;
                        member.y = center.y() - member.h / 2;
                        member.rotation = std::fmod(member.rotation + frame.rotation + 360, 360);
                    }
                }
                if (members.size() == 1 && group.isEmpty()) {
                    members.first().group.clear();
                }
                out << members;
                continue;
            }
            const bool isPicture = kind == QLatin1String("pic");
            const bool isLine = kind == QLatin1String("cxnSp");
            const bool isFrame = kind == QLatin1String("graphicFrame");
            if (kind != QLatin1String("sp") && !isPicture && !isLine && !isFrame) {
                continue;
            }
            Item item;
            item.id = newId();
            item.group = group;
            const Placeholder ph = placeholderOf(e);
            const bool placed = !ph.element.isNull();
            if (placed && (ph.type == QLatin1String("ftr") || ph.type == QLatin1String("sldNum") || ph.type == QLatin1String("dt"))) {
                // The footer and the number belong to the master.
                if (ph.type == QLatin1String("ftr") && footer.isEmpty()) {
                    QStringList lines;
                    for (const Paragraph &paragraph : paragraphs(kid(e, "txBody"), false)) {
                        lines << paragraph.plain();
                    }
                    footer = lines.join(QLatin1Char(' ')).trimmed();
                } else if (ph.type == QLatin1String("sldNum")) {
                    numbers = true;
                }
                continue;
            }
            QString spid;
            for (const QDomElement &nv : kids(e)) {
                if (nv.localName().startsWith(QLatin1String("nv"))) {
                    spid = kid(nv, "cNvPr").attribute(QStringLiteral("id"));
                }
            }
            shapeItems.insert(spid, item.id);

            if (isFrame) {
                const QDomElement table = kid(e, {"graphic", "graphicData", "tbl"});
                if (table.isNull() || !geometry(kid(e, "xfrm"), item)) {
                    continue;
                }
                item.type = QStringLiteral("table");
                item.rotation = 0;
                item.cols = int(kids(kid(table, "tblGrid"), "gridCol").size());
                const QList<QDomElement> rows = kids(table, "tr");
                item.rows = int(rows.size());
                if (item.rows < 1 || item.cols < 1) {
                    continue;
                }
                for (int row = 0; row < item.rows; ++row) {
                    const QList<QDomElement> cells = kids(rows.at(row), "tc");
                    for (int col = 0; col < item.cols; ++col) {
                        QStringList lines;
                        if (col < cells.size()) {
                            for (const Paragraph &paragraph : paragraphs(kid(cells.at(col), "txBody"), false)) {
                                lines << paragraph.plain();
                            }
                            if (row == 0 && col == 0) {
                                item.fill = color(kid(cells.at(col), {"tcPr", "solidFill"}));
                                if (item.fill == QLatin1String("@accent1")) {
                                    item.fill.clear();
                                }
                            }
                        }
                        item.cells << lines.join(QLatin1Char('\n'));
                    }
                }
                out << item;
                continue;
            }

            const QDomElement props = kid(e, "spPr");
            const QDomElement style = kid(e, "style");
            // A placeholder that says nothing of its place stands where its layout, or the master, puts it.
            QList<QDomElement> chain = {e};
            if (placed) {
                for (const QDomElement &sheet : sheets) {
                    const QDomElement parent = parentOf(ph, sheet);
                    if (!parent.isNull()) {
                        chain << parent;
                    }
                }
            }
            bool where = false;
            for (const QDomElement &link : std::as_const(chain)) {
                if (geometry(kid(link, {"spPr", "xfrm"}), item)) {
                    where = true;
                    break;
                }
            }
            if (!where) {
                continue;
            }
            if (placed) {
                if (ph.type == QLatin1String("ctrTitle")) item.role = QStringLiteral("ctitle");
                else if (ph.type == QLatin1String("title")) item.role = QStringLiteral("title");
                else if (ph.type == QLatin1String("subTitle")) item.role = QStringLiteral("subtitle");
                else if (ph.type == QLatin1String("pic")) item.role = QStringLiteral("picture");
                else item.role = layout == QLatin1String("picture") ? QStringLiteral("caption") : QStringLiteral("body");
            }
            double alpha = 1;
            bool found = false;
            const QDomElement outline = kid(props, "ln");
            QString stroke = fill(outline, &alpha, &found);
            double strokeWidth = outline.hasAttribute(QStringLiteral("w")) ? points(outline.attribute(QStringLiteral("w"))) : 0;
            if (!found && !placed && !style.isNull() && kid(style, "lnRef").attribute(QStringLiteral("idx")).toInt() > 0) {
                stroke = color(kid(style, "lnRef"));
            }
            if (!stroke.isEmpty() && strokeWidth <= 0) {
                strokeWidth = 1;
            }
            item.stroke = stroke;
            item.strokeWidth = stroke.isEmpty() ? 0 : strokeWidth;
            item.shadow = !kid(props, {"effectLst", "outerShdw"}).isNull();

            if (isPicture) {
                item.type = QStringLiteral("image");
                const QDomElement blip = kid(e, {"blipFill", "blip"});
                const QString target = links.byId.value(attr(blip, "embed"));
                const QByteArray bytes = target.isEmpty() ? QByteArray() : zip.file(target);
                if (!bytes.isEmpty()) {
                    item.image = deck.addMedia(bytes, target.section(QLatin1Char('.'), -1));
                }
                const QDomElement fade = kid(blip, "alphaModFix");
                if (!fade.isNull()) {
                    item.opacity = fade.attribute(QStringLiteral("amt")).toDouble() / 100000.0;
                }
                if (item.image.isEmpty() && item.role.isEmpty()) {
                    continue;
                }
                out << item;
                continue;
            }
            bool flip = false;
            const QString preset = kid(props, "prstGeom").attribute(QStringLiteral("prst"), QStringLiteral("rect"));
            const QString shape = shapeOf(preset, &flip);
            if (isLine || shape == QLatin1String("line")) {
                item.type = QStringLiteral("shape");
                item.shape = QStringLiteral("line");
                const auto headed = [&](const char *end) {
                    const QString type = kid(outline, end).attribute(QStringLiteral("type"));
                    return !type.isEmpty() && type != QLatin1String("none");
                };
                item.arrow = headed("tailEnd") || headed("headEnd");
                if (item.stroke.isEmpty()) {
                    item.stroke = ours ? QStringLiteral("@text") : look.text.name();
                    item.strokeWidth = 1;
                }
                item.opacity = alpha;
                out << item;
                continue;
            }
            if (placed && ph.type == QLatin1String("pic")) {
                item.type = QStringLiteral("image");
                out << item;
                continue;
            }
            const bool box = placed || kid(e, {"nvSpPr", "cNvSpPr"}).attribute(QStringLiteral("txBox")) == QLatin1String("1");
            item.type = box ? QStringLiteral("text") : QStringLiteral("shape");
            if (!box) {
                item.shape = shape;
                item.flipH = item.flipH != flip;
            }
            item.fill = fill(props, &alpha, &found);
            if (!found && !box && !style.isNull() && kid(style, "fillRef").attribute(QStringLiteral("idx")).toInt() > 0) {
                item.fill = color(kid(style, "fillRef"));
            }
            // How the text sits: said here, or by the layout, or as the role has it.
            const QDomElement body = kid(e, "txBody");
            QString anchor;
            for (const QDomElement &link : std::as_const(chain)) {
                anchor = kid(link, {"txBody", "bodyPr"}).attribute(QStringLiteral("anchor"));
                if (!anchor.isEmpty()) {
                    break;
                }
            }
            if (anchor.isEmpty()) {
                item.valign = item.role == QLatin1String("ctitle") ? 2 : item.role == QLatin1String("title") || item.role == QLatin1String("caption") ? 1 : 0;
            } else {
                item.valign = anchor == QLatin1String("ctr") ? 1 : anchor == QLatin1String("b") ? 2 : 0;
            }
            double ink = 1;
            item.text = paragraphs(body, item.role == QLatin1String("body"), &ink);
            item.opacity = item.fill.isEmpty() && item.stroke.isEmpty() ? ink : alpha;
            if (!placed) {
                // Outside a placeholder, text that says nothing of its size is 18 points.
                for (Paragraph &paragraph : item.text) {
                    for (Run &run : paragraph.runs) {
                        if (run.size <= 0) run.size = 18;
                        if (run.bold < 0) run.bold = 0;
                    }
                }
            }
            if (item.text.isEmpty() && box) {
                Paragraph paragraph;
                paragraph.bullet = item.role == QLatin1String("body");
                item.text << paragraph;
            }
            settle(item);
            if (!box && !item.hasText()) {
                item.text.clear();
            }
            out << item;
        }
    }
};

QString layoutOf(const QDomElement &sheet)
{
    const QString type = sheet.attribute(QStringLiteral("type"));
    if (type == QLatin1String("title")) return QStringLiteral("title");
    if (type == QLatin1String("obj") || type == QLatin1String("tx")) return QStringLiteral("content");
    if (type == QLatin1String("twoObj") || type == QLatin1String("twoTxTwoObj") || type == QLatin1String("twoColTx")) return QStringLiteral("columns");
    if (type == QLatin1String("titleOnly")) return QStringLiteral("titleOnly");
    if (type == QLatin1String("picTx")) return QStringLiteral("picture");
    if (type == QLatin1String("blank")) return QStringLiteral("blank");
    return {};
}

}

bool Pptx::read(const QByteArray &bytes, Presentation &deck, QString *error)
{
    const auto fail = [&](const QString &why) {
        if (error) {
            *error = why;
        }
        return false;
    };
    const ZipReader zip(bytes);
    if (!zip.isValid()) {
        return fail(QStringLiteral("no es un archivo .pptx"));
    }
    Presentation loaded;
    Reader r(zip, loaded);
    QString main = r.rels(QString()).byType.value(QStringLiteral("officeDocument")).value(0);
    if (main.isEmpty()) {
        main = QStringLiteral("ppt/presentation.xml");
    }
    const QDomElement top = r.root(main);
    if (top.isNull() || top.localName() != QLatin1String("presentation")) {
        return fail(QStringLiteral("no contiene una presentación"));
    }
    const Rels links = r.rels(main);
    const QDomElement size = kid(top, "sldSz");
    if (!size.isNull()) {
        loaded.width = points(size.attribute(QStringLiteral("cx")));
        loaded.height = points(size.attribute(QStringLiteral("cy")));
    }
    if (loaded.width < 10 || loaded.height < 10) {
        return fail(QStringLiteral("el tamaño de las diapositivas no es válido"));
    }

    // The theme: one of ours by its name, or the nearest of them with the file's colours on top.
    const QString masterPart = links.byType.value(QStringLiteral("slideMaster")).value(0);
    const QDomElement master = r.root(masterPart);
    QString themePart = r.rels(masterPart).byType.value(QStringLiteral("theme")).value(0);
    if (themePart.isEmpty()) {
        themePart = links.byType.value(QStringLiteral("theme")).value(0);
    }
    const QDomElement theme = r.root(themePart);
    const QDomElement elements = kid(theme, "themeElements");
    for (const QDomElement &slot : kids(kid(elements, "clrScheme"))) {
        const QDomElement value = slot.firstChildElement();
        const QString text = value.localName() == QLatin1String("sysClr") ? value.attribute(QStringLiteral("lastClr")) : value.attribute(QStringLiteral("val"));
        r.scheme.insert(slot.localName(), QColor::fromString(QLatin1Char('#') + text));
    }
    const QDomElement map = kid(master, "clrMap");
    for (const char *name : {"bg1", "tx1", "bg2", "tx2"}) {
        r.colorMap.insert(QLatin1String(name), map.attribute(QLatin1String(name),
            QLatin1String(name[0] == 'b' ? (name[2] == '1' ? "lt1" : "lt2") : (name[2] == '1' ? "dk1" : "dk2"))));
    }
    const QString major = kid(elements, {"fontScheme", "majorFont", "latin"}).attribute(QStringLiteral("typeface"));
    const QString minor = kid(elements, {"fontScheme", "minorFont", "latin"}).attribute(QStringLiteral("typeface"));
    const QColor ink = r.schemeColor(QStringLiteral("tx1"));
    const QColor paper = r.schemeColor(QStringLiteral("bg1"));
    const QString themeName = theme.attribute(QStringLiteral("name"));
    const QString ourId = themeName.startsWith(QLatin1String("Wonderland ")) ? themeName.mid(11) : QString();
    r.ours = !ourId.isEmpty() && Model::theme(ourId).id == ourId;

    // The background of the master: a colour of its own, or the theme's.
    const QDomElement masterBg = kid(master, {"cSld", "bg"});
    QColor background = paper;
    bool gradient = false;
    {
        const QDomElement props = kid(masterBg, "bgPr");
        r.look.bg1 = paper; // Enough for `color` to resolve against while the look is not there yet.
        if (!kid(props, "solidFill").isNull()) {
            background = QColor::fromString(r.color(kid(props, "solidFill")).startsWith(QLatin1Char('@')) ? paper.name() : r.color(kid(props, "solidFill")));
        } else if (!kid(props, "gradFill").isNull()) {
            gradient = true;
            const QString first = r.color(kid(props, {"gradFill", "gsLst", "gs"}));
            background = first.startsWith(QLatin1Char('@')) || first.isEmpty() ? paper : QColor::fromString(first);
        } else if (!kid(masterBg, "bgRef").isNull()) {
            const QString ref = r.color(kid(masterBg, "bgRef"));
            background = ref.startsWith(QLatin1Char('@')) || ref.isEmpty() ? paper : QColor::fromString(ref);
        }
    }
    if (r.ours) {
        const Theme &t = Model::theme(ourId);
        loaded.theme = ourId;
        const QColor bg1 = QColor::fromString(t.bg1);
        if (!gradient && !(t.bg1 == t.bg2 && bg1.rgb() == background.rgb())) {
            loaded.master.background = background.name();
        }
        const QColor title = r.schemeColor(QStringLiteral("tx2"));
        if (title.rgb() != QColor::fromString(t.title).rgb()) loaded.master.titleColor = title.name();
        if (ink.rgb() != QColor::fromString(t.text).rgb()) loaded.master.textColor = ink.name();
        if (!major.isEmpty() && major != t.titleFont) loaded.master.titleFont = major;
        if (!minor.isEmpty() && minor != t.bodyFont) loaded.master.bodyFont = minor;
    } else {
        const double luma = 0.299 * background.redF() + 0.587 * background.greenF() + 0.114 * background.blueF();
        loaded.theme = luma > 0.5 ? QStringLiteral("blanco") : QStringLiteral("negro");
        const Theme &t = Model::theme(loaded.theme);
        if (background.rgb() != QColor::fromString(t.bg1).rgb()) loaded.master.background = background.name();
        QString title = r.color(kid(master, {"txStyles", "titleStyle", "lvl1pPr", "defRPr", "solidFill"}));
        if (title.isEmpty()) title = ink.name();
        loaded.master.titleColor = title;
        loaded.master.textColor = ink.name();
        if (!major.isEmpty()) loaded.master.titleFont = major;
        if (!minor.isEmpty()) loaded.master.bodyFont = minor;
    }
    r.look = loaded.look();

    // The slides, in the order of the presentation.
    QStringList parts;
    for (const QDomElement &entry : kids(kid(top, "sldIdLst"), "sldId")) {
        // Two attributes are called `id`: the relationship is the one with a prefix.
        const QDomNamedNodeMap attrs = entry.attributes();
        for (int i = 0; i < attrs.count(); ++i) {
            const QDomAttr a = attrs.item(i).toAttr();
            if (a.localName() == QLatin1String("id") && !a.prefix().isEmpty() && links.byId.contains(a.value())) {
                parts << links.byId.value(a.value());
            }
        }
    }
    for (const QString &part : std::as_const(parts)) {
        const QDomElement sld = r.root(part);
        if (sld.isNull()) {
            continue;
        }
        const Rels own = r.rels(part);
        const QString layoutPart = own.byType.value(QStringLiteral("slideLayout")).value(0);
        const QDomElement layoutSheet = r.root(layoutPart);
        QString layoutMaster = r.rels(layoutPart).byType.value(QStringLiteral("slideMaster")).value(0);
        const QDomElement masterSheet = layoutMaster.isEmpty() ? master : r.root(layoutMaster);
        Slide slide;
        slide.id = newId();
        slide.layout = layoutOf(layoutSheet);
        slide.hidden = sld.attribute(QStringLiteral("show")) == QLatin1String("0") || sld.attribute(QStringLiteral("show")) == QLatin1String("false");
        const QDomElement bg = kid(sld, {"cSld", "bg", "bgPr"});
        if (!bg.isNull()) {
            bool found = false;
            slide.background = r.fill(bg, nullptr, &found);
        }
        r.shapeItems.clear();
        r.readShapes(kid(sld, {"cSld", "spTree"}), part, own, {layoutSheet, masterSheet}, slide.layout, QString(), slide.items);
        if (slide.layout.isEmpty()) {
            // A layout of another kind: the nearest of ours by what the slide holds.
            bool title = false;
            bool body = false;
            for (const Item &item : std::as_const(slide.items)) {
                title = title || item.role == QLatin1String("title") || item.role == QLatin1String("ctitle");
                body = body || item.role == QLatin1String("body");
            }
            slide.layout = title && body ? QStringLiteral("content") : title ? QStringLiteral("titleOnly") : QStringLiteral("blank");
        }
        QList<QDomElement> transitions;
        all(sld, "transition", transitions);
        if (!transitions.isEmpty()) {
            const QDomElement kind = transitions.first().firstChildElement();
            const QString name = kind.localName();
            slide.transition = name == QLatin1String("push") ? QStringLiteral("push")
                : name == QLatin1String("fade") && (kind.attribute(QStringLiteral("thruBlk")) == QLatin1String("1") || kind.attribute(QStringLiteral("thruBlk")) == QLatin1String("true"))
                    ? QStringLiteral("fade") : QStringLiteral("dissolve");
        }
        // The entrances: whatever comes in on a click, by flying or by just being there.
        QList<QDomElement> nodes;
        all(kid(sld, "timing"), "cTn", nodes);
        for (const QDomElement &node : std::as_const(nodes)) {
            if (node.attribute(QStringLiteral("presetClass")) != QLatin1String("entr")) {
                continue;
            }
            QList<QDomElement> targets;
            all(node, "spTgt", targets);
            if (targets.isEmpty()) {
                continue;
            }
            const int index = slide.indexOf(r.shapeItems.value(targets.first().attribute(QStringLiteral("spid"))));
            if (index >= 0) {
                slide.items[index].anim = node.attribute(QStringLiteral("presetID")) == QLatin1String("1") ? QStringLiteral("appear") : QStringLiteral("slide");
            }
        }
        const QString notesPart = own.byType.value(QStringLiteral("notesSlide")).value(0);
        if (!notesPart.isEmpty()) {
            for (const QDomElement &shape : kids(kid(r.root(notesPart), {"cSld", "spTree"}), "sp")) {
                if (Reader::placeholderOf(shape).type == QLatin1String("body")) {
                    QStringList lines;
                    for (const Paragraph &paragraph : r.paragraphs(kid(shape, "txBody"), false)) {
                        lines << paragraph.plain();
                    }
                    slide.notes = lines.join(QLatin1Char('\n'));
                }
            }
        }
        loaded.slides << slide;
    }
    if (loaded.slides.isEmpty()) {
        return fail(QStringLiteral("no tiene diapositivas"));
    }
    loaded.master.footer = r.footer;
    loaded.master.numbers = r.numbers;

    // Every deck is 540 points high here: one of another size is scaled to it.
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
