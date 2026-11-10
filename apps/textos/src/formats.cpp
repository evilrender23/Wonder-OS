#include "formats.h"

#include "docx.h"
#include "odt.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringDecoder>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>
#include <QUrl>

#include <functional>
#include <memory>

namespace Formats {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

bool writeBytes(const QString &path, const QByteArray &bytes, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        return fail(error, QStringLiteral("No se ha podido escribir el archivo."));
    }
    return true;
}

/// Text files are UTF-8 unless they say (a BOM) or show (bytes that are not) otherwise.
QString decode(const QByteArray &bytes)
{
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        return QString::fromUtf8(bytes.mid(3));
    }
    if (bytes.startsWith("\xFF\xFE") || bytes.startsWith("\xFE\xFF")) {
        QStringDecoder utf16(QStringDecoder::Utf16);
        return utf16(bytes);
    }
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8(bytes);
    return utf8.hasError() ? QString::fromLatin1(bytes) : text;
}

bool isFixed(const QTextCharFormat &format)
{
    if (format.fontFixedPitch()) {
        return true;
    }
    const QString family = Doc::familyOf(format).toLower();
    return family.contains(QLatin1String("mono")) || family.contains(QLatin1String("courier")) || family == QLatin1String("consolas");
}

/// Pictures a text format names by address (a file beside the document, a
/// data: URL) become pictures of the document, so that every writer finds them.
void adoptImages(QTextDocument *document)
{
    struct Found {
        int position;
        int length;
        QTextImageFormat format;
    };
    QList<Found> found;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().isImageFormat()) {
                found.append({fragment.position(), fragment.length(), fragment.charFormat().toImageFormat()});
            }
        }
    }
    QHash<QString, QString> adopted;
    QHash<QString, QImage> pictures;
    for (const Found &one : std::as_const(found)) {
        const QString name = one.format.name();
        if (name.startsWith(QLatin1String("textos-image-"))) {
            continue;
        }
        QString mine = adopted.value(name);
        QImage image;
        if (mine.isEmpty()) {
            if (name.startsWith(QLatin1String("data:"))) {
                // A picture written into the page itself.
                const qsizetype comma = name.indexOf(QLatin1Char(','));
                const QByteArray payload = name.mid(comma + 1).toLatin1();
                image.loadFromData(name.left(comma).endsWith(QLatin1String(";base64")) ? QByteArray::fromBase64(payload) : QByteArray::fromPercentEncoding(payload));
            } else {
                image = Doc::image(document, name);
            }
            if (image.isNull()) {
                continue;
            }
            mine = Doc::addImage(document, image);
            adopted.insert(name, mine);
            pictures.insert(mine, image);
        } else {
            image = pictures.value(mine);
        }
        QTextImageFormat format = one.format;
        format.setName(mine);
        if (format.width() <= 0) {
            // No wider than a page's text is by default.
            const double width = qMin<double>(image.width(), 600);
            format.setWidth(width);
            format.setHeight(image.width() > 0 ? width * image.height() / image.width() : image.height());
        } else if (format.height() <= 0 && image.width() > 0) {
            format.setHeight(format.width() * image.height() / image.width());
        }
        QTextCursor cursor(document);
        cursor.setPosition(one.position);
        cursor.setPosition(one.position + one.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(format);
    }
    // Qt looks a picture up by its address against the base: with one set,
    // those the document owns by name alone would not be found again.
    document->setBaseUrl(QUrl());
}

/// Qt's readers leave an empty paragraph at the top of a table's first cell
/// and another under every table; neither was in the file.
void tidyTables(QTextDocument *document)
{
    const auto drop = [&](const QTextBlock &empty) {
        const QTextBlock next = empty.next();
        if (!next.isValid() || empty.length() > 1) {
            return;
        }
        const QTextBlockFormat block = next.blockFormat();
        const QTextCharFormat chars = QTextCursor(next).blockCharFormat();
        QTextCursor at(empty);
        at.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
        at.removeSelectedText();
        at.setBlockFormat(block);
        at.setBlockCharFormat(chars);
    };
    const std::function<void(QTextFrame *)> walk = [&](QTextFrame *frame) {
        const QList<QTextFrame *> children = frame->childFrames();
        for (QTextFrame *inner : children) {
            walk(inner);
            auto *table = qobject_cast<QTextTable *>(inner);
            if (!table) {
                continue;
            }
            const QTextTableCell first = table->cellAt(0, 0);
            const QTextBlock top = first.firstCursorPosition().block();
            if (top.length() <= 1 && top.next().isValid() && top.next().position() <= first.lastPosition()) {
                drop(top);
            }
            const QTextBlock after = document->findBlock(table->lastPosition() + 1);
            QTextCursor probe(after);
            if (after.isValid() && after.length() <= 1 && after.next().isValid() && !after.textList() && probe.currentTable() == QTextCursor(after.next()).currentTable()) {
                drop(after);
            }
        }
    };
    QTextCursor edit(document);
    edit.beginEditBlock();
    walk(document->rootFrame());
    edit.endEditBlock();
}

/// Gives the paragraphs of a Markdown or HTML file the styles of Textos:
/// what Qt made of «#», «>» and fences says which.
void restyle(QTextDocument *document, bool keepLooks)
{
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    // Headings four deep are what a title over three levels is written as.
    bool titled = false;
    for (QTextBlock block = document->begin(); block.isValid() && !titled; block = block.next()) {
        titled = block.blockFormat().headingLevel() >= 4;
    }
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        const QTextBlockFormat old = block.blockFormat();
        QString id = QStringLiteral("body");
        bool code = old.hasProperty(QTextFormat::BlockCodeFence) || old.hasProperty(QTextFormat::BlockCodeLanguage)
            || old.nonBreakableLines();
        if (old.headingLevel() > 0) {
            const int level = old.headingLevel() - (titled ? 1 : 0);
            id = level == 0 ? QStringLiteral("title") : QLatin1Char('h') + QString::number(qMin(level, 3));
            code = false;
        } else if (code) {
            id = QStringLiteral("code");
        } else if (old.hasProperty(QTextFormat::BlockQuoteLevel) && old.intProperty(QTextFormat::BlockQuoteLevel) > 0) {
            id = QStringLiteral("quote");
        }
        QTextCursor at(block);
        if (keepLooks) {
            Doc::tagStyle(at, id == QLatin1String("body") && block.blockFormat().stringProperty(Doc::StyleProperty).isEmpty() ? id : id);
            continue;
        }
        const Doc::Style &style = Doc::style(id);
        QTextBlockFormat format = style.block;
        format.setObjectIndex(old.objectIndex());
        if (old.hasProperty(QTextFormat::BlockAlignment) && (old.alignment() & (Qt::AlignHCenter | Qt::AlignRight | Qt::AlignJustify))) {
            format.setAlignment(old.alignment());
        }
        at.setBlockFormat(format);
        at.setBlockCharFormat(style.chars);
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
            QTextCharFormat wanted = style.chars;
            if (style.heading == 0 && piece.format.fontWeight() >= QFont::DemiBold) {
                wanted.setFontWeight(QFont::Bold);
            }
            if (piece.format.fontItalic()) {
                wanted.setFontItalic(true);
            }
            if (piece.format.fontStrikeOut()) {
                wanted.setFontStrikeOut(true);
            }
            if (!code && isFixed(piece.format)) {
                wanted.setFontFamilies({Doc::monoFamily()});
                wanted.setBackground(QColor(0xf2, 0xf2, 0xf4));
            }
            if (piece.format.isAnchor() && !piece.format.anchorHref().isEmpty()) {
                wanted.merge(Doc::linkFormat(piece.format.anchorHref()));
            }
            QTextCursor part(document);
            part.setPosition(piece.start);
            part.setPosition(piece.start + piece.length, QTextCursor::KeepAnchor);
            part.setCharFormat(wanted);
        }
    }
    // Qt's tables come without lines.
    const std::function<void(QTextFrame *)> tables = [&](QTextFrame *frame) {
        for (QTextFrame *inner : frame->childFrames()) {
            if (auto *table = qobject_cast<QTextTable *>(inner)) {
                QTextTableFormat format = Doc::tableFormat(table->columns());
                table->setFormat(format);
                // The first row of a Markdown table is its header.
                QTextCharFormat strong;
                strong.setFontWeight(QFont::Bold);
                for (int column = 0; column < table->columns(); ++column) {
                    const QTextTableCell cell = table->cellAt(0, column);
                    QTextCursor header = cell.firstCursorPosition();
                    header.setPosition(cell.lastPosition(), QTextCursor::KeepAnchor);
                    header.mergeCharFormat(strong);
                }
            }
            tables(inner);
        }
    };
    if (!keepLooks) {
        tables(document->rootFrame());
    }
    cursor.endEditBlock();
}

// --- Markdown out -----------------------------------------------------------------

QString escapeMarkdown(const QString &text)
{
    QString out;
    for (const QChar c : text) {
        if (QStringLiteral("\\`*_[]<>#|~").contains(c)) {
            out += QLatin1Char('\\');
        }
        out += c;
    }
    return out;
}

QString dataUrl(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

/// The text of a paragraph with its marks; `plain` for where marks do not go (code).
QString inlineMarkdown(const QTextDocument *document, const QTextBlock &block, bool plain, bool header)
{
    // What the style of the paragraph already is needs no marks, nor does the header of a table.
    Doc::Look usual = Doc::lookOf(Doc::style(Doc::styleOf(block)).chars);
    usual.bold = usual.bold || header;
    QString out;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (!fragment.isValid()) {
            continue;
        }
        const QTextCharFormat format = fragment.charFormat();
        if (format.isImageFormat()) {
            const QImage image = Doc::image(document, format.toImageFormat().name());
            if (!image.isNull() && !plain) {
                out += QStringLiteral("![](%1)").arg(dataUrl(image));
            }
            continue;
        }
        QString text = fragment.text();
        if (plain) {
            out += text.replace(QChar::LineSeparator, QLatin1Char('\n'));
            continue;
        }
        // Marks hug the words: the spaces around go outside them.
        qsizetype lead = 0;
        while (lead < text.size() && text.at(lead).isSpace()) {
            ++lead;
        }
        qsizetype trail = 0;
        while (trail < text.size() - lead && text.at(text.size() - 1 - trail).isSpace()) {
            ++trail;
        }
        const QString before = text.left(lead);
        const QString after = text.right(trail);
        QString core = escapeMarkdown(text.mid(lead, text.size() - lead - trail));
        if (core.isEmpty()) {
            out += text.replace(QChar::LineSeparator, QStringLiteral("  \n"));
            continue;
        }
        const Doc::Look look = Doc::lookOf(format);
        if (isFixed(format)) {
            core = QLatin1Char('`') + text.mid(lead, text.size() - lead - trail) + QLatin1Char('`');
        }
        if (look.strike) {
            core = QStringLiteral("~~") + core + QStringLiteral("~~");
        }
        if (look.italic && !usual.italic) {
            core = QLatin1Char('*') + core + QLatin1Char('*');
        }
        if (look.bold && !usual.bold) {
            core = QStringLiteral("**") + core + QStringLiteral("**");
        }
        if (format.isAnchor() && !format.anchorHref().isEmpty()) {
            core = QStringLiteral("[%1](%2)").arg(core, format.anchorHref());
        }
        out += (before + core + after).replace(QChar::LineSeparator, QStringLiteral("  \n"));
    }
    return out;
}

void markdownBlocks(const QTextDocument *document, QTextFrame::iterator it, const QTextFrame::iterator &end, QString &out, int shift)
{
    bool inCode = false;
    bool inList = false;
    const auto closeCode = [&] {
        if (inCode) {
            out += QStringLiteral("```\n\n");
            inCode = false;
        }
    };
    for (; it != end && !it.atEnd(); ++it) {
        if (QTextFrame *frame = it.currentFrame()) {
            closeCode();
            auto *table = qobject_cast<QTextTable *>(frame);
            if (!table) {
                markdownBlocks(document, frame->begin(), frame->end(), out, shift);
                continue;
            }
            if (inList) {
                out += QLatin1Char('\n');
                inList = false;
            }
            for (int row = 0; row < table->rows(); ++row) {
                out += QLatin1Char('|');
                for (int column = 0; column < table->columns(); ++column) {
                    const QTextTableCell cell = table->cellAt(row, column);
                    QStringList parts;
                    if (cell.row() == row && cell.column() == column) {
                        for (auto inner = cell.begin(); !inner.atEnd(); ++inner) {
                            if (inner.currentBlock().isValid()) {
                                parts << inlineMarkdown(document, inner.currentBlock(), false, row == 0).replace(QStringLiteral("  \n"), QStringLiteral("<br>"));
                            }
                        }
                    }
                    out += QLatin1Char(' ') + parts.join(QStringLiteral("<br>")) + QStringLiteral(" |");
                }
                out += QLatin1Char('\n');
                if (row == 0) {
                    out += QLatin1Char('|');
                    for (int column = 0; column < table->columns(); ++column) {
                        out += QStringLiteral(" --- |");
                    }
                    out += QLatin1Char('\n');
                }
            }
            out += QLatin1Char('\n');
            continue;
        }
        const QTextBlock block = it.currentBlock();
        if (!block.isValid()) {
            continue;
        }
        const QString id = Doc::styleOf(block);
        if (id == QLatin1String("code") && !block.textList()) {
            if (!inCode) {
                if (inList) {
                    out += QLatin1Char('\n');
                    inList = false;
                }
                out += QStringLiteral("```\n");
                inCode = true;
            }
            out += inlineMarkdown(document, block, true, false) + QLatin1Char('\n');
            continue;
        }
        closeCode();
        if (QTextList *list = block.textList()) {
            const int level = qMax(1, list->format().indent());
            const bool numbered = list->format().style() < QTextListFormat::ListSquare;
            out += QString((level - 1) * 4, QLatin1Char(' '));
            out += numbered ? QStringLiteral("%1. ").arg(list->itemNumber(block) + 1) : QStringLiteral("- ");
            out += inlineMarkdown(document, block, false, false) + QLatin1Char('\n');
            inList = true;
            continue;
        }
        if (inList) {
            out += QLatin1Char('\n');
            inList = false;
        }
        const int level = Doc::outlineLevel(block);
        const QString text = inlineMarkdown(document, block, false, false);
        if (text.trimmed().isEmpty()) {
            continue;
        }
        if (level >= 0) {
            out += QString(level == 0 ? 1 : level + shift, QLatin1Char('#')) + QLatin1Char(' ') + text + QStringLiteral("\n\n");
        } else if (id == QLatin1String("quote")) {
            out += QStringLiteral("> ") + QString(text).replace(QStringLiteral("\n"), QStringLiteral("\n> ")) + QStringLiteral("\n\n");
        } else {
            out += text + QStringLiteral("\n\n");
        }
    }
    closeCode();
    if (inList) {
        out += QLatin1Char('\n');
    }
}

} // namespace

Kind kindOf(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("docx") || suffix == QLatin1String("docm") || suffix == QLatin1String("dotx")) {
        return Kind::Docx;
    }
    if (suffix == QLatin1String("odt") || suffix == QLatin1String("ott")) {
        return Kind::Odt;
    }
    if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown")) {
        return Kind::Markdown;
    }
    if (suffix == QLatin1String("html") || suffix == QLatin1String("htm")) {
        return Kind::Html;
    }
    if (suffix == QLatin1String("rtf")) {
        return Kind::Rtf;
    }
    if (suffix == QLatin1String("pdf")) {
        return Kind::Pdf;
    }
    if (suffix == QLatin1String("txt") || suffix == QLatin1String("text") || suffix == QLatin1String("log")) {
        return Kind::Text;
    }
    return Kind::Unknown;
}

bool keepsFormatting(Kind kind)
{
    return kind == Kind::Docx || kind == Kind::Odt || kind == Kind::Rtf || kind == Kind::Html;
}

void fromPlainText(const QString &text, QTextDocument *document)
{
    Doc::prepare(document);
    const Doc::Style &body = Doc::style(QStringLiteral("body"));
    QTextBlockFormat block = body.block;
    // Lines of a text file are lines, not paragraphs apart.
    block.setBottomMargin(0);
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    cursor.setBlockFormat(block);
    cursor.setBlockCharFormat(body.chars);
    cursor.setCharFormat(body.chars);
    QString normal = text;
    normal.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (normal.endsWith(QLatin1Char('\n'))) {
        normal.chop(1);
    }
    const QStringList lines = normal.split(QLatin1Char('\n'));
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            cursor.insertBlock(block, body.chars);
        }
        cursor.insertText(lines.at(i), body.chars);
    }
    cursor.endEditBlock();
}

namespace {
void plainBlocks(QTextFrame::iterator it, QStringList &lines)
{
    for (; !it.atEnd(); ++it) {
        if (QTextFrame *frame = it.currentFrame()) {
            auto *table = qobject_cast<QTextTable *>(frame);
            if (!table) {
                plainBlocks(frame->begin(), lines);
                continue;
            }
            // A row is a line, its cells a tab apart.
            for (int row = 0; row < table->rows(); ++row) {
                QStringList cells;
                for (int column = 0; column < table->columns(); ++column) {
                    const QTextTableCell cell = table->cellAt(row, column);
                    QStringList inside;
                    if (cell.row() == row && cell.column() == column) {
                        plainBlocks(cell.begin(), inside);
                    }
                    cells << inside.join(QLatin1Char(' '));
                }
                lines << cells.join(QLatin1Char('\t'));
            }
        } else if (it.currentBlock().isValid()) {
            lines << it.currentBlock().text();
        }
    }
}
} // namespace

QString toPlainText(const QTextDocument *document)
{
    QStringList lines;
    plainBlocks(document->rootFrame()->begin(), lines);
    QString text = lines.join(QLatin1Char('\n'));
    text.replace(QChar::LineSeparator, QLatin1Char('\n')).replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    text.remove(QChar::ObjectReplacementCharacter);
    text.replace(QChar::Nbsp, QLatin1Char(' '));
    return text;
}

void fromMarkdown(const QString &text, QTextDocument *document, const QString &folder)
{
    if (!folder.isEmpty()) {
        document->setBaseUrl(QUrl::fromLocalFile(folder + QLatin1Char('/')));
    }
    // Qt drops a picture that has no words for those who cannot see it.
    QString source = text;
    source.replace(QStringLiteral("![]("), QStringLiteral("![imagen]("));
    document->setMarkdown(source, QTextDocument::MarkdownDialectGitHub);
    Doc::prepare(document);
    adoptImages(document);
    tidyTables(document);
    restyle(document, false);
}

QString toMarkdown(const QTextDocument *document)
{
    QString out;
    QTextFrame *root = document->rootFrame();
    // A title takes the one «#» and pushes the headings a level down.
    bool titled = false;
    for (QTextBlock block = document->begin(); block.isValid() && !titled; block = block.next()) {
        titled = Doc::outlineLevel(block) == 0;
    }
    markdownBlocks(document, root->begin(), root->end(), out, titled ? 1 : 0);
    while (out.endsWith(QStringLiteral("\n\n"))) {
        out.chop(1);
    }
    return out;
}

void fromHtml(const QString &html, QTextDocument *document, const QString &folder)
{
    if (!folder.isEmpty()) {
        document->setBaseUrl(QUrl::fromLocalFile(folder + QLatin1Char('/')));
    }
    // A page Textos wrote says which style each of its paragraphs has.
    static const QRegularExpression marker(QStringLiteral("<meta name=\"textos-styles\" content=\"([a-z0-9,]*)\""));
    const QStringList written = marker.match(html).captured(1).split(QLatin1Char(','), Qt::SkipEmptyParts);
    document->setHtml(html);
    Doc::prepare(document);
    adoptImages(document);
    tidyTables(document);
    const bool mine = !written.isEmpty() && written.size() == document->blockCount();
    int index = 0;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next(), ++index) {
        const QTextBlockFormat format = block.blockFormat();
        QString id;
        if (mine) {
            id = written.at(index);
        } else if (format.headingLevel() > 0) {
            // Headings keep what the page said of them, and lead the outline.
            id = QLatin1Char('h') + QString::number(qMin(format.headingLevel(), 3));
        } else if (format.nonBreakableLines() || format.hasProperty(QTextFormat::BlockCodeFence)) {
            id = QStringLiteral("code");
        } else if (format.intProperty(QTextFormat::BlockQuoteLevel) > 0) {
            id = QStringLiteral("quote");
        }
        if (!id.isEmpty()) {
            QTextCursor at(block);
            Doc::tagStyle(at, id);
        }
    }
}

QString toHtml(const QTextDocument *document)
{
    QString html = document->toHtml();
    QStringList ids;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        ids << Doc::styleOf(block);
    }
    // Qt's page is HTML 4; what goes out is a page of today that says its encoding.
    const qsizetype head = html.indexOf(QLatin1String("<head>"));
    if (head >= 0) {
        html = QStringLiteral("<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><meta name=\"textos-styles\" content=\"%1\">").arg(ids.join(QLatin1Char(',')))
            + html.mid(head + 6);
    }
    static const QRegularExpression contentType(QStringLiteral("<meta http-equiv=\"Content-Type\"[^>]*>"));
    html.remove(contentType);
    static const QRegularExpression source(QStringLiteral("src=\"(textos-image-\\d+)\""));
    QString out;
    qsizetype last = 0;
    auto matches = source.globalMatch(html);
    while (matches.hasNext()) {
        const auto match = matches.next();
        out += QStringView(html).mid(last, match.capturedStart() - last);
        const QImage image = Doc::image(document, match.captured(1));
        out += image.isNull() ? match.captured(0) : QStringLiteral("src=\"%1\"").arg(dataUrl(image));
        last = match.capturedEnd();
    }
    out += QStringView(html).mid(last);
    return out;
}

bool load(const QString &path, QTextDocument *document, Doc::PageSetup *setup, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(error, QStringLiteral("No se ha podido abrir «%1».").arg(QFileInfo(path).fileName()));
    }
    const QByteArray bytes = file.readAll();
    file.close();
    Doc::prepare(document);
    Kind kind = kindOf(path);
    // What a file is counts for more than what it is called.
    if (bytes.startsWith("PK\x03\x04")) {
        if (kind != Kind::Docx && kind != Kind::Odt) {
            kind = bytes.mid(30, 80).contains("opendocument") ? Kind::Odt : Kind::Docx;
        }
    } else if (bytes.startsWith("{\\rtf")) {
        kind = Kind::Rtf;
    } else if (kind == Kind::Docx || kind == Kind::Odt) {
        return fail(error, QStringLiteral("«%1» no es un documento válido.").arg(QFileInfo(path).fileName()));
    }
    const QString folder = QFileInfo(path).absolutePath();
    switch (kind) {
    case Kind::Docx:
        if (!Docx::fromBytes(bytes, document, setup, error)) {
            // An .odt someone named .docx, or the other way round.
            QString ignored;
            document->clear();
            Doc::prepare(document);
            if (!Odt::fromBytes(bytes, document, setup, &ignored)) {
                return false;
            }
        }
        break;
    case Kind::Odt:
        if (!Odt::fromBytes(bytes, document, setup, error)) {
            QString ignored;
            document->clear();
            Doc::prepare(document);
            if (!Docx::fromBytes(bytes, document, setup, &ignored)) {
                return false;
            }
        }
        break;
    case Kind::Markdown:
        fromMarkdown(decode(bytes), document, folder);
        break;
    case Kind::Html:
        fromHtml(decode(bytes), document, folder);
        break;
    case Kind::Rtf:
        if (!fromRtf(bytes, document, setup)) {
            return fail(error, QStringLiteral("«%1» no es un documento RTF válido.").arg(QFileInfo(path).fileName()));
        }
        break;
    case Kind::Pdf:
        return fail(error, QStringLiteral("Textos no abre archivos PDF."));
    case Kind::Text:
    case Kind::Unknown:
        if (bytes.left(4096).contains('\0')) {
            return fail(error, QStringLiteral("«%1» no es un documento de texto.").arg(QFileInfo(path).fileName()));
        }
        fromPlainText(decode(bytes), document);
        break;
    }
    Doc::prepare(document);
    return true;
}

bool save(const QString &path, const QTextDocument *document, const Doc::PageSetup &setup, QString *error)
{
    switch (kindOf(path)) {
    case Kind::Docx:
        return Docx::write(path, document, setup, error);
    case Kind::Odt:
        return Odt::write(path, document, setup, error);
    case Kind::Markdown:
        return writeBytes(path, toMarkdown(document).toUtf8(), error);
    case Kind::Html:
        return writeBytes(path, toHtml(document).toUtf8(), error);
    case Kind::Rtf:
        return writeBytes(path, toRtf(document, setup), error);
    case Kind::Pdf:
        return writePdf(path, document, setup, error);
    case Kind::Text:
        return writeBytes(path, toPlainText(document).toUtf8(), error);
    case Kind::Unknown:
        break;
    }
    return fail(error, QStringLiteral("Textos no sabe escribir archivos «.%1».").arg(QFileInfo(path).suffix()));
}

// --- Pages ------------------------------------------------------------------------

int pageCount(const QTextDocument *document)
{
    return qMax(1, document->pageCount());
}

void paintPage(QPainter *painter, const QTextDocument *document, const Doc::PageSetup &setup, int page, int pages,
               const QAbstractTextDocumentLayout::PaintContext *context)
{
    Q_UNUSED(pages)
    const QSizeF sheet = setup.pagePx();
    const QSizeF content = setup.contentPx();
    const double left = setup.left * Doc::pxPerCm;
    const double top = setup.top * Doc::pxPerCm;

    painter->save();
    painter->translate(left, top - page * content.height());
    const QRectF slice(0, page * content.height(), content.width(), content.height());
    painter->setClipRect(slice);
    QAbstractTextDocumentLayout::PaintContext paint;
    if (context) {
        paint = *context;
    }
    paint.clip = slice;
    paint.palette.setColor(QPalette::Text, Doc::ink());
    document->documentLayout()->draw(painter, paint);
    painter->restore();

    const QString header = setup.header.trimmed();
    const QString footer = setup.footer.trimmed();
    if (header.isEmpty() && footer.isEmpty() && !setup.pageNumbers) {
        return;
    }
    painter->save();
    QFont font(Doc::defaultFamily());
    font.setPixelSize(qRound(9 * Doc::pxPerPt));
    painter->setFont(font);
    painter->setPen(QColor(0x6e, 0x6e, 0x73));
    const double bottom = sheet.height() - top - content.height();
    const QRectF above(left, 0, content.width(), top);
    const QRectF below(left, sheet.height() - bottom, content.width(), bottom);
    if (!header.isEmpty()) {
        painter->drawText(above, Qt::AlignCenter | Qt::TextSingleLine, header);
    }
    if (!footer.isEmpty()) {
        painter->drawText(below, Qt::AlignCenter | Qt::TextSingleLine, footer);
    }
    if (setup.pageNumbers) {
        painter->drawText(below, (footer.isEmpty() ? Qt::AlignHCenter : Qt::AlignRight) | Qt::AlignVCenter | Qt::TextSingleLine, QString::number(page + 1));
    }
    painter->restore();
}

bool writePdf(const QString &path, const QTextDocument *document, const Doc::PageSetup &setup, QString *error)
{
    // A copy laid out for the page, whatever the caller's is laid out for.
    const std::unique_ptr<QTextDocument> copy(document->clone());
    Doc::prepare(copy.get());
    copy->setPageSize(setup.contentPx());

    const QString partial = path + QStringLiteral(".parcial");
    {
        QPdfWriter writer(partial);
        const QSizeF sheet = setup.sizeCm() * 10;
        const QSizeF upright = setup.landscape ? sheet.transposed() : sheet;
        writer.setPageLayout(QPageLayout(QPageSize(upright, QPageSize::Millimeter), setup.landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                                         QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter));
        writer.setResolution(96);
        writer.setCreator(QStringLiteral("Wonderland Textos"));
        QPainter painter;
        if (!painter.begin(&writer)) {
            QFile::remove(partial);
            return fail(error, QStringLiteral("No se ha podido escribir el PDF."));
        }
        const int pages = pageCount(copy.get());
        for (int page = 0; page < pages; ++page) {
            if (page > 0) {
                writer.newPage();
            }
            paintPage(&painter, copy.get(), setup, page, pages);
        }
        painter.end();
    }
    QFile::remove(path);
    if (!QFile::rename(partial, path)) {
        QFile::remove(partial);
        return fail(error, QStringLiteral("No se ha podido escribir el PDF."));
    }
    return true;
}

} // namespace Formats
