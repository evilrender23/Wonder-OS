#pragma once

#include "eval_p.h"
#include "numfmt.h"

#include <cmath>

/// Shorthand for the bodies of the functions of the sheet.
#define FN [](Eval &E, QList<Arg> &a) -> Arg
#define LAZY [](Eval &E, const Node &node) -> Arg
/// The argument `i` as a number in `var`, or leave with its error.
#define NUM(var, i)                                                                                                                                            \
    double var = 0;                                                                                                                                            \
    {                                                                                                                                                          \
        Value e_;                                                                                                                                              \
        if (!Eval::number(E.scalar(a[i]), &var, &e_)) {                                                                                                        \
            return Arg(e_);                                                                                                                                    \
        }                                                                                                                                                      \
    }
/// The same for an argument that may be left out.
#define OPT(var, i, fallback)                                                                                                                                  \
    double var = fallback;                                                                                                                                     \
    if (a.size() > i && !a[i].missing) {                                                                                                                       \
        Value e_;                                                                                                                                              \
        if (!Eval::number(E.scalar(a[i]), &var, &e_)) {                                                                                                        \
            return Arg(e_);                                                                                                                                    \
        }                                                                                                                                                      \
    }
/// The argument `i` as text in `var`; errors leave.
#define TXT(var, i)                                                                                                                                            \
    QString var;                                                                                                                                               \
    {                                                                                                                                                          \
        const Value v_ = E.scalar(a[i]);                                                                                                                       \
        if (v_.isError()) {                                                                                                                                    \
            return Arg(v_);                                                                                                                                    \
        }                                                                                                                                                      \
        var = Eval::text(v_);                                                                                                                                  \
    }

inline bool given(const QList<Arg> &a, int i)
{
    return a.size() > i && !a.at(i).missing;
}

/// The numbers of a list of arguments the way SUM takes them: in ranges only
/// real numbers count; a loose value has to be one, or read as one.
inline bool numbers(Eval &E, const QList<Arg> &a, QList<double> *out, Value *error, int from = 0)
{
    bool ok = true;
    for (int i = from; i < a.size() && ok; ++i) {
        const Arg &arg = a.at(i);
        if (arg.missing) {
            continue;
        }
        if (arg.kind == Arg::Scalar) {
            double number = 0;
            if (!Eval::number(arg.v, &number, error)) {
                return false;
            }
            out->append(number);
            continue;
        }
        E.each(arg, [&](const Value &value) {
            if (!ok) {
                return;
            }
            if (value.isError()) {
                *error = value;
                ok = false;
            } else if (value.isNumber()) {
                out->append(value.num);
            }
        });
    }
    return ok;
}

/// Everything in an argument as a flat list, blanks included.
inline QList<Value> flat(Eval &E, const Arg &arg)
{
    return E.matrix(arg).cells;
}

/// `mode` 0 rounds half away from zero, 1 goes away from zero, -1 towards it.
inline double roundTo(double x, double digits, int mode)
{
    const double m = std::pow(10.0, std::trunc(digits));
    const double sign = x < 0 ? -1.0 : 1.0;
    double v = std::fabs(x) * m;
    // 1.005 * 100 is 100.49999999999999: lean it back to what was meant.
    const double nudge = v * 1e-14;
    if (mode == 0) {
        v = std::floor(v + nudge + 0.5);
    } else if (mode > 0) {
        v = std::ceil(v - nudge);
    } else {
        v = std::floor(v + nudge);
    }
    return sign * v / m;
}

inline Arg checked(double x)
{
    if (std::isnan(x) || std::isinf(x)) {
        return Arg(Err::Num);
    }
    return Arg(x);
}

void registerMath(QList<FuncDef> &table);
void registerLogic(QList<FuncDef> &table);
void registerText(QList<FuncDef> &table);
void registerDates(QList<FuncDef> &table);
void registerLookup(QList<FuncDef> &table);
void registerStats(QList<FuncDef> &table);
void registerFinance(QList<FuncDef> &table);
