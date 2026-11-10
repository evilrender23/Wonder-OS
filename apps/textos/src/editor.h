#pragma once

#include "docmodel.h"

#include <QObject>
#include <QRectF>
#include <QTextCursor>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class QPainter;
class QTextDocument;
class QTextTable;
class QTimer;

/// One document being written: the text, the cursor in it, and everything the
/// window can do to either. It knows nothing of the window — keys and clicks
/// reach it as numbers — so the tests drive it as the window does.
class Editor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("App.editor is the one")
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    /// The file's name, or «Sin título».
    Q_PROPERTY(QString name READ name NOTIFY pathChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    /// The file's format does not keep everything the document has (text, Markdown).
    Q_PROPERTY(bool lossy READ lossy NOTIFY pathChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY pagesChanged)
    Q_PROPERTY(QVariantMap setup READ setupMap NOTIFY setupChanged)
    /// The sheet, in layout pixels (96 to the inch).
    Q_PROPERTY(qreal pageWidth READ pageWidth NOTIFY setupChanged)
    Q_PROPERTY(qreal pageHeight READ pageHeight NOTIFY setupChanged)
    /// {words, characters, charactersNoSpaces, paragraphs, selectedWords, selectedCharacters}
    Q_PROPERTY(QVariantMap counts READ counts NOTIFY countsChanged)
    /// [{text, level (0 the title, 1–3), position, page}]
    Q_PROPERTY(QVariantList outline READ outline NOTIFY outlineChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    /// What is under the cursor: bold, italic, underline, strike, vertical,
    /// family, size, color, highlight, alignment, lineHeight, before, after,
    /// left, right, first, style, list, listLevel, table, rows, columns,
    /// image, imageWidth, imageHeight, link, selection, pageBreak, empty
    /// (the paragraph has nothing in it yet).
    Q_PROPERTY(QVariantMap format READ format NOTIFY formatChanged)
    Q_PROPERTY(int cursorPage READ cursorPage NOTIFY cursorMoved)
    Q_PROPERTY(int position READ position NOTIFY cursorMoved)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY findChanged)
    /// From 1; 0 when the cursor is on none.
    Q_PROPERTY(int matchIndex READ matchIndex NOTIFY findChanged)
    /// [{id, name, family, size, bold, italic}] for the panel of styles.
    Q_PROPERTY(QVariantList styles READ styles CONSTANT)
    Q_PROPERTY(QStringList papers READ papers CONSTANT)
    /// «# », «- », «1. », «> », **bold** and ``` turn into what they mean as they are typed.
    Q_PROPERTY(bool markdown MEMBER m_markdown NOTIFY markdownChanged)

public:
    explicit Editor(QObject *parent = nullptr);
    ~Editor() override;

    QTextDocument *document() const { return m_document; }
    QTextCursor &cursor() { return m_cursor; }
    const Doc::PageSetup &setup() const { return m_setup; }

    QString path() const { return m_path; }
    QString name() const;
    bool modified() const;
    bool lossy() const;
    int pageCount() const;
    QVariantMap setupMap() const { return m_setup.toMap(); }
    qreal pageWidth() const { return m_setup.pagePx().width(); }
    qreal pageHeight() const { return m_setup.pagePx().height(); }
    QVariantMap counts() const { return m_counts; }
    QVariantList outline() const { return m_outline; }
    bool canUndo() const;
    bool canRedo() const;
    QVariantMap format() const { return m_format; }
    int cursorPage() const;
    int position() const { return m_cursor.position(); }
    int matchCount() const { return int(m_matches.size()); }
    int matchIndex() const;
    QVariantList styles() const;
    QStringList papers() const { return Doc::PageSetup::papers(); }
    /// Counts up with every change to the text: what autosave watches.
    int revision() const { return m_revision; }

    // --- Files ---
    /// blank, letter, resume, report, minutes or sample.
    Q_INVOKABLE void newDocument(const QString &kind = QStringLiteral("blank"));
    Q_INVOKABLE bool load(const QString &path);
    /// Reads a file as the document without making it the document's file (recovery).
    bool loadAs(const QString &file, const QString &path);
    /// False when the document has no file yet.
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QString &path);
    /// Writes a copy (a PDF, another format) and leaves the document as it is.
    Q_INVOKABLE bool exportTo(const QString &path);
    Q_INVOKABLE void setSetup(const QVariantMap &setup);
    Q_INVOKABLE QString plainText() const;

    // --- Characters ---
    Q_INVOKABLE void toggleBold();
    Q_INVOKABLE void toggleItalic();
    Q_INVOKABLE void toggleUnderline();
    Q_INVOKABLE void toggleStrike();
    /// 0 on the line, 1 superscript, 2 subscript.
    Q_INVOKABLE void setVertical(int vertical);
    Q_INVOKABLE void setFamily(const QString &family);
    Q_INVOKABLE void setPointSize(double size);
    Q_INVOKABLE void setColor(const QColor &color);
    /// An invalid colour takes the highlight away.
    Q_INVOKABLE void setHighlight(const QColor &color);
    Q_INVOKABLE void clearHighlight() { setHighlight(QColor()); }

    // --- Paragraphs ---
    /// left, center, right or justify.
    Q_INVOKABLE void setAlignment(const QString &alignment);
    /// Per cent of a line.
    Q_INVOKABLE void setLineHeight(int percent);
    /// Points before and after.
    Q_INVOKABLE void setSpacing(double before, double after);
    /// Centimetres.
    Q_INVOKABLE void setIndents(double left, double right, double first);
    Q_INVOKABLE void applyStyle(const QString &id);
    /// 0 none, 1 bullets, 2 numbers; the one it already is takes it away.
    Q_INVOKABLE void setList(int kind);
    Q_INVOKABLE void indentMore();
    Q_INVOKABLE void indentLess();

    // --- Things in the text ---
    Q_INVOKABLE void insertText(const QString &text);
    Q_INVOKABLE void insertTable(int rows, int columns);
    Q_INVOKABLE void addRow(bool below = true);
    Q_INVOKABLE void addColumn(bool after = true);
    Q_INVOKABLE void removeRow();
    Q_INVOKABLE void removeColumn();
    Q_INVOKABLE void removeTable();
    Q_INVOKABLE bool insertImage(const QString &path);
    bool insertImage(const QImage &image);
    /// Centimetres; the height follows.
    Q_INVOKABLE void setImageWidth(double width);
    /// With a selection, makes it a link; without, writes `text` (or the address) as one.
    Q_INVOKABLE void setLink(const QString &href, const QString &text = QString());
    Q_INVOKABLE void removeLink();
    Q_INVOKABLE void insertPageBreak();
    /// A centred «* * *» between two paragraphs.
    Q_INVOKABLE void insertSeparator();

    // --- Editing ---
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE QString selectedText() const;
    /// A key, as Qt names it. False when it is not the editor's.
    Q_INVOKABLE bool key(int key, int modifiers, const QString &text);

    // --- The mouse, in layout pixels from the corner of a sheet ---
    Q_INVOKABLE void press(int page, qreal x, qreal y, bool extend = false);
    Q_INVOKABLE void drag(int page, qreal x, qreal y);
    Q_INVOKABLE void selectWord(int page, qreal x, qreal y);
    Q_INVOKABLE void selectParagraph(int page, qreal x, qreal y);
    /// The address of the link there, if any.
    Q_INVOKABLE QString linkAt(int page, qreal x, qreal y) const;
    Q_INVOKABLE void goTo(int position);
    Q_INVOKABLE void select(int from, int to);
    /// Where the cursor is, on the sheet of `cursorPage`.
    Q_INVOKABLE QRectF caretRect() const;
    /// The paragraph the cursor is in, as much of it as is on that sheet.
    Q_INVOKABLE QRectF paragraphRect() const;

    // --- Find ---
    Q_INVOKABLE void setFind(const QString &text, bool caseSensitive = false, bool wholeWords = false);
    Q_INVOKABLE void findNext(bool forward = true);
    Q_INVOKABLE void replaceOne(const QString &with);
    Q_INVOKABLE int replaceAll(const QString &with);

    /// Draws a sheet's text, selection and (if `caret`) cursor. The paper is the caller's.
    void paintPage(QPainter *painter, int page, bool caret) const;
    /// Counts and outline are worked out a moment after the text changes; this does it now.
    Q_INVOKABLE void refreshNow();

Q_SIGNALS:
    void pathChanged();
    void markdownChanged();
    void modifiedChanged();
    void pagesChanged();
    void setupChanged();
    void countsChanged();
    void outlineChanged();
    void undoChanged();
    void formatChanged();
    void cursorMoved();
    void findChanged();
    /// Something on the sheets looks different.
    void repaint();
    /// The document is another one: every sheet, from the top.
    void replaced();
    void said(const QString &message);

private:
    void adopt(QTextDocument *document);
    void touched();
    void moved();
    void refreshFormat();
    void refreshFind();
    int positionAt(int page, qreal x, qreal y) const;
    QPointF pointAt(int page, qreal x, qreal y) const;
    QRectF caretInDocument(int position) const;
    /// The position of the picture the selection is, or -1.
    int selectedImage() const;
    void mergeChars(const QTextCharFormat &format);
    void mergeBlocks(const QTextBlockFormat &format);
    void relist(int kind, int shift);
    void newParagraph();
    void typed(const QString &text);
    void shorthand(QChar last);
    bool backspace(bool word);
    void dress(QTextTable *table, int row, int rows, int column, int columns);
    void even(QTextTable *table);
    void paragraph(const QString &style, const QString &text, Qt::Alignment alignment = Qt::AlignLeft);
    void build(const QString &kind);

    QTextDocument *m_document = nullptr;
    QTextCursor m_cursor;
    Doc::PageSetup m_setup;
    QTimer *m_refresh;
    QString m_path;
    QVariantMap m_format;
    QVariantMap m_counts;
    QVariantList m_outline;
    QString m_find;
    bool m_findCase = false;
    bool m_findWords = false;
    QList<std::pair<int, int>> m_matches;
    int m_revision = 0;
    int m_pages = 1;
    bool m_markdown = true;
    bool m_setupDirty = false;
    bool m_loading = false;
    bool m_started = false;
};
