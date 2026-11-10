#include "odt.h"

#include "zip.h"

#include <QBuffer>
#include <QDomDocument>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>
#include <QXmlStreamWriter>

#include <functional>

namespace Odt {

namespace {

const char *const mimeType = "application/vnd.oasis.opendocument.text";

const std::pair<const char *, const char *> namespaces[] = {
    {"office", "urn:oasis:names:tc:opendocument:xmlns:office:1.0"},
    {"style", "urn:oasis:names:tc:opendocument:xmlns:style:1.0"},
    {"text", "urn:oasis:names:tc:opendocument:xmlns:text:1.0"},
    {"table", "urn:oasis:names:tc:opendocument:xmlns:table:1.0"},
    {"draw", "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"},
    {"fo", "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"},
    {"xlink", "http://www.w3.org/1999/xlink"},
    {"svg", "urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"},
};

QString cm(double px)
{
    return QString::number(px / Doc::pxPerCm, 'f', 3) + QStringLiteral("cm");
}

QString pt(double points)
{
    return QString::number(points, 'g', 6) + QStringLiteral("pt");
}

/// The names LibreOffice and Word know the styles of Textos by.
QString odfStyleName(const QString &id)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("title"), QStringLiteral("Title")},
        {QStringLiteral("h1"), QStringLiteral("Heading_20_1")},
        {QStringLiteral("h2"), QStringLiteral("Heading_20_2")},
        {QStringLiteral("h3"), QStringLiteral("Heading_20_3")},
        {QStringLiteral("body"), QStringLiteral("Standard")},
        {QStringLiteral("quote"), QStringLiteral("Quotations")},
        {QStringLiteral("code"), QStringLiteral("Preformatted_20_Text")},
    };
    return names.value(id, QStringLiteral("Standard"));
}

QString displayName(const QString &name)
{
    QString shown = name;
    return shown.replace(QLatin1String("_20_"), QLatin1String(" "));
}

QColor fill(const QTextFormat &format)
{
    const QBrush brush = format.background();
    if (!format.hasProperty(QTextFormat::BackgroundBrush) || brush.style() == Qt::NoBrush || brush.color().alpha() == 0) {
        return {};
    }
    QColor color = brush.color().toRgb();
    color.setAlpha(255);
    return color;
}

QString alignmentName(Qt::Alignment alignment)
{
    if (alignment & Qt::AlignHCenter) {
        return QStringLiteral("center");
    }
    if (alignment & Qt::AlignRight) {
        return QStringLiteral("end");
    }
    if (alignment & Qt::AlignJustify) {
        return QStringLiteral("justify");
    }
    return QStringLiteral("start");
}

int lineHeightOf(const QTextBlockFormat &format)
{
    return format.lineHeightType() == QTextBlockFormat::ProportionalHeight && format.lineHeight() > 0 ? qRound(format.lineHeight()) : 100;
}

/// 1 bullets, 2 decimal, 3 a, 4 A, 5 i, 6 I.
int kindOf(QTextListFormat::Style style)
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
        const QByteArray content = contentXml();
        ZipWriter zip;
        zip.add(QStringLiteral("mimetype"), mimeType, false);
        zip.add(QStringLiteral("content.xml"), content);
        zip.add(QStringLiteral("styles.xml"), stylesXml());
        zip.add(QStringLiteral("meta.xml"),
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<office:document-meta xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
                "xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" office:version=\"1.2\">"
                "<office:meta><meta:generator>Wonderland Textos</meta:generator></office:meta></office:document-meta>");
        QByteArray manifest = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                              "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.2\">"
                              "<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.2\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>"
                              "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
                              "<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>"
                              "<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>";
        for (int i = 0; i < m_pictures.size(); ++i) {
            const QString path = QStringLiteral("Pictures/image%1.png").arg(i + 1);
            zip.add(path, m_pictures.at(i), false);
            manifest += "<manifest:file-entry manifest:full-path=\"" + path.toUtf8() + "\" manifest:media-type=\"image/png\"/>";
        }
        manifest += "</manifest:manifest>";
        zip.add(QStringLiteral("META-INF/manifest.xml"), manifest);
        return zip.finish();
    }

private:
    static void declare(QXmlStreamWriter &x)
    {
        for (const auto &[prefix, uri] : namespaces) {
            x.writeAttribute(QStringLiteral("xmlns:") + QLatin1String(prefix), QLatin1String(uri));
        }
        x.writeAttribute(QStringLiteral("office:version"), QStringLiteral("1.2"));
    }

    /// The attributes of a style:text-properties saying what differs from `base`
    /// (everything, when `base` is null).
    static QList<std::pair<QString, QString>> textProperties(const Doc::Look &look, const Doc::Look *base)
    {
        QList<std::pair<QString, QString>> all;
        if (!base || look.family != base->family) {
            all.append({QStringLiteral("style:font-name"), look.family});
            all.append({QStringLiteral("fo:font-family"), QLatin1Char('\'') + look.family + QLatin1Char('\'')});
        }
        if (!base || !qFuzzyCompare(look.size, base->size)) {
            all.append({QStringLiteral("fo:font-size"), pt(look.size)});
            all.append({QStringLiteral("style:font-size-asian"), pt(look.size)});
            all.append({QStringLiteral("style:font-size-complex"), pt(look.size)});
        }
        if (!base || look.bold != base->bold) {
            const QString weight = look.bold ? QStringLiteral("bold") : QStringLiteral("normal");
            all.append({QStringLiteral("fo:font-weight"), weight});
            all.append({QStringLiteral("style:font-weight-asian"), weight});
            all.append({QStringLiteral("style:font-weight-complex"), weight});
        }
        if (!base || look.italic != base->italic) {
            const QString slant = look.italic ? QStringLiteral("italic") : QStringLiteral("normal");
            all.append({QStringLiteral("fo:font-style"), slant});
            all.append({QStringLiteral("style:font-style-asian"), slant});
            all.append({QStringLiteral("style:font-style-complex"), slant});
        }
        if (!base || look.underline != base->underline) {
            all.append({QStringLiteral("style:text-underline-style"), look.underline ? QStringLiteral("solid") : QStringLiteral("none")});
            if (look.underline) {
                all.append({QStringLiteral("style:text-underline-width"), QStringLiteral("auto")});
                all.append({QStringLiteral("style:text-underline-color"), QStringLiteral("font-color")});
            }
        }
        if (!base || look.strike != base->strike) {
            all.append({QStringLiteral("style:text-line-through-style"), look.strike ? QStringLiteral("solid") : QStringLiteral("none")});
            if (look.strike) {
                all.append({QStringLiteral("style:text-line-through-type"), QStringLiteral("single")});
            }
        }
        if (!base || look.color != base->color) {
            all.append({QStringLiteral("fo:color"), look.color.name()});
        }
        if (base ? look.background != base->background : look.background.isValid()) {
            all.append({QStringLiteral("fo:background-color"), look.background.isValid() ? look.background.name() : QStringLiteral("transparent")});
        }
        if (base ? look.vertical != base->vertical : look.vertical != 0) {
            all.append({QStringLiteral("style:text-position"),
                        look.vertical == 1 ? QStringLiteral("super 58%") : look.vertical == 2 ? QStringLiteral("sub 58%") : QStringLiteral("0% 100%")});
        }
        return all;
    }

    static QList<std::pair<QString, QString>> paragraphProperties(const QTextBlockFormat &format, const QTextBlockFormat *base, bool inList)
    {
        QList<std::pair<QString, QString>> all;
        const auto differs = [](double a, double b) {
            return qAbs(a - b) > 0.1;
        };
        if (!base || differs(format.topMargin(), base->topMargin())) {
            all.append({QStringLiteral("fo:margin-top"), cm(format.topMargin())});
        }
        if (!base || differs(format.bottomMargin(), base->bottomMargin())) {
            all.append({QStringLiteral("fo:margin-bottom"), cm(format.bottomMargin())});
        }
        if (!inList) {
            if (!base || differs(format.leftMargin(), base->leftMargin())) {
                all.append({QStringLiteral("fo:margin-left"), cm(format.leftMargin())});
            }
            if (!base || differs(format.rightMargin(), base->rightMargin())) {
                all.append({QStringLiteral("fo:margin-right"), cm(format.rightMargin())});
            }
            if (!base || differs(format.textIndent(), base->textIndent())) {
                all.append({QStringLiteral("fo:text-indent"), cm(format.textIndent())});
            }
        }
        if (!base || lineHeightOf(format) != lineHeightOf(*base)) {
            all.append({QStringLiteral("fo:line-height"), QString::number(lineHeightOf(format)) + QLatin1Char('%')});
        }
        const QString alignment = format.hasProperty(QTextFormat::BlockAlignment) ? alignmentName(format.alignment()) : QStringLiteral("start");
        const QString baseAlignment = base && base->hasProperty(QTextFormat::BlockAlignment) ? alignmentName(base->alignment()) : QStringLiteral("start");
        if (base ? alignment != baseAlignment : alignment != QLatin1String("start")) {
            all.append({QStringLiteral("fo:text-align"), alignment});
        }
        const QColor background = fill(format);
        if (base ? background != fill(*base) : background.isValid()) {
            all.append({QStringLiteral("fo:background-color"), background.isValid() ? background.name() : QStringLiteral("transparent")});
        }
        if (format.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore) {
            all.append({QStringLiteral("fo:break-before"), QStringLiteral("page")});
        }
        return all;
    }

    static QString keyOf(const QString &parent, const QList<std::pair<QString, QString>> &properties)
    {
        QString key = parent;
        for (const auto &[name, value] : properties) {
            key += QLatin1Char('|') + name + QLatin1Char('=') + value;
        }
        return key;
    }

    void automatic(const QString &name, const QString &family, const QString &parent, const char *element, const QList<std::pair<QString, QString>> &properties)
    {
        QBuffer automaticOut(&m_automatic);
        automaticOut.open(QIODevice::Append);
        QXmlStreamWriter x(&automaticOut);
        x.writeStartElement(QStringLiteral("style:style"));
        x.writeAttribute(QStringLiteral("style:name"), name);
        x.writeAttribute(QStringLiteral("style:family"), family);
        if (!parent.isEmpty()) {
            x.writeAttribute(QStringLiteral("style:parent-style-name"), parent);
        }
        x.writeEmptyElement(QLatin1String(element));
        for (const auto &[attribute, value] : properties) {
            x.writeAttribute(attribute, value);
        }
        x.writeEndElement();
    }

    QString paragraphStyle(const QTextBlock &block)
    {
        const Doc::Style &style = Doc::style(Doc::styleOf(block));
        const QString parent = odfStyleName(style.id);
        const auto properties = paragraphProperties(block.blockFormat(), &style.block, block.textList() != nullptr);
        if (properties.isEmpty()) {
            return parent;
        }
        const QString key = keyOf(parent, properties);
        auto it = m_paragraphStyles.constFind(key);
        if (it != m_paragraphStyles.constEnd()) {
            return it.value();
        }
        const QString name = QStringLiteral("P%1").arg(m_paragraphStyles.size() + 1);
        m_paragraphStyles.insert(key, name);
        automatic(name, QStringLiteral("paragraph"), parent, "style:paragraph-properties", properties);
        return name;
    }

    QString textStyle(const Doc::Look &look, const Doc::Look &base)
    {
        const auto properties = textProperties(look, &base);
        if (properties.isEmpty()) {
            return {};
        }
        const QString key = keyOf(QString(), properties);
        auto it = m_textStyles.constFind(key);
        if (it != m_textStyles.constEnd()) {
            return it.value();
        }
        const QString name = QStringLiteral("T%1").arg(m_textStyles.size() + 1);
        m_textStyles.insert(key, name);
        automatic(name, QStringLiteral("text"), QString(), "style:text-properties", properties);
        if (look.family != base.family) {
            m_fonts.insert(look.family);
        }
        return name;
    }

    void characters(const QString &text)
    {
        QString pending;
        const auto flush = [&] {
            if (!pending.isEmpty()) {
                m_x->writeCharacters(pending);
                pending.clear();
            }
        };
        for (qsizetype i = 0; i < text.size(); ++i) {
            const QChar c = text.at(i);
            if (c == QChar::LineSeparator || c == QLatin1Char('\n')) {
                flush();
                m_x->writeEmptyElement(QStringLiteral("text:line-break"));
            } else if (c == QLatin1Char('\t')) {
                flush();
                m_x->writeEmptyElement(QStringLiteral("text:tab"));
            } else if (c == QLatin1Char(' ')) {
                // A run of spaces, or one where XML would drop it.
                qsizetype count = 1;
                while (i + count < text.size() && text.at(i + count) == QLatin1Char(' ')) {
                    ++count;
                }
                const bool edge = (i == 0 && pending.isEmpty()) || i + count == text.size();
                if (count == 1 && !edge) {
                    pending += c;
                } else {
                    qsizetype kept = 0;
                    if (!edge || i > 0 || !pending.isEmpty()) {
                        pending += c;
                        kept = 1;
                    }
                    flush();
                    if (count - kept > 0) {
                        m_x->writeEmptyElement(QStringLiteral("text:s"));
                        if (count - kept > 1) {
                            m_x->writeAttribute(QStringLiteral("text:c"), QString::number(count - kept));
                        }
                    }
                    i += count - 1;
                }
            } else if (c.unicode() >= 0x20 && c != QChar::ObjectReplacementCharacter) {
                pending += c;
            }
        }
        flush();
    }

    void picture(const QTextImageFormat &format)
    {
        const QString name = format.name();
        int index = int(m_pictureNames.indexOf(name));
        QSize natural;
        if (index < 0) {
            const QImage image = Doc::image(m_document, name);
            if (image.isNull()) {
                return;
            }
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
            m_pictures.append(bytes);
            m_pictureNames.append(name);
            m_pictureSizes.append(image.size());
            index = int(m_pictures.size()) - 1;
        }
        natural = m_pictureSizes.at(index);
        const double width = format.width() > 0 ? format.width() : natural.width();
        const double height = format.height() > 0 ? format.height()
            : (format.width() > 0 && natural.width() > 0 ? format.width() * natural.height() / natural.width() : natural.height());
        m_x->writeStartElement(QStringLiteral("draw:frame"));
        m_x->writeAttribute(QStringLiteral("draw:style-name"), QStringLiteral("fr1"));
        m_x->writeAttribute(QStringLiteral("draw:name"), QStringLiteral("Imagen%1").arg(++m_frames));
        m_x->writeAttribute(QStringLiteral("text:anchor-type"), QStringLiteral("as-char"));
        m_x->writeAttribute(QStringLiteral("svg:width"), cm(width));
        m_x->writeAttribute(QStringLiteral("svg:height"), cm(height));
        m_x->writeAttribute(QStringLiteral("draw:z-index"), QStringLiteral("0"));
        m_x->writeEmptyElement(QStringLiteral("draw:image"));
        m_x->writeAttribute(QStringLiteral("xlink:href"), QStringLiteral("Pictures/image%1.png").arg(index + 1));
        m_x->writeAttribute(QStringLiteral("xlink:type"), QStringLiteral("simple"));
        m_x->writeAttribute(QStringLiteral("xlink:show"), QStringLiteral("embed"));
        m_x->writeAttribute(QStringLiteral("xlink:actuate"), QStringLiteral("onLoad"));
        m_x->writeEndElement();
    }

    void paragraph(const QTextBlock &block)
    {
        const Doc::Style &style = Doc::style(Doc::styleOf(block));
        const Doc::Look base = Doc::lookOf(style.chars);
        const int level = Doc::outlineLevel(block);
        const QString styleName = paragraphStyle(block);
        if (level > 0) {
            m_x->writeStartElement(QStringLiteral("text:h"));
            m_x->writeAttribute(QStringLiteral("text:style-name"), styleName);
            m_x->writeAttribute(QStringLiteral("text:outline-level"), QString::number(level));
        } else {
            m_x->writeStartElement(QStringLiteral("text:p"));
            m_x->writeAttribute(QStringLiteral("text:style-name"), styleName);
        }
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
                    m_x->writeStartElement(QStringLiteral("text:a"));
                    m_x->writeAttribute(QStringLiteral("xlink:type"), QStringLiteral("simple"));
                    m_x->writeAttribute(QStringLiteral("xlink:href"), href);
                }
                openLink = href;
            }
            if (chars.isImageFormat()) {
                for (qsizetype i = 0; i < fragment.length(); ++i) {
                    picture(chars.toImageFormat());
                }
                continue;
            }
            const QString name = textStyle(Doc::lookOf(chars), base);
            if (!name.isEmpty()) {
                m_x->writeStartElement(QStringLiteral("text:span"));
                m_x->writeAttribute(QStringLiteral("text:style-name"), name);
            }
            characters(fragment.text());
            if (!name.isEmpty()) {
                m_x->writeEndElement();
            }
        }
        if (!openLink.isEmpty()) {
            m_x->writeEndElement();
        }
        m_x->writeEndElement();
    }

    void closeLists(int downTo)
    {
        while (m_openLists.size() > downTo) {
            m_x->writeEndElement(); // list-item
            m_x->writeEndElement(); // list
            m_openLists.removeLast();
        }
    }

    void listItem(const QTextBlock &block)
    {
        QTextList *list = block.textList();
        const int level = qBound(1, list->format().indent(), 9);
        if (m_openLists.size() >= level && m_openLists.at(level - 1) == list) {
            closeLists(level);
            m_x->writeEndElement();
            m_x->writeStartElement(QStringLiteral("text:list-item"));
        } else {
            if (m_openLists.size() >= level) {
                closeLists(level - 1);
            }
            while (m_openLists.size() < level) {
                m_x->writeStartElement(QStringLiteral("text:list"));
                m_x->writeAttribute(QStringLiteral("text:style-name"), QStringLiteral("L%1").arg(kindOf(list->format().style())));
                m_x->writeStartElement(QStringLiteral("text:list-item"));
                m_openLists.append(m_openLists.size() + 1 == level ? list : nullptr);
            }
        }
        paragraph(block);
    }

    void table(QTextTable *table)
    {
        const QString name = QStringLiteral("Tabla%1").arg(++m_tables);
        const int columns = table->columns();
        const double total = m_setup.contentPx().width();
        const QList<QTextLength> constraints = table->format().columnWidthConstraints();
        double share = 0;
        QList<double> parts;
        for (int column = 0; column < columns; ++column) {
            const QTextLength length = column < constraints.size() ? constraints.at(column) : QTextLength();
            parts << (length.type() == QTextLength::PercentageLength ? length.rawValue() : 100.0 / columns);
            share += parts.last();
        }
        {
            QBuffer automaticOut(&m_automatic);
        automaticOut.open(QIODevice::Append);
        QXmlStreamWriter x(&automaticOut);
            x.writeStartElement(QStringLiteral("style:style"));
            x.writeAttribute(QStringLiteral("style:name"), name);
            x.writeAttribute(QStringLiteral("style:family"), QStringLiteral("table"));
            x.writeEmptyElement(QStringLiteral("style:table-properties"));
            x.writeAttribute(QStringLiteral("style:width"), cm(total));
            x.writeAttribute(QStringLiteral("table:align"), QStringLiteral("margins"));
            x.writeAttribute(QStringLiteral("fo:margin-top"), cm(4));
            x.writeAttribute(QStringLiteral("fo:margin-bottom"), cm(10));
            x.writeEndElement();
            for (int column = 0; column < columns; ++column) {
                x.writeStartElement(QStringLiteral("style:style"));
                x.writeAttribute(QStringLiteral("style:name"), QStringLiteral("%1.C%2").arg(name).arg(column + 1));
                x.writeAttribute(QStringLiteral("style:family"), QStringLiteral("table-column"));
                x.writeEmptyElement(QStringLiteral("style:table-column-properties"));
                x.writeAttribute(QStringLiteral("style:column-width"), cm(total * parts.at(column) / qMax(1.0, share)));
                x.writeAttribute(QStringLiteral("style:rel-column-width"), QString::number(qRound(65535 * parts.at(column) / qMax(1.0, share))) + QLatin1Char('*'));
                x.writeEndElement();
            }
        }
        const QList<QTextList *> outerLists = m_openLists;
        m_openLists.clear();

        m_x->writeStartElement(QStringLiteral("table:table"));
        m_x->writeAttribute(QStringLiteral("table:name"), name);
        m_x->writeAttribute(QStringLiteral("table:style-name"), name);
        for (int column = 0; column < columns; ++column) {
            m_x->writeEmptyElement(QStringLiteral("table:table-column"));
            m_x->writeAttribute(QStringLiteral("table:style-name"), QStringLiteral("%1.C%2").arg(name).arg(column + 1));
        }
        for (int row = 0; row < table->rows(); ++row) {
            m_x->writeStartElement(QStringLiteral("table:table-row"));
            for (int column = 0; column < columns; ++column) {
                const QTextTableCell cell = table->cellAt(row, column);
                if (!cell.isValid() || cell.row() != row || cell.column() != column) {
                    m_x->writeEmptyElement(QStringLiteral("table:covered-table-cell"));
                    continue;
                }
                m_x->writeStartElement(QStringLiteral("table:table-cell"));
                m_x->writeAttribute(QStringLiteral("table:style-name"), cellStyle(fill(cell.format())));
                m_x->writeAttribute(QStringLiteral("office:value-type"), QStringLiteral("string"));
                if (cell.columnSpan() > 1) {
                    m_x->writeAttribute(QStringLiteral("table:number-columns-spanned"), QString::number(cell.columnSpan()));
                }
                if (cell.rowSpan() > 1) {
                    m_x->writeAttribute(QStringLiteral("table:number-rows-spanned"), QString::number(cell.rowSpan()));
                }
                blocks(cell.begin(), cell.end());
                m_x->writeEndElement();
            }
            m_x->writeEndElement();
        }
        m_x->writeEndElement();
        m_openLists = outerLists;
    }

    QString cellStyle(const QColor &background)
    {
        const QString key = background.isValid() ? background.name() : QString();
        auto it = m_cellStyles.constFind(key);
        if (it != m_cellStyles.constEnd()) {
            return it.value();
        }
        const QString name = QStringLiteral("Celda%1").arg(m_cellStyles.size() + 1);
        m_cellStyles.insert(key, name);
        QBuffer automaticOut(&m_automatic);
        automaticOut.open(QIODevice::Append);
        QXmlStreamWriter x(&automaticOut);
        x.writeStartElement(QStringLiteral("style:style"));
        x.writeAttribute(QStringLiteral("style:name"), name);
        x.writeAttribute(QStringLiteral("style:family"), QStringLiteral("table-cell"));
        x.writeEmptyElement(QStringLiteral("style:table-cell-properties"));
        x.writeAttribute(QStringLiteral("fo:padding"), cm(5));
        x.writeAttribute(QStringLiteral("fo:border"), QStringLiteral("0.5pt solid #b4b4ba"));
        if (background.isValid()) {
            x.writeAttribute(QStringLiteral("fo:background-color"), background.name());
        }
        x.writeEndElement();
        return name;
    }

    void blocks(QTextFrame::iterator it, const QTextFrame::iterator &end)
    {
        const int depth = int(m_openLists.size());
        for (; it != end && !it.atEnd(); ++it) {
            if (QTextFrame *frame = it.currentFrame()) {
                closeLists(depth);
                if (auto *inner = qobject_cast<QTextTable *>(frame)) {
                    table(inner);
                } else {
                    blocks(frame->begin(), frame->end());
                }
            } else if (it.currentBlock().isValid()) {
                const QTextBlock block = it.currentBlock();
                if (block.textList()) {
                    listItem(block);
                } else {
                    closeLists(depth);
                    paragraph(block);
                }
            }
        }
        closeLists(depth);
    }

    static void fontFaces(QXmlStreamWriter &x, const QSet<QString> &fonts)
    {
        x.writeStartElement(QStringLiteral("office:font-face-decls"));
        QStringList sorted(fonts.begin(), fonts.end());
        sorted.sort();
        for (const QString &family : std::as_const(sorted)) {
            x.writeEmptyElement(QStringLiteral("style:font-face"));
            x.writeAttribute(QStringLiteral("style:name"), family);
            x.writeAttribute(QStringLiteral("svg:font-family"), QLatin1Char('\'') + family + QLatin1Char('\''));
        }
        x.writeEndElement();
    }

    static void listStyles(QXmlStreamWriter &x)
    {
        static const char *formats[] = {"", "1", "a", "A", "i", "I"};
        static const QString bullets[] = {QStringLiteral("\u2022"), QStringLiteral("\u25E6"), QStringLiteral("\u25AA")};
        for (int kind = 1; kind <= 6; ++kind) {
            x.writeStartElement(QStringLiteral("text:list-style"));
            x.writeAttribute(QStringLiteral("style:name"), QStringLiteral("L%1").arg(kind));
            for (int level = 1; level <= 9; ++level) {
                if (kind == 1) {
                    x.writeStartElement(QStringLiteral("text:list-level-style-bullet"));
                    x.writeAttribute(QStringLiteral("text:level"), QString::number(level));
                    x.writeAttribute(QStringLiteral("text:bullet-char"), bullets[(level - 1) % 3]);
                } else {
                    x.writeStartElement(QStringLiteral("text:list-level-style-number"));
                    x.writeAttribute(QStringLiteral("text:level"), QString::number(level));
                    x.writeAttribute(QStringLiteral("style:num-suffix"), QStringLiteral("."));
                    x.writeAttribute(QStringLiteral("style:num-format"), QLatin1String(formats[kind - 1]));
                }
                x.writeStartElement(QStringLiteral("style:list-level-properties"));
                x.writeAttribute(QStringLiteral("text:list-level-position-and-space-mode"), QStringLiteral("label-alignment"));
                x.writeEmptyElement(QStringLiteral("style:list-level-label-alignment"));
                x.writeAttribute(QStringLiteral("text:label-followed-by"), QStringLiteral("listtab"));
                x.writeAttribute(QStringLiteral("text:list-tab-stop-position"), QString::number(0.635 * (level + 1), 'f', 3) + QStringLiteral("cm"));
                x.writeAttribute(QStringLiteral("fo:text-indent"), QStringLiteral("-0.635cm"));
                x.writeAttribute(QStringLiteral("fo:margin-left"), QString::number(0.635 * (level + 1), 'f', 3) + QStringLiteral("cm"));
                x.writeEndElement();
                x.writeEndElement();
            }
            x.writeEndElement();
        }
    }

    QByteArray contentXml()
    {
        QByteArray body;
        {
            QXmlStreamWriter x(&body);
            m_x = &x;
            QTextFrame *root = m_document->rootFrame();
            blocks(root->begin(), root->end());
            m_x = nullptr;
        }
        for (const Doc::Style &style : Doc::styles()) {
            m_fonts.insert(Doc::lookOf(style.chars).family);
        }
        QByteArray out;
        QBuffer device(&out);
        device.open(QIODevice::WriteOnly);
        QXmlStreamWriter x(&device);
        x.writeStartDocument();
        x.writeStartElement(QStringLiteral("office:document-content"));
        declare(x);
        fontFaces(x, m_fonts);
        x.writeStartElement(QStringLiteral("office:automatic-styles"));
        x.writeCharacters(QString()); // closes the start tag before the raw styles
        device.write(m_automatic);
        x.writeStartElement(QStringLiteral("style:style"));
        x.writeAttribute(QStringLiteral("style:name"), QStringLiteral("fr1"));
        x.writeAttribute(QStringLiteral("style:family"), QStringLiteral("graphic"));
        x.writeEmptyElement(QStringLiteral("style:graphic-properties"));
        x.writeAttribute(QStringLiteral("style:vertical-pos"), QStringLiteral("bottom"));
        x.writeAttribute(QStringLiteral("style:vertical-rel"), QStringLiteral("baseline"));
        x.writeAttribute(QStringLiteral("fo:border"), QStringLiteral("none"));
        x.writeAttribute(QStringLiteral("fo:padding"), QStringLiteral("0cm"));
        x.writeEndElement();
        listStyles(x);
        x.writeEndElement();
        x.writeStartElement(QStringLiteral("office:body"));
        x.writeStartElement(QStringLiteral("office:text"));
        x.writeCharacters(QString());
        device.write(body);
        x.writeEndElement();
        x.writeEndElement();
        x.writeEndElement();
        x.writeEndDocument();
        return out;
    }

    QByteArray stylesXml()
    {
        QByteArray out;
        QXmlStreamWriter x(&out);
        x.writeStartDocument();
        x.writeStartElement(QStringLiteral("office:document-styles"));
        declare(x);
        QSet<QString> fonts;
        for (const Doc::Style &style : Doc::styles()) {
            fonts.insert(Doc::lookOf(style.chars).family);
        }
        fontFaces(x, fonts);

        const Doc::Style &body = Doc::style(QStringLiteral("body"));
        const Doc::Look normal = Doc::lookOf(body.chars);
        x.writeStartElement(QStringLiteral("office:styles"));
        x.writeStartElement(QStringLiteral("style:default-style"));
        x.writeAttribute(QStringLiteral("style:family"), QStringLiteral("paragraph"));
        x.writeEmptyElement(QStringLiteral("style:paragraph-properties"));
        x.writeAttribute(QStringLiteral("style:tab-stop-distance"), QStringLiteral("1.25cm"));
        x.writeEmptyElement(QStringLiteral("style:text-properties"));
        for (const auto &[attribute, value] : textProperties(normal, nullptr)) {
            x.writeAttribute(attribute, value);
        }
        x.writeAttribute(QStringLiteral("fo:language"), QStringLiteral("es"));
        x.writeAttribute(QStringLiteral("fo:country"), QStringLiteral("ES"));
        x.writeEndElement();

        for (const Doc::Style &style : Doc::styles()) {
            const bool isBody = style.id == QLatin1String("body");
            const QString name = odfStyleName(style.id);
            x.writeStartElement(QStringLiteral("style:style"));
            x.writeAttribute(QStringLiteral("style:name"), name);
            if (displayName(name) != name) {
                x.writeAttribute(QStringLiteral("style:display-name"), displayName(name));
            }
            x.writeAttribute(QStringLiteral("style:family"), QStringLiteral("paragraph"));
            if (!isBody) {
                x.writeAttribute(QStringLiteral("style:parent-style-name"), QStringLiteral("Standard"));
                x.writeAttribute(QStringLiteral("style:next-style-name"),
                                 style.id == QLatin1String("quote") || style.id == QLatin1String("code") ? name : QStringLiteral("Standard"));
            }
            if (style.heading > 0) {
                x.writeAttribute(QStringLiteral("style:default-outline-level"), QString::number(style.heading));
            }
            x.writeAttribute(QStringLiteral("style:class"), style.heading > 0 || style.id == QLatin1String("title") ? QStringLiteral("chapter") : QStringLiteral("text"));
            x.writeEmptyElement(QStringLiteral("style:paragraph-properties"));
            for (const auto &[attribute, value] : paragraphProperties(style.block, nullptr, false)) {
                x.writeAttribute(attribute, value);
            }
            if (style.heading > 0 || style.id == QLatin1String("title")) {
                x.writeAttribute(QStringLiteral("fo:keep-with-next"), QStringLiteral("always"));
            }
            const auto properties = textProperties(Doc::lookOf(style.chars), isBody ? nullptr : &normal);
            if (!properties.isEmpty()) {
                x.writeEmptyElement(QStringLiteral("style:text-properties"));
                for (const auto &[attribute, value] : properties) {
                    x.writeAttribute(attribute, value);
                }
            }
            x.writeEndElement();
        }
        const auto margin = [&](const QString &name) {
            x.writeStartElement(QStringLiteral("style:style"));
            x.writeAttribute(QStringLiteral("style:name"), name);
            x.writeAttribute(QStringLiteral("style:family"), QStringLiteral("paragraph"));
            x.writeAttribute(QStringLiteral("style:parent-style-name"), QStringLiteral("Standard"));
            x.writeAttribute(QStringLiteral("style:class"), QStringLiteral("extra"));
            x.writeStartElement(QStringLiteral("style:paragraph-properties"));
            x.writeAttribute(QStringLiteral("fo:margin-top"), QStringLiteral("0cm"));
            x.writeAttribute(QStringLiteral("fo:margin-bottom"), QStringLiteral("0cm"));
            x.writeAttribute(QStringLiteral("fo:line-height"), QStringLiteral("100%"));
            x.writeAttribute(QStringLiteral("fo:text-align"), QStringLiteral("center"));
            x.writeStartElement(QStringLiteral("style:tab-stops"));
            x.writeEmptyElement(QStringLiteral("style:tab-stop"));
            x.writeAttribute(QStringLiteral("style:position"), cm(m_setup.contentPx().width()));
            x.writeAttribute(QStringLiteral("style:type"), QStringLiteral("right"));
            x.writeEndElement();
            x.writeEndElement();
            x.writeEmptyElement(QStringLiteral("style:text-properties"));
            x.writeAttribute(QStringLiteral("fo:color"), QStringLiteral("#6e6e73"));
            x.writeAttribute(QStringLiteral("fo:font-size"), QStringLiteral("9pt"));
            x.writeEndElement();
        };
        margin(QStringLiteral("Header"));
        margin(QStringLiteral("Footer"));
        x.writeEndElement(); // office:styles

        const QSizeF page = m_setup.sizeCm();
        const bool header = !m_setup.header.trimmed().isEmpty();
        const bool footer = !m_setup.footer.trimmed().isEmpty() || m_setup.pageNumbers;
        // The header and the footer live inside the margin, as in Textos:
        // the page margin shrinks by what they take.
        const double band = 0.9;
        x.writeStartElement(QStringLiteral("office:automatic-styles"));
        x.writeStartElement(QStringLiteral("style:page-layout"));
        x.writeAttribute(QStringLiteral("style:name"), QStringLiteral("pm1"));
        x.writeStartElement(QStringLiteral("style:page-layout-properties"));
        x.writeAttribute(QStringLiteral("fo:page-width"), QString::number(page.width(), 'f', 3) + QStringLiteral("cm"));
        x.writeAttribute(QStringLiteral("fo:page-height"), QString::number(page.height(), 'f', 3) + QStringLiteral("cm"));
        x.writeAttribute(QStringLiteral("style:print-orientation"), m_setup.landscape ? QStringLiteral("landscape") : QStringLiteral("portrait"));
        const double top = header ? qMax(0.0, m_setup.top - band) : m_setup.top;
        const double bottom = footer ? qMax(0.0, m_setup.bottom - band) : m_setup.bottom;
        x.writeAttribute(QStringLiteral("fo:margin-top"), QString::number(top, 'f', 3) + QStringLiteral("cm"));
        x.writeAttribute(QStringLiteral("fo:margin-bottom"), QString::number(bottom, 'f', 3) + QStringLiteral("cm"));
        x.writeAttribute(QStringLiteral("fo:margin-left"), QString::number(m_setup.left, 'f', 3) + QStringLiteral("cm"));
        x.writeAttribute(QStringLiteral("fo:margin-right"), QString::number(m_setup.right, 'f', 3) + QStringLiteral("cm"));
        x.writeEndElement();
        const auto bandStyle = [&](const char *element, bool present, double height, const char *gap) {
            if (!present) {
                return;
            }
            x.writeStartElement(QLatin1String(element));
            x.writeEmptyElement(QStringLiteral("style:header-footer-properties"));
            x.writeAttribute(QStringLiteral("fo:min-height"), QString::number(height, 'f', 3) + QStringLiteral("cm"));
            x.writeAttribute(QLatin1String(gap), QStringLiteral("0.4cm"));
            x.writeEndElement();
        };
        bandStyle("style:header-style", header, m_setup.top - top, "fo:margin-bottom");
        bandStyle("style:footer-style", footer, m_setup.bottom - bottom, "fo:margin-top");
        x.writeEndElement(); // page-layout
        x.writeEndElement(); // automatic-styles

        x.writeStartElement(QStringLiteral("office:master-styles"));
        x.writeStartElement(QStringLiteral("style:master-page"));
        x.writeAttribute(QStringLiteral("style:name"), QStringLiteral("Standard"));
        x.writeAttribute(QStringLiteral("style:page-layout-name"), QStringLiteral("pm1"));
        if (header) {
            x.writeStartElement(QStringLiteral("style:header"));
            x.writeStartElement(QStringLiteral("text:p"));
            x.writeAttribute(QStringLiteral("text:style-name"), QStringLiteral("Header"));
            x.writeCharacters(m_setup.header.trimmed());
            x.writeEndElement();
            x.writeEndElement();
        }
        if (footer) {
            const QString words = m_setup.footer.trimmed();
            x.writeStartElement(QStringLiteral("style:footer"));
            x.writeStartElement(QStringLiteral("text:p"));
            x.writeAttribute(QStringLiteral("text:style-name"), QStringLiteral("Footer"));
            x.writeCharacters(words);
            if (m_setup.pageNumbers) {
                if (!words.isEmpty()) {
                    x.writeEmptyElement(QStringLiteral("text:tab"));
                }
                x.writeStartElement(QStringLiteral("text:page-number"));
                x.writeAttribute(QStringLiteral("text:select-page"), QStringLiteral("current"));
                x.writeCharacters(QStringLiteral("1"));
                x.writeEndElement();
            }
            x.writeEndElement();
            x.writeEndElement();
        }
        x.writeEndElement();
        x.writeEndElement();
        x.writeEndElement();
        x.writeEndDocument();
        return out;
    }

    const QTextDocument *m_document;
    Doc::PageSetup m_setup;
    QXmlStreamWriter *m_x = nullptr;
    QByteArray m_automatic;
    QHash<QString, QString> m_paragraphStyles;
    QHash<QString, QString> m_textStyles;
    QHash<QString, QString> m_cellStyles;
    QSet<QString> m_fonts;
    QList<QTextList *> m_openLists;
    QList<QByteArray> m_pictures;
    QStringList m_pictureNames;
    QList<QSize> m_pictureSizes;
    int m_tables = 0;
    int m_frames = 0;
};

// --- Reading ----------------------------------------------------------------------

QString local(const QDomElement &element)
{
    const QString name = element.tagName();
    return name.mid(name.indexOf(QLatin1Char(':')) + 1);
}

bool is(const QDomElement &element, const char *name)
{
    return element.tagName() == QLatin1String(name);
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

/// A length of ODF ("2.5cm", "12pt", "0.5in"…) in layout pixels; `ok` false for percentages and nonsense.
double length(const QString &text, bool *ok = nullptr)
{
    static const std::pair<const char *, double> units[] = {
        {"cm", Doc::pxPerCm}, {"mm", Doc::pxPerCm / 10}, {"in", 96.0}, {"pt", Doc::pxPerPt}, {"pc", 16.0}, {"px", 1.0},
    };
    const QString trimmed = text.trimmed();
    for (const auto &[unit, factor] : units) {
        if (trimmed.endsWith(QLatin1String(unit))) {
            bool number = false;
            const double value = trimmed.chopped(2).toDouble(&number);
            if (ok) {
                *ok = number;
            }
            return number ? value * factor : 0;
        }
    }
    if (ok) {
        *ok = false;
    }
    return 0;
}

QString mapStyle(const QString &name, const QString &shown)
{
    const QString n = displayName(shown.isEmpty() ? name : shown).toLower().simplified();
    if (n == QLatin1String("title") || n == QLatin1String("título")) {
        return QStringLiteral("title");
    }
    for (int level = 1; level <= 3; ++level) {
        const QString number = QString::number(level);
        if (n == QLatin1String("heading ") + number || n == QLatin1String("título ") + number || n == QLatin1String("encabezado ") + number) {
            return QLatin1Char('h') + number;
        }
    }
    if (n == QLatin1String("quotations") || n == QLatin1String("quote") || n == QLatin1String("cita") || n == QLatin1String("citas")) {
        return QStringLiteral("quote");
    }
    if (n == QLatin1String("preformatted text") || n == QLatin1String("code") || n == QLatin1String("código") || n == QLatin1String("source text")
        || n == QLatin1String("texto preformateado")) {
        return QStringLiteral("code");
    }
    return {};
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
        const QByteArray content = m_zip.file(QStringLiteral("content.xml"));
        QDomDocument contentDom;
        if (content.isNull() || !contentDom.setContent(content, QDomDocument::ParseOption::PreserveSpacingOnlyNodes)) {
            if (error) {
                *error = QStringLiteral("El archivo no contiene un documento OpenDocument.");
            }
            return false;
        }
        QDomDocument stylesDom;
        stylesDom.setContent(m_zip.file(QStringLiteral("styles.xml")), QDomDocument::ParseOption::PreserveSpacingOnlyNodes);
        const QDomElement stylesRoot = stylesDom.documentElement();
        const QDomElement contentRoot = contentDom.documentElement();

        // What LibreOffice assumes when a file says nothing.
        m_defaultChars.setFontPointSize(12);
        fonts(child(stylesRoot, "office:font-face-decls"));
        fonts(child(contentRoot, "office:font-face-decls"));
        styles(child(stylesRoot, "office:styles"), false);
        styles(child(stylesRoot, "office:automatic-styles"), false);
        const QDomElement masters = child(stylesRoot, "office:master-styles");
        // The styles of content.xml hide those of styles.xml with the same name.
        styles(child(contentRoot, "office:automatic-styles"), true);

        const QDomElement text = child(child(contentRoot, "office:body"), "office:text");
        if (text.isNull()) {
            if (error) {
                *error = QStringLiteral("El archivo no es un documento de texto OpenDocument.");
            }
            return false;
        }
        m_cursor.beginEditBlock();
        blocks(text, 0, nullptr);
        m_cursor.endEditBlock();
        if (m_setup) {
            page(masters);
        }
        return true;
    }

private:
    struct StyleInfo {
        QString parent;
        QString shown;
        QString family;
        QString masterPage;
        QTextBlockFormat block;
        QTextCharFormat chars;
        QString mapped;
        bool breakAfter = false;
        bool resolved = false;
        bool automatic = false;
    };
    struct ListLevels {
        QHash<int, QTextListFormat::Style> levels;
    };

    void fonts(const QDomElement &declarations)
    {
        for (QDomElement e = declarations.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            if (is(e, "style:font-face")) {
                QString family = e.attribute(QStringLiteral("svg:font-family"));
                family = family.section(QLatin1Char(','), 0, 0).trimmed();
                family.remove(QLatin1Char('\''));
                family.remove(QLatin1Char('"'));
                if (!family.isEmpty()) {
                    m_fontFaces.insert(e.attribute(QStringLiteral("style:name")), family);
                }
            }
        }
    }

    QTextCharFormat textFormat(const QDomElement &properties) const
    {
        QTextCharFormat format;
        if (properties.isNull()) {
            return format;
        }
        QString family = m_fontFaces.value(properties.attribute(QStringLiteral("style:font-name")));
        if (family.isEmpty()) {
            family = properties.attribute(QStringLiteral("fo:font-family")).section(QLatin1Char(','), 0, 0).trimmed();
            family.remove(QLatin1Char('\''));
            family.remove(QLatin1Char('"'));
        }
        if (family.isEmpty()) {
            family = properties.attribute(QStringLiteral("style:font-name"));
        }
        if (!family.isEmpty()) {
            format.setFontFamilies({family});
        }
        if (properties.hasAttribute(QStringLiteral("fo:font-size"))) {
            const QString size = properties.attribute(QStringLiteral("fo:font-size"));
            if (size.endsWith(QLatin1String("pt")) && size.chopped(2).toDouble() > 0) {
                format.setFontPointSize(size.chopped(2).toDouble());
            }
        }
        if (properties.hasAttribute(QStringLiteral("fo:font-weight"))) {
            const QString weight = properties.attribute(QStringLiteral("fo:font-weight"));
            format.setFontWeight(weight == QLatin1String("bold") || weight.toInt() >= 600 ? QFont::Bold : QFont::Normal);
        }
        if (properties.hasAttribute(QStringLiteral("fo:font-style"))) {
            format.setFontItalic(properties.attribute(QStringLiteral("fo:font-style")) != QLatin1String("normal"));
        }
        if (properties.hasAttribute(QStringLiteral("style:text-underline-style"))) {
            format.setFontUnderline(properties.attribute(QStringLiteral("style:text-underline-style")) != QLatin1String("none"));
        }
        if (properties.hasAttribute(QStringLiteral("style:text-line-through-style"))) {
            format.setFontStrikeOut(properties.attribute(QStringLiteral("style:text-line-through-style")) != QLatin1String("none"));
        } else if (properties.hasAttribute(QStringLiteral("style:text-line-through-type"))) {
            format.setFontStrikeOut(properties.attribute(QStringLiteral("style:text-line-through-type")) != QLatin1String("none"));
        }
        if (properties.hasAttribute(QStringLiteral("fo:color"))) {
            const QColor color(properties.attribute(QStringLiteral("fo:color")));
            if (color.isValid()) {
                format.setForeground(color);
            }
        }
        if (properties.hasAttribute(QStringLiteral("fo:background-color"))) {
            const QColor color(properties.attribute(QStringLiteral("fo:background-color")));
            if (color.isValid() && properties.attribute(QStringLiteral("fo:background-color")) != QLatin1String("transparent")) {
                format.setBackground(color);
            } else {
                format.setBackground(Qt::NoBrush);
            }
        }
        if (properties.hasAttribute(QStringLiteral("style:text-position"))) {
            const QString position = properties.attribute(QStringLiteral("style:text-position")).section(QLatin1Char(' '), 0, 0);
            const bool percent = position.endsWith(QLatin1Char('%'));
            const double shift = percent ? position.chopped(1).toDouble() : 0;
            format.setVerticalAlignment(position == QLatin1String("super") || shift > 0 ? QTextCharFormat::AlignSuperScript
                : position == QLatin1String("sub") || shift < 0 ? QTextCharFormat::AlignSubScript : QTextCharFormat::AlignNormal);
        }
        return format;
    }

    static QTextBlockFormat blockFormat(const QDomElement &properties, bool *breakAfter)
    {
        QTextBlockFormat format;
        if (properties.isNull()) {
            return format;
        }
        const auto set = [&](const char *name, auto setter) {
            if (properties.hasAttribute(QLatin1String(name))) {
                bool ok = false;
                const double value = length(properties.attribute(QLatin1String(name)), &ok);
                if (ok) {
                    setter(value);
                }
            }
        };
        if (properties.hasAttribute(QStringLiteral("fo:margin"))) {
            bool ok = false;
            const double all = length(properties.attribute(QStringLiteral("fo:margin")), &ok);
            if (ok) {
                format.setTopMargin(all);
                format.setBottomMargin(all);
                format.setLeftMargin(all);
                format.setRightMargin(all);
            }
        }
        set("fo:margin-top", [&](double v) { format.setTopMargin(v); });
        set("fo:margin-bottom", [&](double v) { format.setBottomMargin(v); });
        set("fo:margin-left", [&](double v) { format.setLeftMargin(v); });
        set("fo:margin-right", [&](double v) { format.setRightMargin(v); });
        set("fo:text-indent", [&](double v) { format.setTextIndent(v); });
        if (properties.hasAttribute(QStringLiteral("fo:text-align"))) {
            const QString value = properties.attribute(QStringLiteral("fo:text-align"));
            format.setAlignment(value == QLatin1String("center") ? Qt::AlignHCenter
                : value == QLatin1String("end") || value == QLatin1String("right") ? Qt::AlignRight
                : value == QLatin1String("justify") ? Qt::AlignJustify : Qt::AlignLeft);
        }
        if (properties.hasAttribute(QStringLiteral("fo:line-height"))) {
            const QString value = properties.attribute(QStringLiteral("fo:line-height"));
            if (value.endsWith(QLatin1Char('%'))) {
                const double percent = value.chopped(1).toDouble();
                if (percent > 0) {
                    format.setLineHeight(percent, QTextBlockFormat::ProportionalHeight);
                }
            } else {
                bool ok = false;
                const double fixed = length(value, &ok);
                if (ok && fixed > 0) {
                    format.setLineHeight(fixed, QTextBlockFormat::FixedHeight);
                }
            }
        }
        if (properties.hasAttribute(QStringLiteral("fo:background-color"))) {
            const QString value = properties.attribute(QStringLiteral("fo:background-color"));
            const QColor color(value);
            if (color.isValid() && value != QLatin1String("transparent")) {
                format.setBackground(color);
            } else {
                format.setBackground(Qt::NoBrush);
            }
        }
        if (properties.hasAttribute(QStringLiteral("fo:break-before"))) {
            format.setPageBreakPolicy(properties.attribute(QStringLiteral("fo:break-before")) == QLatin1String("page") ? QTextFormat::PageBreak_AlwaysBefore
                                                                                                                 : QTextFormat::PageBreak_Auto);
        }
        if (breakAfter && properties.attribute(QStringLiteral("fo:break-after")) == QLatin1String("page")) {
            *breakAfter = true;
        }
        return format;
    }

    void styles(const QDomElement &parent, bool content)
    {
        for (QDomElement e = parent.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            if (is(e, "style:default-style")) {
                if (e.attribute(QStringLiteral("style:family")) == QLatin1String("paragraph")) {
                    m_defaultBlock = blockFormat(child(e, "style:paragraph-properties"), nullptr);
                    m_defaultChars.merge(textFormat(child(e, "style:text-properties")));
                }
            } else if (is(e, "style:style")) {
                StyleInfo info;
                info.family = e.attribute(QStringLiteral("style:family"));
                info.parent = e.attribute(QStringLiteral("style:parent-style-name"));
                info.shown = e.attribute(QStringLiteral("style:display-name"));
                info.masterPage = e.attribute(QStringLiteral("style:master-page-name"));
                info.block = blockFormat(child(e, "style:paragraph-properties"), &info.breakAfter);
                info.chars = textFormat(child(e, "style:text-properties"));
                info.automatic = content || is(parent, "office:automatic-styles");
                const QString name = e.attribute(QStringLiteral("style:name"));
                if (info.family == QLatin1String("paragraph") && !info.automatic) {
                    info.mapped = mapStyle(name, info.shown);
                }
                if (info.family == QLatin1String("table-cell")) {
                    const QString background = child(e, "style:table-cell-properties").attribute(QStringLiteral("fo:background-color"));
                    const QColor color(background);
                    if (color.isValid() && background != QLatin1String("transparent")) {
                        m_cellColors.insert(name, color);
                    }
                } else if (info.family == QLatin1String("table-column")) {
                    const QDomElement properties = child(e, "style:table-column-properties");
                    QString relative = properties.attribute(QStringLiteral("style:rel-column-width"));
                    relative.remove(QLatin1Char('*'));
                    const double width = relative.toDouble() > 0 ? relative.toDouble() : length(properties.attribute(QStringLiteral("style:column-width")));
                    if (width > 0) {
                        m_columnWidths.insert(name, width);
                    }
                }
                m_styles.insert(info.family + QLatin1Char('/') + name, info);
            } else if (is(e, "text:list-style")) {
                ListLevels levels;
                for (QDomElement l = e.firstChildElement(); !l.isNull(); l = l.nextSiblingElement()) {
                    const int level = l.attribute(QStringLiteral("text:level")).toInt();
                    if (is(l, "text:list-level-style-number")) {
                        const QString format = l.attribute(QStringLiteral("style:num-format"));
                        levels.levels.insert(level, format == QLatin1String("a") ? QTextListFormat::ListLowerAlpha
                            : format == QLatin1String("A") ? QTextListFormat::ListUpperAlpha
                            : format == QLatin1String("i") ? QTextListFormat::ListLowerRoman
                            : format == QLatin1String("I") ? QTextListFormat::ListUpperRoman
                            : format.isEmpty() ? QTextListFormat::ListDisc : QTextListFormat::ListDecimal);
                    } else if (is(l, "text:list-level-style-bullet") || is(l, "text:list-level-style-image")) {
                        static const QTextListFormat::Style marks[] = {QTextListFormat::ListDisc, QTextListFormat::ListCircle, QTextListFormat::ListSquare};
                        levels.levels.insert(level, marks[qMax(0, level - 1) % 3]);
                    }
                }
                m_listStyles.insert(e.attribute(QStringLiteral("style:name")), levels);
            } else if (is(e, "style:page-layout")) {
                m_pageLayouts.insert(e.attribute(QStringLiteral("style:name")), e);
            }
        }
    }

    const StyleInfo *resolved(const QString &family, const QString &name, int depth = 0)
    {
        auto it = m_styles.find(family + QLatin1Char('/') + name);
        if (it == m_styles.end()) {
            return nullptr;
        }
        StyleInfo &info = it.value();
        if (!info.resolved) {
            info.resolved = true;
            if (!info.parent.isEmpty() && info.parent != name && depth < 20) {
                if (const StyleInfo *parent = resolved(family, info.parent, depth + 1)) {
                    QTextBlockFormat block = parent->block;
                    // A page break is the paragraph's own, never its parent's.
                    block.clearProperty(QTextFormat::PageBreakPolicy);
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

    void page(const QDomElement &masters)
    {
        QDomElement master = child(masters, "style:master-page");
        for (QDomElement e = masters.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            if (is(e, "style:master-page") && e.attribute(QStringLiteral("style:name")) == QLatin1String("Standard")) {
                master = e;
            }
        }
        if (master.isNull()) {
            return;
        }
        const QDomElement layout = m_pageLayouts.value(master.attribute(QStringLiteral("style:page-layout-name")));
        const QDomElement properties = child(layout, "style:page-layout-properties");
        const QDomElement header = child(master, "style:header");
        const QDomElement footer = child(master, "style:footer");
        if (!properties.isNull()) {
            const double width = length(properties.attribute(QStringLiteral("fo:page-width")));
            const double height = length(properties.attribute(QStringLiteral("fo:page-height")));
            m_setup->setSizeCm(width / Doc::pxPerCm, height / Doc::pxPerCm);
            const auto margin = [&](const char *name, double fallback, double extra) {
                QString value = properties.attribute(QLatin1String(name));
                if (value.isEmpty()) {
                    value = properties.attribute(QStringLiteral("fo:margin"));
                }
                bool ok = false;
                const double px = length(value, &ok);
                return ok ? qBound(0.0, qRound((px / Doc::pxPerCm + extra) * 100) / 100.0, 10.0) : fallback;
            };
            // What a header or a footer takes is margin too, for Textos.
            const auto band = [&](const char *style, bool present) {
                if (!present) {
                    return 0.0;
                }
                const QDomElement p = child(child(layout, style), "style:header-footer-properties");
                double height = length(p.attribute(QStringLiteral("fo:min-height")));
                if (height <= 0) {
                    height = length(p.attribute(QStringLiteral("svg:height")));
                }
                return height / Doc::pxPerCm;
            };
            m_setup->top = margin("fo:margin-top", m_setup->top, band("style:header-style", !header.isNull()));
            m_setup->bottom = margin("fo:margin-bottom", m_setup->bottom, band("style:footer-style", !footer.isNull()));
            m_setup->left = margin("fo:margin-left", m_setup->left, 0);
            m_setup->right = margin("fo:margin-right", m_setup->right, 0);
        }
        bool number = false;
        m_setup->header = plain(header, &number).simplified();
        number = false;
        m_setup->footer = plain(footer, &number).simplified();
        m_setup->pageNumbers = number;
    }

    /// The words under an element, without its page number fields.
    static QString plain(const QDomElement &element, bool *number)
    {
        QString text;
        for (QDomNode node = element.firstChild(); !node.isNull(); node = node.nextSibling()) {
            if (node.isText()) {
                text += node.nodeValue();
            } else if (node.isElement()) {
                const QDomElement e = node.toElement();
                if (is(e, "text:page-number")) {
                    *number = true;
                } else if (is(e, "text:tab") || is(e, "text:s") || is(e, "text:line-break")) {
                    text += QLatin1Char(' ');
                } else {
                    text += plain(e, number);
                    if (is(e, "text:p")) {
                        text += QLatin1Char(' ');
                    }
                }
            }
        }
        return text;
    }

    // --- The body ---

    void blocks(const QDomElement &parent, int listLevel, const ListLevels *listStyle)
    {
        for (QDomElement e = parent.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            if (is(e, "text:p") || is(e, "text:h")) {
                paragraph(e, listLevel, listStyle);
            } else if (is(e, "text:list")) {
                list(e, listLevel, listStyle);
            } else if (is(e, "table:table")) {
                table(e);
            } else if (is(e, "text:section") || is(e, "text:table-of-content") || is(e, "text:index-body")) {
                blocks(e, listLevel, listStyle);
            }
        }
    }

    void list(const QDomElement &element, int outerLevel, const ListLevels *outerStyle)
    {
        const int level = outerLevel + 1;
        const QString name = element.attribute(QStringLiteral("text:style-name"));
        const ListLevels *style = m_listStyles.contains(name) ? &m_listStyles[name] : outerStyle;
        // A new list, unless it goes on where another of its level left off.
        if (element.attribute(QStringLiteral("text:continue-numbering")) != QLatin1String("true")) {
            m_lists.remove(level);
        }
        for (QDomElement item = element.firstChildElement(); !item.isNull(); item = item.nextSiblingElement()) {
            if (is(item, "text:list-item")) {
                m_itemStarted = false;
                blocks(item, level, style);
            } else if (is(item, "text:list-header")) {
                blocks(item, 0, nullptr);
            }
        }
        m_lists.remove(level + 1);
        if (outerLevel == 0) {
            m_lists.clear();
        }
    }

    void paragraph(const QDomElement &p, int listLevel, const ListLevels *listStyle)
    {
        const StyleInfo *style = resolved(QStringLiteral("paragraph"), p.attribute(QStringLiteral("text:style-name")));
        QTextBlockFormat block = m_defaultBlock;
        QTextCharFormat chars = m_defaultChars;
        bool breakAfter = false;
        QString mapped;
        if (style) {
            block.merge(style->block);
            chars.merge(style->chars);
            breakAfter = style->breakAfter;
            mapped = style->mapped;
            if (style->automatic && !style->masterPage.isEmpty() && !m_first) {
                block.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
            }
        }
        if (is(p, "text:h")) {
            const int level = qBound(1, p.attribute(QStringLiteral("text:outline-level"), QStringLiteral("1")).toInt(), 6);
            block.setHeadingLevel(level);
            if (mapped.isEmpty() && level <= 3) {
                mapped = QLatin1Char('h') + QString::number(level);
            }
        }
        if (!mapped.isEmpty()) {
            block.setProperty(Doc::StyleProperty, mapped);
            if (const int heading = Doc::style(mapped).heading) {
                block.setHeadingLevel(heading);
            }
        }
        if (m_breakPending) {
            block.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
            m_breakPending = false;
        }
        const bool listed = listLevel > 0 && !m_itemStarted;
        if (listLevel > 0) {
            // The list gives the indent.
            block.setLeftMargin(0);
            block.setTextIndent(0);
            if (!listed) {
                // A second paragraph of the same item: under the first, without a mark.
                block.setIndent(listLevel);
            }
        }
        if (m_unused) {
            m_cursor.setBlockFormat(block);
            m_cursor.setBlockCharFormat(chars);
            m_unused = false;
        } else {
            m_cursor.insertBlock(block, chars);
        }
        m_first = false;
        m_cursor.setCharFormat(chars);
        if (listed) {
            m_itemStarted = true;
            QTextList *open = m_lists.value(listLevel);
            if (open) {
                open->add(m_cursor.block());
            } else {
                QTextListFormat format;
                format.setStyle(listStyle ? listStyle->levels.value(listLevel, QTextListFormat::ListDisc) : QTextListFormat::ListDisc);
                format.setIndent(listLevel);
                m_lists.insert(listLevel, m_cursor.createList(format));
            }
        }
        inlines(p, chars, QString());
        m_breakPending = breakAfter;
    }

    void inlines(const QDomElement &parent, const QTextCharFormat &format, const QString &href)
    {
        for (QDomNode node = parent.firstChild(); !node.isNull(); node = node.nextSibling()) {
            if (node.isText()) {
                // White space between elements is not text, unless it sits between words.
                QString text = node.nodeValue();
                text.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\t'), QLatin1Char(' '));
                static const QRegularExpression spaces(QStringLiteral(" {2,}"));
                text.replace(spaces, QStringLiteral(" "));
                if (!text.isEmpty()) {
                    m_cursor.insertText(text, format);
                }
                continue;
            }
            const QDomElement e = node.toElement();
            if (e.isNull()) {
                continue;
            }
            if (is(e, "text:span")) {
                QTextCharFormat inner = format;
                if (const StyleInfo *style = resolved(QStringLiteral("text"), e.attribute(QStringLiteral("text:style-name")))) {
                    inner.merge(style->chars);
                }
                if (!href.isEmpty()) {
                    inner.merge(Doc::linkFormat(href));
                }
                inlines(e, inner, href);
            } else if (is(e, "text:a")) {
                const QString target = e.attribute(QStringLiteral("xlink:href"));
                QTextCharFormat inner = format;
                inner.merge(Doc::linkFormat(target));
                inlines(e, inner, target);
            } else if (is(e, "text:s")) {
                m_cursor.insertText(QString(qMax(1, e.attribute(QStringLiteral("text:c"), QStringLiteral("1")).toInt()), QLatin1Char(' ')), format);
            } else if (is(e, "text:tab")) {
                m_cursor.insertText(QStringLiteral("\t"), format);
            } else if (is(e, "text:line-break")) {
                m_cursor.insertText(QString(QChar::LineSeparator), format);
            } else if (is(e, "draw:frame")) {
                frame(e, format);
            } else if (is(e, "text:note") || is(e, "office:annotation") || is(e, "text:tracked-changes") || is(e, "text:change")) {
                // Not carried.
            } else if (local(e) == QLatin1String("soft-page-break")) {
                // Where LibreOffice happened to break: nothing of the document's.
            } else {
                // Fields and the like: what they show.
                inlines(e, format, href);
            }
        }
    }

    void frame(const QDomElement &frame, const QTextCharFormat &format)
    {
        const QDomElement picture = child(frame, "draw:image");
        if (picture.isNull()) {
            // A text box: its paragraphs, in line.
            const QDomElement box = child(frame, "draw:text-box");
            for (QDomElement p = box.firstChildElement(); !p.isNull(); p = p.nextSiblingElement()) {
                if (is(p, "text:p")) {
                    inlines(p, format, QString());
                }
            }
            return;
        }
        QString href = picture.attribute(QStringLiteral("xlink:href"));
        if (href.startsWith(QLatin1String("./"))) {
            href.remove(0, 2);
        }
        QString name = m_images.value(href);
        if (name.isEmpty()) {
            const QImage image = QImage::fromData(m_zip.file(href));
            if (image.isNull()) {
                return;
            }
            name = Doc::addImage(m_document, image);
            m_images.insert(href, name);
        }
        const QImage image = Doc::image(m_document, name);
        double width = length(frame.attribute(QStringLiteral("svg:width")));
        double height = length(frame.attribute(QStringLiteral("svg:height")));
        if (width < 1 || height < 1) {
            width = image.width();
            height = image.height();
        }
        QTextImageFormat imageFormat;
        imageFormat.setName(name);
        imageFormat.setWidth(width);
        imageFormat.setHeight(height);
        m_cursor.insertImage(imageFormat);
        m_cursor.setCharFormat(format);
    }

    void table(const QDomElement &element)
    {
        QList<QDomElement> rows;
        QList<double> widths;
        const std::function<void(const QDomElement &)> collect = [&](const QDomElement &parent) {
            for (QDomElement e = parent.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
                if (is(e, "table:table-row")) {
                    rows << e;
                } else if (is(e, "table:table-column")) {
                    const int repeat = qBound(1, e.attribute(QStringLiteral("table:number-columns-repeated"), QStringLiteral("1")).toInt(), 256);
                    for (int i = 0; i < repeat; ++i) {
                        widths << m_columnWidths.value(e.attribute(QStringLiteral("table:style-name")), 0);
                    }
                } else if (is(e, "table:table-header-rows") || is(e, "table:table-rows") || is(e, "table:table-row-group") || is(e, "table:table-columns")
                           || is(e, "table:table-header-columns")) {
                    collect(e);
                }
            }
        };
        collect(element);
        int columns = 0;
        for (const QDomElement &row : std::as_const(rows)) {
            int count = 0;
            for (QDomElement c = row.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
                if (is(c, "table:table-cell") || is(c, "table:covered-table-cell")) {
                    count += qBound(1, c.attribute(QStringLiteral("table:number-columns-repeated"), QStringLiteral("1")).toInt(), 256);
                }
            }
            columns = qMax(columns, count);
        }
        if (rows.isEmpty() || columns == 0 || columns > 256) {
            return;
        }
        QTextTableFormat format = Doc::tableFormat(columns);
        double total = 0;
        for (const double width : std::as_const(widths)) {
            total += width;
        }
        if (widths.size() == columns && total > 0 && !widths.contains(0)) {
            QList<QTextLength> constraints;
            for (const double width : std::as_const(widths)) {
                constraints << QTextLength(QTextLength::PercentageLength, width * 100 / total);
            }
            format.setColumnWidthConstraints(constraints);
        }
        QTextTable *made = m_cursor.insertTable(int(rows.size()), columns, format);
        m_first = false;
        struct Merge {
            int row;
            int column;
            int rows;
            int columns;
        };
        QList<Merge> merges;
        const QHash<int, QTextList *> outerLists = m_lists;
        const bool outerItem = m_itemStarted;
        for (int row = 0; row < rows.size(); ++row) {
            int column = 0;
            for (QDomElement c = rows.at(row).firstChildElement(); !c.isNull() && column < columns; c = c.nextSiblingElement()) {
                const int repeat = qBound(1, c.attribute(QStringLiteral("table:number-columns-repeated"), QStringLiteral("1")).toInt(), 256);
                if (is(c, "table:covered-table-cell")) {
                    column += repeat;
                    continue;
                }
                if (!is(c, "table:table-cell")) {
                    continue;
                }
                const int columnSpan = qBound(1, c.attribute(QStringLiteral("table:number-columns-spanned"), QStringLiteral("1")).toInt(), columns - column);
                const int rowSpan = qBound(1, c.attribute(QStringLiteral("table:number-rows-spanned"), QStringLiteral("1")).toInt(), int(rows.size()) - row);
                if (columnSpan > 1 || rowSpan > 1) {
                    merges.append({row, column, rowSpan, columnSpan});
                }
                QTextTableCell cell = made->cellAt(row, column);
                const QColor background = m_cellColors.value(c.attribute(QStringLiteral("table:style-name")));
                if (background.isValid()) {
                    QTextCharFormat cellFormat = cell.format();
                    cellFormat.setBackground(background);
                    cell.setFormat(cellFormat);
                }
                m_cursor = cell.firstCursorPosition();
                m_unused = true;
                m_lists.clear();
                m_itemStarted = false;
                blocks(c, 0, nullptr);
                column += repeat;
            }
        }
        for (const Merge &merge : std::as_const(merges)) {
            made->mergeCells(merge.row, merge.column, merge.rows, merge.columns);
        }
        m_lists = outerLists;
        m_itemStarted = outerItem;
        m_cursor = QTextCursor(m_document);
        m_cursor.setPosition(made->lastPosition() + 1);
        m_unused = true;
        m_breakPending = false;
    }

    const ZipReader &m_zip;
    QTextDocument *m_document;
    Doc::PageSetup *m_setup;
    QTextCursor m_cursor;
    QHash<QString, QString> m_fontFaces;
    QHash<QString, StyleInfo> m_styles;
    QHash<QString, ListLevels> m_listStyles;
    QHash<QString, QDomElement> m_pageLayouts;
    QHash<QString, QColor> m_cellColors;
    QHash<QString, double> m_columnWidths;
    QHash<QString, QString> m_images;
    QHash<int, QTextList *> m_lists;
    QTextBlockFormat m_defaultBlock;
    QTextCharFormat m_defaultChars;
    bool m_unused = true;
    bool m_first = true;
    bool m_breakPending = false;
    bool m_itemStarted = false;
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
    if (!zip.isValid() || !zip.contains(QStringLiteral("content.xml"))) {
        if (error) {
            *error = QStringLiteral("El archivo no es un documento OpenDocument (.odt).");
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

} // namespace Odt
