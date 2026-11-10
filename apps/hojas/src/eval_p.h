#pragma once

#include "formula.h"

#include <QList>

#include <memory>

/// A block of values: what a range or an array operation comes to.
struct Matrix {
    int rows = 0;
    int cols = 0;
    QList<Value> cells;

    Matrix() = default;
    Matrix(int r, int c)
        : rows(r)
        , cols(c)
        , cells(qsizetype(r) * c)
    {
    }
    const Value &at(int r, int c) const { return cells.at(qsizetype(r) * cols + c); }
    Value &at(int r, int c) { return cells[qsizetype(r) * cols + c]; }
};

/// What an expression gives before anyone asks for a single value: a value,
/// a reference to cells, or a block of values.
struct Arg {
    enum Kind { Scalar, Range, Array };

    Kind kind = Scalar;
    Value v;
    RangeRef ref;
    std::shared_ptr<Matrix> m;
    /// An argument that was left out.
    bool missing = false;

    Arg() = default;
    Arg(const Value &value)
        : v(value)
    {
    }
    Arg(double number)
        : v(number)
    {
    }
    Arg(Err error)
        : v(error)
    {
    }
    Arg(const RangeRef &range)
        : kind(Range)
        , ref(range)
    {
    }
    Arg(const std::shared_ptr<Matrix> &matrix)
        : kind(Array)
        , m(matrix)
    {
    }
};

class Eval;
using Fn = Arg (*)(Eval &, QList<Arg> &);
using LazyFn = Arg (*)(Eval &, const Node &);

struct FuncDef {
    const char *name;
    const char *es;
    int min;
    int max; // -1: as many as wanted
    Fn fn;
    LazyFn lazy;
    bool isVolatile;
    const char *signature;
    const char *help;
};

const FuncDef *findFunction(const QString &canonical);
const QList<FuncDef> &allFunctions();

class Eval
{
public:
    explicit Eval(const EvalContext &context)
        : ctx(context)
    {
    }

    Arg eval(const Node &node);
    /// One value out of anything: a range gives its only cell, or the one on
    /// the formula's own row or column.
    Value scalar(const Arg &arg);
    /// The size of a range (stopped at the last cell in use) or of an array.
    void size(const Arg &arg, int *rows, int *cols) const;
    Value at(const Arg &arg, int row, int col);
    /// Every value that is there, in reading order; blanks of a range are skipped.
    void each(const Arg &arg, const std::function<void(const Value &)> &visit);
    /// All of it, blanks included.
    Matrix matrix(const Arg &arg);
    /// The range clipped to what the sheet has in use.
    RangeRef clipped(const RangeRef &range) const;

    /// A number out of a value, the way arithmetic wants it: text that reads
    /// as a number counts. An error comes back in `error`.
    static bool number(const Value &value, double *out, Value *error);
    static QString text(const Value &value);
    static bool truth(const Value &value, bool *out, Value *error);
    /// <0, 0 or >0: numbers before text before true/false, text without case.
    static int compare(const Value &a, const Value &b);

    EvalContext ctx;
};

/// «>=10», «<>x», «man*»: the test of SUMIF and friends.
class Criterion
{
public:
    explicit Criterion(const Value &criterion);
    bool matches(const Value &value) const;

private:
    enum Op { Eq, Ne, Lt, Le, Gt, Ge };
    Op m_op = Eq;
    Value m_operand;
    bool m_wild = false;
    QString m_pattern;
};
