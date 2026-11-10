#include "pptx.h"

#include "richtext.h"
#include "zip.h"

#include <QDateTime>

#include <cmath>

using namespace Model;

namespace {

const QString xmlHead = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
const QString nsAll = QStringLiteral(
    " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\""
    " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\""
    " xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\"");
const QString relType = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/");
const QString lang = QStringLiteral("es-ES");

QString esc(const QString &text)
{
    QString clean;
    clean.reserve(text.size());
    for (const QChar c : text) {
        // XML 1.0 has no place for the other control characters.
        if (c.unicode() >= 0x20 || c == QLatin1Char('\t')) {
            clean += c;
        }
    }
    return clean.toHtmlEscaped();
}

qint64 emu(double points)
{
    return qRound64(points * 12700);
}

QString hex(const QColor &color)
{
    return color.name().mid(1).toUpper();
}

QString schemeName(const QString &token)
{
    if (token == QLatin1String("@text")) return QStringLiteral("tx1");
    if (token == QLatin1String("@title")) return QStringLiteral("tx2");
    if (token == QLatin1String("@bg")) return QStringLiteral("bg1");
    if (token.startsWith(QLatin1String("@accent"))) {
        const int n = token.mid(7).toInt();
        if (n >= 1 && n <= 6) {
            return QStringLiteral("accent%1").arg(n);
        }
    }
    return {};
}

QString colorXml(const QString &token, double alpha = 1)
{
    const QString inner = alpha < 0.999 ? QStringLiteral("<a:alpha val=\"%1\"/>").arg(qRound(qBound(0.0, alpha, 1.0) * 100000)) : QString();
    const QString scheme = schemeName(token);
    if (!scheme.isEmpty()) {
        return QStringLiteral("<a:schemeClr val=\"%1\">%2</a:schemeClr>").arg(scheme, inner);
    }
    const QColor color = QColor::fromString(token);
    return QStringLiteral("<a:srgbClr val=\"%1\">%2</a:srgbClr>").arg(hex(color.isValid() ? color : QColor(Qt::black)), inner);
}

QString solid(const QString &token, double alpha = 1)
{
    return QStringLiteral("<a:solidFill>%1</a:solidFill>").arg(colorXml(token, alpha));
}

struct Rel {
    QString type;
    QString target;
};

QByteArray relsXml(const QList<Rel> &rels)
{
    QString out = xmlHead + QStringLiteral("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    for (int i = 0; i < rels.size(); ++i) {
        out += QStringLiteral("<Relationship Id=\"rId%1\" Type=\"%2\" Target=\"%3\"/>").arg(i + 1).arg(rels.at(i).type, esc(rels.at(i).target));
    }
    return (out + QStringLiteral("</Relationships>")).toUtf8();
}

class Writer
{
public:
    explicit Writer(const Presentation &deck)
        : deck(deck), look(deck.look()) {}

    const Presentation &deck;
    const Look look;
    int shapeId = 1;
    QHash<QString, int> shapeIds;
    /// The pictures of the part being written, in the order of their relationships.
    QStringList pictures;
    int firstPictureRel = 2;

    QString xfrm(const Item &item, const char *tag = "a:xfrm") const
    {
        QString attrs;
        if (!qFuzzyIsNull(item.rotation)) attrs += QStringLiteral(" rot=\"%1\"").arg(qRound64(item.rotation * 60000));
        if (item.flipH) attrs += QStringLiteral(" flipH=\"1\"");
        if (item.flipV) attrs += QStringLiteral(" flipV=\"1\"");
        return QStringLiteral("<%1%2><a:off x=\"%3\" y=\"%4\"/><a:ext cx=\"%5\" cy=\"%6\"/></%1>")
            .arg(QLatin1String(tag), attrs).arg(emu(item.x)).arg(emu(item.y)).arg(emu(item.w)).arg(emu(item.h));
    }

    static QString shadow(const Item &item)
    {
        if (!item.shadow) {
            return {};
        }
        return QStringLiteral("<a:effectLst><a:outerShdw blurRad=\"76200\" dist=\"50800\" dir=\"5400000\" algn=\"t\" rotWithShape=\"0\">"
                              "<a:srgbClr val=\"000000\"><a:alpha val=\"35000\"/></a:srgbClr></a:outerShdw></a:effectLst>");
    }

    static QString line(const Item &item, bool tail = false)
    {
        if (item.stroke.isEmpty() || item.strokeWidth <= 0) {
            return QStringLiteral("<a:ln><a:noFill/></a:ln>");
        }
        return QStringLiteral("<a:ln w=\"%1\">%2%3</a:ln>").arg(emu(item.strokeWidth)).arg(solid(item.stroke, item.opacity),
            tail ? QStringLiteral("<a:tailEnd type=\"triangle\" w=\"lg\" len=\"lg\"/>") : QString());
    }

    QString runProps(const Run &run, const Item &item, const TextDefaults &d, const char *tag = "a:rPr") const
    {
        const bool titled = item.role == QLatin1String("title") || item.role == QLatin1String("ctitle");
        const bool bold = run.bold < 0 ? d.bold : run.bold > 0;
        QString attrs = QStringLiteral(" lang=\"%1\" sz=\"%2\" b=\"%3\" i=\"%4\"").arg(lang).arg(qRound((run.size > 0 ? run.size : d.size) * 100))
            .arg(bold ? 1 : 0).arg(run.italic ? 1 : 0);
        if (run.underline) attrs += QStringLiteral(" u=\"sng\"");
        if (run.strike) attrs += QStringLiteral(" strike=\"sngStrike\"");
        // What the item leaves to the theme stays tied to it: a colour of the scheme, a font of the scheme.
        QString color = run.color;
        if (color.isEmpty()) {
            color = titled ? QStringLiteral("@title") : d.color.rgb() == look.text.rgb() ? QStringLiteral("@text") : d.color.name();
        }
        const QString font = !run.font.isEmpty() ? run.font : titled ? QStringLiteral("+mj-lt") : d.font == look.bodyFont ? QStringLiteral("+mn-lt") : d.font;
        return QStringLiteral("<%1%2 dirty=\"0\">%3<a:latin typeface=\"%4\"/><a:cs typeface=\"%4\"/></%1>")
            .arg(QLatin1String(tag), attrs, solid(color, item.opacity), esc(font));
    }

    QString paragraphs(const Item &item) const
    {
        const TextDefaults d = Model::textDefaults(item, look);
        static const char *aligns[] = {"l", "ctr", "r", "just"};
        QList<Paragraph> all = item.text;
        if (all.isEmpty()) {
            all << Paragraph();
        }
        QString out;
        for (const Paragraph &paragraph : all) {
            const int align = qBound(0, paragraph.align < 0 ? d.align : paragraph.align, 3);
            QString attrs = QStringLiteral(" algn=\"%1\"").arg(QLatin1String(aligns[align]));
            if (paragraph.level > 0) attrs += QStringLiteral(" lvl=\"%1\"").arg(qMin(paragraph.level, 8));
            // As the painter sets them: the text forty points in for each level, the bullet twenty before it.
            const qint64 indent = 508000;
            if (paragraph.bullet) {
                attrs += QStringLiteral(" marL=\"%1\" indent=\"-%2\"").arg(indent * (paragraph.level + 1)).arg(indent / 2);
            } else {
                attrs += QStringLiteral(" marL=\"0\" indent=\"0\"");
            }
            out += QStringLiteral("<a:p><a:pPr%1><a:spcBef><a:spcPts val=\"0\"/></a:spcBef><a:spcAft><a:spcPts val=\"%2\"/></a:spcAft>%3</a:pPr>")
                .arg(attrs).arg(qRound(d.spacing * 100))
                .arg(paragraph.bullet ? QStringLiteral("<a:buFont typeface=\"Arial\"/><a:buChar char=\"%1\"/>").arg(paragraph.level % 2 ? QStringLiteral("◦") : QStringLiteral("•")) : QStringLiteral("<a:buNone/>"));
            for (const Run &run : paragraph.runs) {
                // A line break inside a paragraph is an element of its own.
                const QStringList pieces = QString(run.text).replace(QChar::LineSeparator, QLatin1Char('\n')).split(QLatin1Char('\n'));
                for (int i = 0; i < pieces.size(); ++i) {
                    if (i > 0) {
                        out += QStringLiteral("<a:br>%1</a:br>").arg(runProps(run, item, d));
                    }
                    if (!pieces.at(i).isEmpty()) {
                        out += QStringLiteral("<a:r>%1<a:t>%2</a:t></a:r>").arg(runProps(run, item, d), esc(pieces.at(i)));
                    }
                }
            }
            const Run last = paragraph.runs.isEmpty() ? Run() : paragraph.runs.last();
            out += runProps(last, item, d, "a:endParaRPr") + QStringLiteral("</a:p>");
        }
        return out;
    }

    QString textBody(const Item &item, const char *tag = "p:txBody") const
    {
        static const char *anchors[] = {"t", "ctr", "b"};
        return QStringLiteral("<%1><a:bodyPr wrap=\"square\" lIns=\"101600\" tIns=\"50800\" rIns=\"101600\" bIns=\"50800\" rtlCol=\"0\" anchor=\"%2\">"
                              "<a:noAutofit/></a:bodyPr><a:lstStyle/>%3</%1>")
            .arg(QLatin1String(tag), QLatin1String(anchors[qBound(0, item.valign, 2)]), paragraphs(item));
    }

    /// The placeholder an item is, counted among those of its kind on the slide.
    static QString placeholder(const Item &item, const QList<Item> &items)
    {
        if (item.role == QLatin1String("ctitle")) return QStringLiteral("<p:ph type=\"ctrTitle\"/>");
        if (item.role == QLatin1String("title")) return QStringLiteral("<p:ph type=\"title\"/>");
        if (item.role == QLatin1String("subtitle")) return QStringLiteral("<p:ph type=\"subTitle\" idx=\"1\"/>");
        if (item.role == QLatin1String("picture")) return QStringLiteral("<p:ph type=\"pic\" idx=\"1\"/>");
        if (item.role == QLatin1String("caption")) return QStringLiteral("<p:ph type=\"body\" sz=\"half\" idx=\"2\"/>");
        if (item.role == QLatin1String("body")) {
            int nth = 1;
            for (const Item &other : items) {
                if (other.id == item.id) {
                    break;
                }
                if (other.role == QLatin1String("body")) {
                    ++nth;
                }
            }
            return QStringLiteral("<p:ph idx=\"%1\"/>").arg(nth);
        }
        return {};
    }

    static QString preset(const Item &item)
    {
        static const QHash<QString, QString> names = {
            {QStringLiteral("rect"), QStringLiteral("rect")}, {QStringLiteral("rounded"), QStringLiteral("roundRect")},
            {QStringLiteral("ellipse"), QStringLiteral("ellipse")}, {QStringLiteral("triangle"), QStringLiteral("triangle")},
            {QStringLiteral("diamond"), QStringLiteral("diamond")}, {QStringLiteral("arrow"), QStringLiteral("rightArrow")},
            {QStringLiteral("star"), QStringLiteral("star5")},
        };
        return QStringLiteral("<a:prstGeom prst=\"%1\"><a:avLst/></a:prstGeom>").arg(names.value(item.shape, QStringLiteral("rect")));
    }

    QString table(const Item &item, int id) const
    {
        const TextDefaults d = Model::textDefaults(item, look);
        const QString head = item.fill.isEmpty() ? QStringLiteral("@accent1") : item.fill;
        const QColor headColor = look.color(head);
        const double luma = 0.299 * headColor.redF() + 0.587 * headColor.greenF() + 0.114 * headColor.blueF();
        const QString headInk = luma < 0.6 ? QStringLiteral("#ffffff") : QStringLiteral("#1d1d1f");
        // The ink thinned over the paper, as a colour of its own: a see-through rule is drawn solid by some readers.
        const QColor paper = QColor::fromRgbF((look.bg1.redF() + look.bg2.redF()) / 2, (look.bg1.greenF() + look.bg2.greenF()) / 2,
                                              (look.bg1.blueF() + look.bg2.blueF()) / 2);
        const QColor thinned = QColor::fromRgbF(float(paper.redF() + (look.text.redF() - paper.redF()) * 0.25),
                                                float(paper.greenF() + (look.text.greenF() - paper.greenF()) * 0.25),
                                                float(paper.blueF() + (look.text.blueF() - paper.blueF()) * 0.25));
        const QString rule = QStringLiteral("<a:solidFill><a:srgbClr val=\"%1\"/></a:solidFill>").arg(thinned.name().mid(1));
        QString borders;
        for (const char *side : {"lnL", "lnR", "lnT", "lnB"}) {
            borders += QStringLiteral("<a:%1 w=\"12700\">%2</a:%1>").arg(QLatin1String(side), rule);
        }
        QString out = QStringLiteral("<p:graphicFrame><p:nvGraphicFramePr><p:cNvPr id=\"%1\" name=\"Tabla %1\"/><p:cNvGraphicFramePr>"
                                     "<a:graphicFrameLocks noGrp=\"1\"/></p:cNvGraphicFramePr><p:nvPr/></p:nvGraphicFramePr>%2"
                                     "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/table\">"
                                     "<a:tbl><a:tblPr firstRow=\"1\" bandRow=\"1\"/><a:tblGrid>").arg(id).arg(xfrm(item, "p:xfrm"));
        // The columns add up to the width exactly, whatever the rounding.
        qint64 used = 0;
        for (int col = 0; col < item.cols; ++col) {
            const qint64 upTo = emu(item.w * (col + 1) / item.cols);
            out += QStringLiteral("<a:gridCol w=\"%1\"/>").arg(upTo - used);
            used = upTo;
        }
        out += QStringLiteral("</a:tblGrid>");
        used = 0;
        for (int row = 0; row < item.rows; ++row) {
            const qint64 upTo = emu(item.h * (row + 1) / item.rows);
            out += QStringLiteral("<a:tr h=\"%1\">").arg(upTo - used);
            used = upTo;
            for (int col = 0; col < item.cols; ++col) {
                Item cell;
                cell.type = QStringLiteral("table");
                Run run;
                run.text = item.cell(row, col);
                run.bold = row == 0 ? 1 : 0;
                run.color = row == 0 ? headInk : QString();
                Paragraph paragraph;
                if (!run.text.isEmpty()) {
                    paragraph.runs << run;
                }
                cell.text << paragraph;
                const QString fill = row == 0 ? solid(head) : row % 2 == 0 ? solid(QStringLiteral("@text"), 0.06) : QStringLiteral("<a:noFill/>");
                out += QStringLiteral("<a:tc><a:txBody><a:bodyPr/><a:lstStyle/>%1</a:txBody>"
                                      "<a:tcPr marL=\"101600\" marR=\"101600\" marT=\"0\" marB=\"0\" anchor=\"ctr\">%2%3</a:tcPr></a:tc>")
                    .arg(paragraphs(cell), borders, fill);
            }
            out += QStringLiteral("</a:tr>");
        }
        Q_UNUSED(d)
        return out + QStringLiteral("</a:tbl></a:graphicData></a:graphic></p:graphicFrame>");
    }

    QString shape(const Item &item, const QList<Item> &items)
    {
        const int id = ++shapeId;
        shapeIds.insert(item.id, id);
        const bool isLine = item.type == QLatin1String("shape") && item.shape == QLatin1String("line");
        if (isLine) {
            return QStringLiteral("<p:cxnSp><p:nvCxnSpPr><p:cNvPr id=\"%1\" name=\"Línea %1\"/><p:cNvCxnSpPr/><p:nvPr/></p:nvCxnSpPr>"
                                  "<p:spPr>%2<a:prstGeom prst=\"%3\"><a:avLst/></a:prstGeom>%4%5</p:spPr></p:cxnSp>")
                .arg(id).arg(xfrm(item), item.arrow ? QStringLiteral("straightConnector1") : QStringLiteral("line"), line(item, item.arrow), shadow(item));
        }
        if (item.type == QLatin1String("table")) {
            return table(item, id);
        }
        const QString ph = placeholder(item, items);
        if (item.type == QLatin1String("image") && !item.image.isEmpty()) {
            if (!pictures.contains(item.image)) {
                pictures << item.image;
            }
            const int rel = firstPictureRel + int(pictures.indexOf(item.image));
            const QString fade = item.opacity < 0.999 ? QStringLiteral("<a:alphaModFix amt=\"%1\"/>").arg(qRound(item.opacity * 100000)) : QString();
            return QStringLiteral("<p:pic><p:nvPicPr><p:cNvPr id=\"%1\" name=\"Imagen %1\"/><p:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></p:cNvPicPr>"
                                  "<p:nvPr>%2</p:nvPr></p:nvPicPr><p:blipFill><a:blip r:embed=\"rId%3\">%4</a:blip><a:stretch><a:fillRect/></a:stretch></p:blipFill>"
                                  "<p:spPr>%5<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>%6%7</p:spPr></p:pic>")
                .arg(id).arg(ph).arg(rel).arg(fade, xfrm(item), line(item), shadow(item));
        }
        const bool box = item.type == QLatin1String("text");
        const QString fill = item.fill.isEmpty() ? QStringLiteral("<a:noFill/>") : solid(item.fill, item.opacity);
        const QString body = item.type == QLatin1String("image") ? QString() : textBody(item);
        return QStringLiteral("<p:sp><p:nvSpPr><p:cNvPr id=\"%1\" name=\"%2 %1\"/><p:cNvSpPr%3>%4</p:cNvSpPr><p:nvPr>%5</p:nvPr></p:nvSpPr>"
                              "<p:spPr>%6%7%8%9%10</p:spPr>%11</p:sp>")
            .arg(id).arg(box ? QStringLiteral("Texto") : QStringLiteral("Forma"), box && ph.isEmpty() ? QStringLiteral(" txBox=\"1\"") : QString(),
                 ph.isEmpty() ? QString() : QStringLiteral("<a:spLocks noGrp=\"1\"/>"), ph, xfrm(item), preset(item), fill, line(item))
            .arg(shadow(item), body);
    }

    /// The footer and the slide number the master asks for; empty placeholders on the master and the layouts.
    QString furniture(int number, bool filled)
    {
        QString out;
        const auto add = [&](const char *type, int idx, double x, double w, const char *align, const QString &content) {
            Item item;
            item.x = deck.width * x;
            item.y = deck.height - 34;
            item.w = deck.width * w;
            item.h = 20;
            out += QStringLiteral("<p:sp><p:nvSpPr><p:cNvPr id=\"%1\" name=\"%2\"/><p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr><p:nvPr>"
                                  "<p:ph type=\"%2\" sz=\"quarter\" idx=\"%3\"/></p:nvPr></p:nvSpPr><p:spPr>%4</p:spPr><p:txBody>"
                                  "<a:bodyPr lIns=\"0\" tIns=\"0\" rIns=\"0\" bIns=\"0\" anchor=\"ctr\"/><a:lstStyle/><a:p><a:pPr algn=\"%5\"/>%6</a:p></p:txBody></p:sp>")
                .arg(++shapeId).arg(QLatin1String(type)).arg(idx).arg(xfrm(item), QLatin1String(align), content);
        };
        const QString props = QStringLiteral("<a:rPr lang=\"%1\" sz=\"1200\" dirty=\"0\">%2<a:latin typeface=\"+mn-lt\"/></a:rPr>").arg(lang, solid(QStringLiteral("@text"), 0.6));
        if (!filled || !deck.master.footer.isEmpty()) {
            add("ftr", 11, 0.06, 0.6, "l", filled ? QStringLiteral("<a:r>%1<a:t>%2</a:t></a:r>").arg(props, esc(deck.master.footer)) : QString());
        }
        if (!filled || deck.master.numbers) {
            add("sldNum", 12, 0.74, 0.2, "r",
                QStringLiteral("<a:fld id=\"{B6F15528-21DE-4FAA-801E-634DDDAF4B2B}\" type=\"slidenum\">%1<a:t>%2</a:t></a:fld>").arg(props, filled ? QString::number(number) : QStringLiteral("‹Nº›")));
        }
        return out;
    }

    static QString treeOpen()
    {
        return QStringLiteral("<p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr>"
                              "<a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>");
    }

    QString tree(const QList<Item> &items)
    {
        QString out = treeOpen();
        for (int i = 0; i < items.size();) {
            const QString group = items.at(i).group;
            int end = i + 1;
            while (!group.isEmpty() && end < items.size() && items.at(end).group == group) {
                ++end;
            }
            if (end - i < 2) {
                out += shape(items.at(i), items);
                ++i;
                continue;
            }
            // Neighbours of one group: a group shape around them, in the slide's own coordinates.
            const QRectF box = Model::bounds(items.mid(i, end - i));
            const QString frame = QStringLiteral("x=\"%1\" y=\"%2\"").arg(emu(box.x())).arg(emu(box.y()));
            const QString extent = QStringLiteral("cx=\"%1\" cy=\"%2\"").arg(emu(box.width())).arg(emu(box.height()));
            const int id = ++shapeId;
            out += QStringLiteral("<p:grpSp><p:nvGrpSpPr><p:cNvPr id=\"%1\" name=\"Grupo %1\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr>"
                                  "<a:xfrm><a:off %2/><a:ext %3/><a:chOff %2/><a:chExt %3/></a:xfrm></p:grpSpPr>").arg(id).arg(frame, extent);
            for (; i < end; ++i) {
                out += shape(items.at(i), items);
            }
            out += QStringLiteral("</p:grpSp>");
        }
        return out;
    }

    /// The entrance of each build, one click each, in PowerPoint's own timing tree.
    QString timing(const Slide &slide) const
    {
        QString steps;
        int id = 2;
        for (const Item &item : slide.items) {
            if (item.anim.isEmpty() || !shapeIds.contains(item.id)) {
                continue;
            }
            const int spid = shapeIds.value(item.id);
            const bool slides = item.anim == QLatin1String("slide");
            const QString target = QStringLiteral("<p:tgtEl><p:spTgt spid=\"%1\"/></p:tgtEl>").arg(spid);
            QString effect = QStringLiteral("<p:set><p:cBhvr><p:cTn id=\"%1\" dur=\"1\" fill=\"hold\"><p:stCondLst><p:cond delay=\"0\"/></p:stCondLst></p:cTn>%2"
                                            "<p:attrNameLst><p:attrName>style.visibility</p:attrName></p:attrNameLst></p:cBhvr><p:to><p:strVal val=\"visible\"/></p:to></p:set>")
                .arg(id + 3).arg(target);
            int used = 4;
            if (slides) {
                const auto move = [&](const char *attr, const QString &from) {
                    effect += QStringLiteral("<p:anim calcmode=\"lin\" valueType=\"num\"><p:cBhvr additive=\"base\"><p:cTn id=\"%1\" dur=\"500\" fill=\"hold\"/>%2"
                                             "<p:attrNameLst><p:attrName>%3</p:attrName></p:attrNameLst></p:cBhvr><p:tavLst><p:tav tm=\"0\"><p:val><p:strVal val=\"%4\"/></p:val></p:tav>"
                                             "<p:tav tm=\"100000\"><p:val><p:strVal val=\"#%3\"/></p:val></p:tav></p:tavLst></p:anim>")
                        .arg(id + used).arg(target, QLatin1String(attr), from);
                    ++used;
                };
                move("ppt_x", QStringLiteral("0-#ppt_w/2"));
                move("ppt_y", QStringLiteral("#ppt_y"));
            }
            steps += QStringLiteral("<p:par><p:cTn id=\"%1\" fill=\"hold\"><p:stCondLst><p:cond delay=\"indefinite\"/></p:stCondLst><p:childTnLst>"
                                    "<p:par><p:cTn id=\"%2\" fill=\"hold\"><p:stCondLst><p:cond delay=\"0\"/></p:stCondLst><p:childTnLst>"
                                    "<p:par><p:cTn id=\"%3\" presetID=\"%4\" presetClass=\"entr\" presetSubtype=\"%5\" fill=\"hold\" nodeType=\"clickEffect\">"
                                    "<p:stCondLst><p:cond delay=\"0\"/></p:stCondLst><p:childTnLst>%6</p:childTnLst></p:cTn></p:par>"
                                    "</p:childTnLst></p:cTn></p:par></p:childTnLst></p:cTn></p:par>")
                .arg(id).arg(id + 1).arg(id + 2).arg(slides ? 2 : 1).arg(slides ? 8 : 0).arg(effect);
            id += used;
        }
        if (steps.isEmpty()) {
            return {};
        }
        return QStringLiteral("<p:timing><p:tnLst><p:par><p:cTn id=\"1\" dur=\"indefinite\" restart=\"never\" nodeType=\"tmRoot\"><p:childTnLst>"
                              "<p:seq concurrent=\"1\" nextAc=\"seek\"><p:cTn id=\"2\" dur=\"indefinite\" nodeType=\"mainSeq\"><p:childTnLst>%1</p:childTnLst></p:cTn>"
                              "<p:prevCondLst><p:cond evt=\"onPrev\" delay=\"0\"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:prevCondLst>"
                              "<p:nextCondLst><p:cond evt=\"onNext\" delay=\"0\"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:nextCondLst></p:seq>"
                              "</p:childTnLst></p:cTn></p:par></p:tnLst></p:timing>").arg(steps);
    }

    QString background(const QString &own) const
    {
        QString fill;
        if (!own.isEmpty()) {
            fill = solid(own);
        } else if (look.bg1 == look.bg2) {
            fill = solid(QStringLiteral("@bg"));
        } else {
            fill = QStringLiteral("<a:gradFill rotWithShape=\"1\"><a:gsLst><a:gs pos=\"0\"><a:schemeClr val=\"bg1\"/></a:gs>"
                                  "<a:gs pos=\"100000\"><a:schemeClr val=\"bg2\"/></a:gs></a:gsLst><a:lin ang=\"5400000\" scaled=\"0\"/></a:gradFill>");
        }
        return QStringLiteral("<p:bg><p:bgPr>%1<a:effectLst/></p:bgPr></p:bg>").arg(fill);
    }

    QString theme(const QString &name) const
    {
        QString colors;
        const auto add = [&](const char *slot, const QColor &color) {
            colors += QStringLiteral("<a:%1><a:srgbClr val=\"%2\"/></a:%1>").arg(QLatin1String(slot), hex(color));
        };
        add("dk1", look.text);
        add("lt1", look.bg1);
        add("dk2", look.title);
        add("lt2", look.bg2);
        for (int i = 0; i < 6; ++i) {
            add(qPrintable(QStringLiteral("accent%1").arg(i + 1)), look.accents.value(i, QColor(0x0a, 0x7a, 0xff)));
        }
        add("hlink", look.accents.value(0, QColor(0x0a, 0x7a, 0xff)));
        add("folHlink", look.accents.value(4, QColor(0xaf, 0x52, 0xde)));
        const QString phFill = QStringLiteral("<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>");
        const QString phLine = QStringLiteral("<a:ln w=\"6350\">%1</a:ln>").arg(phFill);
        const QString noEffect = QStringLiteral("<a:effectStyle><a:effectLst/></a:effectStyle>");
        const auto font = [](const char *slot, const QString &face) {
            return QStringLiteral("<a:%1><a:latin typeface=\"%2\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:%1>").arg(QLatin1String(slot), esc(face));
        };
        return xmlHead + QStringLiteral("<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" name=\"%1\"><a:themeElements>"
                                        "<a:clrScheme name=\"%1\">%2</a:clrScheme><a:fontScheme name=\"%1\">%3%4</a:fontScheme>"
                                        "<a:fmtScheme name=\"Office\"><a:fillStyleLst>%5%5%5</a:fillStyleLst><a:lnStyleLst>%6%6%6</a:lnStyleLst>"
                                        "<a:effectStyleLst>%7%7%7</a:effectStyleLst><a:bgFillStyleLst>%5%5%5</a:bgFillStyleLst></a:fmtScheme>"
                                        "</a:themeElements><a:objectDefaults/><a:extraClrSchemeLst/></a:theme>")
            .arg(esc(name), colors, font("majorFont", look.titleFont), font("minorFont", look.bodyFont), phFill, phLine, noEffect);
    }
};

const QString clrMap = QStringLiteral("<p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" accent2=\"accent2\" accent3=\"accent3\" "
                                      "accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" hlink=\"hlink\" folHlink=\"folHlink\"/>");
const QString mapOverride = QStringLiteral("<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>");

QString layoutType(const QString &layout)
{
    if (layout == QLatin1String("title")) return QStringLiteral("title");
    if (layout == QLatin1String("content")) return QStringLiteral("obj");
    if (layout == QLatin1String("columns")) return QStringLiteral("twoObj");
    if (layout == QLatin1String("titleOnly")) return QStringLiteral("titleOnly");
    if (layout == QLatin1String("picture")) return QStringLiteral("picTx");
    return QStringLiteral("blank");
}

QString mimeOf(const QString &suffix)
{
    static const QHash<QString, QString> types = {
        {QStringLiteral("png"), QStringLiteral("image/png")}, {QStringLiteral("jpg"), QStringLiteral("image/jpeg")},
        {QStringLiteral("jpeg"), QStringLiteral("image/jpeg")}, {QStringLiteral("gif"), QStringLiteral("image/gif")},
        {QStringLiteral("bmp"), QStringLiteral("image/bmp")}, {QStringLiteral("tif"), QStringLiteral("image/tiff")},
        {QStringLiteral("tiff"), QStringLiteral("image/tiff")}, {QStringLiteral("svg"), QStringLiteral("image/svg+xml")},
        {QStringLiteral("emf"), QStringLiteral("image/x-emf")}, {QStringLiteral("wmf"), QStringLiteral("image/x-wmf")},
        {QStringLiteral("webp"), QStringLiteral("image/webp")},
    };
    return types.value(suffix.toLower(), QStringLiteral("application/octet-stream"));
}

}

QByteArray Pptx::write(const Presentation &deck)
{
    Writer w(deck);
    ZipWriter zip;
    const QStringList layouts = Model::layouts();
    const QString themeName = QStringLiteral("Wonderland ") + Model::theme(deck.theme).id;
    QString types = xmlHead + QStringLiteral("<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                                             "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                                             "<Default Extension=\"xml\" ContentType=\"application/xml\"/>");
    const QString office = QStringLiteral("application/vnd.openxmlformats-officedocument.");
    const auto part = [&](const QString &name, const QString &type) {
        types += QStringLiteral("<Override PartName=\"/%1\" ContentType=\"%2\"/>").arg(name, type);
    };

    // The slides come first: they say which pictures the package holds.
    QStringList usedMedia;
    QList<QByteArray> slideXml;
    QList<QByteArray> slideRels;
    for (int i = 0; i < deck.slides.size(); ++i) {
        const Slide &slide = deck.slides.at(i);
        w.shapeId = 1;
        w.shapeIds.clear();
        w.pictures.clear();
        const bool notes = !slide.notes.isEmpty();
        w.firstPictureRel = notes ? 3 : 2;
        QString body = w.tree(slide.items) + w.furniture(i + 1, true) + QStringLiteral("</p:spTree>");
        QString transition;
        if (slide.transition == QLatin1String("dissolve")) transition = QStringLiteral("<p:transition spd=\"med\"><p:fade/></p:transition>");
        else if (slide.transition == QLatin1String("fade")) transition = QStringLiteral("<p:transition spd=\"med\"><p:fade thruBlk=\"1\"/></p:transition>");
        else if (slide.transition == QLatin1String("push")) transition = QStringLiteral("<p:transition spd=\"med\"><p:push dir=\"l\"/></p:transition>");
        const QString xml = xmlHead + QStringLiteral("<p:sld%1%2><p:cSld>%3%4</p:cSld>%5%6%7</p:sld>")
            .arg(nsAll, slide.hidden ? QStringLiteral(" show=\"0\"") : QString(),
                 slide.background.isEmpty() ? QString() : w.background(slide.background), body, mapOverride, transition, w.timing(slide));
        slideXml << xml.toUtf8();
        QList<Rel> rels = {{relType + QStringLiteral("slideLayout"),
                            QStringLiteral("../slideLayouts/slideLayout%1.xml").arg(qMax(0, int(layouts.indexOf(slide.layout))) + 1)}};
        if (notes) {
            rels << Rel{relType + QStringLiteral("notesSlide"), QStringLiteral("../notesSlides/notesSlide%1.xml").arg(i + 1)};
        }
        for (const QString &key : std::as_const(w.pictures)) {
            rels << Rel{relType + QStringLiteral("image"), QStringLiteral("../media/") + key};
            if (!usedMedia.contains(key)) {
                usedMedia << key;
            }
        }
        slideRels << relsXml(rels);
    }
    QStringList suffixes;
    for (const QString &key : std::as_const(usedMedia)) {
        const QString suffix = key.section(QLatin1Char('.'), -1).toLower();
        if (!suffixes.contains(suffix)) {
            suffixes << suffix;
            types += QStringLiteral("<Default Extension=\"%1\" ContentType=\"%2\"/>").arg(suffix, mimeOf(suffix));
        }
    }

    part(QStringLiteral("ppt/presentation.xml"), office + QStringLiteral("presentationml.presentation.main+xml"));
    part(QStringLiteral("ppt/slideMasters/slideMaster1.xml"), office + QStringLiteral("presentationml.slideMaster+xml"));
    part(QStringLiteral("ppt/notesMasters/notesMaster1.xml"), office + QStringLiteral("presentationml.notesMaster+xml"));
    part(QStringLiteral("ppt/theme/theme1.xml"), office + QStringLiteral("theme+xml"));
    part(QStringLiteral("ppt/theme/theme2.xml"), office + QStringLiteral("theme+xml"));
    part(QStringLiteral("ppt/presProps.xml"), office + QStringLiteral("presentationml.presProps+xml"));
    part(QStringLiteral("docProps/core.xml"), QStringLiteral("application/vnd.openxmlformats-package.core-properties+xml"));
    part(QStringLiteral("docProps/app.xml"), office + QStringLiteral("extended-properties+xml"));
    for (int i = 0; i < layouts.size(); ++i) {
        part(QStringLiteral("ppt/slideLayouts/slideLayout%1.xml").arg(i + 1), office + QStringLiteral("presentationml.slideLayout+xml"));
    }
    for (int i = 0; i < deck.slides.size(); ++i) {
        part(QStringLiteral("ppt/slides/slide%1.xml").arg(i + 1), office + QStringLiteral("presentationml.slide+xml"));
        if (!deck.slides.at(i).notes.isEmpty()) {
            part(QStringLiteral("ppt/notesSlides/notesSlide%1.xml").arg(i + 1), office + QStringLiteral("presentationml.notesSlide+xml"));
        }
    }
    zip.add(QStringLiteral("[Content_Types].xml"), (types + QStringLiteral("</Types>")).toUtf8());
    zip.add(QStringLiteral("_rels/.rels"), relsXml({
        {relType + QStringLiteral("officeDocument"), QStringLiteral("ppt/presentation.xml")},
        {QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties"), QStringLiteral("docProps/core.xml")},
        {relType + QStringLiteral("extended-properties"), QStringLiteral("docProps/app.xml")}}));
    const QString now = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ssZ"));
    zip.add(QStringLiteral("docProps/core.xml"), (xmlHead + QStringLiteral(
        "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
        "xmlns:dcterms=\"http://purl.org/dc/terms/\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
        "<dcterms:created xsi:type=\"dcterms:W3CDTF\">%1</dcterms:created><dcterms:modified xsi:type=\"dcterms:W3CDTF\">%1</dcterms:modified></cp:coreProperties>").arg(now)).toUtf8());
    zip.add(QStringLiteral("docProps/app.xml"), (xmlHead + QStringLiteral(
        "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\">"
        "<Application>Wonderland Diapositivas</Application><Slides>%1</Slides></Properties>").arg(deck.slides.size())).toUtf8());

    // presentation.xml: rId1 the master, rId2 the notes master, rId3 the theme, rId4 presProps, then the slides.
    QString ids;
    QList<Rel> rels = {{relType + QStringLiteral("slideMaster"), QStringLiteral("slideMasters/slideMaster1.xml")},
                       {relType + QStringLiteral("notesMaster"), QStringLiteral("notesMasters/notesMaster1.xml")},
                       {relType + QStringLiteral("theme"), QStringLiteral("theme/theme1.xml")},
                       {relType + QStringLiteral("presProps"), QStringLiteral("presProps.xml")}};
    for (int i = 0; i < deck.slides.size(); ++i) {
        rels << Rel{relType + QStringLiteral("slide"), QStringLiteral("slides/slide%1.xml").arg(i + 1)};
        ids += QStringLiteral("<p:sldId id=\"%1\" r:id=\"rId%2\"/>").arg(256 + i).arg(rels.size());
    }
    zip.add(QStringLiteral("ppt/presentation.xml"), (xmlHead + QStringLiteral(
        "<p:presentation%1 saveSubsetFonts=\"1\"><p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" r:id=\"rId1\"/></p:sldMasterIdLst>"
        "<p:notesMasterIdLst><p:notesMasterId r:id=\"rId2\"/></p:notesMasterIdLst><p:sldIdLst>%2</p:sldIdLst>"
        "<p:sldSz cx=\"%3\" cy=\"%4\"/><p:notesSz cx=\"6858000\" cy=\"9144000\"/></p:presentation>")
        .arg(nsAll, ids).arg(emu(deck.width)).arg(emu(deck.height))).toUtf8());
    zip.add(QStringLiteral("ppt/_rels/presentation.xml.rels"), relsXml(rels));
    zip.add(QStringLiteral("ppt/presProps.xml"), (xmlHead + QStringLiteral("<p:presentationPr%1/>").arg(nsAll)).toUtf8());
    zip.add(QStringLiteral("ppt/theme/theme1.xml"), w.theme(themeName).toUtf8());
    zip.add(QStringLiteral("ppt/theme/theme2.xml"), w.theme(themeName).toUtf8());

    // The master: the background, a title and a body, and the styles text falls back on.
    {
        w.shapeId = 1;
        const QList<Item> items = Model::layoutItems(QStringLiteral("content"), deck.size());
        QString layoutIds;
        QList<Rel> masterRels;
        for (int i = 0; i < layouts.size(); ++i) {
            masterRels << Rel{relType + QStringLiteral("slideLayout"), QStringLiteral("../slideLayouts/slideLayout%1.xml").arg(i + 1)};
            layoutIds += QStringLiteral("<p:sldLayoutId id=\"%1\" r:id=\"rId%2\"/>").arg(2147483649LL + i).arg(i + 1);
        }
        masterRels << Rel{relType + QStringLiteral("theme"), QStringLiteral("../theme/theme1.xml")};
        const auto level = [&](const char *font, const char *color, int size, bool bold, bool bullet) {
            return QStringLiteral("<a:lvl1pPr marL=\"%1\" indent=\"%2\" algn=\"l\">%3<a:defRPr sz=\"%4\" b=\"%5\"><a:solidFill><a:schemeClr val=\"%6\"/></a:solidFill>"
                                  "<a:latin typeface=\"%7\"/></a:defRPr></a:lvl1pPr>")
                .arg(bullet ? 342900 : 0).arg(bullet ? -342900 : 0)
                .arg(bullet ? QStringLiteral("<a:buFont typeface=\"Arial\"/><a:buChar char=\"•\"/>") : QStringLiteral("<a:buNone/>"))
                .arg(size).arg(bold ? 1 : 0).arg(QLatin1String(color), QLatin1String(font));
        };
        const QString xml = xmlHead + QStringLiteral("<p:sldMaster%1><p:cSld>%2%3%4</p:spTree></p:cSld>%5<p:sldLayoutIdLst>%6</p:sldLayoutIdLst>"
                                                     "<p:txStyles><p:titleStyle>%7</p:titleStyle><p:bodyStyle>%8</p:bodyStyle><p:otherStyle>%9</p:otherStyle></p:txStyles></p:sldMaster>")
            .arg(nsAll, w.background(QString()), w.tree(items).replace(QLatin1String("<p:ph idx=\"1\"/>"), QLatin1String("<p:ph type=\"body\" idx=\"1\"/>")),
                 w.furniture(0, false), clrMap, layoutIds,
                 level("+mj-lt", "tx2", 3800, w.look.titleBold, false), level("+mn-lt", "tx1", 2400, false, true), level("+mn-lt", "tx1", 1800, false, false));
        zip.add(QStringLiteral("ppt/slideMasters/slideMaster1.xml"), xml.toUtf8());
        zip.add(QStringLiteral("ppt/slideMasters/_rels/slideMaster1.xml.rels"), relsXml(masterRels));
    }
    for (int i = 0; i < layouts.size(); ++i) {
        w.shapeId = 1;
        const QString xml = xmlHead + QStringLiteral("<p:sldLayout%1 type=\"%2\" preserve=\"1\"><p:cSld name=\"%3\">%4%5</p:spTree></p:cSld>%6</p:sldLayout>")
            .arg(nsAll, layoutType(layouts.at(i)), esc(Model::layoutName(layouts.at(i))),
                 w.tree(Model::layoutItems(layouts.at(i), deck.size())), w.furniture(0, false), mapOverride);
        zip.add(QStringLiteral("ppt/slideLayouts/slideLayout%1.xml").arg(i + 1), xml.toUtf8());
        zip.add(QStringLiteral("ppt/slideLayouts/_rels/slideLayout%1.xml.rels").arg(i + 1),
                relsXml({{relType + QStringLiteral("slideMaster"), QStringLiteral("../slideMasters/slideMaster1.xml")}}));
    }

    // The notes: a master of their own, and a page for every slide that has some.
    const auto notesTree = [&](const QString &text, bool master) {
        const QString imageBox = master ? QStringLiteral("<a:xfrm><a:off x=\"685800\" y=\"1143000\"/><a:ext cx=\"5486400\" cy=\"3086100\"/></a:xfrm>") : QString();
        const QString textBox = master ? QStringLiteral("<a:xfrm><a:off x=\"685800\" y=\"4400550\"/><a:ext cx=\"5486400\" cy=\"3600450\"/></a:xfrm>") : QString();
        QString paragraphs;
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            paragraphs += line.isEmpty() ? QStringLiteral("<a:p><a:endParaRPr lang=\"%1\"/></a:p>").arg(lang)
                                         : QStringLiteral("<a:p><a:r><a:rPr lang=\"%1\" dirty=\"0\"/><a:t>%2</a:t></a:r></a:p>").arg(lang, esc(line));
        }
        return Writer::treeOpen() + QStringLiteral(
            "<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"Imagen de la diapositiva\"/><p:cNvSpPr><a:spLocks noGrp=\"1\" noRot=\"1\" noChangeAspect=\"1\"/></p:cNvSpPr>"
            "<p:nvPr><p:ph type=\"sldImg\"%3/></p:nvPr></p:nvSpPr><p:spPr>%1</p:spPr></p:sp>"
            "<p:sp><p:nvSpPr><p:cNvPr id=\"3\" name=\"Notas\"/><p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr><p:nvPr><p:ph type=\"body\" idx=\"%4\"/></p:nvPr></p:nvSpPr>"
            "<p:spPr>%2</p:spPr><p:txBody><a:bodyPr/><a:lstStyle/>%5</p:txBody></p:sp></p:spTree>")
            .arg(imageBox, textBox, master ? QStringLiteral(" idx=\"2\"") : QString(), master ? QStringLiteral("3") : QStringLiteral("1"), paragraphs);
    };
    zip.add(QStringLiteral("ppt/notesMasters/notesMaster1.xml"), (xmlHead + QStringLiteral("<p:notesMaster%1><p:cSld><p:bg><p:bgPr><a:solidFill><a:srgbClr val=\"FFFFFF\"/></a:solidFill><a:effectLst/></p:bgPr></p:bg>%2</p:cSld>%3"
        "<p:notesStyle><a:lvl1pPr><a:defRPr sz=\"1200\"><a:solidFill><a:srgbClr val=\"000000\"/></a:solidFill><a:latin typeface=\"+mn-lt\"/></a:defRPr></a:lvl1pPr></p:notesStyle></p:notesMaster>")
        .arg(nsAll, notesTree(QString(), true), clrMap)).toUtf8());
    zip.add(QStringLiteral("ppt/notesMasters/_rels/notesMaster1.xml.rels"), relsXml({{relType + QStringLiteral("theme"), QStringLiteral("../theme/theme2.xml")}}));
    for (int i = 0; i < deck.slides.size(); ++i) {
        zip.add(QStringLiteral("ppt/slides/slide%1.xml").arg(i + 1), slideXml.at(i));
        zip.add(QStringLiteral("ppt/slides/_rels/slide%1.xml.rels").arg(i + 1), slideRels.at(i));
        if (deck.slides.at(i).notes.isEmpty()) {
            continue;
        }
        zip.add(QStringLiteral("ppt/notesSlides/notesSlide%1.xml").arg(i + 1),
                (xmlHead + QStringLiteral("<p:notes%1><p:cSld>%2</p:cSld>%3</p:notes>").arg(nsAll, notesTree(deck.slides.at(i).notes, false), mapOverride)).toUtf8());
        zip.add(QStringLiteral("ppt/notesSlides/_rels/notesSlide%1.xml.rels").arg(i + 1),
                relsXml({{relType + QStringLiteral("notesMaster"), QStringLiteral("../notesMasters/notesMaster1.xml")},
                         {relType + QStringLiteral("slide"), QStringLiteral("../slides/slide%1.xml").arg(i + 1)}}));
    }
    for (const QString &key : std::as_const(usedMedia)) {
        // Pictures are compressed already.
        zip.add(QStringLiteral("ppt/media/") + key, deck.media.value(key), false);
    }
    return zip.finish();
}
