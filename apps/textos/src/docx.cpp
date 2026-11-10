#include "docx.h"

#include "zip.h"

#include <QBuffer>
#include <QDomDocument>
#include <QFile>
#include <QHash>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>
#include <QMap>
#include <QSaveFile>
#include <QXmlStreamWriter>

#include <functional>

namespace Docx {

namespace {

constexpr double twipsPerPx = 15.0;
constexpr double twipsPerCm = 1440.0 / 2.54;
constexpr double emuPerPx = 9525.0;

// Carried in a block format while a paragraph's properties are being put
// together; never left in the document.
constexpr int NumIdProperty = QTextFormat::UserProperty + 20;
constexpr int NumLevelProperty = QTextFormat::UserProperty + 21;

const QLatin1String nsW("http://schemas.openxmlformats.org/wordprocessingml/2006/main");
const QLatin1String nsR("http://schemas.openxmlformats.org/officeDocument/2006/relationships");
const QLatin1String nsWp("http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing");
const QLatin1String nsA("http://schemas.openxmlformats.org/drawingml/2006/main");
const QLatin1String nsPic("http://schemas.openxmlformats.org/drawingml/2006/picture");
const QLatin1String relBase("http://schemas.openxmlformats.org/officeDocument/2006/relationships/");

QString hex(const QColor &color)
{
    return color.name(QColor::HexRgb).mid(1).toUpper();
}

/// The style ids Word knows these by, in the order of Doc::styles().
QString wordStyleId(const QString &id)
{
    static const QHash<QString, QString> ids = {
        {QStringLiteral("title"), QStringLiteral("Title")},
        {QStringLiteral("h1"), QStringLiteral("Heading1")},
        {QStringLiteral("h2"), QStringLiteral("Heading2")},
        {QStringLiteral("h3"), QStringLiteral("Heading3")},
        {QStringLiteral("body"), QStringLiteral("Normal")},
        {QStringLiteral("quote"), QStringLiteral("Quote")},
        {QStringLiteral("code"), QStringLiteral("Code")},
    };
    return ids.value(id, QStringLiteral("Normal"));
}

QString wordStyleName(const QString &id)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("title"), QStringLiteral("Title")},
        {QStringLiteral("h1"), QStringLiteral("heading 1")},
        {QStringLiteral("h2"), QStringLiteral("heading 2")},
        {QStringLiteral("h3"), QStringLiteral("heading 3")},
        {QStringLiteral("body"), QStringLiteral("Normal")},
        {QStringLiteral("quote"), QStringLiteral("Quote")},
        {QStringLiteral("code"), QStringLiteral("Code")},
    };
    return names.value(id, QStringLiteral("Normal"));
}

/// The numbering definitions written: one per kind of list.
int abstractFor(QTextListFormat::Style style)
{
    switch (style) {
    case QTextListFormat::ListDecimal:
        return 2;
    case QTextListFormat::ListLowerAlpha:
        return 3;
    case QTextListFormat::ListUpperAlpha:
        return 4;
    case QTextListFormat::ListLowerRoman:
        return 5;
    case QTextListFormat::ListUpperRoman:
        return 6;
    default:
        return 1;
    }
}

// --- Writing ----------------------------------------------------------------------

class Writer
{
public:
    Writer(const QTextDocument *document, const Doc::PageSetup &setup)
        : m_document(document)
        , m_setup(setup)
    {
    }

    QByteArray package()
    {
        const QByteArray body = documentXml();
        ZipWriter zip;
        zip.add(QStringLiteral("[Content_Types].xml"), contentTypes());
        zip.add(QStringLiteral("_rels/.rels"),
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
                "</Relationships>");
        zip.add(QStringLiteral("word/document.xml"), body);
        zip.add(QStringLiteral("word/styles.xml"), stylesXml());
        zip.add(QStringLiteral("word/numbering.xml"), numberingXml());
        if (hasHeader()) {
            zip.add(QStringLiteral("word/header1.xml"), marginXml(true));
        }
        if (hasFooter()) {
            zip.add(QStringLiteral("word/footer1.xml"), marginXml(false));
        }
        zip.add(QStringLiteral("word/_rels/document.xml.rels"), relsXml());
        for (const Media &media : std::as_const(m_media)) {
            zip.add(QStringLiteral("word/") + media.target, media.bytes, false);
        }
        return zip.finish();
    }

private:
    struct Media {
        QString id;
        QString target;
        QByteArray bytes;
        QSize size;
    };

    bool hasHeader() const { return !m_setup.header.trimmed().isEmpty(); }
    bool hasFooter() const { return !m_setup.footer.trimmed().isEmpty() || m_setup.pageNumbers; }

    QString nextId() { return QStringLiteral("rId%1").arg(m_nextId++); }

    QByteArray contentTypes() const
    {
        QByteArray out = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                         "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                         "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                         "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                         "<Default Extension=\"png\" ContentType=\"image/png\"/>"
                         "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
                         "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
                         "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
                         "<Override PartName=\"/word/numbering.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/>";
        if (hasHeader()) {
            out += "<Override PartName=\"/word/header1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml\"/>";
        }
        if (hasFooter()) {
            out += "<Override PartName=\"/word/footer1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\"/>";
        }
        out += "</Types>";
        return out;
    }

    QByteArray relsXml() const
    {
        QByteArray out;
        QXmlStreamWriter x(&out);
        x.writeStartDocument(QStringLiteral("1.0"), true);
        x.writeStartElement(QStringLiteral("Relationships"));
        x.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships"));
        const auto relation = [&](const QString &id, const QString &type, const QString &target, bool external = false) {
            x.writeStartElement(QStringLiteral("Relationship"));
            x.writeAttribute(QStringLiteral("Id"), id);
            x.writeAttribute(QStringLiteral("Type"), relBase + type);
            x.writeAttribute(QStringLiteral("Target"), target);
            if (external) {
                x.writeAttribute(QStringLiteral("TargetMode"), QStringLiteral("External"));
            }
            x.writeEndElement();
        };
        relation(QStringLiteral("rId1"), QStringLiteral("styles"), QStringLiteral("styles.xml"));
        relation(QStringLiteral("rId2"), QStringLiteral("numbering"), QStringLiteral("numbering.xml"));
        if (hasHeader()) {
            relation(QStringLiteral("rId3"), QStringLiteral("header"), QStringLiteral("header1.xml"));
        }
        if (hasFooter()) {
            relation(QStringLiteral("rId4"), QStringLiteral("footer"), QStringLiteral("footer1.xml"));
        }
        for (const Media &media : m_media) {
            relation(media.id, QStringLiteral("image"), media.target);
        }
        for (auto it = m_links.constBegin(); it != m_links.constEnd(); ++it) {
            relation(it.value(), QStringLiteral("hyperlink"), it.key(), true);
        }
        x.writeEndElement();
        x.writeEndDocument();
        return out;
    }

    void val(const QString &element, const QString &value)
    {
        m_x->writeEmptyElement(element);
        m_x->writeAttribute(QStringLiteral("w:val"), value);
    }

    void toggle(const QString &element, bool on)
    {
        m_x->writeEmptyElement(element);
        if (!on) {
            m_x->writeAttribute(QStringLiteral("w:val"), QStringLiteral("0"));
        }
    }

    /// What of `look` the style does not already say. `always` writes an (empty) rPr anyway.
    void runProperties(const Doc::Look &look, const Doc::Look &base, bool link, bool always = false)
    {
        const bool same = look == base && !link;
        if (same && !always) {
            return;
        }
        m_x->writeStartElement(QStringLiteral("w:rPr"));
        if (link) {
            val(QStringLiteral("w:rStyle"), QStringLiteral("Hyperlink"));
        }
        if (look.family != base.family) {
            m_x->writeEmptyElement(QStringLiteral("w:rFonts"));
            m_x->writeAttribute(QStringLiteral("w:ascii"), look.family);
            m_x->writeAttribute(QStringLiteral("w:hAnsi"), look.family);
            m_x->writeAttribute(QStringLiteral("w:cs"), look.family);
        }
        if (look.bold != base.bold) {
            toggle(QStringLiteral("w:b"), look.bold);
        }
        if (look.italic != base.italic) {
            toggle(QStringLiteral("w:i"), look.italic);
        }
        if (look.strike != base.strike) {
            toggle(QStringLiteral("w:strike"), look.strike);
        }
        if (look.color != base.color) {
            val(QStringLiteral("w:color"), hex(look.color));
        }
        if (!qFuzzyCompare(look.size, base.size)) {
            val(QStringLiteral("w:sz"), QString::number(qRound(look.size * 2)));
            val(QStringLiteral("w:szCs"), QString::number(qRound(look.size * 2)));
        }
        if (look.underline != base.underline) {
            val(QStringLiteral("w:u"), look.underline ? QStringLiteral("single") : QStringLiteral("none"));
        }
        if (look.background != base.background) {
            m_x->writeEmptyElement(QStringLiteral("w:shd"));
            m_x->writeAttribute(QStringLiteral("w:val"), QStringLiteral("clear"));
            m_x->writeAttribute(QStringLiteral("w:color"), QStringLiteral("auto"));
            m_x->writeAttribute(QStringLiteral("w:fill"), look.background.isValid() ? hex(look.background) : QStringLiteral("auto"));
        }
        if (look.vertical != base.vertical) {
            val(QStringLiteral("w:vertAlign"), look.vertical == 1 ? QStringLiteral("superscript") : look.vertical == 2 ? QStringLiteral("subscript") : QStringLiteral("baseline"));
        }
        m_x->writeEndElement();
    }

    static QString alignmentName(Qt::Alignment alignment)
    {
        if (alignment & Qt::AlignHCenter) {
            return QStringLiteral("center");
        }
        if (alignment & Qt::AlignRight) {
            return QStringLiteral("right");
        }
        if (alignment & Qt::AlignJustify) {
            return QStringLiteral("both");
        }
        return QStringLiteral("left");
    }

    static int lineValue(const QTextBlockFormat &format)
    {
        return format.lineHeightType() == QTextBlockFormat::ProportionalHeight && format.lineHeight() > 0 ? qRound(format.lineHeight() * 2.4) : 240;
    }

    /// The spacing, indents and shading of `format`; all of them for a style
    /// (`base` null), the ones that differ from the style for a paragraph.
    void paragraphLayout(const QTextBlockFormat &format, const QTextBlockFormat *base, bool inList)
    {
        const bool spacing = !base || !qFuzzyCompare(1 + format.topMargin(), 1 + base->topMargin()) || !qFuzzyCompare(1 + format.bottomMargin(), 1 + base->bottomMargin())
            || lineValue(format) != lineValue(*base);
        if (spacing) {
            m_x->writeEmptyElement(QStringLiteral("w:spacing"));
            m_x->writeAttribute(QStringLiteral("w:before"), QString::number(qRound(format.topMargin() * twipsPerPx)));
            m_x->writeAttribute(QStringLiteral("w:after"), QString::number(qRound(format.bottomMargin() * twipsPerPx)));
            m_x->writeAttribute(QStringLiteral("w:line"), QString::number(lineValue(format)));
            m_x->writeAttribute(QStringLiteral("w:lineRule"), QStringLiteral("auto"));
        }
        const bool indents = !inList && (base ? (!qFuzzyCompare(1 + format.leftMargin(), 1 + base->leftMargin()) || !qFuzzyCompare(1 + format.rightMargin(), 1 + base->rightMargin())
                                                 || !qFuzzyCompare(1 + format.textIndent(), 1 + base->textIndent()))
                                              : (format.leftMargin() != 0 || format.rightMargin() != 0 || format.textIndent() != 0));
        if (indents) {
            m_x->writeEmptyElement(QStringLiteral("w:ind"));
            m_x->writeAttribute(QStringLiteral("w:left"), QString::number(qRound(format.leftMargin() * twipsPerPx)));
            m_x->writeAttribute(QStringLiteral("w:right"), QString::number(qRound(format.rightMargin() * twipsPerPx)));
            if (format.textIndent() < 0) {
                m_x->writeAttribute(QStringLiteral("w:hanging"), QString::number(qRound(-format.textIndent() * twipsPerPx)));
            } else {
                m_x->writeAttribute(QStringLiteral("w:firstLine"), QString::number(qRound(format.textIndent() * twipsPerPx)));
            }
        }
    }

    static QColor fill(const QTextFormat &format)
    {
        const QBrush brush = format.background();
        if (!format.hasProperty(QTextFormat::BackgroundBrush) || brush.style() == Qt::NoBrush || brush.color().alpha() == 0) {
            return {};
        }
        QColor color = brush.color().toRgb();
        color.setAlpha(255);
        return color;
    }

    void shading(const QColor &color)
    {
        m_x->writeEmptyElement(QStringLiteral("w:shd"));
        m_x->writeAttribute(QStringLiteral("w:val"), QStringLiteral("clear"));
        m_x->writeAttribute(QStringLiteral("w:color"), QStringLiteral("auto"));
        m_x->writeAttribute(QStringLiteral("w:fill"), color.isValid() ? hex(color) : QStringLiteral("auto"));
    }

    int numberFor(QTextList *list)
    {
        auto it = m_numbers.constFind(list);
        if (it != m_numbers.constEnd()) {
            return it.value();
        }
        const int id = int(m_numbers.size()) + 1;
        m_numbers.insert(list, id);
        m_numberKinds.append(abstractFor(list->format().style()));
        return id;
    }

    void paragraph(const QTextBlock &block)
    {
        const QTextBlockFormat format = block.blockFormat();
        const Doc::Style &style = Doc::style(Doc::styleOf(block));
        const Doc::Look base = Doc::lookOf(style.chars);
        QTextList *list = block.textList();

        m_x->writeStartElement(QStringLiteral("w:p"));
        m_x->writeStartElement(QStringLiteral("w:pPr"));
        if (style.id != QLatin1String("body")) {
            val(QStringLiteral("w:pStyle"), wordStyleId(style.id));
        }
        if (format.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore) {
            m_x->writeEmptyElement(QStringLiteral("w:pageBreakBefore"));
        }
        if (list) {
            m_x->writeStartElement(QStringLiteral("w:numPr"));
            val(QStringLiteral("w:ilvl"), QString::number(qBound(0, list->format().indent() - 1, 8)));
            val(QStringLiteral("w:numId"), QString::number(numberFor(list)));
            m_x->writeEndElement();
        }
        const QColor background = fill(format);
        if (background != fill(style.block)) {
            shading(background);
        }
        paragraphLayout(format, &style.block, list != nullptr);
        if (format.hasProperty(QTextFormat::BlockAlignment) && alignmentName(format.alignment()) != QLatin1String("left")) {
            val(QStringLiteral("w:jc"), alignmentName(format.alignment()));
        }
        m_x->writeEndElement();

        QString openLink;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) {
                continue;
            }
            const QTextCharFormat chars = fragment.charFormat();
            const QString href = chars.isAnchor() ? chars.anchorHref() : QString();
            if (href != openLink) {
                if (!openLink.isEmpty()) {
                    m_x->writeEndElement();
                }
                if (!href.isEmpty()) {
                    m_x->writeStartElement(QStringLiteral("w:hyperlink"));
                    if (href.startsWith(QLatin1Char('#'))) {
                        m_x->writeAttribute(QStringLiteral("w:anchor"), href.mid(1));
                    } else {
                        auto link = m_links.find(href);
                        if (link == m_links.end()) {
                            link = m_links.insert(href, nextId());
                        }
                        m_x->writeAttribute(QStringLiteral("r:id"), link.value());
                    }
                }
                openLink = href;
            }
            if (chars.isImageFormat()) {
                const QTextImageFormat picture = chars.toImageFormat();
                for (qsizetype i = 0; i < fragment.length(); ++i) {
                    drawing(picture);
                }
                continue;
            }
            Doc::Look look = Doc::lookOf(chars);
            if (!href.isEmpty()) {
                // The Hyperlink style brings the colour and the line.
                look.color = base.color;
                look.underline = base.underline;
            }
            run(fragment.text(), look, base, !href.isEmpty());
        }
        if (!openLink.isEmpty()) {
            m_x->writeEndElement();
        }
        m_x->writeEndElement();
    }

    void run(const QString &text, const Doc::Look &look, const Doc::Look &base, bool link)
    {
        m_x->writeStartElement(QStringLiteral("w:r"));
        runProperties(look, base, link);
        QString pending;
        const auto flush = [&] {
            if (pending.isEmpty()) {
                return;
            }
            m_x->writeStartElement(QStringLiteral("w:t"));
            m_x->writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
            m_x->writeCharacters(pending);
            m_x->writeEndElement();
            pending.clear();
        };
        for (const QChar c : text) {
            if (c == QChar::LineSeparator || c == QLatin1Char('\n')) {
                flush();
                m_x->writeEmptyElement(QStringLiteral("w:br"));
            } else if (c == QLatin1Char('\t')) {
                flush();
                m_x->writeEmptyElement(QStringLiteral("w:tab"));
            } else if (c.unicode() >= 0x20 && c != QChar::ObjectReplacementCharacter) {
                pending += c;
            }
        }
        flush();
        m_x->writeEndElement();
    }

    void drawing(const QTextImageFormat &picture)
    {
        const QString name = picture.name();
        qsizetype index = -1;
        for (qsizetype i = 0; i < m_media.size(); ++i) {
            if (m_mediaNames.at(i) == name) {
                index = i;
            }
        }
        if (index < 0) {
            const QImage image = Doc::image(m_document, name);
            if (image.isNull()) {
                return;
            }
            Media media;
            QBuffer buffer(&media.bytes);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
            media.id = nextId();
            media.target = QStringLiteral("media/image%1.png").arg(m_media.size() + 1);
            media.size = image.size();
            m_media.append(media);
            m_mediaNames.append(name);
            index = m_media.size() - 1;
        }
        const Media &media = m_media.at(index);
        double width = picture.width() > 0 ? picture.width() : media.size.width();
        double height = picture.height() > 0 ? picture.height() : (picture.width() > 0 && media.size.width() > 0 ? picture.width() * media.size.height() / media.size.width() : media.size.height());
        const QString cx = QString::number(qRound64(width * emuPerPx));
        const QString cy = QString::number(qRound64(height * emuPerPx));
        const QString number = QString::number(++m_drawings);

        m_x->writeStartElement(QStringLiteral("w:r"));
        m_x->writeStartElement(QStringLiteral("w:drawing"));
        m_x->writeStartElement(QStringLiteral("wp:inline"));
        m_x->writeAttribute(QStringLiteral("distT"), QStringLiteral("0"));
        m_x->writeAttribute(QStringLiteral("distB"), QStringLiteral("0"));
        m_x->writeAttribute(QStringLiteral("distL"), QStringLiteral("0"));
        m_x->writeAttribute(QStringLiteral("distR"), QStringLiteral("0"));
        m_x->writeEmptyElement(QStringLiteral("wp:extent"));
        m_x->writeAttribute(QStringLiteral("cx"), cx);
        m_x->writeAttribute(QStringLiteral("cy"), cy);
        m_x->writeEmptyElement(QStringLiteral("wp:docPr"));
        m_x->writeAttribute(QStringLiteral("id"), number);
        m_x->writeAttribute(QStringLiteral("name"), QStringLiteral("Imagen ") + number);
        m_x->writeStartElement(QStringLiteral("a:graphic"));
        m_x->writeStartElement(QStringLiteral("a:graphicData"));
        m_x->writeAttribute(QStringLiteral("uri"), nsPic);
        m_x->writeStartElement(QStringLiteral("pic:pic"));
        m_x->writeStartElement(QStringLiteral("pic:nvPicPr"));
        m_x->writeEmptyElement(QStringLiteral("pic:cNvPr"));
        m_x->writeAttribute(QStringLiteral("id"), number);
        m_x->writeAttribute(QStringLiteral("name"), media.target.section(QLatin1Char('/'), -1));
        m_x->writeEmptyElement(QStringLiteral("pic:cNvPicPr"));
        m_x->writeEndElement();
        m_x->writeStartElement(QStringLiteral("pic:blipFill"));
        m_x->writeEmptyElement(QStringLiteral("a:blip"));
        m_x->writeAttribute(QStringLiteral("r:embed"), media.id);
        m_x->writeStartElement(QStringLiteral("a:stretch"));
        m_x->writeEmptyElement(QStringLiteral("a:fillRect"));
        m_x->writeEndElement();
        m_x->writeEndElement();
        m_x->writeStartElement(QStringLiteral("pic:spPr"));
        m_x->writeStartElement(QStringLiteral("a:xfrm"));
        m_x->writeEmptyElement(QStringLiteral("a:off"));
        m_x->writeAttribute(QStringLiteral("x"), QStringLiteral("0"));
        m_x->writeAttribute(QStringLiteral("y"), QStringLiteral("0"));
        m_x->writeEmptyElement(QStringLiteral("a:ext"));
        m_x->writeAttribute(QStringLiteral("cx"), cx);
        m_x->writeAttribute(QStringLiteral("cy"), cy);
        m_x->writeEndElement();
        m_x->writeStartElement(QStringLiteral("a:prstGeom"));
        m_x->writeAttribute(QStringLiteral("prst"), QStringLiteral("rect"));
        m_x->writeEmptyElement(QStringLiteral("a:avLst"));
        m_x->writeEndElement();
        m_x->writeEndElement(); // spPr
        m_x->writeEndElement(); // pic
        m_x->writeEndElement(); // graphicData
        m_x->writeEndElement(); // graphic
        m_x->writeEndElement(); // inline
        m_x->writeEndElement(); // drawing
        m_x->writeEndElement(); // r
    }

    void border(const QString &side)
    {
        m_x->writeEmptyElement(side);
        m_x->writeAttribute(QStringLiteral("w:val"), QStringLiteral("single"));
        m_x->writeAttribute(QStringLiteral("w:sz"), QStringLiteral("4"));
        m_x->writeAttribute(QStringLiteral("w:space"), QStringLiteral("0"));
        m_x->writeAttribute(QStringLiteral("w:color"), QStringLiteral("B4B4BA"));
    }

    void table(QTextTable *table)
    {
        const int columns = table->columns();
        const double total = m_setup.contentPx().width() * twipsPerPx;
        const QList<QTextLength> constraints = table->format().columnWidthConstraints();
        QList<int> widths;
        double share = 0;
        for (int column = 0; column < columns; ++column) {
            const QTextLength length = column < constraints.size() ? constraints.at(column) : QTextLength();
            share += length.type() == QTextLength::PercentageLength ? length.rawValue() : 100.0 / columns;
        }
        for (int column = 0; column < columns; ++column) {
            const QTextLength length = column < constraints.size() ? constraints.at(column) : QTextLength();
            const double part = length.type() == QTextLength::PercentageLength ? length.rawValue() : 100.0 / columns;
            widths << qRound(total * part / qMax(1.0, share));
        }

        m_x->writeStartElement(QStringLiteral("w:tbl"));
        m_x->writeStartElement(QStringLiteral("w:tblPr"));
        val(QStringLiteral("w:tblStyle"), QStringLiteral("TableGrid"));
        m_x->writeEmptyElement(QStringLiteral("w:tblW"));
        m_x->writeAttribute(QStringLiteral("w:w"), QStringLiteral("5000"));
        m_x->writeAttribute(QStringLiteral("w:type"), QStringLiteral("pct"));
        m_x->writeStartElement(QStringLiteral("w:tblBorders"));
        for (const char *side : {"w:top", "w:left", "w:bottom", "w:right", "w:insideH", "w:insideV"}) {
            border(QLatin1String(side));
        }
        m_x->writeEndElement();
        val(QStringLiteral("w:tblLayout"), QStringLiteral("fixed"));
        m_x->writeEndElement();
        m_x->writeStartElement(QStringLiteral("w:tblGrid"));
        for (const int width : std::as_const(widths)) {
            m_x->writeEmptyElement(QStringLiteral("w:gridCol"));
            m_x->writeAttribute(QStringLiteral("w:w"), QString::number(width));
        }
        m_x->writeEndElement();

        for (int row = 0; row < table->rows(); ++row) {
            m_x->writeStartElement(QStringLiteral("w:tr"));
            for (int column = 0; column < columns; ++column) {
                const QTextTableCell cell = table->cellAt(row, column);
                if (!cell.isValid() || cell.column() != column) {
                    // The rest of a cell that spans columns.
                    continue;
                }
                const int span = qMax(1, cell.columnSpan());
                int width = 0;
                for (int i = column; i < qMin(columns, column + span); ++i) {
                    width += widths.at(i);
                }
                const bool continued = cell.row() != row;
                m_x->writeStartElement(QStringLiteral("w:tc"));
                m_x->writeStartElement(QStringLiteral("w:tcPr"));
                m_x->writeEmptyElement(QStringLiteral("w:tcW"));
                m_x->writeAttribute(QStringLiteral("w:w"), QString::number(width));
                m_x->writeAttribute(QStringLiteral("w:type"), QStringLiteral("dxa"));
                if (span > 1) {
                    val(QStringLiteral("w:gridSpan"), QString::number(span));
                }
                if (continued) {
                    m_x->writeEmptyElement(QStringLiteral("w:vMerge"));
                } else if (cell.rowSpan() > 1) {
                    val(QStringLiteral("w:vMerge"), QStringLiteral("restart"));
                }
                const QColor background = fill(cell.format());
                if (background.isValid()) {
                    shading(background);
                }
                m_x->writeEndElement();
                if (continued) {
                    m_x->writeEmptyElement(QStringLiteral("w:p"));
                } else {
                    blocks(cell.begin(), cell.end());
                }
                m_x->writeEndElement();
            }
            m_x->writeEndElement();
        }
        m_x->writeEndElement();
    }

    /// A cell must end in a paragraph: returns whether the last thing written was one.
    void blocks(QTextFrame::iterator it, const QTextFrame::iterator &end)
    {
        bool paragraphLast = false;
        for (; it != end && !it.atEnd(); ++it) {
            if (QTextFrame *frame = it.currentFrame()) {
                if (auto *inner = qobject_cast<QTextTable *>(frame)) {
                    table(inner);
                    paragraphLast = false;
                } else {
                    blocks(frame->begin(), frame->end());
                }
            } else if (it.currentBlock().isValid()) {
                paragraph(it.currentBlock());
                paragraphLast = true;
            }
        }
        if (!paragraphLast) {
            m_x->writeEmptyElement(QStringLiteral("w:p"));
        }
    }

    void rootNamespaces(QXmlStreamWriter &x)
    {
        x.writeAttribute(QStringLiteral("xmlns:w"), nsW);
        x.writeAttribute(QStringLiteral("xmlns:r"), nsR);
        x.writeAttribute(QStringLiteral("xmlns:wp"), nsWp);
        x.writeAttribute(QStringLiteral("xmlns:a"), nsA);
        x.writeAttribute(QStringLiteral("xmlns:pic"), nsPic);
    }

    QByteArray documentXml()
    {
        QByteArray out;
        QXmlStreamWriter x(&out);
        m_x = &x;
        x.writeStartDocument(QStringLiteral("1.0"), true);
        x.writeStartElement(QStringLiteral("w:document"));
        rootNamespaces(x);
        x.writeStartElement(QStringLiteral("w:body"));
        QTextFrame *root = m_document->rootFrame();
        blocks(root->begin(), root->end());

        x.writeStartElement(QStringLiteral("w:sectPr"));
        if (hasHeader()) {
            x.writeEmptyElement(QStringLiteral("w:headerReference"));
            x.writeAttribute(QStringLiteral("w:type"), QStringLiteral("default"));
            x.writeAttribute(QStringLiteral("r:id"), QStringLiteral("rId3"));
        }
        if (hasFooter()) {
            x.writeEmptyElement(QStringLiteral("w:footerReference"));
            x.writeAttribute(QStringLiteral("w:type"), QStringLiteral("default"));
            x.writeAttribute(QStringLiteral("r:id"), QStringLiteral("rId4"));
        }
        const QSizeF page = m_setup.sizeCm();
        x.writeEmptyElement(QStringLiteral("w:pgSz"));
        x.writeAttribute(QStringLiteral("w:w"), QString::number(qRound(page.width() * twipsPerCm)));
        x.writeAttribute(QStringLiteral("w:h"), QString::number(qRound(page.height() * twipsPerCm)));
        if (m_setup.landscape) {
            x.writeAttribute(QStringLiteral("w:orient"), QStringLiteral("landscape"));
        }
        x.writeEmptyElement(QStringLiteral("w:pgMar"));
        x.writeAttribute(QStringLiteral("w:top"), QString::number(qRound(m_setup.top * twipsPerCm)));
        x.writeAttribute(QStringLiteral("w:right"), QString::number(qRound(m_setup.right * twipsPerCm)));
        x.writeAttribute(QStringLiteral("w:bottom"), QString::number(qRound(m_setup.bottom * twipsPerCm)));
        x.writeAttribute(QStringLiteral("w:left"), QString::number(qRound(m_setup.left * twipsPerCm)));
        x.writeAttribute(QStringLiteral("w:header"), QString::number(qRound(qMin(1.25, m_setup.top / 2) * twipsPerCm)));
        x.writeAttribute(QStringLiteral("w:footer"), QString::number(qRound(qMin(1.25, m_setup.bottom / 2) * twipsPerCm)));
        x.writeAttribute(QStringLiteral("w:gutter"), QStringLiteral("0"));
        x.writeEndElement(); // sectPr
        x.writeEndElement(); // body
        x.writeEndElement(); // document
        x.writeEndDocument();
        m_x = nullptr;
        return out;
    }

    /// The header (centred) or the footer (centred; with the page number at
    /// the right when there is one and words as well).
    QByteArray marginXml(bool header)
    {
        QByteArray out;
        QXmlStreamWriter x(&out);
        m_x = &x;
        x.writeStartDocument(QStringLiteral("1.0"), true);
        x.writeStartElement(header ? QStringLiteral("w:hdr") : QStringLiteral("w:ftr"));
        rootNamespaces(x);
        x.writeStartElement(QStringLiteral("w:p"));
        x.writeStartElement(QStringLiteral("w:pPr"));
        val(QStringLiteral("w:pStyle"), header ? QStringLiteral("Header") : QStringLiteral("Footer"));
        const QString text = (header ? m_setup.header : m_setup.footer).trimmed();
        const bool number = !header && m_setup.pageNumbers;
        const bool both = number && !text.isEmpty();
        const int width = qRound(m_setup.contentPx().width() * twipsPerPx);
        if (both) {
            x.writeStartElement(QStringLiteral("w:tabs"));
            x.writeEmptyElement(QStringLiteral("w:tab"));
            x.writeAttribute(QStringLiteral("w:val"), QStringLiteral("center"));
            x.writeAttribute(QStringLiteral("w:pos"), QString::number(width / 2));
            x.writeEmptyElement(QStringLiteral("w:tab"));
            x.writeAttribute(QStringLiteral("w:val"), QStringLiteral("right"));
            x.writeAttribute(QStringLiteral("w:pos"), QString::number(width));
            x.writeEndElement();
        } else {
            val(QStringLiteral("w:jc"), QStringLiteral("center"));
        }
        x.writeEndElement();
        const auto words = [&](const QString &what) {
            x.writeStartElement(QStringLiteral("w:r"));
            x.writeStartElement(QStringLiteral("w:t"));
            x.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
            x.writeCharacters(what);
            x.writeEndElement();
            x.writeEndElement();
        };
        const auto tab = [&] {
            x.writeStartElement(QStringLiteral("w:r"));
            x.writeEmptyElement(QStringLiteral("w:tab"));
            x.writeEndElement();
        };
        if (both) {
            tab();
        }
        if (!text.isEmpty()) {
            words(text);
        }
        if (both) {
            tab();
        }
        if (number) {
            x.writeStartElement(QStringLiteral("w:fldSimple"));
            x.writeAttribute(QStringLiteral("w:instr"), QStringLiteral(" PAGE "));
            words(QStringLiteral("1"));
            x.writeEndElement();
        }
        x.writeEndElement(); // p
        x.writeEndElement();
        x.writeEndDocument();
        m_x = nullptr;
        return out;
    }

    QByteArray stylesXml()
    {
        QByteArray out;
        QXmlStreamWriter x(&out);
        m_x = &x;
        x.writeStartDocument(QStringLiteral("1.0"), true);
        x.writeStartElement(QStringLiteral("w:styles"));
        x.writeAttribute(QStringLiteral("xmlns:w"), nsW);

        const Doc::Style &body = Doc::style(QStringLiteral("body"));
        const Doc::Look normal = Doc::lookOf(body.chars);
        x.writeStartElement(QStringLiteral("w:docDefaults"));
        x.writeStartElement(QStringLiteral("w:rPrDefault"));
        x.writeStartElement(QStringLiteral("w:rPr"));
        x.writeEmptyElement(QStringLiteral("w:rFonts"));
        x.writeAttribute(QStringLiteral("w:ascii"), normal.family);
        x.writeAttribute(QStringLiteral("w:hAnsi"), normal.family);
        x.writeAttribute(QStringLiteral("w:cs"), normal.family);
        x.writeAttribute(QStringLiteral("w:eastAsia"), normal.family);
        val(QStringLiteral("w:sz"), QString::number(qRound(normal.size * 2)));
        val(QStringLiteral("w:szCs"), QString::number(qRound(normal.size * 2)));
        x.writeEmptyElement(QStringLiteral("w:lang"));
        x.writeAttribute(QStringLiteral("w:val"), QStringLiteral("es-ES"));
        x.writeEndElement();
        x.writeEndElement();
        x.writeStartElement(QStringLiteral("w:pPrDefault"));
        x.writeEmptyElement(QStringLiteral("w:pPr"));
        x.writeEndElement();
        x.writeEndElement();

        for (const Doc::Style &style : Doc::styles()) {
            const bool isBody = style.id == QLatin1String("body");
            x.writeStartElement(QStringLiteral("w:style"));
            x.writeAttribute(QStringLiteral("w:type"), QStringLiteral("paragraph"));
            if (isBody) {
                x.writeAttribute(QStringLiteral("w:default"), QStringLiteral("1"));
            }
            x.writeAttribute(QStringLiteral("w:styleId"), wordStyleId(style.id));
            val(QStringLiteral("w:name"), wordStyleName(style.id));
            if (!isBody) {
                val(QStringLiteral("w:basedOn"), QStringLiteral("Normal"));
                val(QStringLiteral("w:next"), style.id == QLatin1String("quote") || style.id == QLatin1String("code") ? wordStyleId(style.id) : QStringLiteral("Normal"));
            }
            x.writeEmptyElement(QStringLiteral("w:qFormat"));
            x.writeStartElement(QStringLiteral("w:pPr"));
            if (style.heading > 0 || style.id == QLatin1String("title")) {
                x.writeEmptyElement(QStringLiteral("w:keepNext"));
            }
            const QColor background = fill(style.block);
            if (background.isValid()) {
                shading(background);
            }
            // Every style spells its spacing out: none inherits Normal's.
            paragraphLayout(style.block, nullptr, false);
            if (style.heading > 0) {
                val(QStringLiteral("w:outlineLvl"), QString::number(style.heading - 1));
            }
            x.writeEndElement();
            runProperties(Doc::lookOf(style.chars), normal, false);
            x.writeEndElement();
        }

        const auto margin = [&](const QString &id, const QString &name) {
            x.writeStartElement(QStringLiteral("w:style"));
            x.writeAttribute(QStringLiteral("w:type"), QStringLiteral("paragraph"));
            x.writeAttribute(QStringLiteral("w:styleId"), id);
            val(QStringLiteral("w:name"), name);
            val(QStringLiteral("w:basedOn"), QStringLiteral("Normal"));
            x.writeStartElement(QStringLiteral("w:pPr"));
            x.writeEmptyElement(QStringLiteral("w:spacing"));
            x.writeAttribute(QStringLiteral("w:before"), QStringLiteral("0"));
            x.writeAttribute(QStringLiteral("w:after"), QStringLiteral("0"));
            x.writeAttribute(QStringLiteral("w:line"), QStringLiteral("240"));
            x.writeAttribute(QStringLiteral("w:lineRule"), QStringLiteral("auto"));
            x.writeEndElement();
            x.writeStartElement(QStringLiteral("w:rPr"));
            val(QStringLiteral("w:color"), QStringLiteral("6E6E73"));
            val(QStringLiteral("w:sz"), QStringLiteral("18"));
            x.writeEndElement();
            x.writeEndElement();
        };
        margin(QStringLiteral("Header"), QStringLiteral("header"));
        margin(QStringLiteral("Footer"), QStringLiteral("footer"));

        x.writeStartElement(QStringLiteral("w:style"));
        x.writeAttribute(QStringLiteral("w:type"), QStringLiteral("character"));
        x.writeAttribute(QStringLiteral("w:styleId"), QStringLiteral("Hyperlink"));
        val(QStringLiteral("w:name"), QStringLiteral("Hyperlink"));
        x.writeStartElement(QStringLiteral("w:rPr"));
        val(QStringLiteral("w:color"), hex(Doc::linkFormat(QString()).foreground().color()));
        val(QStringLiteral("w:u"), QStringLiteral("single"));
        x.writeEndElement();
        x.writeEndElement();

        x.writeStartElement(QStringLiteral("w:style"));
        x.writeAttribute(QStringLiteral("w:type"), QStringLiteral("table"));
        x.writeAttribute(QStringLiteral("w:styleId"), QStringLiteral("TableGrid"));
        val(QStringLiteral("w:name"), QStringLiteral("Table Grid"));
        x.writeStartElement(QStringLiteral("w:tblPr"));
        x.writeStartElement(QStringLiteral("w:tblBorders"));
        for (const char *side : {"w:top", "w:left", "w:bottom", "w:right", "w:insideH", "w:insideV"}) {
            border(QLatin1String(side));
        }
        x.writeEndElement();
        x.writeStartElement(QStringLiteral("w:tblCellMar"));
        for (const char *side : {"w:top", "w:left", "w:bottom", "w:right"}) {
            x.writeEmptyElement(QLatin1String(side));
            x.writeAttribute(QStringLiteral("w:w"), QStringLiteral("75"));
            x.writeAttribute(QStringLiteral("w:type"), QStringLiteral("dxa"));
        }
        x.writeEndElement();
        x.writeEndElement();
        x.writeEndElement();

        x.writeEndElement();
        x.writeEndDocument();
        m_x = nullptr;
        return out;
    }

    QByteArray numberingXml()
    {
        QByteArray out;
        QXmlStreamWriter x(&out);
        m_x = &x;
        x.writeStartDocument(QStringLiteral("1.0"), true);
        x.writeStartElement(QStringLiteral("w:numbering"));
        x.writeAttribute(QStringLiteral("xmlns:w"), nsW);
        static const char *formats[] = {"bullet", "decimal", "lowerLetter", "upperLetter", "lowerRoman", "upperRoman"};
        static const QString bullets[] = {QStringLiteral("\u2022"), QStringLiteral("\u25E6"), QStringLiteral("\u25AA")};
        for (int kind = 1; kind <= 6; ++kind) {
            x.writeStartElement(QStringLiteral("w:abstractNum"));
            x.writeAttribute(QStringLiteral("w:abstractNumId"), QString::number(kind));
            val(QStringLiteral("w:multiLevelType"), QStringLiteral("hybridMultilevel"));
            for (int level = 0; level < 9; ++level) {
                x.writeStartElement(QStringLiteral("w:lvl"));
                x.writeAttribute(QStringLiteral("w:ilvl"), QString::number(level));
                val(QStringLiteral("w:start"), QStringLiteral("1"));
                val(QStringLiteral("w:numFmt"), QLatin1String(formats[kind - 1]));
                val(QStringLiteral("w:lvlText"), kind == 1 ? bullets[level % 3] : QStringLiteral("%%1.").arg(level + 1));
                val(QStringLiteral("w:lvlJc"), QStringLiteral("left"));
                x.writeStartElement(QStringLiteral("w:pPr"));
                x.writeEmptyElement(QStringLiteral("w:ind"));
                x.writeAttribute(QStringLiteral("w:left"), QString::number(720 * (level + 1)));
                x.writeAttribute(QStringLiteral("w:hanging"), QStringLiteral("360"));
                x.writeEndElement();
                x.writeEndElement();
            }
            x.writeEndElement();
        }
        for (int i = 0; i < m_numberKinds.size(); ++i) {
            x.writeStartElement(QStringLiteral("w:num"));
            x.writeAttribute(QStringLiteral("w:numId"), QString::number(i + 1));
            val(QStringLiteral("w:abstractNumId"), QString::number(m_numberKinds.at(i)));
            {
                // Each list is its own and counts from one, not on from the list before.
                for (int level = 0; level < 9; ++level) {
                    x.writeStartElement(QStringLiteral("w:lvlOverride"));
                    x.writeAttribute(QStringLiteral("w:ilvl"), QString::number(level));
                    val(QStringLiteral("w:startOverride"), QStringLiteral("1"));
                    x.writeEndElement();
                }
            }
            x.writeEndElement();
        }
        x.writeEndElement();
        x.writeEndDocument();
        m_x = nullptr;
        return out;
    }

    const QTextDocument *m_document;
    Doc::PageSetup m_setup;
    QXmlStreamWriter *m_x = nullptr;
    QHash<QTextList *, int> m_numbers;
    QList<int> m_numberKinds;
    QList<Media> m_media;
    QStringList m_mediaNames;
    QMap<QString, QString> m_links;
    int m_nextId = 5;
    int m_drawings = 0;
};

// --- Reading ----------------------------------------------------------------------

QString local(const QDomElement &element)
{
    const QString name = element.tagName();
    return name.mid(name.indexOf(QLatin1Char(':')) + 1);
}

bool is(const QDomElement &element, const char *name)
{
    return local(element) == QLatin1String(name);
}

QDomElement child(const QDomElement &parent, const char *name)
{
    for (QDomElement e = parent.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
        if (is(e, name)) {
            return e;
        }
    }
    return {};
}

/// The attribute of that name, whatever its prefix.
QString attr(const QDomElement &element, const char *name)
{
    const QDomNamedNodeMap all = element.attributes();
    for (int i = 0; i < all.count(); ++i) {
        const QString full = all.item(i).nodeName();
        if (full.mid(full.indexOf(QLatin1Char(':')) + 1) == QLatin1String(name)) {
            return all.item(i).nodeValue();
        }
    }
    return {};
}

bool hasAttr(const QDomElement &element, const char *name)
{
    const QDomNamedNodeMap all = element.attributes();
    for (int i = 0; i < all.count(); ++i) {
        const QString full = all.item(i).nodeName();
        if (full.mid(full.indexOf(QLatin1Char(':')) + 1) == QLatin1String(name)) {
            return true;
        }
    }
    return false;
}

/// <w:b/> is on; <w:b w:val="0"/> is off.
bool on(const QDomElement &element)
{
    const QString value = attr(element, "val");
    return value != QLatin1String("0") && value != QLatin1String("false") && value != QLatin1String("off") && value != QLatin1String("none");
}

QDomDocument parse(const QByteArray &xml)
{
    QDomDocument document;
    document.setContent(xml, QDomDocument::ParseOption::PreserveSpacingOnlyNodes);
    return document;
}

QColor namedHighlight(const QString &name)
{
    static const QHash<QString, QString> colors = {
        {QStringLiteral("yellow"), QStringLiteral("#ffff00")}, {QStringLiteral("green"), QStringLiteral("#00ff00")},
        {QStringLiteral("cyan"), QStringLiteral("#00ffff")}, {QStringLiteral("magenta"), QStringLiteral("#ff00ff")},
        {QStringLiteral("blue"), QStringLiteral("#0000ff")}, {QStringLiteral("red"), QStringLiteral("#ff0000")},
        {QStringLiteral("darkBlue"), QStringLiteral("#000080")}, {QStringLiteral("darkCyan"), QStringLiteral("#008080")},
        {QStringLiteral("darkGreen"), QStringLiteral("#008000")}, {QStringLiteral("darkMagenta"), QStringLiteral("#800080")},
        {QStringLiteral("darkRed"), QStringLiteral("#800000")}, {QStringLiteral("darkYellow"), QStringLiteral("#808000")},
        {QStringLiteral("darkGray"), QStringLiteral("#808080")}, {QStringLiteral("lightGray"), QStringLiteral("#c0c0c0")},
        {QStringLiteral("black"), QStringLiteral("#000000")}, {QStringLiteral("white"), QStringLiteral("#ffffff")},
    };
    const QString value = colors.value(name);
    return value.isEmpty() ? QColor() : QColor(value);
}

QColor hexColor(const QString &value)
{
    if (value.size() != 6) {
        return {};
    }
    const QColor color(QLatin1Char('#') + value);
    return color;
}

class Reader
{
public:
    Reader(const ZipReader &zip, QTextDocument *document, Doc::PageSetup *setup)
        : m_zip(zip)
        , m_document(document)
        , m_setup(setup)
        , m_cursor(document)
    {
    }

    bool run(QString *error)
    {
        // The main part is wherever the package says it is.
        QString main = QStringLiteral("word/document.xml");
        const QDomDocument top = parse(m_zip.file(QStringLiteral("_rels/.rels")));
        for (QDomElement r = top.documentElement().firstChildElement(); !r.isNull(); r = r.nextSiblingElement()) {
            if (attr(r, "Type").endsWith(QLatin1String("/officeDocument"))) {
                main = attr(r, "Target");
                if (main.startsWith(QLatin1Char('/'))) {
                    main.remove(0, 1);
                }
            }
        }
        const QByteArray xml = m_zip.file(main);
        if (xml.isNull()) {
            if (error) {
                *error = QStringLiteral("El archivo no contiene un documento de Word.");
            }
            return false;
        }
        m_folder = main.contains(QLatin1Char('/')) ? main.section(QLatin1Char('/'), 0, -2) + QLatin1Char('/') : QString();
        const QString file = main.section(QLatin1Char('/'), -1);
        m_relations = relations(m_folder + QStringLiteral("_rels/") + file + QStringLiteral(".rels"));

        readTheme();
        readStyles();
        readNumbering();

        // A part that is not XML to its end is a broken document, not a short one.
        QDomDocument dom;
        const bool whole = bool(dom.setContent(xml, QDomDocument::ParseOption::PreserveSpacingOnlyNodes));
        const QDomElement body = child(dom.documentElement(), "body");
        if (!whole || body.isNull()) {
            if (error) {
                *error = QStringLiteral("El documento de Word está dañado.");
            }
            return false;
        }
        m_cursor.beginEditBlock();
        blocks(body);
        m_cursor.endEditBlock();
        QDomElement section = child(body, "sectPr");
        if (section.isNull()) {
            // A document may keep its only section in its last paragraph.
            const QDomNodeList all = body.elementsByTagName(QStringLiteral("w:sectPr"));
            if (!all.isEmpty()) {
                section = all.at(all.count() - 1).toElement();
            }
        }
        if (!section.isNull() && m_setup) {
            readSection(section);
        }
        return true;
    }

private:
    struct Relation {
        QString target;
        bool external = false;
    };
    struct StyleInfo {
        QString id;
        QString name;
        QString parent;
        QTextBlockFormat block;
        QTextCharFormat chars;
        /// The Textos style this is, if any.
        QString mapped;
        bool resolved = false;
    };
    struct Level {
        QTextListFormat::Style style = QTextListFormat::ListDisc;
        int start = 1;
        /// Counts for the outline only: nothing is drawn before the paragraph.
        bool silent = false;
    };

    QHash<QString, Relation> relations(const QString &path) const
    {
        QHash<QString, Relation> all;
        const QDomDocument dom = parse(m_zip.file(path));
        for (QDomElement r = dom.documentElement().firstChildElement(); !r.isNull(); r = r.nextSiblingElement()) {
            Relation relation;
            relation.target = attr(r, "Target");
            relation.external = attr(r, "TargetMode") == QLatin1String("External");
            all.insert(attr(r, "Id"), relation);
        }
        return all;
    }

    QString partPath(const QString &target) const
    {
        if (target.startsWith(QLatin1Char('/'))) {
            return target.mid(1);
        }
        QStringList parts = (m_folder + target).split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QStringList clean;
        for (const QString &part : std::as_const(parts)) {
            if (part == QLatin1String("..")) {
                if (!clean.isEmpty()) {
                    clean.removeLast();
                }
            } else if (part != QLatin1String(".")) {
                clean << part;
            }
        }
        return clean.join(QLatin1Char('/'));
    }

    void readTheme()
    {
        QString path = m_folder + QStringLiteral("theme/theme1.xml");
        for (const Relation &relation : std::as_const(m_relations)) {
            if (relation.target.contains(QLatin1String("theme"))) {
                path = partPath(relation.target);
            }
        }
        const QDomDocument dom = parse(m_zip.file(path));
        const QDomNodeList major = dom.elementsByTagName(QStringLiteral("a:majorFont"));
        const QDomNodeList minor = dom.elementsByTagName(QStringLiteral("a:minorFont"));
        if (!major.isEmpty()) {
            m_majorFont = attr(child(major.at(0).toElement(), "latin"), "typeface");
        }
        if (!minor.isEmpty()) {
            m_minorFont = attr(child(minor.at(0).toElement(), "latin"), "typeface");
        }
    }

    QTextCharFormat runFormat(const QDomElement &rPr) const
    {
        QTextCharFormat format;
        for (QDomElement e = rPr.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            const QString name = local(e);
            if (name == QLatin1String("rFonts")) {
                QString family = attr(e, "ascii");
                if (family.isEmpty()) {
                    family = attr(e, "hAnsi");
                }
                if (family.isEmpty()) {
                    const QString theme = attr(e, "asciiTheme");
                    if (!theme.isEmpty()) {
                        family = theme.startsWith(QLatin1String("major")) ? m_majorFont : m_minorFont;
                    }
                }
                if (!family.isEmpty()) {
                    format.setFontFamilies({family});
                }
            } else if (name == QLatin1String("b")) {
                format.setFontWeight(on(e) ? QFont::Bold : QFont::Normal);
            } else if (name == QLatin1String("i")) {
                format.setFontItalic(on(e));
            } else if (name == QLatin1String("u")) {
                format.setFontUnderline(on(e));
            } else if (name == QLatin1String("strike") || name == QLatin1String("dstrike")) {
                format.setFontStrikeOut(on(e));
            } else if (name == QLatin1String("caps")) {
                format.setFontCapitalization(on(e) ? QFont::AllUppercase : QFont::MixedCase);
            } else if (name == QLatin1String("smallCaps")) {
                format.setFontCapitalization(on(e) ? QFont::SmallCaps : QFont::MixedCase);
            } else if (name == QLatin1String("color")) {
                const QColor color = hexColor(attr(e, "val"));
                if (color.isValid()) {
                    format.setForeground(color);
                } else {
                    format.clearForeground();
                }
            } else if (name == QLatin1String("sz")) {
                const double half = attr(e, "val").toDouble();
                if (half > 0) {
                    format.setFontPointSize(half / 2);
                }
            } else if (name == QLatin1String("highlight")) {
                const QColor color = namedHighlight(attr(e, "val"));
                if (color.isValid()) {
                    format.setBackground(color);
                } else {
                    format.clearBackground();
                }
            } else if (name == QLatin1String("shd")) {
                const QColor color = hexColor(attr(e, "fill"));
                if (color.isValid()) {
                    format.setBackground(color);
                } else if (!format.hasProperty(QTextFormat::BackgroundBrush)) {
                    format.clearBackground();
                }
            } else if (name == QLatin1String("vertAlign")) {
                const QString value = attr(e, "val");
                format.setVerticalAlignment(value == QLatin1String("superscript") ? QTextCharFormat::AlignSuperScript
                    : value == QLatin1String("subscript") ? QTextCharFormat::AlignSubScript : QTextCharFormat::AlignNormal);
            }
        }
        return format;
    }

    QTextBlockFormat paragraphFormat(const QDomElement &pPr) const
    {
        QTextBlockFormat format;
        for (QDomElement e = pPr.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            const QString name = local(e);
            if (name == QLatin1String("jc")) {
                const QString value = attr(e, "val");
                format.setAlignment(value == QLatin1String("center") ? Qt::AlignHCenter
                    : value == QLatin1String("right") || value == QLatin1String("end") ? Qt::AlignRight
                    : value == QLatin1String("both") || value == QLatin1String("distribute") ? Qt::AlignJustify : Qt::AlignLeft);
            } else if (name == QLatin1String("ind")) {
                const QString left = hasAttr(e, "left") ? attr(e, "left") : attr(e, "start");
                const QString right = hasAttr(e, "right") ? attr(e, "right") : attr(e, "end");
                if (!left.isEmpty()) {
                    format.setLeftMargin(left.toDouble() / twipsPerPx);
                }
                if (!right.isEmpty()) {
                    format.setRightMargin(right.toDouble() / twipsPerPx);
                }
                if (hasAttr(e, "hanging")) {
                    format.setTextIndent(-attr(e, "hanging").toDouble() / twipsPerPx);
                } else if (hasAttr(e, "firstLine")) {
                    format.setTextIndent(attr(e, "firstLine").toDouble() / twipsPerPx);
                }
            } else if (name == QLatin1String("spacing")) {
                if (hasAttr(e, "before")) {
                    format.setTopMargin(attr(e, "before").toDouble() / twipsPerPx);
                }
                if (hasAttr(e, "after")) {
                    format.setBottomMargin(attr(e, "after").toDouble() / twipsPerPx);
                }
                if (hasAttr(e, "line")) {
                    const double line = attr(e, "line").toDouble();
                    const QString rule = attr(e, "lineRule");
                    if (rule == QLatin1String("exact")) {
                        format.setLineHeight(line / twipsPerPx, QTextBlockFormat::FixedHeight);
                    } else if (rule == QLatin1String("atLeast")) {
                        format.setLineHeight(line / twipsPerPx, QTextBlockFormat::MinimumHeight);
                    } else if (line > 0) {
                        format.setLineHeight(line / 2.4, QTextBlockFormat::ProportionalHeight);
                    }
                }
            } else if (name == QLatin1String("pageBreakBefore")) {
                format.setPageBreakPolicy(on(e) ? QTextFormat::PageBreak_AlwaysBefore : QTextFormat::PageBreak_Auto);
            } else if (name == QLatin1String("shd")) {
                const QColor color = hexColor(attr(e, "fill"));
                if (color.isValid()) {
                    format.setBackground(color);
                } else {
                    format.setBackground(Qt::NoBrush);
                }
            } else if (name == QLatin1String("outlineLvl")) {
                const int level = attr(e, "val").toInt();
                format.setHeadingLevel(level >= 0 && level < 9 ? qMin(level + 1, 6) : 0);
            } else if (name == QLatin1String("numPr")) {
                const QDomElement id = child(e, "numId");
                if (!id.isNull()) {
                    format.setProperty(NumIdProperty, attr(id, "val").toInt());
                }
                const QDomElement level = child(e, "ilvl");
                if (!level.isNull()) {
                    format.setProperty(NumLevelProperty, attr(level, "val").toInt());
                }
            }
        }
        return format;
    }

    static QString mapStyle(const QString &id, const QString &name)
    {
        const QString n = name.toLower().simplified();
        const QString i = id.toLower();
        const auto either = [&](const QStringList &names) {
            return names.contains(n) || names.contains(i);
        };
        if (either({QStringLiteral("title"), QStringLiteral("título"), QStringLiteral("titulo"), QStringLiteral("ttulo")})) {
            return QStringLiteral("title");
        }
        for (int level = 1; level <= 3; ++level) {
            const QString number = QString::number(level);
            if (either({QStringLiteral("heading ") + number, QStringLiteral("heading") + number, QStringLiteral("título ") + number,
                        QStringLiteral("ttulo") + number, QStringLiteral("encabezado ") + number, QStringLiteral("titre ") + number})) {
                return QLatin1Char('h') + number;
            }
        }
        if (either({QStringLiteral("quote"), QStringLiteral("cita"), QStringLiteral("quotations"), QStringLiteral("intense quote")})) {
            return QStringLiteral("quote");
        }
        if (either({QStringLiteral("code"), QStringLiteral("código"), QStringLiteral("preformatted text"), QStringLiteral("html preformatted"),
                    QStringLiteral("source code"), QStringLiteral("preformattedtext"), QStringLiteral("htmlpreformatted"), QStringLiteral("sourcecode")})) {
            return QStringLiteral("code");
        }
        return {};
    }

    void readStyles()
    {
        // What Word assumes when a document says nothing.
        m_defaultChars.setFontPointSize(10);
        m_defaultChars.setFontFamilies({QStringLiteral("Times New Roman")});
        QString path = m_folder + QStringLiteral("styles.xml");
        for (const Relation &relation : std::as_const(m_relations)) {
            if (relation.target.endsWith(QLatin1String("styles.xml"))) {
                path = partPath(relation.target);
            }
        }
        const QByteArray part = m_zip.file(path);
        if (part.isEmpty()) {
            // Nothing said of how it looks: it looks as a document of Textos does.
            m_defaultChars.setFontPointSize(Doc::defaultPointSize);
            m_defaultChars.setFontFamilies({Doc::defaultFamily()});
        }
        const QDomDocument dom = parse(part);
        const QDomElement root = dom.documentElement();
        const QDomElement defaults = child(root, "docDefaults");
        m_defaultChars.merge(runFormat(child(child(defaults, "rPrDefault"), "rPr")));
        m_defaultBlock = paragraphFormat(child(child(defaults, "pPrDefault"), "pPr"));
        for (QDomElement e = root.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            if (!is(e, "style")) {
                continue;
            }
            StyleInfo info;
            info.id = attr(e, "styleId");
            info.name = attr(child(e, "name"), "val");
            info.parent = attr(child(e, "basedOn"), "val");
            info.block = paragraphFormat(child(e, "pPr"));
            info.chars = runFormat(child(e, "rPr"));
            const QString type = attr(e, "type");
            if (type == QLatin1String("paragraph")) {
                info.mapped = mapStyle(info.id, info.name);
                if (on(e) && hasAttr(e, "default")) {
                    m_defaultStyle = info.id;
                }
            }
            m_styles.insert(info.id, info);
        }
    }

    const StyleInfo *resolved(const QString &id, int depth = 0)
    {
        auto it = m_styles.find(id);
        if (it == m_styles.end()) {
            return nullptr;
        }
        StyleInfo &info = it.value();
        if (!info.resolved) {
            info.resolved = true;
            if (!info.parent.isEmpty() && info.parent != id && depth < 20) {
                if (const StyleInfo *parent = resolved(info.parent, depth + 1)) {
                    QTextBlockFormat block = parent->block;
                    block.merge(info.block);
                    info.block = block;
                    QTextCharFormat chars = parent->chars;
                    chars.merge(info.chars);
                    info.chars = chars;
                    if (info.mapped.isEmpty()) {
                        info.mapped = parent->mapped;
                    }
                }
            }
        }
        return &info;
    }

    void readNumbering()
    {
        QString path = m_folder + QStringLiteral("numbering.xml");
        for (const Relation &relation : std::as_const(m_relations)) {
            if (relation.target.endsWith(QLatin1String("numbering.xml"))) {
                path = partPath(relation.target);
            }
        }
        const QDomDocument dom = parse(m_zip.file(path));
        const QDomElement root = dom.documentElement();
        QHash<QString, QHash<int, Level>> abstracts;
        for (QDomElement e = root.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            if (!is(e, "abstractNum")) {
                continue;
            }
            QHash<int, Level> levels;
            for (QDomElement l = e.firstChildElement(); !l.isNull(); l = l.nextSiblingElement()) {
                if (!is(l, "lvl")) {
                    continue;
                }
                const int index = attr(l, "ilvl").toInt();
                const QString format = attr(child(l, "numFmt"), "val");
                Level level;
                if (format == QLatin1String("bullet")) {
                    static const QTextListFormat::Style marks[] = {QTextListFormat::ListDisc, QTextListFormat::ListCircle, QTextListFormat::ListSquare};
                    level.style = marks[index % 3];
                } else if (format == QLatin1String("lowerLetter")) {
                    level.style = QTextListFormat::ListLowerAlpha;
                } else if (format == QLatin1String("upperLetter")) {
                    level.style = QTextListFormat::ListUpperAlpha;
                } else if (format == QLatin1String("lowerRoman")) {
                    level.style = QTextListFormat::ListLowerRoman;
                } else if (format == QLatin1String("upperRoman")) {
                    level.style = QTextListFormat::ListUpperRoman;
                } else if (format == QLatin1String("none")) {
                    level.style = QTextListFormat::ListDisc;
                    level.silent = true;
                } else {
                    level.style = QTextListFormat::ListDecimal;
                }
                const QDomElement start = child(l, "start");
                level.start = start.isNull() ? 1 : attr(start, "val").toInt();
                levels.insert(index, level);
            }
            abstracts.insert(attr(e, "abstractNumId"), levels);
        }
        for (QDomElement e = root.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            if (is(e, "num")) {
                const int id = attr(e, "numId").toInt();
                const QString abstract = attr(child(e, "abstractNumId"), "val");
                m_numbering.insert(id, abstracts.value(abstract));
                m_abstractOf.insert(id, abstract);
                if (!e.elementsByTagName(QStringLiteral("w:startOverride")).isEmpty()) {
                    m_restarts.insert(id);
                }
            }
        }
    }

    void readSection(const QDomElement &section)
    {
        const QDomElement size = child(section, "pgSz");
        if (!size.isNull()) {
            m_setup->setSizeCm(attr(size, "w").toDouble() / twipsPerCm, attr(size, "h").toDouble() / twipsPerCm);
        }
        const QDomElement margins = child(section, "pgMar");
        if (!margins.isNull()) {
            const auto cm = [&](const char *name, double fallback) {
                return hasAttr(margins, name) ? qBound(0.0, qRound(attr(margins, name).toDouble() / twipsPerCm * 100) / 100.0, 10.0) : fallback;
            };
            m_setup->top = cm("top", m_setup->top);
            m_setup->bottom = cm("bottom", m_setup->bottom);
            m_setup->left = cm("left", m_setup->left);
            m_setup->right = cm("right", m_setup->right);
        }
        for (QDomElement e = section.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            const bool header = is(e, "headerReference");
            if ((!header && !is(e, "footerReference")) || (hasAttr(e, "type") && attr(e, "type") != QLatin1String("default"))) {
                continue;
            }
            const Relation relation = m_relations.value(attr(e, "id"));
            if (relation.target.isEmpty()) {
                continue;
            }
            const QDomDocument dom = parse(m_zip.file(partPath(relation.target)));
            bool number = false;
            const QString text = marginText(dom.documentElement(), &number).simplified();
            if (header) {
                m_setup->header = text;
            } else {
                m_setup->footer = text;
                m_setup->pageNumbers = number;
            }
        }
    }

    /// The words of a header or footer, without the result of its PAGE field.
    QString marginText(const QDomElement &element, bool *number)
    {
        QString text;
        bool inField = false;
        bool pageField = false;
        const std::function<void(const QDomElement &)> walk = [&](const QDomElement &parent) {
            for (QDomElement e = parent.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
                const QString name = local(e);
                if (name == QLatin1String("fldSimple")) {
                    if (attr(e, "instr").contains(QLatin1String("PAGE"))) {
                        *number = true;
                    } else {
                        walk(e);
                    }
                } else if (name == QLatin1String("fldChar")) {
                    const QString type = attr(e, "fldCharType");
                    if (type == QLatin1String("begin")) {
                        inField = true;
                        pageField = false;
                    } else if (type == QLatin1String("end")) {
                        inField = false;
                        pageField = false;
                    }
                } else if (name == QLatin1String("instrText")) {
                    if (e.text().contains(QLatin1String("PAGE"))) {
                        *number = true;
                        pageField = true;
                    }
                } else if (name == QLatin1String("t")) {
                    if (!(inField && pageField)) {
                        text += e.text();
                    }
                } else if (name == QLatin1String("tab")) {
                    if (!is(parent, "tabs")) {
                        text += QLatin1Char(' ');
                    }
                } else if (name == QLatin1String("p")) {
                    walk(e);
                    text += QLatin1Char(' ');
                } else {
                    walk(e);
                }
            }
        };
        walk(element);
        return text;
    }

    // --- The body ---

    void blocks(const QDomElement &parent)
    {
        for (QDomElement e = parent.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            const QString name = local(e);
            if (name == QLatin1String("p")) {
                paragraph(e);
            } else if (name == QLatin1String("tbl")) {
                table(e);
            } else if (name == QLatin1String("sdt")) {
                blocks(child(e, "sdtContent"));
            } else if (name == QLatin1String("ins") || name == QLatin1String("customXml") || name == QLatin1String("smartTag")) {
                blocks(e);
            }
        }
    }

    void paragraph(const QDomElement &p)
    {
        const QDomElement pPr = child(p, "pPr");
        QString styleId = attr(child(pPr, "pStyle"), "val");
        if (styleId.isEmpty()) {
            styleId = m_defaultStyle;
        }
        const StyleInfo *style = resolved(styleId);
        // A style the document names without describing is still known by its name.
        const QString mapped = style ? style->mapped : mapStyle(styleId, QString());

        QTextBlockFormat block = m_defaultBlock;
        block.merge(m_tableBlock);
        QTextCharFormat chars = m_defaultChars;
        if (style) {
            block.merge(style->block);
            chars.merge(style->chars);
        }
        block.merge(paragraphFormat(pPr));
        if (!style && !mapped.isEmpty()) {
            block.merge(Doc::style(mapped).block);
            chars.merge(Doc::style(mapped).chars);
            block.merge(paragraphFormat(pPr));
        }
        if (!mapped.isEmpty()) {
            block.setProperty(Doc::StyleProperty, mapped);
            if (const int heading = Doc::style(mapped).heading) {
                block.setHeadingLevel(heading);
            }
        }
        int numId = block.intProperty(NumIdProperty);
        const int level = qBound(0, block.intProperty(NumLevelProperty), 8);
        if (numId > 0 && m_numbering.value(numId).value(level).silent) {
            // What LibreOffice hangs its headings from: a numbering that shows nothing.
            numId = 0;
        }
        block.clearProperty(NumIdProperty);
        block.clearProperty(NumLevelProperty);
        if (m_breakPending) {
            block.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
            m_breakPending = false;
        }
        if (numId > 0 && m_numbering.contains(numId)) {
            // The numbering sets the indents of a list item; Qt does the same from the list.
            block.setLeftMargin(0);
            block.setTextIndent(0);
        }

        if (m_unused) {
            m_cursor.setBlockFormat(block);
            m_cursor.setBlockCharFormat(chars);
            m_unused = false;
        } else {
            m_cursor.insertBlock(block, chars);
        }
        m_cursor.setCharFormat(chars);
        if (numId > 0 && m_numbering.contains(numId)) {
            // Numberings of one definition are one list going on, as Word counts
            // them, until one of them says it starts again.
            const QString abstract = m_abstractOf.value(numId);
            if (m_restarts.contains(numId) && !m_seenNums.contains(numId)) {
                for (auto it = m_lists.begin(); it != m_lists.end();) {
                    it = it.key().first == abstract ? m_lists.erase(it) : std::next(it);
                }
            }
            m_seenNums.insert(numId);
            const QPair<QString, int> key(abstract, level);
            QTextList *list = m_lists.value(key);
            if (list) {
                list->add(m_cursor.block());
            } else {
                const Level definition = m_numbering.value(numId).value(level);
                QTextListFormat format;
                format.setStyle(definition.style);
                format.setIndent(level + 1);
                if (definition.start > 1 && definition.style != QTextListFormat::ListDisc) {
                    format.setStart(definition.start);
                }
                m_lists.insert(key, m_cursor.createList(format));
            }
        }

        m_paragraphBlock = block;
        m_paragraphChars = chars;
        m_paragraphText = false;
        m_fieldDepth = 0;
        inlines(p, chars, QString());
        if (m_breakPending && !m_paragraphText) {
            // A paragraph that only holds a page break: the break belongs to the next one.
            m_unused = true;
        }
    }

    void text(const QString &what, const QTextCharFormat &format)
    {
        if (what.isEmpty()) {
            return;
        }
        if (m_breakPending && m_paragraphText) {
            QTextBlockFormat block = m_paragraphBlock;
            block.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
            m_cursor.insertBlock(block, m_paragraphChars);
            m_breakPending = false;
        } else if (m_breakPending) {
            // The break comes before any of its text: the paragraph itself starts the page.
            m_paragraphBlock.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
            QTextBlockFormat block = m_cursor.blockFormat();
            block.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
            m_cursor.setBlockFormat(block);
            m_breakPending = false;
        }
        m_cursor.insertText(what, format);
        m_paragraphText = true;
    }

    void inlines(const QDomElement &parent, const QTextCharFormat &base, const QString &href)
    {
        for (QDomElement e = parent.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            const QString name = local(e);
            if (name == QLatin1String("r")) {
                runElement(e, base, href);
            } else if (name == QLatin1String("hyperlink")) {
                QString target;
                const QString id = attr(e, "id");
                if (!id.isEmpty()) {
                    target = m_relations.value(id).target;
                }
                const QString anchor = attr(e, "anchor");
                if (!anchor.isEmpty()) {
                    target += QLatin1Char('#') + anchor;
                }
                inlines(e, base, target);
            } else if (name == QLatin1String("ins") || name == QLatin1String("smartTag") || name == QLatin1String("fldSimple") || name == QLatin1String("customXml")
                       || name == QLatin1String("moveTo")) {
                inlines(e, base, href);
            } else if (name == QLatin1String("sdt")) {
                inlines(child(e, "sdtContent"), base, href);
            }
        }
    }

    void runElement(const QDomElement &r, const QTextCharFormat &base, const QString &href)
    {
        const QDomElement rPr = child(r, "rPr");
        QTextCharFormat format = base;
        const QString characterStyle = attr(child(rPr, "rStyle"), "val");
        if (!characterStyle.isEmpty()) {
            if (const StyleInfo *style = resolved(characterStyle)) {
                format.merge(style->chars);
            }
        }
        format.merge(runFormat(rPr));
        if (!href.isEmpty()) {
            format.merge(Doc::linkFormat(href));
        }
        for (QDomElement e = r.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            const QString name = local(e);
            if (name == QLatin1String("t")) {
                text(e.text(), format);
            } else if (name == QLatin1String("tab") || name == QLatin1String("ptab")) {
                text(QStringLiteral("\t"), format);
            } else if (name == QLatin1String("br") || name == QLatin1String("cr")) {
                const QString type = attr(e, "type");
                if (type == QLatin1String("page") || type == QLatin1String("column")) {
                    m_breakPending = true;
                } else {
                    text(QString(QChar::LineSeparator), format);
                }
            } else if (name == QLatin1String("noBreakHyphen")) {
                text(QStringLiteral("\u2011"), format);
            } else if (name == QLatin1String("drawing") || name == QLatin1String("pict") || name == QLatin1String("object")) {
                picture(e);
            } else if (name == QLatin1String("AlternateContent")) {
                QDomElement branch = child(e, "Choice");
                if (branch.isNull()) {
                    branch = child(e, "Fallback");
                }
                for (QDomElement inner = branch.firstChildElement(); !inner.isNull(); inner = inner.nextSiblingElement()) {
                    if (is(inner, "drawing") || is(inner, "pict")) {
                        picture(inner);
                    }
                }
            }
        }
    }

    static QDomElement descendant(const QDomElement &parent, const char *name)
    {
        for (QDomElement e = parent.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            if (is(e, name)) {
                return e;
            }
            const QDomElement inner = descendant(e, name);
            if (!inner.isNull()) {
                return inner;
            }
        }
        return {};
    }

    void picture(const QDomElement &element)
    {
        QString id;
        QSizeF size;
        const QDomElement blip = descendant(element, "blip");
        if (!blip.isNull()) {
            id = attr(blip, "embed");
            const QDomElement extent = descendant(element, "extent");
            if (!extent.isNull()) {
                size = QSizeF(attr(extent, "cx").toDouble() / emuPerPx, attr(extent, "cy").toDouble() / emuPerPx);
            }
        } else {
            const QDomElement data = descendant(element, "imagedata");
            id = attr(data, "id");
        }
        const Relation relation = m_relations.value(id);
        if (id.isEmpty() || relation.target.isEmpty() || relation.external) {
            return;
        }
        QString name = m_images.value(id);
        if (name.isEmpty()) {
            const QImage image = QImage::fromData(m_zip.file(partPath(relation.target)));
            if (image.isNull()) {
                return;
            }
            name = Doc::addImage(m_document, image);
            m_images.insert(id, name);
        }
        const QImage image = Doc::image(m_document, name);
        QTextImageFormat format;
        format.setName(name);
        if (size.width() < 1 || size.height() < 1) {
            size = image.size();
        }
        format.setWidth(size.width());
        format.setHeight(size.height());
        if (m_breakPending && m_paragraphText) {
            text(QStringLiteral(" "), m_paragraphChars);
        }
        m_cursor.insertImage(format);
        m_cursor.setCharFormat(m_paragraphChars);
        m_paragraphText = true;
    }

    void table(const QDomElement &tbl)
    {
        QList<QDomElement> rows;
        for (QDomElement e = tbl.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            if (is(e, "tr")) {
                rows << e;
            } else if (is(e, "sdt")) {
                for (QDomElement inner = child(e, "sdtContent").firstChildElement(); !inner.isNull(); inner = inner.nextSiblingElement()) {
                    if (is(inner, "tr")) {
                        rows << inner;
                    }
                }
            }
        }
        const auto cellsOf = [](const QDomElement &row) {
            QList<QDomElement> cells;
            for (QDomElement e = row.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
                if (is(e, "tc")) {
                    cells << e;
                } else if (is(e, "sdt")) {
                    for (QDomElement inner = child(e, "sdtContent").firstChildElement(); !inner.isNull(); inner = inner.nextSiblingElement()) {
                        if (is(inner, "tc")) {
                            cells << inner;
                        }
                    }
                }
            }
            return cells;
        };
        const auto spanOf = [](const QDomElement &cell) {
            const QDomElement span = child(child(cell, "tcPr"), "gridSpan");
            return span.isNull() ? 1 : qMax(1, attr(span, "val").toInt());
        };
        int columns = 0;
        for (const QDomElement &row : std::as_const(rows)) {
            int count = 0;
            for (const QDomElement &cell : cellsOf(row)) {
                count += spanOf(cell);
            }
            columns = qMax(columns, count);
        }
        if (rows.isEmpty() || columns == 0 || columns > 256) {
            return;
        }

        QTextTableFormat format = Doc::tableFormat(columns);
        QList<double> grid;
        double total = 0;
        for (QDomElement e = child(tbl, "tblGrid").firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            if (is(e, "gridCol")) {
                grid << qMax(1.0, attr(e, "w").toDouble());
                total += grid.last();
            }
        }
        if (grid.size() == columns && total > 0) {
            QList<QTextLength> widths;
            for (const double width : std::as_const(grid)) {
                widths << QTextLength(QTextLength::PercentageLength, width * 100 / total);
            }
            format.setColumnWidthConstraints(widths);
        }
        // What the table's style says of the paragraphs inside (none, when it has no style).
        const QTextBlockFormat outerTableBlock = m_tableBlock;
        m_tableBlock = QTextBlockFormat();
        if (const StyleInfo *style = resolved(attr(child(child(tbl, "tblPr"), "tblStyle"), "val"))) {
            m_tableBlock = style->block;
        }

        QTextTable *made = m_cursor.insertTable(int(rows.size()), columns, format);
        struct Merge {
            int row;
            int column;
            int rows;
            int columns;
        };
        QList<Merge> merges;
        QHash<int, int> open; // column → index in merges of a vertical merge still growing
        for (int row = 0; row < rows.size(); ++row) {
            int column = 0;
            for (const QDomElement &cellElement : cellsOf(rows.at(row))) {
                if (column >= columns) {
                    break;
                }
                const int span = qMin(spanOf(cellElement), columns - column);
                const QDomElement properties = child(cellElement, "tcPr");
                const QDomElement vertical = child(properties, "vMerge");
                const bool continues = !vertical.isNull() && attr(vertical, "val") != QLatin1String("restart");
                if (continues && open.contains(column)) {
                    ++merges[open.value(column)].rows;
                } else {
                    open.remove(column);
                    if (!vertical.isNull() || span > 1) {
                        merges.append({row, column, 1, span});
                        if (!vertical.isNull()) {
                            open.insert(column, int(merges.size()) - 1);
                        }
                    }
                    QTextTableCell cell = made->cellAt(row, column);
                    const QColor background = hexColor(attr(child(properties, "shd"), "fill"));
                    if (background.isValid()) {
                        QTextCharFormat cellFormat = cell.format();
                        cellFormat.setBackground(background);
                        cell.setFormat(cellFormat);
                    }
                    m_cursor = cell.firstCursorPosition();
                    m_unused = true;
                    const QHash<QPair<QString, int>, QTextList *> outerLists = m_lists;
                    blocks(cellElement);
                    m_lists = outerLists;
                }
                column += span;
            }
        }
        for (const Merge &merge : std::as_const(merges)) {
            if (merge.rows > 1 || merge.columns > 1) {
                made->mergeCells(merge.row, merge.column, merge.rows, merge.columns);
            }
        }
        m_tableBlock = outerTableBlock;
        m_cursor = QTextCursor(m_document);
        m_cursor.setPosition(made->lastPosition() + 1);
        m_unused = true;
        m_breakPending = false;
    }

    const ZipReader &m_zip;
    QTextDocument *m_document;
    Doc::PageSetup *m_setup;
    QTextCursor m_cursor;
    QString m_folder;
    QHash<QString, Relation> m_relations;
    QHash<QString, StyleInfo> m_styles;
    QString m_defaultStyle;
    QTextBlockFormat m_defaultBlock;
    QTextBlockFormat m_tableBlock;
    QTextCharFormat m_defaultChars;
    QString m_majorFont;
    QString m_minorFont;
    QHash<int, QHash<int, Level>> m_numbering;
    QHash<int, QString> m_abstractOf;
    QSet<int> m_restarts;
    QSet<int> m_seenNums;
    QHash<QPair<QString, int>, QTextList *> m_lists;
    QHash<QString, QString> m_images;
    QTextBlockFormat m_paragraphBlock;
    QTextCharFormat m_paragraphChars;
    bool m_unused = true;
    bool m_breakPending = false;
    bool m_paragraphText = false;
    int m_fieldDepth = 0;
};

} // namespace

QByteArray toBytes(const QTextDocument *document, const Doc::PageSetup &setup)
{
    return Writer(document, setup).package();
}

bool write(const QString &path, const QTextDocument *document, const Doc::PageSetup &setup, QString *error)
{
    const QByteArray bytes = toBytes(document, setup);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) {
            *error = QStringLiteral("No se ha podido escribir el archivo.");
        }
        return false;
    }
    return true;
}

bool fromBytes(const QByteArray &bytes, QTextDocument *document, Doc::PageSetup *setup, QString *error)
{
    const ZipReader zip(bytes);
    if (!zip.isValid()) {
        if (error) {
            *error = QStringLiteral("El archivo no es un documento de Word (.docx).");
        }
        return false;
    }
    return Reader(zip, document, setup).run(error);
}

bool read(const QString &path, QTextDocument *document, Doc::PageSetup *setup, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("No se ha podido abrir el archivo.");
        }
        return false;
    }
    return fromBytes(file.readAll(), document, setup, error);
}

} // namespace Docx
