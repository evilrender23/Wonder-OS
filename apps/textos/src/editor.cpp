#include "editor.h"

#include "formats.h"

#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QPainter>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTextLayout>
#include <QTextList>
#include <QTextTable>
#include <QTimer>

namespace {

const QColor selectionColor(0x2f, 0x6f, 0xed, 70);
const QColor matchColor(0xff, 0xd6, 0x0a, 120);
const QColor currentMatchColor(0xff, 0x9f, 0x0a, 190);

QTextListFormat::Style listStyle(int kind, int level)
{
    static const QTextListFormat::Style bullets[] = {QTextListFormat::ListDisc, QTextListFormat::ListCircle, QTextListFormat::ListSquare};
    static const QTextListFormat::Style numbers[] = {QTextListFormat::ListDecimal, QTextListFormat::ListLowerAlpha, QTextListFormat::ListLowerRoman};
    return kind == 2 ? numbers[(level - 1) % 3] : bullets[(level - 1) % 3];
}

/// 0 none, 1 bullets, 2 numbers.
int listKind(const QTextList *list)
{
    if (!list) {
        return 0;
    }
    switch (list->format().style()) {
    case QTextListFormat::ListDisc:
    case QTextListFormat::ListCircle:
    case QTextListFormat::ListSquare:
        return 1;
    default:
        return 2;
    }
}

bool isHeadingStyle(const QString &id)
{
    return id == QLatin1String("title") || id == QLatin1String("h1") || id == QLatin1String("h2") || id == QLatin1String("h3");
}

} // namespace

Editor::Editor(QObject *parent)
    : QObject(parent)
    , m_refresh(new QTimer(this))
{
    m_refresh->setSingleShot(true);
    m_refresh->setInterval(200);
    connect(m_refresh, &QTimer::timeout, this, &Editor::refreshNow);
    auto *document = new QTextDocument(this);
    Doc::prepare(document);
    adopt(document);
    m_loading = true;
    build(QStringLiteral("blank"));
    m_document->clearUndoRedoStacks();
    m_document->setModified(false);
    m_cursor.setPosition(0);
    m_loading = false;
    refreshNow();
    refreshFormat();
}

Editor::~Editor() = default;

void Editor::adopt(QTextDocument *document)
{
    if (m_document && m_document != document) {
        m_document->disconnect(this);
        m_document->documentLayout()->disconnect(this);
        m_cursor = QTextCursor();
        m_document->deleteLater();
    }
    m_document = document;
    m_document->setParent(this);
    m_document->setUndoRedoEnabled(true);
    m_document->setPageSize(m_setup.contentPx());
    m_cursor = QTextCursor(m_document);
    connect(m_document, &QTextDocument::contentsChanged, this, &Editor::touched);
    connect(m_document, &QTextDocument::undoAvailable, this, &Editor::undoChanged);
    connect(m_document, &QTextDocument::redoAvailable, this, &Editor::undoChanged);
    connect(m_document, &QTextDocument::modificationChanged, this, &Editor::modifiedChanged);
    connect(m_document->documentLayout(), &QAbstractTextDocumentLayout::pageCountChanged, this, &Editor::pagesChanged);
    connect(m_document->documentLayout(), &QAbstractTextDocumentLayout::update, this, [this] {
        Q_EMIT repaint();
    });
}

void Editor::touched()
{
    if (m_loading) {
        return;
    }
    ++m_revision;
    m_refresh->start();
    // The layout only tells of new pages when someone asks it to lay them out.
    const int pages = pageCount();
    if (pages != m_pages) {
        m_pages = pages;
        Q_EMIT pagesChanged();
    }
    refreshFormat();
    Q_EMIT cursorMoved();
    Q_EMIT repaint();
}

void Editor::moved()
{
    refreshFormat();
    Q_EMIT cursorMoved();
    if (!m_matches.isEmpty()) {
        Q_EMIT findChanged();
    }
    Q_EMIT repaint();
}

QString Editor::name() const
{
    return m_path.isEmpty() ? QStringLiteral("Sin título") : QFileInfo(m_path).fileName();
}

bool Editor::modified() const
{
    return m_document->isModified() || m_setupDirty;
}

bool Editor::lossy() const
{
    return !m_path.isEmpty() && !Formats::keepsFormatting(Formats::kindOf(m_path));
}

int Editor::pageCount() const
{
    return Formats::pageCount(m_document);
}

bool Editor::canUndo() const
{
    return m_document->isUndoAvailable();
}

bool Editor::canRedo() const
{
    return m_document->isRedoAvailable();
}

QVariantList Editor::styles() const
{
    QVariantList list;
    for (const Doc::Style &style : Doc::styles()) {
        const Doc::Look look = Doc::lookOf(style.chars);
        list << QVariantMap{{QStringLiteral("id"), style.id},        {QStringLiteral("name"), style.name},
                            {QStringLiteral("family"), look.family}, {QStringLiteral("size"), look.size},
                            {QStringLiteral("bold"), look.bold},     {QStringLiteral("italic"), look.italic}};
    }
    return list;
}

// --- Files ------------------------------------------------------------------------

void Editor::newDocument(const QString &kind)
{
    m_loading = true;
    auto *document = new QTextDocument(this);
    Doc::prepare(document);
    m_setup = Doc::PageSetup();
    adopt(document);
    build(kind);
    m_document->clearUndoRedoStacks();
    m_document->setModified(false);
    m_setupDirty = false;
    m_cursor.setPosition(0);
    m_path.clear();
    m_loading = false;
    m_find.clear();
    m_matches.clear();
    refreshNow();
    refreshFormat();
    Q_EMIT pathChanged();
    Q_EMIT setupChanged();
    Q_EMIT modifiedChanged();
    Q_EMIT undoChanged();
    Q_EMIT pagesChanged();
    Q_EMIT findChanged();
    Q_EMIT cursorMoved();
    Q_EMIT replaced();
    Q_EMIT repaint();
}

bool Editor::load(const QString &path)
{
    return loadAs(path, path);
}

bool Editor::loadAs(const QString &file, const QString &path)
{
    auto *document = new QTextDocument(this);
    Doc::prepare(document);
    Doc::PageSetup setup;
    QString error;
    if (!Formats::load(file, document, &setup, &error)) {
        delete document;
        Q_EMIT said(error.isEmpty() ? QStringLiteral("No se ha podido abrir «%1».").arg(QFileInfo(file).fileName()) : error);
        return false;
    }
    m_loading = true;
    m_setup = setup;
    adopt(document);
    m_document->clearUndoRedoStacks();
    m_document->setModified(false);
    m_setupDirty = false;
    m_cursor.setPosition(0);
    m_path = path;
    m_loading = false;
    m_find.clear();
    m_matches.clear();
    refreshNow();
    refreshFormat();
    Q_EMIT pathChanged();
    Q_EMIT setupChanged();
    Q_EMIT modifiedChanged();
    Q_EMIT undoChanged();
    Q_EMIT pagesChanged();
    Q_EMIT findChanged();
    Q_EMIT cursorMoved();
    Q_EMIT replaced();
    Q_EMIT repaint();
    return true;
}

bool Editor::save()
{
    return !m_path.isEmpty() && saveAs(m_path);
}

bool Editor::saveAs(const QString &path)
{
    if (Formats::kindOf(path) == Formats::Kind::Pdf) {
        return exportTo(path);
    }
    QString error;
    if (!Formats::save(path, m_document, m_setup, &error)) {
        Q_EMIT said(error.isEmpty() ? QStringLiteral("No se ha podido guardar el documento.") : error);
        return false;
    }
    const bool renamed = path != m_path;
    m_path = path;
    m_setupDirty = false;
    m_document->setModified(false);
    Q_EMIT modifiedChanged();
    if (renamed) {
        Q_EMIT pathChanged();
    }
    return true;
}

bool Editor::exportTo(const QString &path)
{
    QString error;
    if (!Formats::save(path, m_document, m_setup, &error)) {
        Q_EMIT said(error.isEmpty() ? QStringLiteral("No se ha podido exportar el documento.") : error);
        return false;
    }
    return true;
}

void Editor::setSetup(const QVariantMap &map)
{
    QVariantMap merged = m_setup.toMap();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        merged.insert(it.key(), it.value());
    }
    const Doc::PageSetup setup = Doc::PageSetup::fromMap(merged);
    if (setup == m_setup) {
        return;
    }
    m_setup = setup;
    m_setupDirty = true;
    ++m_revision;
    m_document->setPageSize(m_setup.contentPx());
    m_refresh->start();
    Q_EMIT setupChanged();
    Q_EMIT modifiedChanged();
    Q_EMIT pagesChanged();
    Q_EMIT cursorMoved();
    Q_EMIT repaint();
}

QString Editor::plainText() const
{
    return Formats::toPlainText(m_document);
}

// --- Characters -------------------------------------------------------------------

void Editor::mergeChars(const QTextCharFormat &format)
{
    m_cursor.mergeCharFormat(format);
    refreshFormat();
    Q_EMIT repaint();
}

void Editor::toggleBold()
{
    QTextCharFormat format;
    format.setFontWeight(m_format.value(QStringLiteral("bold")).toBool() ? QFont::Normal : QFont::Bold);
    mergeChars(format);
}

void Editor::toggleItalic()
{
    QTextCharFormat format;
    format.setFontItalic(!m_format.value(QStringLiteral("italic")).toBool());
    mergeChars(format);
}

void Editor::toggleUnderline()
{
    QTextCharFormat format;
    format.setFontUnderline(!m_format.value(QStringLiteral("underline")).toBool());
    mergeChars(format);
}

void Editor::toggleStrike()
{
    QTextCharFormat format;
    format.setFontStrikeOut(!m_format.value(QStringLiteral("strike")).toBool());
    mergeChars(format);
}

void Editor::setVertical(int vertical)
{
    QTextCharFormat format;
    format.setVerticalAlignment(vertical == 1 ? QTextCharFormat::AlignSuperScript : vertical == 2 ? QTextCharFormat::AlignSubScript : QTextCharFormat::AlignNormal);
    mergeChars(format);
}

void Editor::setFamily(const QString &family)
{
    if (family.isEmpty()) {
        return;
    }
    QTextCharFormat format;
    format.setFontFamilies({family});
    mergeChars(format);
}

void Editor::setPointSize(double size)
{
    QTextCharFormat format;
    format.setFontPointSize(qBound(4.0, size, 200.0));
    mergeChars(format);
}

void Editor::setColor(const QColor &color)
{
    QTextCharFormat format;
    format.setForeground(color.isValid() ? color : Doc::ink());
    mergeChars(format);
}

void Editor::setHighlight(const QColor &color)
{
    if (color.isValid() && color.alpha() > 0) {
        QTextCharFormat format;
        format.setBackground(color);
        mergeChars(format);
        return;
    }
    // Merging cannot take a property away: every piece is set again without it.
    if (!m_cursor.hasSelection()) {
        QTextCharFormat format = m_cursor.charFormat();
        format.clearBackground();
        m_cursor.setCharFormat(format);
    } else {
        const int from = m_cursor.selectionStart();
        const int to = m_cursor.selectionEnd();
        m_cursor.beginEditBlock();
        for (QTextBlock block = m_document->findBlock(from); block.isValid() && block.position() < to; block = block.next()) {
            QList<std::pair<int, int>> pieces;
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                if (fragment.isValid() && fragment.charFormat().hasProperty(QTextFormat::BackgroundBrush)) {
                    pieces.append({qMax(from, fragment.position()), qMin(to, fragment.position() + fragment.length())});
                }
            }
            for (const auto &[start, end] : std::as_const(pieces)) {
                if (start >= end) {
                    continue;
                }
                QTextCursor part(m_document);
                part.setPosition(start);
                part.setPosition(end, QTextCursor::KeepAnchor);
                part.setPosition(start + 1, QTextCursor::KeepAnchor);
                QTextCharFormat format = part.charFormat();
                format.clearBackground();
                part.setPosition(end, QTextCursor::KeepAnchor);
                part.setCharFormat(format);
            }
        }
        m_cursor.endEditBlock();
    }
    refreshFormat();
    Q_EMIT repaint();
}

// --- Paragraphs -------------------------------------------------------------------

void Editor::mergeBlocks(const QTextBlockFormat &format)
{
    m_cursor.mergeBlockFormat(format);
    refreshFormat();
    Q_EMIT repaint();
}

void Editor::setAlignment(const QString &alignment)
{
    QTextBlockFormat format;
    format.setAlignment(alignment == QLatin1String("center") ? Qt::AlignHCenter
                            : alignment == QLatin1String("right") ? Qt::AlignRight
                            : alignment == QLatin1String("justify") ? Qt::AlignJustify : Qt::AlignLeft);
    mergeBlocks(format);
}

void Editor::setLineHeight(int percent)
{
    QTextBlockFormat format;
    format.setLineHeight(qBound(50, percent, 400), QTextBlockFormat::ProportionalHeight);
    mergeBlocks(format);
}

void Editor::setSpacing(double before, double after)
{
    QTextBlockFormat format;
    format.setTopMargin(qBound(0.0, before, 200.0) * Doc::pxPerPt);
    format.setBottomMargin(qBound(0.0, after, 200.0) * Doc::pxPerPt);
    mergeBlocks(format);
}

void Editor::setIndents(double left, double right, double first)
{
    QTextBlockFormat format;
    format.setLeftMargin(qBound(0.0, left, 15.0) * Doc::pxPerCm);
    format.setRightMargin(qBound(0.0, right, 15.0) * Doc::pxPerCm);
    format.setTextIndent(qBound(-10.0, first, 10.0) * Doc::pxPerCm);
    mergeBlocks(format);
}

void Editor::applyStyle(const QString &id)
{
    Doc::applyStyle(m_cursor, id);
    refreshFormat();
    Q_EMIT repaint();
}

void Editor::relist(int kind, int shift)
{
    const int first = m_document->findBlock(m_cursor.selectionStart()).blockNumber();
    const int last = m_document->findBlock(m_cursor.selectionEnd()).blockNumber();
    m_cursor.beginEditBlock();
    for (int number = first; number <= last; ++number) {
        const QTextBlock block = m_document->findBlockByNumber(number);
        QTextList *old = block.textList();
        const int wantedKind = kind >= 0 ? kind : listKind(old);
        if (wantedKind == 0) {
            if (old) {
                old->remove(block);
                QTextCursor at(block);
                QTextBlockFormat format = at.blockFormat();
                format.setIndent(0);
                at.setBlockFormat(format);
            } else if (shift != 0) {
                QTextCursor at(block);
                QTextBlockFormat format;
                format.setLeftMargin(qBound(0.0, block.blockFormat().leftMargin() + shift * 1.25 * Doc::pxPerCm, 15 * Doc::pxPerCm));
                at.mergeBlockFormat(format);
            }
            continue;
        }
        const int level = qBound(1, (old ? old->format().indent() : 1) + shift, 9);
        const QTextListFormat::Style style = listStyle(wantedKind, level);
        if (old && old->format().indent() == level && old->format().style() == style) {
            continue;
        }
        if (old) {
            old->remove(block);
        }
        // Next to a list of the same kind and depth, it is one more item of it.
        QTextList *neighbour = block.previous().isValid() ? block.previous().textList() : nullptr;
        if (!(neighbour && neighbour->format().indent() == level && neighbour->format().style() == style)) {
            neighbour = nullptr;
            // Numbers go on after a deeper stretch: look back past it.
            for (QTextBlock back = block.previous(); back.isValid() && back.textList(); back = back.previous()) {
                const QTextListFormat format = back.textList()->format();
                if (format.indent() < level) {
                    break;
                }
                if (format.indent() == level) {
                    if (format.style() == style) {
                        neighbour = back.textList();
                    }
                    break;
                }
            }
        }
        QTextCursor at(block);
        QTextBlockFormat plain = at.blockFormat();
        plain.setIndent(0);
        plain.setLeftMargin(0);
        plain.setTextIndent(0);
        at.setBlockFormat(plain);
        if (neighbour) {
            neighbour->add(block);
        } else {
            QTextListFormat format;
            format.setStyle(style);
            format.setIndent(level);
            at.createList(format);
        }
    }
    m_cursor.endEditBlock();
    refreshFormat();
    Q_EMIT repaint();
}

void Editor::setList(int kind)
{
    const int current = listKind(m_cursor.block().textList());
    relist(kind == current ? 0 : qBound(0, kind, 2), 0);
}

void Editor::indentMore()
{
    relist(-1, 1);
}

void Editor::indentLess()
{
    QTextList *list = m_cursor.block().textList();
    if (list && list->format().indent() <= 1 && !m_cursor.hasSelection()) {
        relist(0, 0);
        return;
    }
    relist(-1, -1);
}

// --- Things in the text -----------------------------------------------------------

void Editor::typed(const QString &text)
{
    // What is written after a link is not part of it.
    QTextCharFormat format = m_cursor.charFormat();
    if (!m_cursor.hasSelection() && format.isAnchor()) {
        QTextCursor next(m_cursor);
        const bool inside = next.movePosition(QTextCursor::NextCharacter) && next.block() == m_cursor.block() && next.charFormat().isAnchor()
            && next.charFormat().anchorHref() == format.anchorHref() && !m_cursor.atBlockStart();
        if (!inside) {
            const Doc::Style &style = Doc::style(Doc::styleOf(m_cursor.block()));
            format.setAnchor(false);
            format.setAnchorHref(QString());
            format.setFontUnderline(false);
            format.setForeground(style.chars.foreground());
            m_cursor.insertText(text, format);
            return;
        }
    }
    m_cursor.insertText(text);
    if (m_markdown && text.size() == 1) {
        shorthand(text.at(0));
    }
}

void Editor::shorthand(QChar last)
{
    const QTextBlock block = m_cursor.block();
    const int column = m_cursor.positionInBlock();
    const QString before = block.text().left(column);
    // Its own step, so undoing gives back what was typed, letter by letter.
    if (last == QLatin1Char(' ') && column <= 4) {
        const QString mark = before.chopped(1);
        QString style;
        int list = 0;
        if (mark == QLatin1String("#")) {
            style = QStringLiteral("h1");
        } else if (mark == QLatin1String("##")) {
            style = QStringLiteral("h2");
        } else if (mark == QLatin1String("###")) {
            style = QStringLiteral("h3");
        } else if (mark == QLatin1String(">")) {
            style = QStringLiteral("quote");
        } else if (mark == QLatin1String("-") || mark == QLatin1String("*")) {
            list = 1;
        } else if (mark == QLatin1String("1.") || mark == QLatin1String("1)")) {
            list = 2;
        }
        if ((style.isEmpty() && list == 0) || (list != 0 && block.textList()) || (!style.isEmpty() && Doc::styleOf(block) == style)) {
            return;
        }
        m_cursor.beginEditBlock();
        m_cursor.setPosition(block.position());
        m_cursor.setPosition(block.position() + column, QTextCursor::KeepAnchor);
        m_cursor.removeSelectedText();
        if (list != 0) {
            relist(list, 0);
        } else {
            Doc::applyStyle(m_cursor, style);
        }
        m_cursor.endEditBlock();
        return;
    }
    // **bold** and ~~struck~~, when the second pair closes.
    for (const QString &pair : {QStringLiteral("**"), QStringLiteral("~~")}) {
        if (last != pair.at(0) || !before.endsWith(pair) || before.size() < 5) {
            continue;
        }
        const int open = before.lastIndexOf(pair, column - 3);
        if (open < 0) {
            continue;
        }
        const QString inner = before.mid(open + 2, column - 2 - (open + 2));
        if (inner.isEmpty() || inner.at(0).isSpace() || inner.back().isSpace() || inner.contains(pair.at(0))) {
            continue;
        }
        const int start = block.position() + open;
        m_cursor.beginEditBlock();
        QTextCursor edit(m_document);
        edit.setPosition(block.position() + column - 2);
        edit.setPosition(block.position() + column, QTextCursor::KeepAnchor);
        edit.removeSelectedText();
        edit.setPosition(start);
        edit.setPosition(start + 2, QTextCursor::KeepAnchor);
        edit.removeSelectedText();
        edit.setPosition(start);
        edit.setPosition(start + inner.size(), QTextCursor::KeepAnchor);
        QTextCharFormat format;
        if (pair.at(0) == QLatin1Char('*')) {
            format.setFontWeight(QFont::Bold);
        } else {
            format.setFontStrikeOut(true);
        }
        edit.mergeCharFormat(format);
        // What is written next is plain again.
        m_cursor.setPosition(start + inner.size());
        QTextCharFormat plain = m_cursor.charFormat();
        if (pair.at(0) == QLatin1Char('*')) {
            plain.setFontWeight(Doc::style(Doc::styleOf(block)).chars.fontWeight());
        } else {
            plain.setFontStrikeOut(false);
        }
        m_cursor.setCharFormat(plain);
        m_cursor.endEditBlock();
        return;
    }
}

void Editor::insertSeparator()
{
    m_cursor.beginEditBlock();
    if (m_cursor.hasSelection()) {
        m_cursor.removeSelectedText();
    }
    if (m_cursor.block().length() > 1) {
        m_cursor.movePosition(QTextCursor::EndOfBlock);
        m_cursor.insertBlock();
    }
    if (QTextList *list = m_cursor.block().textList()) {
        list->remove(m_cursor.block());
    }
    Doc::applyStyle(m_cursor, QStringLiteral("body"));
    QTextBlockFormat middle = m_cursor.blockFormat();
    middle.setIndent(0);
    middle.setAlignment(Qt::AlignHCenter);
    m_cursor.setBlockFormat(middle);
    m_cursor.insertText(QStringLiteral("* * *"), Doc::style(QStringLiteral("body")).chars);
    m_cursor.insertBlock();
    QTextBlockFormat after = m_cursor.blockFormat();
    after.setAlignment(Qt::AlignLeft);
    m_cursor.setBlockFormat(after);
    m_cursor.endEditBlock();
    moved();
}

void Editor::insertText(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    m_cursor.beginEditBlock();
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            newParagraph();
        }
        if (!lines.at(i).isEmpty()) {
            typed(lines.at(i));
        }
    }
    m_cursor.endEditBlock();
}

void Editor::newParagraph()
{
    m_cursor.beginEditBlock();
    if (m_cursor.hasSelection()) {
        m_cursor.removeSelectedText();
    }
    const QTextBlock block = m_cursor.block();
    const QString id = Doc::styleOf(block);
    QTextList *list = block.textList();
    if (list && block.length() <= 1) {
        // Return on an empty item ends the list.
        list->remove(block);
        QTextBlockFormat format = m_cursor.blockFormat();
        format.setIndent(0);
        m_cursor.setBlockFormat(format);
        m_cursor.endEditBlock();
        return;
    }
    const bool atEnd = m_cursor.atBlockEnd();
    const bool atStart = m_cursor.atBlockStart() && block.length() > 1;
    m_cursor.insertBlock();
    if (atStart) {
        // The paragraph moved down; the page break stays above it.
        QTextBlockFormat format = m_cursor.blockFormat();
        if (format.pageBreakPolicy() != QTextFormat::PageBreak_Auto) {
            format.setPageBreakPolicy(QTextFormat::PageBreak_Auto);
            m_cursor.setBlockFormat(format);
        }
    } else {
        QTextBlockFormat format = m_cursor.blockFormat();
        if (format.pageBreakPolicy() != QTextFormat::PageBreak_Auto) {
            format.setPageBreakPolicy(QTextFormat::PageBreak_Auto);
            m_cursor.setBlockFormat(format);
        }
        if (atEnd && isHeadingStyle(id)) {
            // After a heading comes text.
            const Qt::Alignment alignment = Qt::AlignLeft;
            Doc::applyStyle(m_cursor, QStringLiteral("body"));
            QTextBlockFormat body;
            body.setAlignment(alignment);
            m_cursor.mergeBlockFormat(body);
        }
    }
    m_cursor.endEditBlock();
}

void Editor::dress(QTextTable *table, int row, int rows, int column, int columns)
{
    QTextBlockFormat block = Doc::style(QStringLiteral("body")).block;
    block.setBottomMargin(0);
    for (int r = row; r < row + rows && r < table->rows(); ++r) {
        for (int c = column; c < column + columns && c < table->columns(); ++c) {
            QTextCursor at = table->cellAt(r, c).firstCursorPosition();
            at.setBlockFormat(block);
            at.setBlockCharFormat(Doc::style(QStringLiteral("body")).chars);
        }
    }
}

void Editor::even(QTextTable *table)
{
    QTextTableFormat format = table->format();
    QList<QTextLength> widths;
    for (int column = 0; column < table->columns(); ++column) {
        widths << QTextLength(QTextLength::PercentageLength, 100.0 / table->columns());
    }
    format.setColumnWidthConstraints(widths);
    table->setFormat(format);
}

void Editor::insertTable(int rows, int columns)
{
    rows = qBound(1, rows, 100);
    columns = qBound(1, columns, 20);
    m_cursor.beginEditBlock();
    if (m_cursor.hasSelection()) {
        m_cursor.removeSelectedText();
    }
    if (m_cursor.currentTable()) {
        // No tables inside tables: it goes after the one the cursor is in.
        m_cursor.setPosition(m_cursor.currentTable()->lastPosition() + 1);
    }
    // A table goes between paragraphs: after the one the cursor is in.
    if (m_cursor.block().length() > 1 && !m_cursor.atBlockEnd()) {
        m_cursor.insertBlock();
        m_cursor.movePosition(QTextCursor::PreviousBlock);
        m_cursor.movePosition(QTextCursor::EndOfBlock);
    }
    QTextTable *table = m_cursor.insertTable(rows, columns, Doc::tableFormat(columns));
    dress(table, 0, rows, 0, columns);
    m_cursor = table->cellAt(0, 0).firstCursorPosition();
    m_cursor.endEditBlock();
    moved();
}

void Editor::addRow(bool below)
{
    QTextTable *table = m_cursor.currentTable();
    if (!table) {
        return;
    }
    const QTextTableCell cell = table->cellAt(m_cursor);
    const int row = cell.row() + (below ? cell.rowSpan() : 0);
    m_cursor.beginEditBlock();
    table->insertRows(row, 1);
    dress(table, row, 1, 0, table->columns());
    m_cursor.endEditBlock();
    moved();
}

void Editor::addColumn(bool after)
{
    QTextTable *table = m_cursor.currentTable();
    if (!table || table->columns() >= 20) {
        return;
    }
    const QTextTableCell cell = table->cellAt(m_cursor);
    const int column = cell.column() + (after ? cell.columnSpan() : 0);
    m_cursor.beginEditBlock();
    table->insertColumns(column, 1);
    dress(table, 0, table->rows(), column, 1);
    even(table);
    m_cursor.endEditBlock();
    moved();
}

void Editor::removeRow()
{
    QTextTable *table = m_cursor.currentTable();
    if (!table) {
        return;
    }
    if (table->rows() <= 1) {
        removeTable();
        return;
    }
    m_cursor.beginEditBlock();
    table->removeRows(table->cellAt(m_cursor).row(), 1);
    m_cursor.endEditBlock();
    moved();
}

void Editor::removeColumn()
{
    QTextTable *table = m_cursor.currentTable();
    if (!table) {
        return;
    }
    if (table->columns() <= 1) {
        removeTable();
        return;
    }
    m_cursor.beginEditBlock();
    table->removeColumns(table->cellAt(m_cursor).column(), 1);
    even(table);
    m_cursor.endEditBlock();
    moved();
}

void Editor::removeTable()
{
    QTextTable *table = m_cursor.currentTable();
    if (!table) {
        return;
    }
    m_cursor.beginEditBlock();
    QTextCursor all(m_document);
    all.setPosition(table->firstPosition() - 1);
    all.setPosition(table->lastPosition() + 1, QTextCursor::KeepAnchor);
    all.removeSelectedText();
    m_cursor.setPosition(qMin(all.position(), m_document->characterCount() - 1));
    m_cursor.endEditBlock();
    moved();
}

bool Editor::insertImage(const QString &path)
{
    QString file = path;
    if (file.startsWith(QLatin1String("file://"))) {
        file = QUrl(file).toLocalFile();
    }
    const QImage image(file);
    if (image.isNull()) {
        Q_EMIT said(QStringLiteral("«%1» no es una imagen que Textos sepa leer.").arg(QFileInfo(file).fileName()));
        return false;
    }
    return insertImage(image);
}

bool Editor::insertImage(const QImage &image)
{
    if (image.isNull()) {
        return false;
    }
    QImage stored = image;
    // Nothing on a sheet needs more than this.
    if (stored.width() > 2400 || stored.height() > 2400) {
        stored = stored.scaled(2400, 2400, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QTextImageFormat format;
    format.setName(Doc::addImage(m_document, stored));
    const double room = m_cursor.currentTable() ? m_setup.contentPx().width() / m_cursor.currentTable()->columns() - 12 : m_setup.contentPx().width();
    const double width = qMin(double(image.width()), room);
    format.setWidth(width);
    format.setHeight(width * image.height() / qMax(1, image.width()));
    m_cursor.insertImage(format);
    return true;
}

int Editor::selectedImage() const
{
    if (m_cursor.selectionEnd() - m_cursor.selectionStart() != 1) {
        return -1;
    }
    QTextCursor at(m_document);
    at.setPosition(m_cursor.selectionStart() + 1);
    return at.charFormat().isImageFormat() ? m_cursor.selectionStart() : -1;
}

void Editor::setImageWidth(double centimetres)
{
    const int position = selectedImage();
    if (position < 0) {
        return;
    }
    QTextCursor at(m_document);
    at.setPosition(position + 1);
    QTextImageFormat format = at.charFormat().toImageFormat();
    const QImage image = Doc::image(m_document, format.name());
    const double old = format.width() > 0 ? format.width() : image.width();
    const double oldHeight = format.height() > 0 ? format.height() : image.height();
    const double width = qBound(8.0, centimetres * Doc::pxPerCm, m_setup.contentPx().width());
    format.setWidth(width);
    format.setHeight(old > 0 ? width * oldHeight / old : width);
    at.setPosition(position);
    at.setPosition(position + 1, QTextCursor::KeepAnchor);
    at.setCharFormat(format);
    refreshFormat();
    Q_EMIT repaint();
}

void Editor::setLink(const QString &href, const QString &text)
{
    QString address = href.trimmed();
    if (address.isEmpty()) {
        removeLink();
        return;
    }
    if (!address.contains(QLatin1Char(':')) && !address.startsWith(QLatin1Char('#'))) {
        address = (address.contains(QLatin1Char('@')) ? QStringLiteral("mailto:") : QStringLiteral("https://")) + address;
    }
    if (m_cursor.hasSelection() && selectedImage() < 0) {
        m_cursor.mergeCharFormat(Doc::linkFormat(address));
    } else {
        QTextCharFormat format = m_cursor.charFormat();
        format.merge(Doc::linkFormat(address));
        if (m_cursor.hasSelection()) {
            m_cursor.clearSelection();
        }
        m_cursor.insertText(text.isEmpty() ? href.trimmed() : text, format);
    }
    refreshFormat();
    Q_EMIT repaint();
}

void Editor::removeLink()
{
    int from = m_cursor.selectionStart();
    int to = m_cursor.selectionEnd();
    if (!m_cursor.hasSelection()) {
        // The whole link the cursor is in.
        const QString href = m_format.value(QStringLiteral("link")).toString();
        if (href.isEmpty()) {
            return;
        }
        const QTextBlock block = m_cursor.block();
        const int position = m_cursor.position();
        from = to = position;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || fragment.charFormat().anchorHref() != href) {
                continue;
            }
            const int start = fragment.position();
            const int end = start + fragment.length();
            if (end >= from && start <= to) {
                from = qMin(from, start);
                to = qMax(to, end);
            }
        }
    }
    if (from >= to) {
        return;
    }
    m_cursor.beginEditBlock();
    for (QTextBlock block = m_document->findBlock(from); block.isValid() && block.position() < to; block = block.next()) {
        const Doc::Style &style = Doc::style(Doc::styleOf(block));
        QList<std::pair<int, int>> pieces;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().isAnchor()) {
                pieces.append({qMax(from, fragment.position()), qMin(to, fragment.position() + fragment.length())});
            }
        }
        for (const auto &[start, end] : std::as_const(pieces)) {
            if (start >= end) {
                continue;
            }
            QTextCursor part(m_document);
            part.setPosition(start);
            part.setPosition(end, QTextCursor::KeepAnchor);
            QTextCharFormat format;
            format.setAnchor(false);
            format.setAnchorHref(QString());
            format.setFontUnderline(false);
            format.setForeground(style.chars.foreground());
            part.mergeCharFormat(format);
        }
    }
    m_cursor.endEditBlock();
    refreshFormat();
    Q_EMIT repaint();
}

void Editor::insertPageBreak()
{
    if (m_cursor.currentTable()) {
        Q_EMIT said(QStringLiteral("Dentro de una tabla no cabe un salto de página."));
        return;
    }
    m_cursor.beginEditBlock();
    if (m_cursor.hasSelection()) {
        m_cursor.removeSelectedText();
    }
    m_cursor.insertBlock();
    QTextBlockFormat format = m_cursor.blockFormat();
    format.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    m_cursor.setBlockFormat(format);
    if (QTextList *list = m_cursor.block().textList()) {
        list->remove(m_cursor.block());
    }
    m_cursor.endEditBlock();
    moved();
}

// --- Editing ----------------------------------------------------------------------

void Editor::undo()
{
    m_document->undo(&m_cursor);
    moved();
}

void Editor::redo()
{
    m_document->redo(&m_cursor);
    moved();
}

void Editor::copy()
{
    if (!m_cursor.hasSelection() || !qGuiApp) {
        return;
    }
    const QTextDocumentFragment fragment = m_cursor.selection();
    auto *data = new QMimeData;
    data->setText(fragment.toPlainText());
    data->setHtml(fragment.toHtml());
    const int image = selectedImage();
    if (image >= 0) {
        QTextCursor at(m_document);
        at.setPosition(image + 1);
        data->setImageData(Doc::image(m_document, at.charFormat().toImageFormat().name()));
    }
    QGuiApplication::clipboard()->setMimeData(data);
}

void Editor::cut()
{
    if (!m_cursor.hasSelection()) {
        return;
    }
    copy();
    m_cursor.removeSelectedText();
}

void Editor::paste()
{
    if (!qGuiApp) {
        return;
    }
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    if (!data) {
        return;
    }
    if (data->hasImage() && !data->hasText()) {
        insertImage(qvariant_cast<QImage>(data->imageData()));
    } else if (data->hasHtml()) {
        // Pictures on a web page are not fetched: only what is in the clipboard comes.
        m_cursor.insertFragment(QTextDocumentFragment::fromHtml(data->html()));
    } else if (data->hasText()) {
        insertText(data->text());
    }
}

void Editor::selectAll()
{
    m_cursor.select(QTextCursor::Document);
    moved();
}

QString Editor::selectedText() const
{
    QString text = m_cursor.selectedText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    text.replace(QChar::LineSeparator, QLatin1Char('\n'));
    return text;
}

bool Editor::backspace(bool word)
{
    if (m_cursor.hasSelection()) {
        m_cursor.removeSelectedText();
        return true;
    }
    if (m_cursor.atBlockStart()) {
        const QTextBlock block = m_cursor.block();
        QTextBlockFormat format = block.blockFormat();
        if (QTextList *list = block.textList()) {
            list->remove(block);
            format = m_cursor.blockFormat();
            format.setIndent(0);
            m_cursor.setBlockFormat(format);
            return true;
        }
        if (format.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore) {
            format.setPageBreakPolicy(QTextFormat::PageBreak_Auto);
            m_cursor.setBlockFormat(format);
            return true;
        }
        if (format.leftMargin() > 0 && Doc::style(Doc::styleOf(block)).block.leftMargin() < format.leftMargin()) {
            format.setLeftMargin(qMax(Doc::style(Doc::styleOf(block)).block.leftMargin(), format.leftMargin() - 1.25 * Doc::pxPerCm));
            m_cursor.setBlockFormat(format);
            return true;
        }
        // Not out of a cell, nor into the table above.
        QTextCursor before(m_cursor);
        if (!before.movePosition(QTextCursor::PreviousCharacter) || before.currentTable() != m_cursor.currentTable()) {
            return true;
        }
        if (m_cursor.currentTable() && m_cursor.currentTable()->cellAt(before) != m_cursor.currentTable()->cellAt(m_cursor)) {
            return true;
        }
    }
    if (word) {
        m_cursor.movePosition(QTextCursor::PreviousWord, QTextCursor::KeepAnchor);
        m_cursor.removeSelectedText();
    } else {
        m_cursor.deletePreviousChar();
    }
    return true;
}

bool Editor::key(int key, int modifiers, const QString &text)
{
    const bool shift = modifiers & Qt::ShiftModifier;
    const bool control = modifiers & Qt::ControlModifier;
    const bool alt = modifiers & Qt::AltModifier;
    const QTextCursor::MoveMode mode = shift ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor;
    const auto go = [&](QTextCursor::MoveOperation operation) {
        // Without Shift, an arrow leaves a selection by its near end.
        if (!shift && m_cursor.hasSelection() && (operation == QTextCursor::Left || operation == QTextCursor::Right)) {
            m_cursor.setPosition(operation == QTextCursor::Left ? m_cursor.selectionStart() : m_cursor.selectionEnd());
        } else {
            m_cursor.movePosition(operation, mode);
        }
        moved();
        return true;
    };

    if (control && !alt) {
        switch (key) {
        case Qt::Key_B:
            toggleBold();
            return true;
        case Qt::Key_I:
            toggleItalic();
            return true;
        case Qt::Key_U:
            toggleUnderline();
            return true;
        case Qt::Key_Z:
            shift ? redo() : undo();
            return true;
        case Qt::Key_Y:
            redo();
            return true;
        case Qt::Key_A:
            selectAll();
            return true;
        case Qt::Key_C:
            copy();
            return true;
        case Qt::Key_X:
            cut();
            return true;
        case Qt::Key_V:
            paste();
            return true;
        case Qt::Key_Left:
            return go(QTextCursor::WordLeft);
        case Qt::Key_Right:
            return go(QTextCursor::WordRight);
        case Qt::Key_Up:
            return go(QTextCursor::PreviousBlock);
        case Qt::Key_Down:
            return go(QTextCursor::NextBlock);
        case Qt::Key_Home:
            return go(QTextCursor::Start);
        case Qt::Key_End:
            return go(QTextCursor::End);
        case Qt::Key_Return:
        case Qt::Key_Enter:
            insertPageBreak();
            return true;
        case Qt::Key_Backspace:
            return backspace(true);
        case Qt::Key_Delete:
            if (!m_cursor.hasSelection()) {
                m_cursor.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
            }
            m_cursor.removeSelectedText();
            return true;
        default:
            return false;
        }
    }

    switch (key) {
    case Qt::Key_Left:
        return go(QTextCursor::Left);
    case Qt::Key_Right:
        return go(QTextCursor::Right);
    case Qt::Key_Up:
        return go(QTextCursor::Up);
    case Qt::Key_Down:
        return go(QTextCursor::Down);
    case Qt::Key_Home:
        return go(QTextCursor::StartOfLine);
    case Qt::Key_End:
        return go(QTextCursor::EndOfLine);
    case Qt::Key_PageUp:
    case Qt::Key_PageDown: {
        const QRectF caret = caretInDocument(m_cursor.position());
        const double step = m_setup.contentPx().height() * (key == Qt::Key_PageUp ? -1 : 1);
        const QPointF target(caret.x(), qBound(0.0, caret.center().y() + step, m_document->size().height() - 1));
        const int position = m_document->documentLayout()->hitTest(target, Qt::FuzzyHit);
        if (position >= 0) {
            m_cursor.setPosition(position, mode);
        }
        moved();
        return true;
    }
    case Qt::Key_Backspace:
        return backspace(false);
    case Qt::Key_Delete:
        if (m_cursor.hasSelection()) {
            m_cursor.removeSelectedText();
        } else {
            // Not out of a cell, nor into the table below.
            QTextCursor after(m_cursor);
            if (m_cursor.atBlockEnd() && (!after.movePosition(QTextCursor::NextCharacter) || after.currentTable() != m_cursor.currentTable()
                                          || (m_cursor.currentTable() && m_cursor.currentTable()->cellAt(after) != m_cursor.currentTable()->cellAt(m_cursor)))) {
                return true;
            }
            m_cursor.deleteChar();
        }
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (shift) {
            m_cursor.insertText(QString(QChar::LineSeparator));
        } else if (m_markdown && !m_cursor.hasSelection() && m_cursor.block().text() == QLatin1String("```")) {
            // A fence opens a block of code.
            m_cursor.beginEditBlock();
            m_cursor.select(QTextCursor::BlockUnderCursor);
            m_cursor.setPosition(m_cursor.block().position());
            m_cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            m_cursor.removeSelectedText();
            if (QTextList *list = m_cursor.block().textList()) {
                list->remove(m_cursor.block());
            }
            Doc::applyStyle(m_cursor, QStringLiteral("code"));
            m_cursor.endEditBlock();
            moved();
        } else {
            newParagraph();
        }
        return true;
    case Qt::Key_Tab:
    case Qt::Key_Backtab: {
        const bool back = key == Qt::Key_Backtab || shift;
        if (QTextTable *table = m_cursor.currentTable()) {
            const QTextTableCell cell = table->cellAt(m_cursor);
            int index = cell.row() * table->columns() + cell.column() + (back ? -1 : 1);
            if (index >= table->rows() * table->columns()) {
                addRow(true);
            }
            index = qMax(0, index);
            m_cursor = table->cellAt(index / table->columns(), index % table->columns()).firstCursorPosition();
            moved();
        } else if (m_cursor.block().textList()) {
            back ? indentLess() : indentMore();
        } else if (!back) {
            m_cursor.insertText(QStringLiteral("\t"));
        } else {
            indentLess();
        }
        return true;
    }
    default:
        break;
    }

    if (alt || text.isEmpty()) {
        return false;
    }
    const QChar first = text.at(0);
    if (!first.isPrint() && first != QLatin1Char('\t')) {
        return false;
    }
    typed(text);
    return true;
}

// --- The mouse --------------------------------------------------------------------

QPointF Editor::pointAt(int page, qreal x, qreal y) const
{
    const QSizeF content = m_setup.contentPx();
    const double inPage = qBound(0.0, y - m_setup.top * Doc::pxPerCm, content.height() - 1);
    return QPointF(qBound(0.0, x - m_setup.left * Doc::pxPerCm, content.width() - 1), qMax(0, page) * content.height() + inPage);
}

int Editor::positionAt(int page, qreal x, qreal y) const
{
    const int position = m_document->documentLayout()->hitTest(pointAt(page, x, y), Qt::FuzzyHit);
    return position < 0 ? m_document->characterCount() - 1 : qMin(position, m_document->characterCount() - 1);
}

void Editor::press(int page, qreal x, qreal y, bool extend)
{
    const QPointF point = pointAt(page, x, y);
    const int position = positionAt(page, x, y);
    m_cursor.setPosition(position, extend ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
    if (!extend && !m_document->documentLayout()->imageAt(point).isEmpty()) {
        // A click on a picture takes hold of it.
        const int exact = m_document->documentLayout()->hitTest(point, Qt::ExactHit);
        for (const int start : {exact, exact - 1, position - 1, position}) {
            if (start < 0 || start >= m_document->characterCount() - 1) {
                continue;
            }
            QTextCursor at(m_document);
            at.setPosition(start + 1);
            if (at.charFormat().isImageFormat()) {
                m_cursor.setPosition(start);
                m_cursor.setPosition(start + 1, QTextCursor::KeepAnchor);
                break;
            }
        }
    }
    moved();
}

void Editor::drag(int page, qreal x, qreal y)
{
    const int position = positionAt(page, x, y);
    if (position != m_cursor.position()) {
        m_cursor.setPosition(position, QTextCursor::KeepAnchor);
        moved();
    }
}

void Editor::selectWord(int page, qreal x, qreal y)
{
    m_cursor.setPosition(positionAt(page, x, y));
    m_cursor.select(QTextCursor::WordUnderCursor);
    moved();
}

void Editor::selectParagraph(int page, qreal x, qreal y)
{
    m_cursor.setPosition(positionAt(page, x, y));
    m_cursor.select(QTextCursor::BlockUnderCursor);
    // Without the break before it.
    if (m_cursor.selectionStart() < m_cursor.block().position() || m_cursor.selectedText().startsWith(QChar::ParagraphSeparator)) {
        const int end = m_cursor.selectionEnd();
        m_cursor.setPosition(m_cursor.selectionStart() + 1);
        m_cursor.setPosition(end, QTextCursor::KeepAnchor);
    }
    moved();
}

QString Editor::linkAt(int page, qreal x, qreal y) const
{
    const QPointF point(x - m_setup.left * Doc::pxPerCm, page * m_setup.contentPx().height() + y - m_setup.top * Doc::pxPerCm);
    return m_document->documentLayout()->anchorAt(point);
}

void Editor::goTo(int position)
{
    m_cursor.setPosition(qBound(0, position, m_document->characterCount() - 1));
    moved();
}

void Editor::select(int from, int to)
{
    const int last = m_document->characterCount() - 1;
    m_cursor.setPosition(qBound(0, from, last));
    m_cursor.setPosition(qBound(0, to, last), QTextCursor::KeepAnchor);
    moved();
}

QRectF Editor::caretInDocument(int position) const
{
    const QTextBlock block = m_document->findBlock(position);
    if (!block.isValid() || !block.layout()) {
        return QRectF();
    }
    const QRectF bounds = m_document->documentLayout()->blockBoundingRect(block);
    const int offset = position - block.position();
    const QTextLine line = block.layout()->lineForTextPosition(offset);
    if (!line.isValid()) {
        return QRectF(bounds.left(), bounds.top(), 1, qMax(12.0, bounds.height()));
    }
    // The layout of a block is already placed in the document.
    const QPointF origin = block.layout()->position();
    return QRectF(origin.x() + line.cursorToX(offset), origin.y() + line.y(), 1, line.height());
}

int Editor::cursorPage() const
{
    const QRectF caret = caretInDocument(m_cursor.position());
    const double height = m_setup.contentPx().height();
    return qBound(0, int(caret.center().y() / height), pageCount() - 1);
}

QRectF Editor::caretRect() const
{
    QRectF caret = caretInDocument(m_cursor.position());
    caret.translate(m_setup.left * Doc::pxPerCm, m_setup.top * Doc::pxPerCm - cursorPage() * m_setup.contentPx().height());
    return caret;
}

// --- Find -------------------------------------------------------------------------

void Editor::refreshFind()
{
    m_matches.clear();
    if (!m_find.isEmpty()) {
        QTextDocument::FindFlags flags;
        if (m_findCase) {
            flags |= QTextDocument::FindCaseSensitively;
        }
        if (m_findWords) {
            flags |= QTextDocument::FindWholeWords;
        }
        QTextCursor found = m_document->find(m_find, 0, flags);
        while (!found.isNull() && m_matches.size() < 20000) {
            m_matches.append({found.selectionStart(), found.selectionEnd()});
            if (found.selectionEnd() == found.selectionStart()) {
                break;
            }
            found = m_document->find(m_find, found, flags);
        }
    }
    Q_EMIT findChanged();
    Q_EMIT repaint();
}

void Editor::setFind(const QString &text, bool caseSensitive, bool wholeWords)
{
    if (text == m_find && caseSensitive == m_findCase && wholeWords == m_findWords) {
        return;
    }
    m_find = text;
    m_findCase = caseSensitive;
    m_findWords = wholeWords;
    refreshFind();
}

int Editor::matchIndex() const
{
    for (int i = 0; i < m_matches.size(); ++i) {
        if (m_matches.at(i).first == m_cursor.selectionStart() && m_matches.at(i).second == m_cursor.selectionEnd()) {
            return i + 1;
        }
    }
    return 0;
}

void Editor::findNext(bool forward)
{
    if (m_matches.isEmpty()) {
        return;
    }
    int index = -1;
    if (forward) {
        const int from = m_cursor.selectionEnd();
        for (int i = 0; i < m_matches.size(); ++i) {
            if (m_matches.at(i).first >= from) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            index = 0;
        }
    } else {
        const int from = m_cursor.selectionStart();
        for (int i = int(m_matches.size()) - 1; i >= 0; --i) {
            if (m_matches.at(i).second <= from) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            index = int(m_matches.size()) - 1;
        }
    }
    m_cursor.setPosition(m_matches.at(index).first);
    m_cursor.setPosition(m_matches.at(index).second, QTextCursor::KeepAnchor);
    moved();
}

void Editor::replaceOne(const QString &with)
{
    if (matchIndex() == 0) {
        findNext(true);
        return;
    }
    // The format of what was there, not of what the cursor last touched.
    QTextCursor first(m_document);
    first.setPosition(m_cursor.selectionStart() + 1);
    const QTextCharFormat format = first.charFormat();
    m_cursor.insertText(with, format);
    refreshFind();
    findNext(true);
}

int Editor::replaceAll(const QString &with)
{
    if (m_matches.isEmpty()) {
        return 0;
    }
    const QList<std::pair<int, int>> matches = m_matches;
    QTextCursor edit(m_document);
    edit.beginEditBlock();
    for (int i = int(matches.size()) - 1; i >= 0; --i) {
        QTextCursor first(m_document);
        first.setPosition(matches.at(i).first + 1);
        const QTextCharFormat format = first.charFormat();
        edit.setPosition(matches.at(i).first);
        edit.setPosition(matches.at(i).second, QTextCursor::KeepAnchor);
        edit.insertText(with, format);
    }
    edit.endEditBlock();
    refreshFind();
    return int(matches.size());
}

// --- What the window shows --------------------------------------------------------

void Editor::refreshFormat()
{
    QTextCharFormat chars = m_cursor.charFormat();
    if (m_cursor.hasSelection()) {
        // What the selection starts with, not what is before it.
        QTextCursor first(m_document);
        first.setPosition(qMin(m_cursor.selectionStart() + 1, m_document->characterCount() - 1));
        chars = first.charFormat();
    }
    const QTextBlock block = m_cursor.hasSelection() ? m_document->findBlock(m_cursor.selectionStart()) : m_cursor.block();
    const QTextBlockFormat format = block.blockFormat();
    const Doc::Look look = Doc::lookOf(chars);
    QVariantMap map;
    map.insert(QStringLiteral("bold"), look.bold);
    map.insert(QStringLiteral("italic"), look.italic);
    map.insert(QStringLiteral("underline"), look.underline && !chars.isAnchor());
    map.insert(QStringLiteral("strike"), look.strike);
    map.insert(QStringLiteral("vertical"), look.vertical);
    map.insert(QStringLiteral("family"), look.family);
    map.insert(QStringLiteral("size"), look.size);
    map.insert(QStringLiteral("color"), look.color.name());
    map.insert(QStringLiteral("highlight"), look.background.isValid() ? look.background.name() : QString());
    const Qt::Alignment alignment = format.alignment();
    map.insert(QStringLiteral("alignment"), alignment & Qt::AlignHCenter ? QStringLiteral("center") : alignment & Qt::AlignRight ? QStringLiteral("right")
                                                : alignment & Qt::AlignJustify ? QStringLiteral("justify") : QStringLiteral("left"));
    map.insert(QStringLiteral("lineHeight"), format.lineHeightType() == QTextBlockFormat::ProportionalHeight && format.lineHeight() > 0 ? qRound(format.lineHeight()) : 100);
    map.insert(QStringLiteral("before"), qRound(format.topMargin() / Doc::pxPerPt * 10) / 10.0);
    map.insert(QStringLiteral("after"), qRound(format.bottomMargin() / Doc::pxPerPt * 10) / 10.0);
    map.insert(QStringLiteral("left"), qRound(format.leftMargin() / Doc::pxPerCm * 100) / 100.0);
    map.insert(QStringLiteral("right"), qRound(format.rightMargin() / Doc::pxPerCm * 100) / 100.0);
    map.insert(QStringLiteral("first"), qRound(format.textIndent() / Doc::pxPerCm * 100) / 100.0);
    map.insert(QStringLiteral("style"), Doc::styleOf(block));
    QTextList *list = block.textList();
    map.insert(QStringLiteral("list"), listKind(list));
    map.insert(QStringLiteral("listLevel"), list ? list->format().indent() : 0);
    QTextTable *table = m_cursor.currentTable();
    map.insert(QStringLiteral("table"), table != nullptr);
    map.insert(QStringLiteral("rows"), table ? table->rows() : 0);
    map.insert(QStringLiteral("columns"), table ? table->columns() : 0);
    const int image = selectedImage();
    map.insert(QStringLiteral("image"), image >= 0);
    if (image >= 0) {
        const QTextImageFormat picture = chars.toImageFormat();
        const QImage stored = Doc::image(m_document, picture.name());
        const double width = picture.width() > 0 ? picture.width() : stored.width();
        const double height = picture.height() > 0 ? picture.height() : stored.height();
        map.insert(QStringLiteral("imageWidth"), qRound(width / Doc::pxPerCm * 100) / 100.0);
        map.insert(QStringLiteral("imageHeight"), qRound(height / Doc::pxPerCm * 100) / 100.0);
    } else {
        map.insert(QStringLiteral("imageWidth"), 0.0);
        map.insert(QStringLiteral("imageHeight"), 0.0);
    }
    map.insert(QStringLiteral("link"), chars.isAnchor() ? chars.anchorHref() : QString());
    map.insert(QStringLiteral("selection"), m_cursor.hasSelection());
    map.insert(QStringLiteral("empty"), !m_cursor.hasSelection() && block.length() <= 1);
    map.insert(QStringLiteral("pageBreak"), bool(format.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore));
    if (map != m_format) {
        m_format = map;
        Q_EMIT formatChanged();
    }
    // The count of what is selected follows the selection.
    const bool had = m_counts.value(QStringLiteral("selectedCharacters")).toInt() > 0;
    if (m_cursor.hasSelection() || had) {
        m_refresh->start();
    }
}

QRectF Editor::paragraphRect() const
{
    const double height = m_setup.contentPx().height();
    const int page = cursorPage();
    QRectF block = m_document->documentLayout()->blockBoundingRect(m_cursor.block());
    block = block.intersected(QRectF(0, page * height, m_setup.contentPx().width(), height));
    block.translate(m_setup.left * Doc::pxPerCm, m_setup.top * Doc::pxPerCm - page * height);
    return block;
}

void Editor::refreshNow()
{
    m_refresh->stop();
    static const QRegularExpression word(QStringLiteral("[\\p{L}\\p{N}][\\p{L}\\p{N}'’\\-_.@/:]*"));
    const auto wordsIn = [](const QString &text) {
        int count = 0;
        auto it = word.globalMatch(text);
        while (it.hasNext()) {
            it.next();
            ++count;
        }
        return count;
    };
    int words = 0;
    int characters = 0;
    int bare = 0;
    int paragraphs = 0;
    QVariantList outline;
    const double pageHeight = m_setup.contentPx().height();
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        QString text = block.text();
        text.remove(QChar::ObjectReplacementCharacter);
        if (!text.trimmed().isEmpty()) {
            ++paragraphs;
        }
        words += wordsIn(text);
        characters += int(text.size());
        for (const QChar c : std::as_const(text)) {
            if (!c.isSpace()) {
                ++bare;
            }
        }
        const int level = Doc::outlineLevel(block);
        if (level >= 0 && !text.trimmed().isEmpty()) {
            const QRectF bounds = m_document->documentLayout()->blockBoundingRect(block);
            outline << QVariantMap{{QStringLiteral("text"), text.simplified()},
                                   {QStringLiteral("level"), level},
                                   {QStringLiteral("position"), block.position()},
                                   {QStringLiteral("page"), qBound(0, int((bounds.top() + 1) / pageHeight), pageCount() - 1) + 1}};
        }
    }
    QString selected = m_cursor.selectedText();
    selected.remove(QChar::ObjectReplacementCharacter);
    QString selectedBare = selected;
    selectedBare.remove(QChar::ParagraphSeparator);
    const QVariantMap counts{{QStringLiteral("words"), words},
                             {QStringLiteral("characters"), characters},
                             {QStringLiteral("charactersNoSpaces"), bare},
                             {QStringLiteral("paragraphs"), paragraphs},
                             {QStringLiteral("selectedWords"), wordsIn(selected)},
                             {QStringLiteral("selectedCharacters"), int(selectedBare.size())}};
    if (counts != m_counts) {
        m_counts = counts;
        Q_EMIT countsChanged();
    }
    if (outline != m_outline) {
        m_outline = outline;
        Q_EMIT outlineChanged();
    }
    if (!m_find.isEmpty()) {
        refreshFind();
    }
}

void Editor::paintPage(QPainter *painter, int page, bool caret) const
{
    QAbstractTextDocumentLayout::PaintContext context;
    for (int i = 0; i < m_matches.size(); ++i) {
        QAbstractTextDocumentLayout::Selection selection;
        selection.cursor = QTextCursor(m_document);
        selection.cursor.setPosition(m_matches.at(i).first);
        selection.cursor.setPosition(m_matches.at(i).second, QTextCursor::KeepAnchor);
        const bool current = m_matches.at(i).first == m_cursor.selectionStart() && m_matches.at(i).second == m_cursor.selectionEnd();
        selection.format.setBackground(current ? currentMatchColor : matchColor);
        context.selections.append(selection);
    }
    if (m_cursor.hasSelection() && matchIndex() == 0) {
        QAbstractTextDocumentLayout::Selection selection;
        selection.cursor = m_cursor;
        selection.format.setBackground(selectionColor);
        context.selections.append(selection);
    }
    Formats::paintPage(painter, m_document, m_setup, page, pageCount(), &context);
    if (caret && !m_cursor.hasSelection() && page == cursorPage()) {
        const QRectF at = caretRect();
        painter->fillRect(QRectF(at.x() - 0.5, at.y(), 1.5, at.height()), Doc::ink());
    }
}

// --- Templates --------------------------------------------------------------------

void Editor::paragraph(const QString &style, const QString &text, Qt::Alignment alignment)
{
    if (!(m_document->characterCount() <= 1 && m_cursor.block().blockNumber() == 0 && m_cursor.block().length() <= 1 && !m_started)) {
        m_cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
    }
    m_started = true;
    if (QTextList *list = m_cursor.block().textList()) {
        list->remove(m_cursor.block());
    }
    QTextBlockFormat plain;
    m_cursor.setBlockFormat(plain);
    Doc::applyStyle(m_cursor, style);
    QTextBlockFormat format;
    format.setAlignment(alignment);
    m_cursor.mergeBlockFormat(format);
    if (!text.isEmpty()) {
        m_cursor.insertText(text, Doc::style(style).chars);
    }
}

void Editor::build(const QString &kind)
{
    m_started = false;
    m_cursor = QTextCursor(m_document);
    m_cursor.beginEditBlock();
    const auto bold = [this](const QString &text) {
        QTextCharFormat format = Doc::style(QStringLiteral("body")).chars;
        format.setFontWeight(QFont::Bold);
        m_cursor.insertText(text, format);
    };
    const auto plain = [this](const QString &text) {
        m_cursor.insertText(text, Doc::style(Doc::styleOf(m_cursor.block())).chars);
    };
    const auto items = [this](int listKindWanted, const QStringList &lines) {
        QTextList *list = nullptr;
        for (const QString &line : lines) {
            paragraph(QStringLiteral("body"), line);
            if (list) {
                list->add(m_cursor.block());
            } else {
                QTextListFormat format;
                format.setStyle(listStyle(listKindWanted, 1));
                format.setIndent(1);
                list = m_cursor.createList(format);
            }
        }
    };

    if (kind == QLatin1String("letter")) {
        paragraph(QStringLiteral("body"), QStringLiteral("Nombre y apellidos"), Qt::AlignRight);
        paragraph(QStringLiteral("body"), QStringLiteral("Calle y número"), Qt::AlignRight);
        paragraph(QStringLiteral("body"), QStringLiteral("Código postal, ciudad"), Qt::AlignRight);
        paragraph(QStringLiteral("body"), QString());
        paragraph(QStringLiteral("body"), QStringLiteral("Destinatario"));
        paragraph(QStringLiteral("body"), QStringLiteral("Empresa o institución"));
        paragraph(QStringLiteral("body"), QStringLiteral("Dirección"));
        paragraph(QStringLiteral("body"), QString());
        paragraph(QStringLiteral("body"), QStringLiteral("Ciudad, a día de mes de año"), Qt::AlignRight);
        paragraph(QStringLiteral("body"), QString());
        paragraph(QStringLiteral("body"), QStringLiteral("Asunto: "));
        m_cursor.movePosition(QTextCursor::StartOfBlock);
        m_cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        QTextCharFormat strong;
        strong.setFontWeight(QFont::Bold);
        m_cursor.mergeCharFormat(strong);
        m_cursor.movePosition(QTextCursor::EndOfBlock);
        paragraph(QStringLiteral("body"), QStringLiteral("Estimada señora, estimado señor:"));
        paragraph(QStringLiteral("body"), QStringLiteral("Escriba aquí el cuerpo de la carta. Exponga el motivo en el primer párrafo, los detalles en los siguientes y lo que espera del destinatario en el último."), Qt::AlignJustify);
        paragraph(QStringLiteral("body"), QStringLiteral("Quedo a su disposición para cualquier aclaración."), Qt::AlignJustify);
        paragraph(QStringLiteral("body"), QStringLiteral("Atentamente,"));
        paragraph(QStringLiteral("body"), QString());
        paragraph(QStringLiteral("body"), QStringLiteral("Nombre y apellidos"));
    } else if (kind == QLatin1String("resume")) {
        paragraph(QStringLiteral("title"), QStringLiteral("Nombre y apellidos"));
        paragraph(QStringLiteral("body"), QStringLiteral("Puesto o profesión · ciudad · correo@ejemplo.com · 600 000 000"));
        paragraph(QStringLiteral("h2"), QStringLiteral("Perfil"));
        paragraph(QStringLiteral("body"), QStringLiteral("Dos o tres frases sobre quién es, qué sabe hacer y qué busca."), Qt::AlignJustify);
        paragraph(QStringLiteral("h2"), QStringLiteral("Experiencia"));
        paragraph(QStringLiteral("body"), QString());
        bold(QStringLiteral("Puesto, Empresa"));
        plain(QStringLiteral(" — 2022 – actualidad"));
        items(1, {QStringLiteral("Un logro concreto, con cifras si las hay."), QStringLiteral("Una responsabilidad principal."), QStringLiteral("Herramientas o métodos empleados.")});
        paragraph(QStringLiteral("body"), QString());
        bold(QStringLiteral("Puesto anterior, Empresa"));
        plain(QStringLiteral(" — 2019 – 2022"));
        items(1, {QStringLiteral("Un logro concreto."), QStringLiteral("Una responsabilidad principal.")});
        paragraph(QStringLiteral("h2"), QStringLiteral("Formación"));
        paragraph(QStringLiteral("body"), QString());
        bold(QStringLiteral("Titulación, Centro"));
        plain(QStringLiteral(" — 2015 – 2019"));
        paragraph(QStringLiteral("h2"), QStringLiteral("Idiomas y competencias"));
        items(1, {QStringLiteral("Español, nativo. Inglés, C1."), QStringLiteral("Competencias técnicas relevantes para el puesto.")});
    } else if (kind == QLatin1String("report")) {
        paragraph(QStringLiteral("title"), QStringLiteral("Título del informe"));
        paragraph(QStringLiteral("body"), QStringLiteral("Autor · Fecha"));
        paragraph(QStringLiteral("h1"), QStringLiteral("Resumen"));
        paragraph(QStringLiteral("body"), QStringLiteral("Un párrafo que cuente de qué trata el informe, qué se ha encontrado y qué se propone."), Qt::AlignJustify);
        paragraph(QStringLiteral("h1"), QStringLiteral("Introducción"));
        paragraph(QStringLiteral("body"), QStringLiteral("Contexto, alcance y objetivos del trabajo."), Qt::AlignJustify);
        paragraph(QStringLiteral("h1"), QStringLiteral("Desarrollo"));
        paragraph(QStringLiteral("h2"), QStringLiteral("Método"));
        paragraph(QStringLiteral("body"), QStringLiteral("Cómo se han obtenido los datos y con qué criterios."), Qt::AlignJustify);
        paragraph(QStringLiteral("h2"), QStringLiteral("Resultados"));
        paragraph(QStringLiteral("body"), QStringLiteral("Lo que se ha observado, con tablas o figuras si ayudan."), Qt::AlignJustify);
        paragraph(QStringLiteral("h1"), QStringLiteral("Conclusiones"));
        items(2, {QStringLiteral("Primera conclusión."), QStringLiteral("Segunda conclusión."), QStringLiteral("Siguientes pasos.")});
        m_setup.pageNumbers = true;
        m_document->setPageSize(m_setup.contentPx());
    } else if (kind == QLatin1String("minutes")) {
        paragraph(QStringLiteral("title"), QStringLiteral("Acta de la reunión"));
        paragraph(QStringLiteral("body"), QStringLiteral("Lugar · Fecha · Hora de inicio"));
        paragraph(QStringLiteral("h1"), QStringLiteral("Asistentes"));
        items(1, {QStringLiteral("Nombre y cargo"), QStringLiteral("Nombre y cargo"), QStringLiteral("Nombre y cargo")});
        paragraph(QStringLiteral("h1"), QStringLiteral("Orden del día"));
        items(2, {QStringLiteral("Lectura y aprobación del acta anterior."), QStringLiteral("Primer asunto."), QStringLiteral("Ruegos y preguntas.")});
        paragraph(QStringLiteral("h1"), QStringLiteral("Desarrollo"));
        paragraph(QStringLiteral("body"), QStringLiteral("Lo que se trató en cada punto, quién intervino y qué se propuso."), Qt::AlignJustify);
        paragraph(QStringLiteral("h1"), QStringLiteral("Acuerdos"));
        items(2, {QStringLiteral("Acuerdo, responsable y plazo."), QStringLiteral("Acuerdo, responsable y plazo.")});
        paragraph(QStringLiteral("body"), QStringLiteral("Sin más asuntos que tratar, se levanta la sesión a las 00:00."), Qt::AlignJustify);
        m_setup.pageNumbers = true;
        m_document->setPageSize(m_setup.contentPx());
    } else if (kind == QLatin1String("sample")) {
        paragraph(QStringLiteral("title"), QStringLiteral("El jardín de Wonderland"));
        paragraph(QStringLiteral("body"), QStringLiteral("Un documento de muestra con "), Qt::AlignJustify);
        bold(QStringLiteral("negrita"));
        plain(QStringLiteral(", "));
        QTextCharFormat format = Doc::style(QStringLiteral("body")).chars;
        format.setFontItalic(true);
        m_cursor.insertText(QStringLiteral("cursiva"), format);
        plain(QStringLiteral(", "));
        format = Doc::style(QStringLiteral("body")).chars;
        format.setFontUnderline(true);
        m_cursor.insertText(QStringLiteral("subrayado"), format);
        plain(QStringLiteral(", "));
        format = Doc::style(QStringLiteral("body")).chars;
        format.setFontStrikeOut(true);
        m_cursor.insertText(QStringLiteral("tachado"), format);
        plain(QStringLiteral(", texto "));
        format = Doc::style(QStringLiteral("body")).chars;
        format.setForeground(QColor(0xd7, 0x00, 0x15));
        m_cursor.insertText(QStringLiteral("de color"), format);
        plain(QStringLiteral(", "));
        format = Doc::style(QStringLiteral("body")).chars;
        format.setBackground(QColor(0xff, 0xe0, 0x66));
        m_cursor.insertText(QStringLiteral("resaltado"), format);
        plain(QStringLiteral(", fórmulas como E = mc"));
        format = Doc::style(QStringLiteral("body")).chars;
        format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
        m_cursor.insertText(QStringLiteral("2"), format);
        plain(QStringLiteral(" y H"));
        format.setVerticalAlignment(QTextCharFormat::AlignSubScript);
        m_cursor.insertText(QStringLiteral("2"), format);
        plain(QStringLiteral("O, y un enlace a "));
        format = Doc::style(QStringLiteral("body")).chars;
        format.merge(Doc::linkFormat(QStringLiteral("https://example.org/")));
        m_cursor.insertText(QStringLiteral("example.org"), format);
        plain(QStringLiteral("."));
        paragraph(QStringLiteral("h1"), QStringLiteral("Lo que crece"));
        paragraph(QStringLiteral("body"), QStringLiteral("En el jardín cada cosa tiene su sitio y su estación. Las listas ayudan a no olvidarse de ninguna:"), Qt::AlignJustify);
        items(1, {QStringLiteral("Rosas blancas, pintadas de rojo"), QStringLiteral("Setas de dos tamaños"), QStringLiteral("Un árbol con puerta")});
        paragraph(QStringLiteral("h2"), QStringLiteral("Orden de las tareas"));
        items(2, {QStringLiteral("Regar al amanecer"), QStringLiteral("Podar antes del té"), QStringLiteral("Contar las cartas de la baraja")});
        paragraph(QStringLiteral("quote"), QStringLiteral("«Empieza por el principio —dijo el Rey con gravedad— y sigue hasta llegar al final; allí te paras.»"));
        paragraph(QStringLiteral("h2"), QStringLiteral("Inventario"));
        QTextTable *table = m_cursor.insertTable(3, 3, Doc::tableFormat(3));
        dress(table, 0, 3, 0, 3);
        const QStringList cells{QStringLiteral("Planta"), QStringLiteral("Cantidad"), QStringLiteral("Estación"),
                                QStringLiteral("Rosal"),  QStringLiteral("12"),       QStringLiteral("Primavera"),
                                QStringLiteral("Seta"),   QStringLiteral("40"),       QStringLiteral("Otoño")};
        for (int i = 0; i < cells.size(); ++i) {
            QTextCursor at = table->cellAt(i / 3, i % 3).firstCursorPosition();
            QTextCharFormat cell = Doc::style(QStringLiteral("body")).chars;
            if (i < 3) {
                cell.setFontWeight(QFont::Bold);
            }
            at.insertText(cells.at(i), cell);
        }
        m_cursor.setPosition(table->lastPosition() + 1);
        m_cursor.setBlockFormat(QTextBlockFormat());
        Doc::applyStyle(m_cursor, QStringLiteral("h3"));
        m_cursor.insertText(QStringLiteral("Una figura"), Doc::style(QStringLiteral("h3")).chars);
        paragraph(QStringLiteral("body"), QString(), Qt::AlignHCenter);
        QImage picture(360, 140, QImage::Format_ARGB32_Premultiplied);
        {
            QPainter painter(&picture);
            QLinearGradient gradient(0, 0, 360, 140);
            gradient.setColorAt(0, QColor(0x2f, 0x6f, 0xed));
            gradient.setColorAt(1, QColor(0x8e, 0x5c, 0xf6));
            painter.fillRect(picture.rect(), gradient);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setBrush(QColor(255, 255, 255, 220));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(QPointF(90, 70), 38, 38);
            painter.drawRoundedRect(QRectF(170, 40, 140, 60), 12, 12);
        }
        QTextImageFormat image;
        image.setName(Doc::addImage(m_document, picture));
        image.setWidth(360);
        image.setHeight(140);
        m_cursor.insertImage(image);
        paragraph(QStringLiteral("h1"), QStringLiteral("Segunda página"));
        QTextBlockFormat breakBefore;
        breakBefore.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
        m_cursor.mergeBlockFormat(breakBefore);
        paragraph(QStringLiteral("body"), QStringLiteral("Este párrafo empieza en una hoja nueva gracias a un salto de página. El pie lleva el número de cada una."), Qt::AlignJustify);
        paragraph(QStringLiteral("code"), QStringLiteral("for (carta in baraja) {"));
        paragraph(QStringLiteral("code"), QStringLiteral("    pintar(carta, \"rojo\")"));
        paragraph(QStringLiteral("code"), QStringLiteral("}"));
        m_setup.header = QStringLiteral("El jardín de Wonderland");
        m_setup.pageNumbers = true;
        m_document->setPageSize(m_setup.contentPx());
    } else {
        Doc::applyStyle(m_cursor, QStringLiteral("body"));
    }
    m_cursor.endEditBlock();
}
