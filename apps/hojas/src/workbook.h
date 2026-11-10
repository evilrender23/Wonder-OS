#pragma once

#include "formula.h"

#include <QHash>
#include <QMap>
#include <QSet>
#include <QStringList>

/// How a cell looks.
struct Style {
    /// The number format's code; empty is General.
    QString format;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    /// #rrggbb, or empty for the theme's.
    QString color;
    QString fill;
    /// 0 as the value asks (numbers right, text left), 1 left, 2 centre, 3 right.
    int align = 0;
    /// 0 bottom, 1 middle, 2 top.
    int valign = 0;
    bool wrap = false;
    /// Bits: 1 left, 2 top, 4 right, 8 bottom.
    int borders = 0;
    QString borderColor;
    /// Points; 0 is the default size.
    int size = 0;

    bool operator==(const Style &o) const
    {
        return format == o.format && bold == o.bold && italic == o.italic && underline == o.underline && strike == o.strike && color == o.color && fill == o.fill
            && align == o.align && valign == o.valign && wrap == o.wrap && borders == o.borders && borderColor == o.borderColor && size == o.size;
    }
};
size_t qHash(const Style &style, size_t seed = 0);

struct Cell {
    Value value;
    NodePtr formula;
    int style = 0;

    bool blank() const { return value.isEmpty() && !formula && style == 0; }
};

/// A rectangle of cells with no sheet attached.
struct Area {
    int r1 = 0;
    int c1 = 0;
    int r2 = 0;
    int c2 = 0;

    int rows() const { return r2 - r1 + 1; }
    int cols() const { return c2 - c1 + 1; }
    bool contains(int row, int col) const { return row >= r1 && row <= r2 && col >= c1 && col <= c2; }
    bool intersects(const Area &o) const { return r1 <= o.r2 && r2 >= o.r1 && c1 <= o.c2 && c2 >= o.c1; }
    bool operator==(const Area &o) const { return r1 == o.r1 && c1 == o.c1 && r2 == o.r2 && c2 == o.c2; }
};

/// A rule that paints the cells of an area when their value passes a test.
struct CondFormat {
    Area area;
    /// «>», «<», «>=», «<=», «=», «<>», «between», «contains», «duplicate», «top», «bottom».
    QString op;
    QString a;
    QString b;
    QString fill;
    QString color;
    bool bold = false;

    bool operator==(const CondFormat &o) const
    {
        return area == o.area && op == o.op && a == o.a && b == o.b && fill == o.fill && color == o.color && bold == o.bold;
    }
};

/// What may be typed in the cells of an area.
struct Validation {
    Area area;
    /// «list», «number», «integer», «date», «length».
    QString kind;
    /// The bounds, as typed; either may be empty.
    QString min;
    QString max;
    /// The choices of a list.
    QStringList items;
    QString message;

    bool operator==(const Validation &o) const
    {
        return area == o.area && kind == o.kind && min == o.min && max == o.max && items == o.items && message == o.message;
    }
};

struct Chart {
    int id = 0;
    /// «column», «bar», «line», «area», «pie», «scatter».
    QString type = QStringLiteral("column");
    QString title;
    /// The cells it draws, on its own sheet.
    Area area;
    bool legend = true;
    /// Whether the first row names the series, and the first column the categories.
    bool headerRow = true;
    bool headerCol = true;
    /// Where it sits: the cell of its corner, and its size in pixels.
    int row = 0;
    int col = 0;
    double width = 480;
    double height = 300;

    bool operator==(const Chart &o) const
    {
        return id == o.id && type == o.type && title == o.title && area == o.area && legend == o.legend && headerRow == o.headerRow && headerCol == o.headerCol
            && row == o.row && col == o.col && width == o.width && height == o.height;
    }
};

/// The numbers a chart draws, ready for the painter.
struct ChartData {
    QStringList categories;
    QStringList seriesNames;
    /// One list per series; NaN where there is no number.
    QList<QList<double>> series;
    /// The categories as numbers, for scatter charts; NaN where they are not.
    QList<double> xs;
};

/// A range turned into a table: a header row, banded rows, filters and,
/// when asked, a row of totals as its last one.
struct Table {
    Area area;
    bool totals = false;

    bool operator==(const Table &o) const { return area == o.area && totals == o.totals; }
};

struct Sheet {
    QString name;
    QMap<int, QMap<int, Cell>> rows;
    /// Sizes in pixels of the columns and rows that are not the default.
    QMap<int, double> colWidths;
    QMap<int, double> rowHeights;
    QSet<int> hiddenRows;
    QSet<int> hiddenCols;
    QList<Area> merges;
    int freezeRows = 0;
    int freezeCols = 0;
    QList<CondFormat> conditionals;
    QList<Validation> validations;
    QList<Table> tables;
    QList<Chart> charts;
    /// The autofilter: its area (headers on the first row), what each column
    /// lets through (by offset in the area), and the rows it hides.
    bool hasFilter = false;
    Area filterArea;
    QMap<int, QSet<QString>> filterAllowed;
    QSet<int> filteredRows;
};

/// Cells lifted from a sheet, to be put down elsewhere.
struct Clip {
    int rows = 0;
    int cols = 0;
    int sheet = 0;
    int row = 0;
    int col = 0;
    QList<Cell> cells;
    /// The same as text, a row per line with tabs between cells.
    QString tsv;
    bool cut = false;

    bool isEmpty() const { return rows == 0 || cols == 0; }
};

struct SelectionStats {
    double sum = 0;
    double average = 0;
    double min = 0;
    double max = 0;
    int numbers = 0;
    int count = 0;
};

/// A workbook: sheets of cells, their formulas kept calculated, and a history
/// to undo. No GUI in here.
class Workbook : public EvalHost
{
public:
    static constexpr double DefaultColWidth = 88;
    static constexpr double DefaultRowHeight = 24;

    Workbook();

    /// Empties it down to one blank sheet, with no history.
    void clear();

    // --- Sheets
    int sheetCount() const { return m_sheets.size(); }
    const Sheet &sheet(int index) const { return m_sheets.at(index); }
    QString sheetName(int index) const { return m_sheets.at(index).name; }
    int sheetIndex(const QString &name) const;
    /// Adds a sheet at the end (or at `at`) and gives its index. An empty name picks «Hoja N».
    int addSheet(const QString &name = QString(), int at = -1);
    bool removeSheet(int index);
    /// False when the name is empty, taken or has characters a name cannot have.
    bool renameSheet(int index, const QString &name);
    bool moveSheet(int from, int to);
    int duplicateSheet(int index);

    // --- Cells
    const Cell *cell(int sheet, int row, int col) const;
    Value value(int sheet, int row, int col);
    /// What the cell shows, formatted. `color` gets the colour the format asks for, if any.
    QString display(int sheet, int row, int col, QString *color = nullptr);
    /// What the editor shows: «=SUMA(A1:A3)» or the value as it would be typed.
    QString input(int sheet, int row, int col) const;
    /// The formula in the canonical way, without «=»; empty when there is none.
    QString formula(int sheet, int row, int col) const;
    /// Types something into a cell. An error message in Spanish comes back when
    /// it cannot be taken (a formula that makes no sense, a validation that fails).
    QString setInput(int sheet, int row, int col, const QString &typed);
    /// For loaders: puts a value or a formula (canonical, without «=») with its
    /// cached value, without history and without calculating.
    void loadCell(int sheet, int row, int col, const Value &value, const QString &formula, int style);
    void finishLoading();
    /// For loaders too: a sheet to fill in directly (sizes, merges, rules), before finishLoading().
    Sheet &loaderSheet(int index) { return m_sheets[index]; }

    // --- Styles
    const Style &style(int id) const { return m_styles.at(id); }
    int styleId(const Style &style);
    const Style &cellStyle(int sheet, int row, int col) const;
    /// The style with the conditional formats that apply laid over it.
    Style effectiveStyle(int sheet, int row, int col);
    void changeStyle(int sheet, const Area &area, const std::function<void(Style &)> &change);
    /// Borders around and inside an area: «all», «outline», «none», «bottom», «top», «left», «right».
    void setBorders(int sheet, const Area &area, const QString &which, const QString &color = QString());

    // --- Areas
    void clearArea(int sheet, const Area &area, bool contents = true, bool formats = false);
    Clip copy(int sheet, const Area &area);
    /// Puts a clip down with its corner at a cell; an area that is a multiple
    /// of the clip gets it repeated. `what`: «all», «values» or «formats».
    Area paste(const Clip &clip, int sheet, const Area &target, const QString &what = QStringLiteral("all"));
    /// Text with tabs and lines, as other programs put on the clipboard.
    Area pasteText(int sheet, int row, int col, const QString &text);
    void fillDown(int sheet, const Area &area);
    void fillRight(int sheet, const Area &area);
    /// Continues the series of the first cells (1, 2 → 3, 4…) over the area.
    void fillSeries(int sheet, const Area &source, const Area &target);
    void insertRows(int sheet, int at, int count);
    void deleteRows(int sheet, int at, int count);
    void insertCols(int sheet, int at, int count);
    void deleteCols(int sheet, int at, int count);
    void sort(int sheet, const Area &area, int column, bool ascending, bool header);
    bool merge(int sheet, const Area &area);
    bool unmerge(int sheet, const Area &area);
    /// The merge a cell belongs to; false when it is on its own.
    bool mergeAt(int sheet, int row, int col, Area *area) const;

    // --- Layout
    double colWidth(int sheet, int col) const;
    double rowHeight(int sheet, int row) const;
    void setColWidth(int sheet, int c1, int c2, double width);
    void setRowHeight(int sheet, int r1, int r2, double height);
    void setColsHidden(int sheet, int c1, int c2, bool hidden);
    void setRowsHidden(int sheet, int r1, int r2, bool hidden);
    bool colHidden(int sheet, int col) const;
    bool rowHidden(int sheet, int row) const;
    void setFreeze(int sheet, int rows, int cols);
    /// The width the widest content of a column wants, by a rough measure.
    double fittingWidth(int sheet, int col, const std::function<double(const QString &, const Style &)> &measure);

    // --- Data
    void setFilter(int sheet, const Area &area);
    void clearFilter(int sheet);
    /// The different texts a filter's column shows, sorted.
    QStringList filterValues(int sheet, int column);
    void setFilterAllowed(int sheet, int column, const QStringList &allowed, bool all);
    /// The next cell holding `text` after (row, col), wrapping around.
    bool find(int sheet, const QString &text, bool matchCase, bool whole, bool forward, int *row, int *col);
    int replaceAll(int sheet, const QString &text, const QString &with, bool matchCase, bool whole);
    bool replaceAt(int sheet, int row, int col, const QString &text, const QString &with, bool matchCase, bool whole);
    void addConditional(int sheet, const CondFormat &rule);
    void clearConditionals(int sheet, const Area &area);
    void setValidation(int sheet, const Validation &rule);
    void clearValidation(int sheet, const Area &area);
    const Validation *validationAt(int sheet, int row, int col) const;
    SelectionStats stats(int sheet, const Area &area);
    /// The block of filled cells around a cell, as Ctrl+A would pick it.
    Area region(int sheet, int row, int col) const;
    /// An area that takes whole rows or columns, cut down to what is in use.
    Area clipped(int sheet, const Area &area) const;

    // --- Tables
    /// Turns an area (its first row the headers) into a table; -1 when it
    /// is a single row or runs into another table.
    int addTable(int sheet, const Area &area);
    /// Back to plain cells; the contents stay.
    void removeTable(int sheet, int index);
    /// Shows or drops the row of totals under the table.
    void setTableTotals(int sheet, int index, bool on);
    int tableAt(int sheet, int row, int col) const;

    // --- Charts
    int addChart(int sheet, const Chart &chart);
    void updateChart(int sheet, const Chart &chart);
    void removeChart(int sheet, int id);
    ChartData chartData(int sheet, const Chart &chart);

    // --- History
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    void undo();
    void redo();
    /// Several changes that undo as one.
    void beginGroup();
    void endGroup();
    bool modified() const { return m_modified; }
    void setModified(bool on) { m_modified = on; }
    /// Nothing to undo from here on: what a new document starts with.
    void forgetHistory()
    {
        m_undo.clear();
        m_redo.clear();
        m_modified = false;
    }
    /// Counts up with every change, for views to know they are stale.
    int revision() const { return m_revision; }

    /// Calculates what is waiting. Volatile formulas (NOW, RAND…) go again when `volatiles`.
    void recalc(bool volatiles = true);
    void recalcAll();
    /// The corner of what is in use on a sheet: one past the last row and column.
    void used(int sheet, int *rows, int *cols) const;

    /// The fixed moment NOW gives, for tests; NaN goes back to the clock.
    void setNow(double serial) { m_now = serial; }

    // --- EvalHost
    int sheetId(const QString &name) const override { return sheetIndex(name); }
    Value cellValue(int sheet, int row, int col) override;
    void extent(int sheet, int *rows, int *cols) const override { used(sheet, rows, cols); }
    bool rowOut(int sheet, int row, bool byHand) const override;
    void forEachCell(const RangeRef &range, const std::function<void(int, int, const Value &)> &visit) override;
    double now() const override;

private:
    struct RangeDep {
        RangeRef range;
        quint64 cell;
    };

    static quint64 key(int sheet, int row, int col) { return (quint64(sheet) << 34) | (quint64(row) << 14) | quint64(col); }
    static int keySheet(quint64 k) { return int(k >> 34); }
    static int keyRow(quint64 k) { return int((k >> 14) & 0xfffff); }
    static int keyCol(quint64 k) { return int(k & 0x3fff); }

    Cell &cellRef(int sheet, int row, int col);
    void dropIfBlank(int sheet, int row, int col);
    /// Puts a formula (or none) in a cell and keeps the graph in step.
    void setFormula(int sheet, int row, int col, const NodePtr &formula);
    void setCell(int sheet, int row, int col, const Cell &cell);
    void attach(quint64 cell, const NodePtr &formula);
    void detach(quint64 cell);
    void touch(quint64 cell);
    void rebuildGraph(bool dirtyAll);
    void compute(quint64 start);
    Value evaluateCell(quint64 cell, const NodePtr &formula);
    void checkpoint();
    void changed();
    void structural(int sheet, const SheetEdit &edit);
    void applyFilter(int sheet);
    QString uniqueSheetName(const QString &wanted) const;
    QString checkValidation(int sheet, int row, int col, const QString &typed, const Value &value) const;

    QList<Sheet> m_sheets;
    QList<Style> m_styles;
    QHash<Style, int> m_styleIds;

    QSet<quint64> m_dirty;
    QSet<quint64> m_visiting;
    QSet<quint64> m_volatile;
    QHash<quint64, QList<RangeRef>> m_precedents;
    QHash<quint64, QList<quint64>> m_singles;
    QHash<quint64, QList<RangeDep>> m_blocks;
    QList<RangeDep> m_big;
    mutable QHash<int, QPair<int, int>> m_used;
    /// How many times each text shows in the area of a «duplicate» rule.
    struct RuleCache {
        int revision = -1;
        QHash<QString, int> counts;
    };
    QHash<quint64, RuleCache> m_ruleCache;

    QList<QList<Sheet>> m_undo;
    QList<QList<Sheet>> m_redo;
    int m_group = 0;
    bool m_groupSaved = false;
    bool m_modified = false;
    int m_revision = 0;
    int m_nextChart = 1;
    double m_now = qQNaN();
};
