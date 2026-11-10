#pragma once

#include <QList>
#include <QSizeF>
#include <QString>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTableFormat>
#include <QImage>
#include <QColor>
#include <QVariantMap>

/// What every part of Textos agrees on about a document: its units, its
/// paragraph styles, and the page it is laid out on. The text itself lives in
/// a QTextDocument, laid out at 96 dots per inch whatever the screen.
namespace Doc {

constexpr double pxPerPt = 96.0 / 72.0;
constexpr double pxPerCm = 96.0 / 2.54;

/// Block format property holding the id of the paragraph style.
constexpr int StyleProperty = QTextFormat::UserProperty + 1;

struct Style {
    QString id;
    /// As the panel shows it.
    QString name;
    /// 0 for body text, 1–3 for headings; the title is 0 too but leads the outline.
    int heading = 0;
    QTextBlockFormat block;
    QTextCharFormat chars;
};

/// Título, Encabezado 1–3, Cuerpo, Cita, Código, in the order of the panel.
const QList<Style> &styles();
/// The style with that id; Cuerpo for an id nobody knows.
const Style &style(const QString &id);
/// The id of the style of a paragraph ("body" when it has none).
QString styleOf(const QTextBlock &block);
/// 1–3 for headings, 0 for the title, -1 for everything else: the outline's levels.
int outlineLevel(const QTextBlock &block);
/// Gives the paragraphs under the cursor (all the selected ones) a style:
/// spacing, size and weight are the style's; alignment, lists, links and
/// underlines stay.
void applyStyle(QTextCursor &cursor, const QString &id);
/// Marks a paragraph as being of a style without touching how it looks (what
/// the readers do with paragraphs that bring their own formatting).
void tagStyle(QTextCursor &cursor, const QString &id);

/// The family new documents are written in, and the one for code.
QString defaultFamily();
QString monoFamily();
constexpr double defaultPointSize = 11.0;
/// How a link looks.
QTextCharFormat linkFormat(const QString &href);

/// Default font, no frame margins, the device everything is measured on.
void prepare(QTextDocument *document);
/// The colour text has when nothing says otherwise.
QColor ink();
/// How a new table looks: hairlines, the whole width, columns alike.
QTextTableFormat tableFormat(int columns);
/// The family and size a piece of text really has, style or not.
QString familyOf(const QTextCharFormat &format);
double pointSizeOf(const QTextCharFormat &format);
/// How a piece of text looks, with every default filled in: what the writers
/// compare against a style to know what to spell out.
struct Look {
    QString family;
    double size = defaultPointSize;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    /// 0 on the line, 1 above it, 2 below.
    int vertical = 0;
    QColor color;
    /// Invalid when there is no highlight.
    QColor background;
    bool operator==(const Look &other) const = default;
};
Look lookOf(const QTextCharFormat &format);
/// The image of that name in the document's resources (null if none).
QImage image(const QTextDocument *document, const QString &name);
/// Adds an image to the resources and returns the name it got.
QString addImage(QTextDocument *document, const QImage &image);

struct PageSetup {
    /// A4, Carta, Legal, A5 or A3.
    QString paper = QStringLiteral("A4");
    bool landscape = false;
    /// Margins in centimetres.
    double top = 2.5;
    double bottom = 2.5;
    double left = 2.5;
    double right = 2.5;
    QString header;
    QString footer;
    /// The page number, at the right of the footer.
    bool pageNumbers = false;

    static QStringList papers();
    /// Centimetres of the sheet as it is turned.
    QSizeF sizeCm() const;
    /// Takes a size in centimetres from a file: the nearest known paper and its turn.
    void setSizeCm(double width, double height);
    QSizeF pagePx() const;
    /// The size of the text area, in layout pixels.
    QSizeF contentPx() const;
    QVariantMap toMap() const;
    static PageSetup fromMap(const QVariantMap &map);
    bool operator==(const PageSetup &other) const = default;
};

} // namespace Doc
