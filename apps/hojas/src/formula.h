#pragma once

#include "value.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace Limits
{
constexpr int Rows = 1048576;
constexpr int Cols = 16384;
}

/// One end of a reference: where, and whether a «$» pins the row or the column.
struct CellRef {
    int row = 0;
    int col = 0;
    bool absRow = false;
    bool absCol = false;
};

/// A rectangle of cells on a sheet, by the sheet's id.
struct RangeRef {
    int sheet = -1;
    int r1 = 0;
    int c1 = 0;
    int r2 = 0;
    int c2 = 0;

    int rows() const { return r2 - r1 + 1; }
    int cols() const { return c2 - c1 + 1; }
    bool single() const { return r1 == r2 && c1 == c2; }
    bool contains(int row, int col) const { return row >= r1 && row <= r2 && col >= c1 && col <= c2; }
    bool operator==(const RangeRef &o) const { return sheet == o.sheet && r1 == o.r1 && c1 == o.c1 && r2 == o.r2 && c2 == o.c2; }
};

/// A formula, parsed.
struct Node {
    enum Kind { Number, String, Bool, Error, Missing, Ref, Range, Unary, Binary, Percent, Paren, Func };

    Kind kind = Missing;
    double num = 0;
    /// The literal of a number as typed, a string, an operator or a function's name.
    QString text;
    Err err = Err::None;
    /// References: the sheet named before «!», if any.
    QString sheet;
    bool hasSheet = false;
    CellRef a;
    CellRef b;
    /// «A:C» and «1:5».
    bool fullCols = false;
    bool fullRows = false;
    QList<std::shared_ptr<const Node>> args;
};
using NodePtr = std::shared_ptr<const Node>;

/// What the evaluator asks of the workbook.
class EvalHost
{
public:
    virtual ~EvalHost() = default;
    /// The id of a sheet by its name, without minding case; -1 when there is none.
    virtual int sheetId(const QString &name) const = 0;
    /// The value of a cell, calculated first if it is a formula waiting.
    virtual Value cellValue(int sheet, int row, int col) = 0;
    /// The rows and columns in use, to stop «A:A» at the last cell.
    virtual void extent(int sheet, int *rows, int *cols) const = 0;
    /// Every cell with something in it inside the range, row by row.
    virtual void forEachCell(const RangeRef &range, const std::function<void(int row, int col, const Value &value)> &visit) = 0;
    /// Today and the time, as a serial number.
    virtual double now() const = 0;
    /// Whether a row is out of sight: left out by a filter, or, when `byHand`
    /// counts too, hidden by the user. SUBTOTAL passes over those.
    virtual bool rowOut(int sheet, int row, bool byHand) const
    {
        Q_UNUSED(sheet)
        Q_UNUSED(row)
        Q_UNUSED(byHand)
        return false;
    }
};

struct EvalContext {
    EvalHost *host = nullptr;
    int sheet = 0;
    int row = 0;
    int col = 0;
};

struct FunctionInfo {
    QString name;
    QString spanish;
    QString signature;
    QString help;
};

/// A structural change to a sheet that references have to follow.
struct SheetEdit {
    enum Kind { InsertRows, DeleteRows, InsertCols, DeleteCols };
    Kind kind = InsertRows;
    /// The sheet that changed, by name.
    QString sheet;
    int at = 0;
    int count = 1;
};

namespace Formula
{
/// Parses the text after «=». Both «SUM(A1,2.5)» and «SUMA(A1;2,5)» are read.
/// Null when it makes no sense; `error` then says why, in Spanish.
NodePtr parse(const QString &text, QString *error = nullptr);
/// The formula as text, without «=»: the canonical way (English names, commas),
/// which is what files hold, or the Spanish way, which is what is shown.
QString print(const NodePtr &node, bool spanish = false);

/// As if copied `rows` down and `cols` right: relative references move, and
/// the ones that fall off the sheet turn into #REF!.
NodePtr shifted(const NodePtr &node, int rows, int cols);
/// After rows or columns were inserted or deleted on a sheet. `home` is the
/// sheet the formula lives on, for references that name none.
NodePtr adjusted(const NodePtr &node, const SheetEdit &edit, const QString &home);
NodePtr sheetRenamed(const NodePtr &node, const QString &from, const QString &to);

/// Every cell and range the formula reads, as written (full columns unclipped).
void precedents(const NodePtr &node, const EvalContext &context, QList<RangeRef> *out);
/// Whether it has to be recalculated every time (NOW, RAND, OFFSET, INDIRECT…).
bool isVolatile(const NodePtr &node);

Value evaluate(const NodePtr &node, const EvalContext &context);

/// «A», «AB», «XFD».
QString columnName(int col);
/// -1 when it is not a column.
int columnIndex(const QString &name);
QString cellName(int row, int col);
/// «B3» or «$B$3».
bool parseCell(const QString &text, CellRef *ref);
/// «B3», «B3:D9», «B:D» or «3:9», with no sheet. The corners come out in order.
bool parseArea(const QString &text, int *r1, int *c1, int *r2, int *c2);
QString areaName(int r1, int c1, int r2, int c2);
/// A sheet's name as a formula writes it: quoted when it needs to be.
QString quoteSheet(const QString &name);

/// Every function there is, sorted by name, for the autocompletion.
QList<FunctionInfo> functions();
/// The canonical name of a function typed in English or Spanish; empty when unknown.
QString canonicalFunction(const QString &name);
}
