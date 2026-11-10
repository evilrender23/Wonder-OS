#include "eval_p.h"

#include "numfmt.h"

#include <QRegularExpression>

#include <cmath>

namespace
{
bool isMulti(const Arg &arg)
{
    return (arg.kind == Arg::Range && !arg.ref.single()) || (arg.kind == Arg::Array && arg.m && arg.m->cells.size() != 1);
}

Value binaryOp(const QString &op, const Value &left, const Value &right)
{
    if (left.isError()) {
        return left;
    }
    if (right.isError()) {
        return right;
    }
    const QChar first = op.at(0);
    if (first == QLatin1Char('&')) {
        return Value(Eval::text(left) + Eval::text(right));
    }
    if (first == QLatin1Char('=') || first == QLatin1Char('<') || first == QLatin1Char('>')) {
        const int order = Eval::compare(left, right);
        if (op == QLatin1String("=")) {
            return Value::boolean(order == 0);
        }
        if (op == QLatin1String("<>")) {
            return Value::boolean(order != 0);
        }
        if (op == QLatin1String("<")) {
            return Value::boolean(order < 0);
        }
        if (op == QLatin1String(">")) {
            return Value::boolean(order > 0);
        }
        if (op == QLatin1String("<=")) {
            return Value::boolean(order <= 0);
        }
        return Value::boolean(order >= 0);
    }
    double a = 0, b = 0;
    Value error;
    if (!Eval::number(left, &a, &error) || !Eval::number(right, &b, &error)) {
        return error;
    }
    double result = 0;
    switch (first.unicode()) {
    case u'+':
        result = a + b;
        break;
    case u'-':
        result = a - b;
        break;
    case u'*':
        result = a * b;
        break;
    case u'/':
        if (b == 0) {
            return Value(Err::Div0);
        }
        result = a / b;
        break;
    case u'^':
        if (a == 0 && b < 0) {
            return Value(Err::Div0);
        }
        result = std::pow(a, b);
        break;
    default:
        return Value(Err::Value);
    }
    if (std::isnan(result) || std::isinf(result)) {
        return Value(Err::Num);
    }
    return Value(result);
}
}

// --- Coercions ----------------------------------------------------------------

bool Eval::number(const Value &value, double *out, Value *error)
{
    switch (value.type) {
    case Value::Number:
    case Value::Bool:
        *out = value.num;
        return true;
    case Value::Empty:
        *out = 0;
        return true;
    case Value::Text: {
        const Value parsed = NumFmt::parseInput(value.text);
        if (parsed.isNumber()) {
            *out = parsed.num;
            return true;
        }
        *error = Value(Err::Value);
        return false;
    }
    case Value::Error:
        *error = value;
        return false;
    }
    return false;
}

QString Eval::text(const Value &value)
{
    switch (value.type) {
    case Value::Number:
        return NumFmt::general(value.num);
    case Value::Bool:
        return value.truth() ? QStringLiteral("VERDADERO") : QStringLiteral("FALSO");
    case Value::Text:
        return value.text;
    case Value::Error:
        return Value::errorText(value.err);
    case Value::Empty:
        break;
    }
    return {};
}

bool Eval::truth(const Value &value, bool *out, Value *error)
{
    switch (value.type) {
    case Value::Number:
    case Value::Bool:
        *out = value.num != 0;
        return true;
    case Value::Empty:
        *out = false;
        return true;
    case Value::Text: {
        const QString upper = value.text.trimmed().toUpper();
        if (upper == QLatin1String("TRUE") || upper == QLatin1String("VERDADERO")) {
            *out = true;
            return true;
        }
        if (upper == QLatin1String("FALSE") || upper == QLatin1String("FALSO")) {
            *out = false;
            return true;
        }
        *error = Value(Err::Value);
        return false;
    }
    case Value::Error:
        *error = value;
        return false;
    }
    return false;
}

int Eval::compare(const Value &left, const Value &right)
{
    Value a = left;
    Value b = right;
    // Nothing compares as the zero of whatever is on the other side.
    if (a.isEmpty() && b.isEmpty()) {
        return 0;
    }
    if (a.isEmpty()) {
        a = b.isText() ? Value(QString()) : b.isBool() ? Value::boolean(false) : Value(0.0);
    }
    if (b.isEmpty()) {
        b = a.isText() ? Value(QString()) : a.isBool() ? Value::boolean(false) : Value(0.0);
    }
    auto rank = [](const Value &v) {
        return v.isNumber() ? 0 : v.isText() ? 1 : 2;
    };
    if (rank(a) != rank(b)) {
        return rank(a) - rank(b);
    }
    if (a.isText()) {
        return QString::compare(a.text, b.text, Qt::CaseInsensitive);
    }
    // Fifteen digits is all a number holds: 0,1+0,2 is 0,3.
    if (a.isNumber() && std::fabs(a.num - b.num) <= 4e-15 * qMax(std::fabs(a.num), std::fabs(b.num))) {
        return 0;
    }
    return a.num < b.num ? -1 : (a.num > b.num ? 1 : 0);
}

// --- Ranges and arrays ----------------------------------------------------------

RangeRef Eval::clipped(const RangeRef &range) const
{
    RangeRef out = range;
    int rows = 0, cols = 0;
    ctx.host->extent(range.sheet, &rows, &cols);
    out.r2 = qMin(out.r2, qMax(rows - 1, out.r1));
    out.c2 = qMin(out.c2, qMax(cols - 1, out.c1));
    return out;
}

void Eval::size(const Arg &arg, int *rows, int *cols) const
{
    if (arg.kind == Arg::Range) {
        const RangeRef range = clipped(arg.ref);
        *rows = range.rows();
        *cols = range.cols();
    } else if (arg.kind == Arg::Array && arg.m) {
        *rows = arg.m->rows;
        *cols = arg.m->cols;
    } else {
        *rows = 1;
        *cols = 1;
    }
}

Value Eval::at(const Arg &arg, int row, int col)
{
    if (arg.kind == Arg::Range) {
        if (row < 0 || col < 0 || row >= arg.ref.rows() || col >= arg.ref.cols()) {
            return Value(Err::Ref);
        }
        return ctx.host->cellValue(arg.ref.sheet, arg.ref.r1 + row, arg.ref.c1 + col);
    }
    if (arg.kind == Arg::Array && arg.m) {
        if (row < 0 || col < 0 || row >= arg.m->rows || col >= arg.m->cols) {
            return Value(Err::Ref);
        }
        return arg.m->at(row, col);
    }
    return arg.v;
}

Value Eval::scalar(const Arg &arg)
{
    if (arg.kind == Arg::Scalar) {
        return arg.v;
    }
    if (arg.kind == Arg::Array) {
        return arg.m && !arg.m->cells.isEmpty() ? arg.m->cells.first() : Value();
    }
    const RangeRef &r = arg.ref;
    if (r.sheet < 0) {
        return Value(Err::Ref);
    }
    if (r.single()) {
        return ctx.host->cellValue(r.sheet, r.r1, r.c1);
    }
    // A column gives the cell on the formula's row; a row, the one on its column.
    if (r.c1 == r.c2 && ctx.row >= r.r1 && ctx.row <= r.r2) {
        return ctx.host->cellValue(r.sheet, ctx.row, r.c1);
    }
    if (r.r1 == r.r2 && ctx.col >= r.c1 && ctx.col <= r.c2) {
        return ctx.host->cellValue(r.sheet, r.r1, ctx.col);
    }
    return Value(Err::Value);
}

void Eval::each(const Arg &arg, const std::function<void(const Value &)> &visit)
{
    if (arg.kind == Arg::Range) {
        if (arg.ref.sheet < 0) {
            visit(Value(Err::Ref));
            return;
        }
        ctx.host->forEachCell(arg.ref, [&visit](int, int, const Value &value) {
            if (!value.isEmpty()) {
                visit(value);
            }
        });
    } else if (arg.kind == Arg::Array) {
        if (arg.m) {
            for (const Value &value : std::as_const(arg.m->cells)) {
                visit(value);
            }
        }
    } else {
        visit(arg.v);
    }
}

Matrix Eval::matrix(const Arg &arg)
{
    if (arg.kind == Arg::Array && arg.m) {
        return *arg.m;
    }
    if (arg.kind == Arg::Range) {
        if (arg.ref.sheet < 0) {
            Matrix one(1, 1);
            one.cells[0] = Value(Err::Ref);
            return one;
        }
        const RangeRef range = clipped(arg.ref);
        Matrix out(range.rows(), range.cols());
        ctx.host->forEachCell(range, [&out, &range](int row, int col, const Value &value) {
            out.at(row - range.r1, col - range.c1) = value;
        });
        return out;
    }
    Matrix one(1, 1);
    one.cells[0] = arg.v;
    return one;
}

// --- The tree -------------------------------------------------------------------

Arg Eval::eval(const Node &node)
{
    switch (node.kind) {
    case Node::Number:
        return Arg(Value(node.num));
    case Node::String:
        return Arg(Value(node.text));
    case Node::Bool:
        return Arg(Value::boolean(node.num != 0));
    case Node::Error:
        return Arg(Value(node.err));
    case Node::Missing: {
        Arg arg;
        arg.missing = true;
        return arg;
    }
    case Node::Ref:
    case Node::Range: {
        RangeRef range;
        range.sheet = node.hasSheet ? ctx.host->sheetId(node.sheet) : ctx.sheet;
        if (range.sheet < 0) {
            return Arg(Err::Ref);
        }
        range.r1 = node.a.row;
        range.c1 = node.a.col;
        range.r2 = node.kind == Node::Range ? node.b.row : node.a.row;
        range.c2 = node.kind == Node::Range ? node.b.col : node.a.col;
        return Arg(range);
    }
    case Node::Paren:
        return eval(*node.args.at(0));
    case Node::Unary:
    case Node::Percent: {
        const Arg operand = eval(*node.args.at(0));
        const bool minus = node.kind == Node::Unary && node.text == QLatin1String("-");
        const bool percent = node.kind == Node::Percent;
        auto apply = [minus, percent](const Value &value) -> Value {
            if (!minus && !percent) {
                return value;
            }
            double number = 0;
            Value error;
            if (!Eval::number(value, &number, &error)) {
                return error;
            }
            return Value(percent ? number / 100.0 : -number);
        };
        if (isMulti(operand)) {
            auto out = std::make_shared<Matrix>(matrix(operand));
            for (Value &value : out->cells) {
                value = apply(value);
            }
            return Arg(out);
        }
        return Arg(apply(scalar(operand)));
    }
    case Node::Binary: {
        const Arg left = eval(*node.args.at(0));
        const Arg right = eval(*node.args.at(1));
        if (!isMulti(left) && !isMulti(right)) {
            return Arg(binaryOp(node.text, scalar(left), scalar(right)));
        }
        // Cell by cell; a single value or a single row or column stretches.
        const Matrix a = matrix(left);
        const Matrix b = matrix(right);
        const int rows = qMax(a.rows, b.rows);
        const int cols = qMax(a.cols, b.cols);
        auto out = std::make_shared<Matrix>(rows, cols);
        auto pick = [](const Matrix &m, int r, int c) -> Value {
            const int rr = m.rows == 1 ? 0 : r;
            const int cc = m.cols == 1 ? 0 : c;
            if (rr >= m.rows || cc >= m.cols) {
                return Value(Err::NA);
            }
            return m.at(rr, cc);
        };
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                out->at(r, c) = binaryOp(node.text, pick(a, r, c), pick(b, r, c));
            }
        }
        return Arg(out);
    }
    case Node::Func: {
        const FuncDef *def = findFunction(node.text);
        if (!def) {
            return Arg(Err::Name);
        }
        const int count = node.args.size();
        if (count < def->min || (def->max >= 0 && count > def->max)) {
            return Arg(Err::Value);
        }
        if (def->lazy) {
            return def->lazy(*this, node);
        }
        QList<Arg> args;
        args.reserve(count);
        for (const NodePtr &arg : node.args) {
            args.append(eval(*arg));
        }
        return def->fn(*this, args);
    }
    }
    return Arg(Err::Value);
}

Value Formula::evaluate(const NodePtr &node, const EvalContext &context)
{
    if (!node) {
        return Value(Err::Name);
    }
    // Formulas that reach each other through OFFSET or INDIRECT recurse here:
    // stop before the stack does.
    static thread_local int depth = 0;
    if (depth > 400) {
        return Value(Err::Cycle);
    }
    ++depth;
    Eval eval(context);
    Value value = eval.scalar(eval.eval(*node));
    --depth;
    if (value.isEmpty()) {
        // A formula always has a value: an empty cell reads as zero.
        return Value(0.0);
    }
    if (value.isNumber() && (std::isnan(value.num) || std::isinf(value.num))) {
        return Value(Err::Num);
    }
    return value;
}

// --- Criteria -------------------------------------------------------------------

Criterion::Criterion(const Value &criterion)
{
    if (!criterion.isText()) {
        m_operand = criterion;
        return;
    }
    QString text = criterion.text;
    if (text.startsWith(QLatin1String(">="))) {
        m_op = Ge;
        text.remove(0, 2);
    } else if (text.startsWith(QLatin1String("<="))) {
        m_op = Le;
        text.remove(0, 2);
    } else if (text.startsWith(QLatin1String("<>"))) {
        m_op = Ne;
        text.remove(0, 2);
    } else if (text.startsWith(QLatin1Char('>'))) {
        m_op = Gt;
        text.remove(0, 1);
    } else if (text.startsWith(QLatin1Char('<'))) {
        m_op = Lt;
        text.remove(0, 1);
    } else if (text.startsWith(QLatin1Char('='))) {
        text.remove(0, 1);
    }
    const Value parsed = text.isEmpty() ? Value() : NumFmt::parseInput(text);
    if (parsed.isNumber() || parsed.isBool()) {
        m_operand = parsed;
        return;
    }
    m_operand = Value(text);
    if ((m_op == Eq || m_op == Ne) && (text.contains(QLatin1Char('*')) || text.contains(QLatin1Char('?')))) {
        m_wild = true;
        // «~*» is a real asterisk.
        QString pattern;
        for (int i = 0; i < text.size(); ++i) {
            const QChar c = text.at(i);
            if (c == QLatin1Char('~') && i + 1 < text.size()) {
                pattern += QRegularExpression::escape(text.mid(++i, 1));
            } else if (c == QLatin1Char('*')) {
                pattern += QLatin1String(".*");
            } else if (c == QLatin1Char('?')) {
                pattern += QLatin1Char('.');
            } else {
                pattern += QRegularExpression::escape(QString(c));
            }
        }
        m_pattern = pattern;
    }
}

bool Criterion::matches(const Value &value) const
{
    if (value.isError()) {
        return false;
    }
    if (m_operand.isNumber()) {
        double number = 0;
        if (value.isNumber()) {
            number = value.num;
        } else if (value.isText()) {
            number = NumFmt::parseNumber(value.text);
            if (std::isnan(number)) {
                return m_op == Ne;
            }
        } else {
            return m_op == Ne;
        }
        const double other = m_operand.num;
        switch (m_op) {
        case Eq:
            return number == other;
        case Ne:
            return number != other;
        case Lt:
            return number < other;
        case Le:
            return number <= other;
        case Gt:
            return number > other;
        case Ge:
            return number >= other;
        }
    }
    if (m_operand.isBool()) {
        const bool same = value.isBool() && value.truth() == m_operand.truth();
        return m_op == Ne ? !same : same;
    }
    // Text; an empty criterion matches cells with nothing in them.
    const QString pattern = m_operand.text;
    if (m_op == Eq || m_op == Ne) {
        bool same = false;
        if (pattern.isEmpty()) {
            same = value.isEmpty() || (value.isText() && value.text.isEmpty());
        } else if (value.isText()) {
            if (m_wild) {
                const QRegularExpression expression(QRegularExpression::anchoredPattern(m_pattern),
                                                    QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
                same = expression.match(value.text).hasMatch();
            } else {
                same = value.text.compare(pattern, Qt::CaseInsensitive) == 0;
            }
        }
        return m_op == Ne ? !same : same;
    }
    if (!value.isText()) {
        return false;
    }
    const int order = QString::compare(value.text, pattern, Qt::CaseInsensitive);
    switch (m_op) {
    case Lt:
        return order < 0;
    case Le:
        return order <= 0;
    case Gt:
        return order > 0;
    default:
        return order >= 0;
    }
}
