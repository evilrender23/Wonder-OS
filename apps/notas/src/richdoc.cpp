#include "richdoc.h"

#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>

Style Style::of(bool dark)
{
    Style style;
    style.dark = dark;
    style.link = QColor(dark ? QStringLiteral("#bf5af2") : QStringLiteral("#8e44d6"));
    style.codeBack = QColor(dark ? QStringLiteral("#2e2e31") : QStringLiteral("#f1f1f4"));
    style.quoteBack = QColor(dark ? QStringLiteral("#2d2638") : QStringLiteral("#f5effc"));
    style.border = QColor(dark ? QStringLiteral("#505055") : QStringLiteral("#c8c8cc"));
    return style;
}

namespace {

const QChar softBreak(QChar::LineSeparator);
const QChar objectChar(QChar::ObjectReplacementCharacter);

bool isHeading(Block::Type type)
{
    return type == Block::Heading1 || type == Block::Heading2 || type == Block::Heading3;
}

bool isListType(Block::Type type)
{
    return type == Block::Bullet || type == Block::Number || type == Block::Task;
}

bool fixedPitch(const QTextCharFormat &format)
{
    if (format.fontFixedPitch()) {
        return true;
    }
    const QStringList families = format.fontFamilies().toStringList();
    for (const QString &family : families) {
        if (family.contains(QLatin1String("mono"), Qt::CaseInsensitive) || family.contains(QLatin1String("courier"), Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool numbered(const QTextList *list)
{
    const int style = list->format().style();
    return style != QTextListFormat::ListStyleUndefined && style <= QTextListFormat::ListDecimal;
}

int listIndent(const QTextBlock &block)
{
    const QTextList *list = block.textList();
    return list ? qBound(0, list->format().indent() - 1, 8) : 0;
}

QTextListFormat listFormat(Block::Type type, int indent)
{
    static const QTextListFormat::Style bullets[] = {QTextListFormat::ListDisc, QTextListFormat::ListCircle, QTextListFormat::ListSquare};
    QTextListFormat format;
    format.setStyle(type == Block::Number ? QTextListFormat::ListDecimal : bullets[indent % 3]);
    format.setIndent(indent + 1);
    return format;
}

QTextTableFormat tableFormat(const Style &style, int columns)
{
    QTextTableFormat format;
    format.setCellPadding(5);
    format.setCellSpacing(0);
    format.setBorder(1);
    format.setBorderCollapse(true);
    format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    format.setBorderBrush(style.border);
    format.setTopMargin(6);
    format.setBottomMargin(6);
    format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    QList<QTextLength> widths;
    for (int i = 0; i < columns; ++i) {
        widths << QTextLength(QTextLength::PercentageLength, 100.0 / qMax(1, columns));
    }
    format.setColumnWidthConstraints(widths);
    return format;
}

QString toDocumentText(QString text)
{
    return text.replace(QLatin1Char('\n'), softBreak);
}

void insertRuns(QTextCursor &cursor, const Runs &runs, Block::Type type, const Style &style)
{
    for (const Run &run : runs) {
        if (run.isImage()) {
            cursor.insertImage(RichDoc::charFormat(run, type, style).toImageFormat());
        } else if (!run.text.isEmpty()) {
            cursor.insertText(toDocumentText(run.text), RichDoc::charFormat(run, type, style));
        }
    }
}

Runs readRuns(const QTextBlock &block, Block::Type type)
{
    Runs runs;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (!fragment.isValid()) {
            continue;
        }
        Run run = RichDoc::runOf(fragment.charFormat(), type);
        if (run.isImage()) {
            // One fragment may hold the same picture several times over.
            for (int i = 0; i < fragment.length(); ++i) {
                runs << run;
            }
            continue;
        }
        run.text = fragment.text();
        run.text.replace(softBreak, QLatin1Char('\n')).remove(objectChar);
        if (run.text.isEmpty()) {
            continue;
        }
        if (!runs.isEmpty() && runs.last().sameLook(run)) {
            runs.last().text += run.text;
        } else {
            runs << run;
        }
    }
    return runs;
}

void readFrame(const QTextFrame *frame, Doc &doc, int headingShift)
{
    for (auto it = frame->begin(); !it.atEnd(); ++it) {
        if (QTextFrame *child = it.currentFrame()) {
            const QTextTable *table = qobject_cast<QTextTable *>(child);
            if (!table) {
                // A box from some web page: what is in it, without the box.
                readFrame(child, doc, headingShift);
                continue;
            }
            Block block;
            block.type = Block::Table;
            for (int row = 0; row < table->rows(); ++row) {
                QList<Runs> cells;
                for (int column = 0; column < table->columns(); ++column) {
                    const QTextTableCell cell = table->cellAt(row, column);
                    Runs runs;
                    if (cell.row() == row && cell.column() == column) {
                        bool first = true;
                        for (auto inside = cell.begin(); !inside.atEnd(); ++inside) {
                            const QTextBlock paragraph = inside.currentBlock();
                            if (!paragraph.isValid()) {
                                continue;
                            }
                            const Runs more = readRuns(paragraph, Block::Paragraph);
                            if (!first) {
                                if (!runs.isEmpty() && !runs.last().isImage()) {
                                    runs.last().text += QLatin1Char('\n');
                                } else {
                                    runs << Run{.text = QStringLiteral("\n")};
                                }
                            }
                            first = false;
                            for (const Run &run : more) {
                                if (!runs.isEmpty() && runs.last().sameLook(run)) {
                                    runs.last().text += run.text;
                                } else {
                                    runs << run;
                                }
                            }
                        }
                        // Empty lines at the ends of a cell are nothing: some readers leave them there.
                        while (!runs.isEmpty() && !runs.first().isImage()) {
                            Run &head = runs.first();
                            while (head.text.startsWith(QLatin1Char('\n'))) {
                                head.text.remove(0, 1);
                            }
                            if (!head.text.isEmpty()) {
                                break;
                            }
                            runs.removeFirst();
                        }
                        while (!runs.isEmpty() && !runs.last().isImage()) {
                            Run &tail = runs.last();
                            while (tail.text.endsWith(QLatin1Char('\n'))) {
                                tail.text.chop(1);
                            }
                            if (!tail.text.isEmpty()) {
                                break;
                            }
                            runs.removeLast();
                        }
                    }
                    cells << runs;
                }
                block.rows << cells;
            }
            if (!block.rows.isEmpty()) {
                doc.blocks << block;
            }
            continue;
        }
        const QTextBlock paragraph = it.currentBlock();
        if (!paragraph.isValid()) {
            continue;
        }
        Block block;
        block.type = RichDoc::typeOf(paragraph, headingShift);
        block.done = paragraph.blockFormat().marker() == QTextBlockFormat::MarkerType::Checked;
        block.indent = block.isList() ? listIndent(paragraph) : 0;
        block.runs = readRuns(paragraph, block.type);
        doc.blocks << block;
    }
}

}

// --- Formats ----------------------------------------------------------------------

Block::Type RichDoc::typeOf(const QTextBlock &block, int headingShift)
{
    const QTextBlockFormat format = block.blockFormat();
    if (format.headingLevel() > 0) {
        const int level = qMax(1, format.headingLevel() - headingShift);
        return level == 1 ? Block::Heading1 : level == 2 ? Block::Heading2 : Block::Heading3;
    }
    if (const QTextList *list = block.textList()) {
        if (format.marker() != QTextBlockFormat::MarkerType::NoMarker) {
            return Block::Task;
        }
        return numbered(list) ? Block::Number : Block::Bullet;
    }
    if (format.hasProperty(QTextFormat::BlockCodeFence) || format.hasProperty(QTextFormat::BlockCodeLanguage)) {
        return Block::Code;
    }
    if (format.intProperty(QTextFormat::BlockQuoteLevel) > 0) {
        return Block::Quote;
    }
    // What other programs' HTML looks like once Qt has read it.
    if (format.nonBreakableLines() && block.length() > 1 && fixedPitch(block.charFormat())) {
        return Block::Code;
    }
    return Block::Paragraph;
}

Run RichDoc::runOf(const QTextCharFormat &format, Block::Type type)
{
    Run run;
    if (format.isImageFormat()) {
        const QTextImageFormat image = format.toImageFormat();
        run.image = image.name();
        run.width = qRound(image.width());
        run.height = qRound(image.height());
        if (!run.image.isEmpty()) {
            return run;
        }
    }
    const bool anchor = format.isAnchor() && !format.anchorHref().isEmpty();
    run.bold = !isHeading(type) && format.fontWeight() >= QFont::DemiBold;
    run.italic = format.fontItalic();
    run.strike = format.fontStrikeOut();
    run.underline = !anchor && format.fontUnderline();
    run.mono = type != Block::Code && fixedPitch(format);
    if (anchor) {
        run.href = format.anchorHref();
    } else if (format.hasProperty(QTextFormat::ForegroundBrush) && format.foreground().style() != Qt::NoBrush) {
        run.color = format.foreground().color().name(QColor::HexRgb);
    }
    if (!run.mono && type != Block::Code && format.hasProperty(QTextFormat::BackgroundBrush) && format.background().style() != Qt::NoBrush) {
        run.highlight = format.background().color().name(QColor::HexRgb);
    }
    return run;
}

QTextCharFormat RichDoc::charFormat(const Run &run, Block::Type type, const Style &style)
{
    if (run.isImage()) {
        QTextImageFormat image;
        image.setName(run.image);
        if (run.width > 0 && run.height > 0) {
            image.setWidth(run.width);
            image.setHeight(run.height);
        }
        return image;
    }
    QTextCharFormat format;
    if (isHeading(type)) {
        static const int sizes[] = {25, 20, 17};
        format.setProperty(QTextFormat::FontPixelSize, sizes[type - Block::Heading1]);
        format.setFontWeight(type == Block::Heading3 ? QFont::DemiBold : QFont::Bold);
    } else if (run.bold) {
        format.setFontWeight(QFont::Bold);
    }
    if (run.italic) format.setFontItalic(true);
    if (run.strike) format.setFontStrikeOut(true);
    if (run.underline) format.setFontUnderline(true);
    if (type == Block::Code || run.mono) {
        format.setFontFamilies({QStringLiteral("monospace")});
        format.setFontFixedPitch(true);
        if (type != Block::Code) {
            format.setBackground(style.codeBack);
        }
    }
    if (!run.highlight.isEmpty() && !run.mono && type != Block::Code) {
        format.setBackground(QColor(run.highlight));
    }
    if (!run.href.isEmpty()) {
        format.setAnchor(true);
        format.setAnchorHref(run.href);
        format.setForeground(style.link);
        format.setFontUnderline(true);
    } else if (!run.color.isEmpty()) {
        format.setForeground(QColor(run.color));
    }
    return format;
}

QTextBlockFormat RichDoc::blockFormat(Block::Type type, bool done, const Style &style)
{
    QTextBlockFormat format;
    switch (type) {
    case Block::Heading1:
    case Block::Heading2:
    case Block::Heading3:
        format.setHeadingLevel(type - Block::Heading1 + 1);
        format.setTopMargin(type == Block::Heading1 ? 14 : 10);
        format.setBottomMargin(4);
        break;
    case Block::Task:
        format.setMarker(done ? QTextBlockFormat::MarkerType::Checked : QTextBlockFormat::MarkerType::Unchecked);
        [[fallthrough]];
    case Block::Bullet:
    case Block::Number:
        format.setTopMargin(2);
        format.setBottomMargin(2);
        break;
    case Block::Quote:
        format.setProperty(QTextFormat::BlockQuoteLevel, 1);
        format.setLeftMargin(14);
        format.setBackground(style.quoteBack);
        break;
    case Block::Code:
        format.setProperty(QTextFormat::BlockCodeFence, QStringLiteral("`"));
        format.setBackground(style.codeBack);
        break;
    default:
        format.setTopMargin(3);
        format.setBottomMargin(3);
    }
    return format;
}

// --- Whole pages --------------------------------------------------------------------

void RichDoc::write(const Doc &doc, QTextDocument *document, const Style &style)
{
    const bool undo = document->isUndoRedoEnabled();
    document->setUndoRedoEnabled(false);
    document->clear();
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    // The block the cursor is in is empty and still nobody's.
    bool fresh = true;
    QHash<int, QTextList *> lists;
    Block::Type listed = Block::Paragraph;
    for (const Block &block : doc.blocks) {
        if (block.type == Block::Table) {
            lists.clear();
            if (block.rows.isEmpty() || block.rows.first().isEmpty()) {
                continue;
            }
            const QTextBlockFormat plain = blockFormat(Block::Paragraph, false, style);
            const QTextCharFormat base = charFormat(Run(), Block::Paragraph, style);
            if (fresh) {
                cursor.setBlockFormat(plain);
                cursor.setBlockCharFormat(base);
            }
            const int columns = block.rows.first().size();
            QTextTable *table = cursor.insertTable(block.rows.size(), columns, tableFormat(style, columns));
            for (int row = 0; row < block.rows.size(); ++row) {
                for (int column = 0; column < columns && column < block.rows.at(row).size(); ++column) {
                    QTextCursor cell = table->cellAt(row, column).firstCursorPosition();
                    cell.setBlockFormat(QTextBlockFormat());
                    cell.setBlockCharFormat(base);
                    cell.setCharFormat(base);
                    insertRuns(cell, block.rows.at(row).at(column), Block::Paragraph, style);
                }
            }
            cursor.setPosition(table->lastPosition() + 1);
            cursor.setBlockFormat(plain);
            cursor.setBlockCharFormat(base);
            cursor.setCharFormat(base);
            fresh = true;
            continue;
        }
        const QTextBlockFormat format = blockFormat(block.type, block.done, style);
        const QTextCharFormat base = charFormat(Run(), block.type, style);
        if (fresh) {
            cursor.setBlockFormat(format);
            cursor.setBlockCharFormat(base);
            cursor.setCharFormat(base);
        } else {
            cursor.insertBlock(format, base);
        }
        fresh = false;
        if (block.isList()) {
            const bool number = block.type == Block::Number;
            for (auto it = lists.begin(); it != lists.end();) {
                it = it.key() > block.indent ? lists.erase(it) : std::next(it);
            }
            QTextList *list = lists.value(block.indent);
            if (list && numbered(list) == number && (block.indent > 0 || (listed == Block::Number) == number)) {
                list->add(cursor.block());
            } else {
                lists.insert(block.indent, cursor.createList(listFormat(block.type, block.indent)));
            }
            if (block.indent == 0) {
                listed = block.type;
            }
        } else {
            lists.clear();
        }
        insertRuns(cursor, block.runs, block.type, style);
    }
    cursor.endEditBlock();
    document->setUndoRedoEnabled(undo);
    document->setModified(false);
}

Doc RichDoc::read(const QTextDocument *document, int headingShift)
{
    Doc doc;
    readFrame(document->rootFrame(), doc, headingShift);
    return doc;
}

// --- Editing ------------------------------------------------------------------------

RichText::RichText(QTextDocument *document, const Style &style)
    : m_document(document)
    , m_style(style)
{
}

QList<RichText::Piece> RichText::pieces(int start, int end) const
{
    QList<Piece> found;
    for (QTextBlock block = m_document->findBlock(start); block.isValid() && block.position() < end; block = block.next()) {
        const Block::Type type = RichDoc::typeOf(block);
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const int from = qMax(start, fragment.position());
            const int to = qMin(end, fragment.position() + fragment.length());
            if (fragment.isValid() && to > from) {
                found << Piece{from, to - from, fragment.charFormat(), type};
            }
        }
    }
    return found;
}

void RichText::restyle(int start, int end, const std::function<void(Run &)> &change)
{
    const QList<Piece> all = pieces(start, end);
    QTextCursor cursor(m_document);
    cursor.beginEditBlock();
    for (const Piece &piece : all) {
        if (piece.format.isImageFormat()) {
            continue;
        }
        Run run = RichDoc::runOf(piece.format, piece.type);
        change(run);
        cursor.setPosition(piece.start);
        cursor.setPosition(piece.start + piece.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(RichDoc::charFormat(run, piece.type, m_style));
    }
    cursor.endEditBlock();
}

void RichText::wordAt(int &start, int &end) const
{
    if (start != end) {
        return;
    }
    QTextCursor cursor(m_document);
    cursor.setPosition(start);
    cursor.select(QTextCursor::WordUnderCursor);
    if (cursor.hasSelection()) {
        start = cursor.selectionStart();
        end = cursor.selectionEnd();
    }
}

void RichText::toggle(int start, int end, const QString &what)
{
    wordAt(start, end);
    bool Run::*flag = what == QLatin1String("bold") ? &Run::bold
        : what == QLatin1String("italic") ? &Run::italic
        : what == QLatin1String("underline") ? &Run::underline
        : what == QLatin1String("strike") ? &Run::strike
        : what == QLatin1String("mono") ? &Run::mono : nullptr;
    if (!flag || start == end) {
        return;
    }
    bool every = true;
    bool any = false;
    for (const Piece &piece : pieces(start, end)) {
        if (!piece.format.isImageFormat()) {
            any = true;
            every = every && RichDoc::runOf(piece.format, piece.type).*flag;
        }
    }
    if (any) {
        restyle(start, end, [flag, every](Run &run) { run.*flag = !every; });
    }
}

void RichText::setColor(int start, int end, const QString &color)
{
    wordAt(start, end);
    restyle(start, end, [&color](Run &run) { run.color = color; });
}

void RichText::setHighlight(int start, int end, const QString &color)
{
    wordAt(start, end);
    restyle(start, end, [&color](Run &run) { run.highlight = color; });
}

void RichText::clearLook(int start, int end)
{
    wordAt(start, end);
    restyle(start, end, [](Run &run) {
        const QString text = run.text;
        const QString href = run.href;
        run = Run();
        run.text = text;
        run.href = href;
    });
}

Block::Type RichText::typeAt(int position) const
{
    return RichDoc::typeOf(m_document->findBlock(position));
}

void RichText::joinList(const QTextBlock &block, Block::Type type, int indent)
{
    const bool number = type == Block::Number;
    auto fits = [&](const QTextBlock &other) {
        const QTextList *list = other.isValid() ? other.textList() : nullptr;
        return list && numbered(list) == number && listIndent(other) == indent;
    };
    // An item joins the list above it, across the deeper items in between.
    QTextBlock above = block.previous();
    while (above.isValid() && above.textList() && listIndent(above) > indent) {
        above = above.previous();
    }
    if (fits(above)) {
        above.textList()->add(block);
    } else if (fits(block.next())) {
        block.next().textList()->add(block);
    } else {
        QTextCursor(block).createList(listFormat(type, indent));
    }
}

void RichText::retype(const QTextBlock &block, Block::Type type, bool done)
{
    const int start = block.position();
    const QList<Piece> old = pieces(start, start + block.length() - 1);
    const Block::Type before = RichDoc::typeOf(block);
    const int indent = isListType(before) ? listIndent(block) : 0;
    if (QTextList *list = block.textList()) {
        list->remove(block);
    }
    QTextCursor cursor(m_document);
    cursor.setPosition(start);
    cursor.setBlockFormat(RichDoc::blockFormat(type, done, m_style));
    for (const Piece &piece : old) {
        if (piece.format.isImageFormat()) {
            continue;
        }
        cursor.setPosition(piece.start);
        cursor.setPosition(piece.start + piece.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(RichDoc::charFormat(RichDoc::runOf(piece.format, before), type, m_style));
    }
    cursor.setPosition(start);
    cursor.setBlockCharFormat(RichDoc::charFormat(Run(), type, m_style));
    if (isListType(type)) {
        joinList(m_document->findBlock(start), type, indent);
    }
}

void RichText::setType(int start, int end, Block::Type type, bool toggle)
{
    if (type == Block::Table) {
        return;
    }
    QList<int> positions;
    bool every = true;
    for (QTextBlock block = m_document->findBlock(start); block.isValid() && block.position() <= end; block = block.next()) {
        // A selection that ends at the start of a line does not take that line.
        if (block.position() == end && end > start && !positions.isEmpty()) {
            break;
        }
        positions << block.position();
        every = every && RichDoc::typeOf(block) == type;
    }
    const Block::Type target = toggle && every ? Block::Paragraph : type;
    QTextCursor cursor(m_document);
    cursor.beginEditBlock();
    for (const int position : std::as_const(positions)) {
        const QTextBlock block = m_document->findBlock(position);
        if (RichDoc::typeOf(block) != target) {
            retype(block, target, false);
        }
    }
    cursor.endEditBlock();
}

bool RichText::toggleDone(int position)
{
    const QTextBlock block = m_document->findBlock(position);
    if (!block.isValid()) {
        return false;
    }
    QTextBlockFormat format = block.blockFormat();
    if (format.marker() == QTextBlockFormat::MarkerType::NoMarker) {
        return false;
    }
    format.setMarker(format.marker() == QTextBlockFormat::MarkerType::Checked ? QTextBlockFormat::MarkerType::Unchecked : QTextBlockFormat::MarkerType::Checked);
    QTextCursor cursor(block);
    cursor.setBlockFormat(format);
    return true;
}

void RichText::indent(int start, int end, int delta)
{
    QList<int> positions;
    for (QTextBlock block = m_document->findBlock(start); block.isValid() && block.position() <= end; block = block.next()) {
        if (block.textList()) {
            positions << block.position();
        }
    }
    QTextCursor cursor(m_document);
    cursor.beginEditBlock();
    for (const int position : std::as_const(positions)) {
        QTextBlock block = m_document->findBlock(position);
        const Block::Type type = RichDoc::typeOf(block);
        const bool done = block.blockFormat().marker() == QTextBlockFormat::MarkerType::Checked;
        const int deeper = qBound(0, listIndent(block) + delta, 4);
        if (deeper == listIndent(block)) {
            continue;
        }
        block.textList()->remove(block);
        cursor.setPosition(position);
        cursor.setBlockFormat(RichDoc::blockFormat(type, done, m_style));
        joinList(m_document->findBlock(position), type, deeper);
    }
    cursor.endEditBlock();
}

// --- Tables -------------------------------------------------------------------------

int RichText::insertTable(int position, int rows, int columns)
{
    QTextCursor cursor(m_document);
    cursor.setPosition(position);
    if (cursor.currentTable() || rows < 1 || columns < 1) {
        return -1;
    }
    rows = qMin(rows, 50);
    columns = qMin(columns, 12);
    cursor.beginEditBlock();
    const QTextBlock block = cursor.block();
    if (RichDoc::typeOf(block) != Block::Paragraph) {
        // A table does not live inside a list or a heading: it goes after it.
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertBlock(RichDoc::blockFormat(Block::Paragraph, false, m_style), RichDoc::charFormat(Run(), Block::Paragraph, m_style));
    }
    QTextTable *table = cursor.insertTable(rows, columns, tableFormat(m_style, columns));
    const QTextCharFormat base = RichDoc::charFormat(Run(), Block::Paragraph, m_style);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            QTextCursor cell = table->cellAt(row, column).firstCursorPosition();
            cell.setBlockFormat(QTextBlockFormat());
            cell.setBlockCharFormat(base);
        }
    }
    cursor.endEditBlock();
    return table->cellAt(0, 0).firstPosition();
}

bool RichText::editTable(int position, const QString &what)
{
    QTextCursor cursor(m_document);
    cursor.setPosition(position);
    QTextTable *table = cursor.currentTable();
    if (!table) {
        return false;
    }
    const QTextTableCell cell = table->cellAt(cursor);
    const int row = cell.row();
    const int column = cell.column();
    cursor.beginEditBlock();
    bool removed = false;
    if (what == QLatin1String("row-above")) {
        table->insertRows(row, 1);
    } else if (what == QLatin1String("row-below")) {
        table->insertRows(row + 1, 1);
    } else if (what == QLatin1String("column-left")) {
        table->insertColumns(column, 1);
    } else if (what == QLatin1String("column-right")) {
        table->insertColumns(column + 1, 1);
    } else if (what == QLatin1String("delete-row")) {
        removed = table->rows() == 1;
        table->removeRows(row, 1);
    } else if (what == QLatin1String("delete-column")) {
        removed = table->columns() == 1;
        table->removeColumns(column, 1);
    } else if (what == QLatin1String("delete-table")) {
        removed = true;
        table->removeRows(0, table->rows());
    } else {
        cursor.endEditBlock();
        return false;
    }
    if (!removed && what.contains(QLatin1String("column"))) {
        // The columns share the width again.
        QTextTableFormat format = table->format();
        QList<QTextLength> widths;
        for (int i = 0; i < table->columns(); ++i) {
            widths << QTextLength(QTextLength::PercentageLength, 100.0 / table->columns());
        }
        format.setColumnWidthConstraints(widths);
        table->setFormat(format);
    }
    cursor.endEditBlock();
    return true;
}

int RichText::nextCell(int position, bool backwards)
{
    QTextCursor cursor(m_document);
    cursor.setPosition(position);
    QTextTable *table = cursor.currentTable();
    if (!table) {
        return -1;
    }
    const QTextTableCell cell = table->cellAt(cursor);
    int index = cell.row() * table->columns() + cell.column() + (backwards ? -1 : 1);
    if (index < 0) {
        return position;
    }
    if (index >= table->rows() * table->columns()) {
        table->appendRows(1);
    }
    return table->cellAt(index / table->columns(), index % table->columns()).firstPosition();
}

// --- Links and pictures ---------------------------------------------------------------

QVariantMap RichText::linkAt(int position) const
{
    const QTextBlock block = m_document->findBlock(position);
    if (!block.isValid()) {
        return {};
    }
    // The fragments of one link, which bold or a colour may have cut in several.
    QString href;
    int start = -1;
    int end = -1;
    QString text;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        const QTextCharFormat format = fragment.charFormat();
        const QString here = format.isAnchor() ? format.anchorHref() : QString();
        if (!here.isEmpty() && here == href && fragment.position() == end) {
            end += fragment.length();
            text += fragment.text();
        } else {
            if (start >= 0 && position >= start && position <= end) {
                break;
            }
            href = here;
            start = here.isEmpty() ? -1 : fragment.position();
            end = here.isEmpty() ? -1 : fragment.position() + fragment.length();
            text = here.isEmpty() ? QString() : fragment.text();
        }
    }
    if (start < 0 || position < start || position > end) {
        return {};
    }
    return {{QStringLiteral("href"), href}, {QStringLiteral("text"), text}, {QStringLiteral("start"), start}, {QStringLiteral("end"), end}};
}

int RichText::setLink(int start, int end, const QString &text, const QString &href)
{
    QString target = href.trimmed();
    static const QRegularExpression scheme(QStringLiteral("^[a-zA-Z][a-zA-Z0-9+.-]*:"));
    if (!target.isEmpty() && !scheme.match(target).hasMatch() && !target.startsWith(QLatin1String("files/"))) {
        target.prepend(target.contains(QLatin1Char('@')) && !target.contains(QLatin1Char('/')) ? QStringLiteral("mailto:") : QStringLiteral("https://"));
    }
    QTextCursor cursor(m_document);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    const QString shown = text.isEmpty() && start == end ? target : text;
    if (shown.isEmpty() || (start != end && cursor.selectedText() == shown)) {
        restyle(start, end, [&target](Run &run) { run.href = target; });
        return end;
    }
    const Block::Type type = typeAt(start);
    QTextCursor probe(m_document);
    probe.setPosition(start == end ? start : start + 1);
    Run run = RichDoc::runOf(probe.charFormat(), type);
    if (run.isImage()) {
        run = Run();
    }
    run.href = target;
    cursor.beginEditBlock();
    cursor.insertText(shown, RichDoc::charFormat(run, type, m_style));
    cursor.endEditBlock();
    return cursor.position();
}

void RichText::removeLink(int position)
{
    const QVariantMap link = linkAt(position);
    if (!link.isEmpty()) {
        restyle(link.value(QStringLiteral("start")).toInt(), link.value(QStringLiteral("end")).toInt(), [](Run &run) { run.href.clear(); });
    }
}

int RichText::insertImage(int position, const QString &source, int width, int height)
{
    QTextCursor cursor(m_document);
    cursor.setPosition(position);
    Run run;
    run.image = source;
    run.width = width;
    run.height = height;
    cursor.insertImage(RichDoc::charFormat(run, Block::Paragraph, m_style).toImageFormat());
    return cursor.position();
}

QVariantMap RichText::imageAt(int position) const
{
    QTextCursor cursor(m_document);
    // The character after the cursor first, then the one before.
    for (const int at : {position, position - 1}) {
        if (at < 0 || at + 1 >= m_document->characterCount()) {
            continue;
        }
        cursor.setPosition(at + 1);
        const QTextCharFormat format = cursor.charFormat();
        if (format.isImageFormat() && m_document->characterAt(at) == objectChar) {
            const QTextImageFormat image = format.toImageFormat();
            return {{QStringLiteral("source"), image.name()}, {QStringLiteral("width"), qRound(image.width())},
                    {QStringLiteral("height"), qRound(image.height())}, {QStringLiteral("position"), at}};
        }
    }
    return {};
}

void RichText::replaceImage(int position, const QString &source, int width, int height)
{
    QTextCursor cursor(m_document);
    cursor.setPosition(position);
    cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
    Run run;
    run.image = source;
    run.width = width;
    run.height = height;
    cursor.beginEditBlock();
    cursor.removeSelectedText();
    cursor.insertImage(RichDoc::charFormat(run, Block::Paragraph, m_style).toImageFormat());
    cursor.endEditBlock();
}

int RichText::insertText(int start, int end, const QString &text)
{
    QTextCursor cursor(m_document);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    const Block::Type type = typeAt(start);
    QTextCursor probe(m_document);
    probe.setPosition(start);
    Run run = RichDoc::runOf(probe.charFormat(), type);
    if (run.isImage()) {
        run = Run();
    }
    run.href.clear();
    cursor.insertText(toDocumentText(text), RichDoc::charFormat(run, type, m_style));
    return cursor.position();
}

// --- Typing ---------------------------------------------------------------------------

int RichText::autoformat(int position)
{
    const QTextBlock block = m_document->findBlock(position);
    if (!block.isValid() || position <= block.position()) {
        return -1;
    }
    const QString before = block.text().left(position - block.position());
    const Block::Type type = RichDoc::typeOf(block);
    const QChar typed = before.back();
    QTextCursor cursor(m_document);

    // Typing past the end of a link is not part of the link.
    cursor.setPosition(position);
    const QTextCharFormat format = cursor.charFormat();
    if (format.isAnchor() && typed != objectChar) {
        bool inside = false;
        if (position < block.position() + block.length() - 1) {
            QTextCursor next(m_document);
            next.setPosition(position + 1);
            inside = next.charFormat().isAnchor() && next.charFormat().anchorHref() == format.anchorHref();
        }
        if (!inside) {
            Run run = RichDoc::runOf(format, type);
            run.href.clear();
            cursor.setPosition(position - 1);
            cursor.setPosition(position, QTextCursor::KeepAnchor);
            cursor.setCharFormat(RichDoc::charFormat(run, type, m_style));
        }
        return -1;
    }

    Block::Type target = Block::Table;
    bool done = false;
    if (typed == QLatin1Char(' ')) {
        const bool task = before == QLatin1String("[] ") || before == QLatin1String("[ ] ");
        const bool ticked = before == QLatin1String("[x] ") || before == QLatin1String("[X] ");
        if (type == Block::Paragraph) {
            if (before == QLatin1String("# ")) target = Block::Heading1;
            else if (before == QLatin1String("## ")) target = Block::Heading2;
            else if (before == QLatin1String("### ")) target = Block::Heading3;
            else if (before == QLatin1String("- ") || before == QLatin1String("* ") || before == QLatin1String("+ ")) target = Block::Bullet;
            else if (before == QLatin1String("1. ") || before == QLatin1String("1) ")) target = Block::Number;
            else if (before == QLatin1String("> ")) target = Block::Quote;
        }
        if ((type == Block::Paragraph || type == Block::Bullet) && (task || ticked)) {
            target = Block::Task;
            done = ticked;
        }
    } else if (typed == QLatin1Char('`') && type == Block::Paragraph && before == QLatin1String("```")) {
        target = Block::Code;
    }
    if (target != Block::Table) {
        cursor.beginEditBlock();
        cursor.setPosition(block.position());
        cursor.setPosition(position, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        retype(m_document->findBlock(block.position()), target, done);
        cursor.endEditBlock();
        return block.position();
    }

    if (typed == QLatin1Char(']') && type != Block::Code && before.endsWith(QLatin1String("]]"))) {
        const int open = before.lastIndexOf(QLatin1String("[["));
        const QString title = open < 0 ? QString() : before.mid(open + 2, before.size() - open - 4).trimmed();
        if (!title.isEmpty() && !title.contains(QLatin1Char('[')) && !title.contains(QLatin1Char(']'))) {
            cursor.setPosition(block.position() + open + 1);
            Run run = RichDoc::runOf(cursor.charFormat(), type);
            if (run.isImage()) {
                run = Run();
            }
            run.href = Doc::wikiHref(title);
            cursor.beginEditBlock();
            cursor.setPosition(block.position() + open);
            cursor.setPosition(position, QTextCursor::KeepAnchor);
            cursor.insertText(title, RichDoc::charFormat(run, type, m_style));
            cursor.endEditBlock();
            return cursor.position();
        }
    }
    return -1;
}

int RichText::newline(int position)
{
    const QTextBlock block = m_document->findBlock(position);
    if (!block.isValid()) {
        return -1;
    }
    const Block::Type type = RichDoc::typeOf(block);
    const bool empty = block.length() <= 1;
    QTextCursor cursor(m_document);
    cursor.setPosition(position);
    if (empty && (isListType(type) || type == Block::Quote || type == Block::Code)) {
        // Return on an empty item ends the list (or comes up one level).
        if (isListType(type) && listIndent(block) > 0) {
            indent(position, position, -1);
        } else {
            cursor.beginEditBlock();
            retype(block, Block::Paragraph, false);
            cursor.endEditBlock();
        }
        return block.position();
    }
    if (type == Block::Task) {
        cursor.beginEditBlock();
        cursor.insertBlock();
        QTextBlockFormat format = cursor.blockFormat();
        // The new task is not done, whatever the one above was; and what went down with the cursor keeps its tick.
        const bool split = position > block.position() && position < block.position() + block.length() - 1;
        if (!split) {
            QTextCursor fresh(m_document);
            fresh.setPosition(position == block.position() ? block.position() : cursor.position());
            QTextBlockFormat open = fresh.blockFormat();
            open.setMarker(QTextBlockFormat::MarkerType::Unchecked);
            fresh.setBlockFormat(open);
        } else {
            format.setMarker(QTextBlockFormat::MarkerType::Unchecked);
            cursor.setBlockFormat(format);
        }
        cursor.endEditBlock();
        return cursor.position();
    }
    if (isHeading(type) && position == block.position() + block.length() - 1) {
        cursor.insertBlock(RichDoc::blockFormat(Block::Paragraph, false, m_style), RichDoc::charFormat(Run(), Block::Paragraph, m_style));
        return cursor.position();
    }
    return -1;
}

int RichText::backspace(int position)
{
    const QTextBlock block = m_document->findBlock(position);
    if (!block.isValid() || position != block.position()) {
        return -1;
    }
    const Block::Type type = RichDoc::typeOf(block);
    if (type == Block::Paragraph) {
        return -1;
    }
    QTextCursor cursor(m_document);
    if (isListType(type) && listIndent(block) > 0) {
        indent(position, position, -1);
    } else {
        cursor.beginEditBlock();
        retype(block, Block::Paragraph, false);
        cursor.endEditBlock();
    }
    return position;
}

QVariantMap RichText::lookAt(int start, int end) const
{
    const QTextBlock block = m_document->findBlock(start);
    if (!block.isValid()) {
        return {};
    }
    const Block::Type type = RichDoc::typeOf(block);
    QTextCursor cursor(m_document);
    cursor.setPosition(end > start ? qMin(start + 1, m_document->characterCount() - 1) : start);
    Run run = RichDoc::runOf(cursor.charFormat(), type);
    const QVariantMap image = imageAt(start);
    if (run.isImage()) {
        run = Run();
    }
    cursor.setPosition(start);
    return {
        {QStringLiteral("type"), Block::typeName(type)},
        {QStringLiteral("done"), block.blockFormat().marker() == QTextBlockFormat::MarkerType::Checked},
        {QStringLiteral("indent"), listIndent(block)},
        {QStringLiteral("bold"), run.bold},
        {QStringLiteral("italic"), run.italic},
        {QStringLiteral("underline"), run.underline},
        {QStringLiteral("strike"), run.strike},
        {QStringLiteral("mono"), run.mono},
        {QStringLiteral("color"), run.color},
        {QStringLiteral("highlight"), run.highlight},
        {QStringLiteral("link"), linkAt(start).value(QStringLiteral("href")).toString()},
        {QStringLiteral("table"), cursor.currentTable() != nullptr},
        {QStringLiteral("image"), image.value(QStringLiteral("source")).toString()},
    };
}
