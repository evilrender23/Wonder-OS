#pragma once

#include "doc.h"

#include <QColor>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QVariantMap>

class QTextDocument;
class QTextList;

/// The colours a page is drawn with: they belong to the theme, not to the page.
struct Style {
    bool dark = false;
    QColor link;
    QColor codeBack;
    QColor quoteBack;
    QColor border;
    static Style of(bool dark);
};

/// Between a Doc and the QTextDocument the editor works on.
namespace RichDoc {

/// Fills the document with the page; what was there is gone, and so is its undo history.
void write(const Doc &doc, QTextDocument *document, const Style &style);
/// Reads the page back. `headingShift` raises the headings: with 1, an <h2> is «Encabezado».
Doc read(const QTextDocument *document, int headingShift = 0);

Block::Type typeOf(const QTextBlock &block, int headingShift = 0);
Run runOf(const QTextCharFormat &format, Block::Type type);
QTextCharFormat charFormat(const Run &run, Block::Type type, const Style &style);
QTextBlockFormat blockFormat(Block::Type type, bool done, const Style &style);

}

/// What the editor does to a page, on a QTextDocument: every method takes
/// positions and answers positions, so it can be driven without a window.
class RichText
{
public:
    RichText(QTextDocument *document, const Style &style);

    /// bold, italic, underline, strike or mono over a selection (or the word at the cursor).
    void toggle(int start, int end, const QString &what);
    /// "" takes the colour away.
    void setColor(int start, int end, const QString &color);
    void setHighlight(int start, int end, const QString &color);
    void clearLook(int start, int end);

    /// Turns the paragraphs of the selection into `type`; when they all are already, back into plain ones.
    void setType(int start, int end, Block::Type type, bool toggle = true);
    Block::Type typeAt(int position) const;
    /// Ticks or unticks the task at the position; false when there is none.
    bool toggleDone(int position);
    /// Nests list items deeper (1) or shallower (-1).
    void indent(int start, int end, int delta);

    /// Answers where the cursor goes: the first cell.
    int insertTable(int position, int rows, int columns);
    /// row-above, row-below, column-left, column-right, delete-row, delete-column, delete-table.
    bool editTable(int position, const QString &what);
    /// The next (or previous) cell, adding a row after the last; -1 outside a table.
    int nextCell(int position, bool backwards);

    /// Puts a link over the selection (or inserts `text`); answers the position after it.
    int setLink(int start, int end, const QString &text, const QString &href);
    /// {href, text, start, end} of the link at the position, or empty.
    QVariantMap linkAt(int position) const;
    void removeLink(int position);

    int insertImage(int position, const QString &source, int width, int height);
    /// {source, width, height, position} of the picture at or just before the position.
    QVariantMap imageAt(int position) const;
    void replaceImage(int position, const QString &source, int width, int height);
    int insertText(int start, int end, const QString &text);

    /// After a character was typed, with the cursor at `position`: Markdown
    /// shortcuts («# », «- », «[] », «```», «[[Page]]»). Answers the new cursor, or -1.
    int autoformat(int position);
    /// Return pressed with no selection. Answers the new cursor, or -1 to let the editor do it.
    int newline(int position);
    /// Backspace at the start of a list item or a heading makes it a plain paragraph.
    int backspace(int position);

    /// What the bar shows as pressed: {type, bold, italic, underline, strike, mono, color, highlight, link, table, image, done}.
    QVariantMap lookAt(int start, int end) const;

private:
    struct Piece {
        int start;
        int length;
        QTextCharFormat format;
        Block::Type type;
    };
    QList<Piece> pieces(int start, int end) const;
    void restyle(int start, int end, const std::function<void(Run &)> &change);
    void retype(const QTextBlock &block, Block::Type type, bool done);
    void joinList(const QTextBlock &block, Block::Type type, int indent);
    void wordAt(int &start, int &end) const;

    QTextDocument *m_document;
    Style m_style;
};
