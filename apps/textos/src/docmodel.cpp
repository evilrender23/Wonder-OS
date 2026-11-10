#include "docmodel.h"

#include <QAbstractTextDocumentLayout>
#include <QFontDatabase>
#include <QImage>
#include <QTextFrame>
#include <QUrl>

namespace Doc {

namespace {

QString firstInstalled(const QStringList &wanted, QFontDatabase::SystemFont fallback)
{
    const QStringList families = QFontDatabase::families();
    for (const QString &family : wanted) {
        if (families.contains(family)) {
            return family;
        }
    }
    return QFontDatabase::systemFont(fallback).family();
}

Style make(const char *id, const QString &name, int heading, double points, bool bold, double before, double after)
{
    Style style;
    style.id = QLatin1String(id);
    style.name = name;
    style.heading = heading;
    style.block.setProperty(StyleProperty, style.id);
    style.block.setTopMargin(before * pxPerPt);
    style.block.setBottomMargin(after * pxPerPt);
    style.block.setLeftMargin(0);
    style.block.setRightMargin(0);
    style.block.setTextIndent(0);
    style.block.setLineHeight(115, QTextBlockFormat::ProportionalHeight);
    style.block.setHeadingLevel(heading);
    style.block.setBackground(Qt::NoBrush);
    style.chars.setFontFamilies({defaultFamily()});
    style.chars.setFontPointSize(points);
    style.chars.setFontWeight(bold ? QFont::Bold : QFont::Normal);
    style.chars.setFontItalic(false);
    style.chars.setForeground(ink());
    return style;
}

QList<Style> build()
{
    QList<Style> all;
    all << make("title", QStringLiteral("Título"), 0, 26, true, 0, 6);
    all << make("h1", QStringLiteral("Encabezado 1"), 1, 20, true, 18, 6);
    all << make("h2", QStringLiteral("Encabezado 2"), 2, 16, true, 14, 4);
    all << make("h3", QStringLiteral("Encabezado 3"), 3, 13, true, 12, 4);
    all << make("body", QStringLiteral("Cuerpo"), 0, defaultPointSize, false, 0, 8);
    Style quote = make("quote", QStringLiteral("Cita"), 0, defaultPointSize, false, 6, 10);
    quote.block.setLeftMargin(32);
    quote.block.setRightMargin(32);
    quote.chars.setFontItalic(true);
    quote.chars.setForeground(QColor(0x55, 0x55, 0x5a));
    all << quote;
    Style code = make("code", QStringLiteral("Código"), 0, 10, false, 0, 0);
    code.block.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
    code.block.setBackground(QColor(0xf2, 0xf2, 0xf4));
    code.block.setLeftMargin(8);
    code.block.setRightMargin(8);
    code.chars.setFontFamilies({monoFamily()});
    all << code;
    return all;
}

} // namespace

QString defaultFamily()
{
    static const QString family = firstInstalled({QStringLiteral("Inter"), QStringLiteral("Noto Sans"), QStringLiteral("Liberation Sans"), QStringLiteral("DejaVu Sans")},
                                                 QFontDatabase::GeneralFont);
    return family;
}

QString monoFamily()
{
    static const QString family = firstInstalled({QStringLiteral("JetBrains Mono"), QStringLiteral("Noto Sans Mono"), QStringLiteral("Liberation Mono"), QStringLiteral("DejaVu Sans Mono")},
                                                 QFontDatabase::FixedFont);
    return family;
}

const QList<Style> &styles()
{
    static const QList<Style> all = build();
    return all;
}

const Style &style(const QString &id)
{
    const QList<Style> &all = styles();
    for (const Style &style : all) {
        if (style.id == id) {
            return style;
        }
    }
    return all.at(4);
}

QString styleOf(const QTextBlock &block)
{
    const QString id = block.blockFormat().stringProperty(StyleProperty);
    return id.isEmpty() ? QStringLiteral("body") : id;
}

int outlineLevel(const QTextBlock &block)
{
    const QString id = styleOf(block);
    if (id == QLatin1String("title")) {
        return 0;
    }
    const int heading = style(id).heading;
    if (heading > 0) {
        return heading;
    }
    // A heading read from a file whose style Textos does not have a name for.
    const int level = block.blockFormat().headingLevel();
    return level > 0 ? qMin(level, 3) : -1;
}

QColor ink()
{
    return QColor(0x1d, 0x1d, 0x1f);
}

QTextTableFormat tableFormat(int columns)
{
    QTextTableFormat format;
    format.setBorder(1);
    format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    format.setBorderBrush(QColor(0xb4, 0xb4, 0xba));
    format.setBorderCollapse(true);
    format.setCellPadding(5);
    format.setCellSpacing(0);
    format.setTopMargin(4);
    format.setBottomMargin(10);
    format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    QList<QTextLength> widths;
    for (int column = 0; column < columns; ++column) {
        widths << QTextLength(QTextLength::PercentageLength, 100.0 / qMax(1, columns));
    }
    format.setColumnWidthConstraints(widths);
    return format;
}

QString familyOf(const QTextCharFormat &format)
{
    const QStringList families = format.fontFamilies().toStringList();
    return families.isEmpty() || families.first().isEmpty() ? defaultFamily() : families.first();
}

double pointSizeOf(const QTextCharFormat &format)
{
    const double size = format.fontPointSize();
    return size > 0 ? size : defaultPointSize;
}

Look lookOf(const QTextCharFormat &format)
{
    Look look;
    look.family = familyOf(format);
    look.size = pointSizeOf(format);
    look.bold = format.hasProperty(QTextFormat::FontWeight) && format.fontWeight() >= QFont::DemiBold;
    look.italic = format.fontItalic();
    look.underline = format.fontUnderline() || format.underlineStyle() == QTextCharFormat::SingleUnderline;
    look.strike = format.fontStrikeOut();
    look.vertical = format.verticalAlignment() == QTextCharFormat::AlignSuperScript ? 1
        : format.verticalAlignment() == QTextCharFormat::AlignSubScript ? 2 : 0;
    const QBrush foreground = format.foreground();
    look.color = format.hasProperty(QTextFormat::ForegroundBrush) && foreground.style() != Qt::NoBrush ? foreground.color().toRgb() : ink();
    look.color.setAlpha(255);
    const QBrush background = format.background();
    if (format.hasProperty(QTextFormat::BackgroundBrush) && background.style() != Qt::NoBrush && background.color().alpha() > 0) {
        look.background = background.color().toRgb();
        look.background.setAlpha(255);
    }
    return look;
}

QTextCharFormat linkFormat(const QString &href)
{
    QTextCharFormat format;
    format.setAnchor(true);
    format.setAnchorHref(href);
    format.setForeground(QColor(0x0a, 0x5f, 0xd9));
    format.setFontUnderline(true);
    return format;
}

void applyStyle(QTextCursor &cursor, const QString &id)
{
    const Style &wanted = style(id);
    QTextDocument *document = cursor.document();
    if (!document) {
        return;
    }
    const int first = document->findBlock(cursor.selectionStart()).blockNumber();
    const int last = document->findBlock(cursor.selectionEnd()).blockNumber();
    cursor.beginEditBlock();
    for (int number = first; number <= last; ++number) {
        const QTextBlock block = document->findBlockByNumber(number);
        const QTextBlockFormat old = block.blockFormat();
        QTextBlockFormat format = wanted.block;
        format.setObjectIndex(old.objectIndex());
        if (old.hasProperty(QTextFormat::BlockAlignment)) {
            format.setAlignment(old.alignment());
        }
        if (old.hasProperty(QTextFormat::PageBreakPolicy)) {
            format.setPageBreakPolicy(old.pageBreakPolicy());
        }
        if (old.indent() > 0) {
            format.setIndent(old.indent());
        }
        QTextCursor at(block);
        at.setBlockFormat(format);
        at.setBlockCharFormat(wanted.chars);
        // Pictures keep their own format, and links their colour.
        struct Piece {
            int start;
            int length;
            QTextCharFormat format;
        };
        QList<Piece> pieces;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid()) {
                pieces.append({fragment.position(), fragment.length(), fragment.charFormat()});
            }
        }
        for (const Piece &piece : std::as_const(pieces)) {
            if (piece.format.isImageFormat()) {
                continue;
            }
            QTextCursor part(document);
            part.setPosition(piece.start);
            part.setPosition(piece.start + piece.length, QTextCursor::KeepAnchor);
            QTextCharFormat merged = wanted.chars;
            if (piece.format.isAnchor()) {
                merged.merge(linkFormat(piece.format.anchorHref()));
            }
            part.mergeCharFormat(merged);
        }
    }
    cursor.endEditBlock();
    // What is typed next takes the style too.
    if (!cursor.hasSelection()) {
        cursor.mergeCharFormat(wanted.chars);
    }
}

void tagStyle(QTextCursor &cursor, const QString &id)
{
    QTextBlockFormat format;
    format.setProperty(StyleProperty, id);
    const int heading = style(id).id == id ? style(id).heading : 0;
    if (heading > 0) {
        format.setHeadingLevel(heading);
    }
    cursor.mergeBlockFormat(format);
}

void prepare(QTextDocument *document)
{
    // Lay out on a fixed 96 dpi device: a point is the same number of pixels
    // on every screen, and on paper.
    static QImage device = [] {
        QImage image(1, 1, QImage::Format_ARGB32_Premultiplied);
        image.setDotsPerMeterX(qRound(96 / 0.0254));
        image.setDotsPerMeterY(qRound(96 / 0.0254));
        return image;
    }();
    document->documentLayout()->setPaintDevice(&device);
    QFont font(defaultFamily());
    font.setPointSizeF(defaultPointSize);
    document->setDefaultFont(font);
    document->setDocumentMargin(0);
    document->setIndentWidth(28);
    QTextOption option = document->defaultTextOption();
    option.setTabStopDistance(1.25 * pxPerCm);
    document->setDefaultTextOption(option);
    QTextFrameFormat frame = document->rootFrame()->frameFormat();
    frame.setMargin(0);
    frame.setPadding(0);
    frame.setBorder(0);
    document->rootFrame()->setFrameFormat(frame);
}

QImage image(const QTextDocument *document, const QString &name)
{
    const QVariant resource = document->resource(QTextDocument::ImageResource, QUrl(name));
    if (resource.metaType().id() == QMetaType::QImage) {
        return resource.value<QImage>();
    }
    if (resource.metaType().id() == QMetaType::QPixmap) {
        return resource.value<QImage>();
    }
    if (resource.metaType().id() == QMetaType::QByteArray) {
        return QImage::fromData(resource.toByteArray());
    }
    return {};
}

QString addImage(QTextDocument *document, const QImage &picture)
{
    QString name;
    for (int number = 1;; ++number) {
        name = QStringLiteral("textos-image-%1").arg(number);
        if (!document->resource(QTextDocument::ImageResource, QUrl(name)).isValid()) {
            break;
        }
    }
    document->addResource(QTextDocument::ImageResource, QUrl(name), picture);
    return name;
}

// --- The page ---------------------------------------------------------------------

namespace {

struct Paper {
    const char *name;
    double width;
    double height;
};
constexpr Paper knownPapers[] = {
    {"A4", 21.0, 29.7},
    {"Carta", 21.59, 27.94},
    {"Legal", 21.59, 35.56},
    {"A5", 14.8, 21.0},
    {"A3", 29.7, 42.0},
};

} // namespace

QStringList PageSetup::papers()
{
    QStringList names;
    for (const Paper &known : knownPapers) {
        names << QLatin1String(known.name);
    }
    return names;
}

QSizeF PageSetup::sizeCm() const
{
    QSizeF size(21.0, 29.7);
    for (const Paper &known : knownPapers) {
        if (paper == QLatin1String(known.name)) {
            size = QSizeF(known.width, known.height);
        }
    }
    return landscape ? size.transposed() : size;
}

void PageSetup::setSizeCm(double width, double height)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    landscape = width > height;
    const double shortSide = qMin(width, height);
    const double longSide = qMax(width, height);
    double best = 1e9;
    for (const Paper &known : knownPapers) {
        const double distance = qAbs(known.width - shortSide) + qAbs(known.height - longSide);
        if (distance < best) {
            best = distance;
            paper = QLatin1String(known.name);
        }
    }
}

QSizeF PageSetup::pagePx() const
{
    return sizeCm() * pxPerCm;
}

QSizeF PageSetup::contentPx() const
{
    const QSizeF page = pagePx();
    // Margins that would leave no room are not obeyed to the end.
    const double width = qMax(page.width() - (left + right) * pxPerCm, 3 * pxPerCm);
    const double height = qMax(page.height() - (top + bottom) * pxPerCm, 3 * pxPerCm);
    return QSizeF(width, height);
}

QVariantMap PageSetup::toMap() const
{
    return {
        {QStringLiteral("paper"), paper},
        {QStringLiteral("landscape"), landscape},
        {QStringLiteral("top"), top},
        {QStringLiteral("bottom"), bottom},
        {QStringLiteral("left"), left},
        {QStringLiteral("right"), right},
        {QStringLiteral("header"), header},
        {QStringLiteral("footer"), footer},
        {QStringLiteral("pageNumbers"), pageNumbers},
    };
}

PageSetup PageSetup::fromMap(const QVariantMap &map)
{
    PageSetup setup;
    const auto margin = [&](const char *key, double fallback) {
        const QVariant value = map.value(QLatin1String(key));
        return value.isValid() ? qBound(0.0, value.toDouble(), 10.0) : fallback;
    };
    const QString paper = map.value(QStringLiteral("paper")).toString();
    if (papers().contains(paper)) {
        setup.paper = paper;
    }
    setup.landscape = map.value(QStringLiteral("landscape")).toBool();
    setup.top = margin("top", setup.top);
    setup.bottom = margin("bottom", setup.bottom);
    setup.left = margin("left", setup.left);
    setup.right = margin("right", setup.right);
    setup.header = map.value(QStringLiteral("header")).toString();
    setup.footer = map.value(QStringLiteral("footer")).toString();
    setup.pageNumbers = map.value(QStringLiteral("pageNumbers")).toBool();
    return setup;
}

} // namespace Doc
