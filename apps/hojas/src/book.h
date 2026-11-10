#pragma once

#include "workbook.h"

#include <QObject>
#include <QVariant>

/// The document the window shows: a workbook, the sheet in sight, and
/// everything the interface asks of them, in plain types. No window in here.
class Book : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool open READ isOpen NOTIFY documentChanged)
    Q_PROPERTY(QString title READ title NOTIFY documentChanged)
    Q_PROPERTY(QString path READ path NOTIFY documentChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(int sheet READ sheet WRITE setSheet NOTIFY changed)
    Q_PROPERTY(QStringList sheets READ sheets NOTIFY changed)
    Q_PROPERTY(int rows READ rows NOTIFY changed)
    Q_PROPERTY(int cols READ cols NOTIFY changed)
    Q_PROPERTY(int freezeRows READ freezeRows NOTIFY changed)
    Q_PROPERTY(int freezeCols READ freezeCols NOTIFY changed)
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY changed)

public:
    /// `dataDir` holds the versions and the recovery copies.
    explicit Book(const QString &dataDir, QObject *parent = nullptr);

    Workbook &workbook() { return m_book; }
    bool isOpen() const { return m_open; }
    QString title() const { return m_title; }
    QString path() const { return m_path; }
    bool modified() const { return m_open && m_book.modified(); }
    int revision() const { return m_revision; }
    bool canUndo() const { return m_book.canUndo(); }
    bool canRedo() const { return m_book.canRedo(); }
    int sheet() const { return m_sheet; }
    void setSheet(int index);
    QStringList sheets() const;
    int rows() const { return m_rows; }
    int cols() const { return m_cols; }
    int freezeRows() const;
    int freezeCols() const;
    double zoom() const { return m_zoom; }
    void setZoom(double zoom);

    // --- The document
    /// A new workbook from a template: blank, budget, invoice, tasks or schedule.
    Q_INVOKABLE void create(const QString &kind = QStringLiteral("blank"));
    /// Reads .xlsx, .ods, .csv, .tsv and .txt. An error message in Spanish, or nothing.
    Q_INVOKABLE QString load(const QString &path);
    /// Writes where it was read from, and keeps a version. An error message, or nothing.
    Q_INVOKABLE QString save();
    Q_INVOKABLE QString saveAs(const QString &path);
    /// A copy in another format (.xlsx, .ods, .csv, .tsv, .pdf); the document stays where it is.
    Q_INVOKABLE QString exportTo(const QString &path);
    Q_INVOKABLE void close();
    /// Renames the document, and its file if it has one.
    Q_INVOKABLE QString rename(const QString &title);
    /// The extension the document is kept in: «xlsx» until saved as something else.
    Q_INVOKABLE QString format() const;
    /// What each template looks like, for its miniature: rows of {text, fill, bold, color}.
    Q_INVOKABLE QVariantList templatePreview(const QString &kind);
    /// Made-up contents that show most of what a sheet can do.
    Q_INVOKABLE void sample();

    // --- Versions and recovery
    Q_INVOKABLE QVariantList versions() const;
    Q_INVOKABLE QVariantList versionPreview(const QString &file) const;
    Q_INVOKABLE QString restoreVersion(const QString &file);
    Q_INVOKABLE void saveRecovery();
    Q_INVOKABLE QVariantList recoveries() const;
    Q_INVOKABLE QString recover(const QString &file);
    Q_INVOKABLE void discardRecovery(const QString &file);

    // --- Sheets
    Q_INVOKABLE void addSheet();
    Q_INVOKABLE bool removeSheet(int index);
    Q_INVOKABLE bool renameSheet(int index, const QString &name);
    Q_INVOKABLE void duplicateSheet(int index);
    Q_INVOKABLE void moveSheet(int from, int to);

    // --- Geometry, in pixels at the current zoom
    Q_INVOKABLE double colWidth(int col) const;
    Q_INVOKABLE double rowHeight(int row) const;
    Q_INVOKABLE double colPos(int col) const;
    Q_INVOKABLE double rowPos(int row) const;
    Q_INVOKABLE int colAt(double x) const;
    Q_INVOKABLE int rowAt(double y) const;
    /// Makes sure the grid reaches a cell, growing it when the cursor nears the end.
    Q_INVOKABLE void reach(int row, int col);
    Q_INVOKABLE void setColWidth(int c1, int c2, double pixels);
    Q_INVOKABLE void setRowHeight(int r1, int r2, double pixels);
    Q_INVOKABLE void fitCols(int c1, int c2);
    Q_INVOKABLE void setColsHidden(int c1, int c2, bool hidden);
    Q_INVOKABLE void setRowsHidden(int r1, int r2, bool hidden);
    Q_INVOKABLE void setFreeze(int rows, int cols);
    /// The next cell that is not hidden, going `step` from a row or column.
    Q_INVOKABLE int nextRow(int row, int step) const;
    Q_INVOKABLE int nextCol(int col, int step) const;
    /// Where Ctrl+arrow lands: the edge of the block of filled cells.
    Q_INVOKABLE QVariantMap jump(int row, int col, int dr, int dc) const;

    // --- Cells
    Q_INVOKABLE QString input(int row, int col) const;
    Q_INVOKABLE QString display(int row, int col);
    Q_INVOKABLE QString setInput(int row, int col, const QString &typed);
    /// The same input in every cell of an area (Ctrl+Intro).
    Q_INVOKABLE QString setInputs(int r1, int c1, int r2, int c2, const QString &typed);
    /// What the inspector shows of a cell.
    Q_INVOKABLE QVariantMap info(int row, int col);
    /// The whole merge a cell belongs to: {r1, c1, r2, c2}.
    Q_INVOKABLE QVariantMap extent(int row, int col) const;
    Q_INVOKABLE void toggleCheck(int row, int col);
    /// What a click on a cell may hit: «check» for a casilla that can be ticked,
    /// «filter» for the button of a filter, or nothing.
    Q_INVOKABLE QString widgetAt(int row, int col);
    Q_INVOKABLE QString name(int r1, int c1, int r2, int c2) const;
    /// «B3» or «A1:C9» as {ok, r1, c1, r2, c2}.
    Q_INVOKABLE QVariantMap parseName(const QString &text) const;
    Q_INVOKABLE QString columnName(int col) const;
    /// The block of filled cells around a cell.
    Q_INVOKABLE QVariantMap region(int row, int col) const;
    Q_INVOKABLE QVariantMap usedArea() const;

    // --- Format
    /// bold, italic, underline, strike, wrap (bool); color, fill (#rrggbb or empty);
    /// align, valign, size (int).
    Q_INVOKABLE void setStyle(int r1, int c1, int r2, int c2, const QString &key, const QVariant &value);
    /// general, number, currency, percent, date, time, scientific or text.
    Q_INVOKABLE void setFormat(int r1, int c1, int r2, int c2, const QString &family);
    Q_INVOKABLE void setFormatCode(int r1, int c1, int r2, int c2, const QString &code);
    Q_INVOKABLE void changeDecimals(int r1, int c1, int r2, int c2, int delta);
    Q_INVOKABLE void setBorders(int r1, int c1, int r2, int c2, const QString &which, const QString &color = QString());
    Q_INVOKABLE void clear(int r1, int c1, int r2, int c2, bool contents, bool formats);
    Q_INVOKABLE bool merge(int r1, int c1, int r2, int c2);
    Q_INVOKABLE bool unmerge(int r1, int c1, int r2, int c2);
    Q_INVOKABLE void addConditional(int r1, int c1, int r2, int c2, const QString &op, const QString &a, const QString &b, const QString &fill, const QString &color, bool bold);
    Q_INVOKABLE void clearConditionals(int r1, int c1, int r2, int c2);
    Q_INVOKABLE QVariantList conditionals() const;
    Q_INVOKABLE void removeConditional(int index);

    // --- Clipboard and filling
    /// Lifts an area and gives it as text with tabs, for the clipboard.
    Q_INVOKABLE QString copy(int r1, int c1, int r2, int c2, bool cut);
    /// Puts down what was copied here when `text` is still it (formulas follow),
    /// or the text itself. `what`: all, values or formats. Gives the area filled.
    Q_INVOKABLE QVariantMap paste(int r1, int c1, int r2, int c2, const QString &text, const QString &what = QStringLiteral("all"));
    /// The area lifted by the last copy on this sheet, for the marching ants; empty when none.
    Q_INVOKABLE QVariantMap copied() const;
    Q_INVOKABLE void forgetCopy();
    Q_INVOKABLE void fillDown(int r1, int c1, int r2, int c2);
    Q_INVOKABLE void fillRight(int r1, int c1, int r2, int c2);
    Q_INVOKABLE void fillSeries(int sr1, int sc1, int sr2, int sc2, int r1, int c1, int r2, int c2);

    // --- Rows and columns
    Q_INVOKABLE void insertRows(int at, int count);
    Q_INVOKABLE void deleteRows(int at, int count);
    Q_INVOKABLE void insertCols(int at, int count);
    Q_INVOKABLE void deleteCols(int at, int count);
    /// text, number, date, check or nothing: what a column mostly holds.
    Q_INVOKABLE QString columnType(int col);
    /// Turns the cells of a column (under its header) into a type.
    Q_INVOKABLE void setColumnType(int col, const QString &type);

    // --- Data
    Q_INVOKABLE void sort(int r1, int c1, int r2, int c2, int column, bool ascending);
    Q_INVOKABLE bool hasFilter() const;
    Q_INVOKABLE void toggleFilter(int r1, int c1, int r2, int c2);
    /// [{text, on}] for the column of a filter (an absolute column).
    Q_INVOKABLE QVariantList filterValues(int column);
    Q_INVOKABLE void setFilterAllowed(int column, const QStringList &allowed, bool all);
    /// {found, row, col}
    Q_INVOKABLE QVariantMap find(const QString &text, int row, int col, bool forward, bool matchCase, bool whole);
    Q_INVOKABLE bool replaceAt(int row, int col, const QString &text, const QString &with, bool matchCase, bool whole);
    Q_INVOKABLE int replaceAll(const QString &text, const QString &with, bool matchCase, bool whole);
    /// {count, numbers, sum, average, min, max} and the same as text.
    Q_INVOKABLE QVariantMap stats(int r1, int c1, int r2, int c2);
    /// Writes SUM, AVERAGE, COUNT… under each column of an area (or beside a
    /// single row) and gives the cells written: {r1, c1, r2, c2}.
    Q_INVOKABLE QVariantMap aggregate(int r1, int c1, int r2, int c2, const QString &function);

    // --- Tables
    Q_INVOKABLE bool makeTable(int r1, int c1, int r2, int c2);
    /// {index, r1, c1, r2, c2, totals}, or empty when the cell is in none.
    Q_INVOKABLE QVariantMap tableAt(int row, int col) const;
    Q_INVOKABLE void removeTable(int index);
    Q_INVOKABLE void setTableTotals(int index, bool on);

    // --- Charts
    /// [{id, type, title, legend, x, y, width, height, range}]
    Q_INVOKABLE QVariantList charts() const;
    Q_INVOKABLE int addChart(int r1, int c1, int r2, int c2, const QString &type);
    /// Changes type, title, legend, headerRow, headerCol, range, width or height.
    Q_INVOKABLE void setChart(int id, const QString &key, const QVariant &value);
    /// Moves a chart so its corner sits on the cell under a point (pixels of the sheet).
    Q_INVOKABLE void moveChart(int id, double x, double y);
    Q_INVOKABLE void removeChart(int id);
    bool chart(int id, Chart *chart) const;

    // --- Formulas
    /// Every function: {name, spanish, signature, help}.
    Q_INVOKABLE QVariantList functions() const;
    /// The functions a few typed letters may mean: [{name, other, signature, help}].
    Q_INVOKABLE QVariantList completions(const QString &prefix) const;
    /// Where the cursor is in a formula: {token, tokenStart} for the name being
    /// typed, and {name, signature, help, argument} for the call it is inside.
    Q_INVOKABLE QVariantMap formulaContext(const QString &text, int cursor) const;
    /// The references of a formula being typed: [{start, length, color, here, r1, c1, r2, c2}].
    Q_INVOKABLE QVariantList references(const QString &text) const;

    // --- History
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    /// Several changes that undo as one.
    Q_INVOKABLE void beginGroup();
    Q_INVOKABLE void endGroup();


Q_SIGNALS:
    void documentChanged();
    void changed();
    /// Sizes, hidden rows or the zoom moved: the views lay out again.
    void layoutChanged();

private:
    void touch();
    void opened(const QString &path, const QString &title);
    void fitExtent();
    Area area(int r1, int c1, int r2, int c2) const;
    QString write(const QString &path);
    QString read(Workbook &into, const QString &path) const;
    QString versionsDir() const;
    void keepVersion(const QString &path);
    QString exportPdf(const QString &path);
    void build(Workbook &book, const QString &kind);

    Workbook m_book;
    QString m_dataDir;
    QString m_path;
    QString m_title;
    QString m_session;
    Clip m_clip;
    QHash<int, QString> m_types;
    bool m_open = false;
    int m_sheet = 0;
    int m_rows = 120;
    int m_cols = 30;
    int m_revision = 0;
    double m_zoom = 1.0;
};
