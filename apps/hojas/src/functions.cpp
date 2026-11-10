#include "fn_p.h"

#include <QHash>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <algorithm>
#include <numeric>

// --- The table ------------------------------------------------------------------

const QList<FuncDef> &allFunctions()
{
    static const QList<FuncDef> table = [] {
        QList<FuncDef> t;
        registerMath(t);
        registerLogic(t);
        registerText(t);
        registerDates(t);
        registerLookup(t);
        registerStats(t);
        registerFinance(t);
        std::sort(t.begin(), t.end(), [](const FuncDef &x, const FuncDef &y) {
            return qstrcmp(x.name, y.name) < 0;
        });
        return t;
    }();
    return table;
}

namespace
{
const QHash<QString, const FuncDef *> &byName()
{
    static const QHash<QString, const FuncDef *> index = [] {
        QHash<QString, const FuncDef *> h;
        for (const FuncDef &def : allFunctions()) {
            h.insert(QString::fromUtf8(def.name), &def);
        }
        return h;
    }();
    return index;
}
const QHash<QString, QString> &bySpanish()
{
    static const QHash<QString, QString> index = [] {
        QHash<QString, QString> h;
        for (const FuncDef &def : allFunctions()) {
            if (def.es[0] != '\0') {
                h.insert(QString::fromUtf8(def.es), QString::fromUtf8(def.name));
            }
        }
        // Spellings older versions used, and the accents people leave out.
        h.insert(QStringLiteral("ANO"), QStringLiteral("YEAR"));
        h.insert(QStringLiteral("DÍA"), QStringLiteral("DAY"));
        h.insert(QStringLiteral("RAÍZ"), QStringLiteral("SQRT"));
        h.insert(QStringLiteral("ÍNDICE"), QStringLiteral("INDEX"));
        h.insert(QStringLiteral("SUMAR.SI.CONJUNTO"), QStringLiteral("SUMIFS"));
        h.insert(QStringLiteral("CONSULTAV"), QStringLiteral("VLOOKUP"));
        h.insert(QStringLiteral("CONSULTAH"), QStringLiteral("HLOOKUP"));
        h.insert(QStringLiteral("CONCATENAR"), QStringLiteral("CONCATENATE"));
        h.insert(QStringLiteral("REDOND"), QStringLiteral("ROUND"));
        h.insert(QStringLiteral("NSHORA"), QStringLiteral("TIME"));
        h.insert(QStringLiteral("ESNÚMERO"), QStringLiteral("ISNUMBER"));
        h.insert(QStringLiteral("ESLÓGICO"), QStringLiteral("ISLOGICAL"));
        h.insert(QStringLiteral("CÓDIGO"), QStringLiteral("CODE"));
        h.insert(QStringLiteral("CARÁCTER"), QStringLiteral("CHAR"));
        h.insert(QStringLiteral("FECHANÚMERO"), QStringLiteral("DATEVALUE"));
        h.insert(QStringLiteral("MÚLTIPLO.SUPERIOR"), QStringLiteral("CEILING"));
        h.insert(QStringLiteral("MÚLTIPLO.INFERIOR"), QStringLiteral("FLOOR"));
        return h;
    }();
    return index;
}
}

const FuncDef *findFunction(const QString &canonical)
{
    return byName().value(canonical, nullptr);
}

QString Formula::canonicalFunction(const QString &typed)
{
    QString name = typed.toUpper();
    if (name.startsWith(QLatin1String("_XLFN."))) {
        name.remove(0, 6);
    }
    // Spanish first: «SI» and «NO» are not English names, and where both
    // languages share a word (MIN, MAX, ABS) they mean the same.
    const auto spanish = bySpanish().constFind(name);
    if (spanish != bySpanish().constEnd()) {
        return spanish.value();
    }
    return byName().contains(name) ? name : QString();
}

QList<FunctionInfo> Formula::functions()
{
    QList<FunctionInfo> out;
    for (const FuncDef &def : allFunctions()) {
        FunctionInfo info;
        info.name = QString::fromUtf8(def.name);
        info.spanish = QString::fromUtf8(def.es);
        info.signature = QString::fromUtf8(def.signature);
        info.help = QString::fromUtf8(def.help);
        out.append(info);
    }
    return out;
}

// --- Mathematics ----------------------------------------------------------------

namespace
{
/// The rows of the first criterion range that pass every criterion, for the
/// …IFS family: pairs of range and criterion start at `from`.
bool passing(Eval &E, const QList<Arg> &a, int from, Matrix *shape, QList<bool> *mask, Value *error)
{
    for (int i = from; i + 1 < a.size(); i += 2) {
        const Matrix m = E.matrix(a.at(i));
        const Value wanted = E.scalar(a.at(i + 1));
        if (wanted.isError()) {
            *error = wanted;
            return false;
        }
        const Criterion criterion(wanted);
        if (i == from) {
            *shape = Matrix(m.rows, m.cols);
            mask->fill(true, m.cells.size());
        }
        for (int r = 0; r < shape->rows; ++r) {
            for (int c = 0; c < shape->cols; ++c) {
                const Value value = r < m.rows && c < m.cols ? m.at(r, c) : Value();
                if (!criterion.matches(value)) {
                    (*mask)[qsizetype(r) * shape->cols + c] = false;
                }
            }
        }
    }
    return true;
}

/// The numbers of `values` on the places the criteria let through.
Arg ifs(Eval &E, const QList<Arg> &a, int valuesAt, int criteriaFrom, const std::function<Arg(const QList<double> &)> &result)
{
    Matrix shape;
    QList<bool> mask;
    Value error;
    if (!passing(E, a, criteriaFrom, &shape, &mask, &error)) {
        return Arg(error);
    }
    QList<double> picked;
    if (valuesAt < 0) {
        for (bool on : std::as_const(mask)) {
            if (on) {
                picked.append(1);
            }
        }
        return result(picked);
    }
    // The values are read from their own corner, however far the range given reaches.
    const Arg &values = a.at(valuesAt);
    for (int r = 0; r < shape.rows; ++r) {
        for (int c = 0; c < shape.cols; ++c) {
            if (!mask.at(qsizetype(r) * shape.cols + c)) {
                continue;
            }
            Value value;
            if (values.kind == Arg::Range) {
                value = E.ctx.host->cellValue(values.ref.sheet, values.ref.r1 + r, values.ref.c1 + c);
            } else {
                value = E.at(values, r, c);
            }
            if (value.isError()) {
                return Arg(value);
            }
            if (value.isNumber()) {
                picked.append(value.num);
            }
        }
    }
    return result(picked);
}

double sumOf(const QList<double> &list)
{
    // Kahan: a column of cents adds up to cents.
    double sum = 0, carry = 0;
    for (double x : list) {
        const double y = x - carry;
        const double t = sum + y;
        carry = (t - sum) - y;
        sum = t;
    }
    return sum;
}

double gcd(double x, double y)
{
    x = std::floor(std::fabs(x));
    y = std::floor(std::fabs(y));
    while (y > 0) {
        const double t = std::fmod(x, y);
        x = y;
        y = t;
    }
    return x;
}
}

void registerMath(QList<FuncDef> &t)
{
    t.append({"SUM", "SUMA", 1, -1, FN {
                  QList<double> list;
                  Value error;
                  if (!numbers(E, a, &list, &error)) {
                      return Arg(error);
                  }
                  return Arg(sumOf(list));
              }, nullptr, false, "número1; [número2]; …", "Suma todos los números."});
    t.append({"AVERAGE", "PROMEDIO", 1, -1, FN {
                  QList<double> list;
                  Value error;
                  if (!numbers(E, a, &list, &error)) {
                      return Arg(error);
                  }
                  if (list.isEmpty()) {
                      return Arg(Err::Div0);
                  }
                  return Arg(sumOf(list) / list.size());
              }, nullptr, false, "número1; [número2]; …", "La media aritmética."});
    t.append({"MIN", "MIN", 1, -1, FN {
                  QList<double> list;
                  Value error;
                  if (!numbers(E, a, &list, &error)) {
                      return Arg(error);
                  }
                  return Arg(list.isEmpty() ? 0.0 : *std::min_element(list.begin(), list.end()));
              }, nullptr, false, "número1; [número2]; …", "El menor de los números."});
    t.append({"MAX", "MAX", 1, -1, FN {
                  QList<double> list;
                  Value error;
                  if (!numbers(E, a, &list, &error)) {
                      return Arg(error);
                  }
                  return Arg(list.isEmpty() ? 0.0 : *std::max_element(list.begin(), list.end()));
              }, nullptr, false, "número1; [número2]; …", "El mayor de los números."});
    t.append({"COUNT", "CONTAR", 1, -1, FN {
                  double count = 0;
                  for (const Arg &arg : std::as_const(a)) {
                      if (arg.kind == Arg::Scalar) {
                          double number = 0;
                          Value error;
                          if (!arg.missing && !arg.v.isEmpty() && Eval::number(arg.v, &number, &error)) {
                              ++count;
                          }
                      } else {
                          E.each(arg, [&count](const Value &value) {
                              if (value.isNumber()) {
                                  ++count;
                              }
                          });
                      }
                  }
                  return Arg(count);
              }, nullptr, false, "valor1; [valor2]; …", "Cuántas celdas tienen números."});
    t.append({"COUNTA", "CONTARA", 1, -1, FN {
                  double count = 0;
                  for (const Arg &arg : std::as_const(a)) {
                      if (arg.kind == Arg::Scalar) {
                          if (!arg.missing && !arg.v.isEmpty()) {
                              ++count;
                          }
                      } else {
                          E.each(arg, [&count](const Value &value) {
                              if (!value.isEmpty()) {
                                  ++count;
                              }
                          });
                      }
                  }
                  return Arg(count);
              }, nullptr, false, "valor1; [valor2]; …", "Cuántas celdas no están vacías."});
    t.append({"COUNTBLANK", "CONTAR.BLANCO", 1, 1, FN {
                  if (a[0].kind != Arg::Range) {
                      return Arg(E.scalar(a[0]).isEmpty() ? 1.0 : 0.0);
                  }
                  double filled = 0;
                  E.each(a[0], [&filled](const Value &value) {
                      if (!(value.isText() && value.text.isEmpty())) {
                          ++filled;
                      }
                  });
                  return Arg(double(a[0].ref.rows()) * double(a[0].ref.cols()) - filled);
              }, nullptr, false, "rango", "Cuántas celdas del rango están vacías."});
    t.append({"SUBTOTAL", "SUBTOTALES", 2, -1, FN {
                  NUM(code, 0);
                  static const char *const names[] = {"AVERAGE", "COUNT", "COUNTA", "MAX", "MIN", "PRODUCT", "STDEV", "STDEVP", "SUM", "VAR", "VARP"};
                  const int which = int(code) > 100 ? int(code) - 100 : int(code);
                  if (which < 1 || which > 11) {
                      return Arg(Err::Value);
                  }
                  const FuncDef *def = findFunction(QString::fromLatin1(names[which - 1]));
                  if (!def || !def->fn) {
                      return Arg(Err::Value);
                  }
                  // Rows a filter leaves out do not count; from 101 on, neither
                  // do the ones hidden by hand.
                  QList<Arg> rest;
                  for (int i = 1; i < a.size(); ++i) {
                      if (a[i].kind != Arg::Range || a[i].ref.sheet < 0) {
                          rest.append(a[i]);
                          continue;
                      }
                      auto seen = std::make_shared<Matrix>();
                      E.ctx.host->forEachCell(a[i].ref, [&](int row, int, const Value &value) {
                          if (!value.isEmpty() && !E.ctx.host->rowOut(a[i].ref.sheet, row, code > 100)) {
                              seen->cells.append(value);
                          }
                      });
                      seen->rows = int(seen->cells.size());
                      seen->cols = 1;
                      rest.append(Arg(seen));
                  }
                  return def->fn(E, rest);
              }, nullptr, true, "función; rango; …", "Un total de lo que se ve del rango: 9 suma, 1 promedio, 2 cuenta, 4 máximo, 5 mínimo…"});
    t.append({"PRODUCT", "PRODUCTO", 1, -1, FN {
                  QList<double> list;
                  Value error;
                  if (!numbers(E, a, &list, &error)) {
                      return Arg(error);
                  }
                  double product = 1;
                  for (double x : std::as_const(list)) {
                      product *= x;
                  }
                  return checked(list.isEmpty() ? 0.0 : product);
              }, nullptr, false, "número1; [número2]; …", "Multiplica todos los números."});
    t.append({"SUMSQ", "SUMA.CUADRADOS", 1, -1, FN {
                  QList<double> list;
                  Value error;
                  if (!numbers(E, a, &list, &error)) {
                      return Arg(error);
                  }
                  double sum = 0;
                  for (double x : std::as_const(list)) {
                      sum += x * x;
                  }
                  return Arg(sum);
              }, nullptr, false, "número1; [número2]; …", "La suma de los cuadrados."});
    t.append({"ROUND", "REDONDEAR", 1, 2, FN {
                  NUM(x, 0);
                  OPT(digits, 1, 0);
                  return Arg(roundTo(x, digits, 0));
              }, nullptr, false, "número; [decimales]", "Redondea a los decimales indicados."});
    t.append({"ROUNDUP", "REDONDEAR.MAS", 1, 2, FN {
                  NUM(x, 0);
                  OPT(digits, 1, 0);
                  return Arg(roundTo(x, digits, 1));
              }, nullptr, false, "número; [decimales]", "Redondea alejándose de cero."});
    t.append({"ROUNDDOWN", "REDONDEAR.MENOS", 1, 2, FN {
                  NUM(x, 0);
                  OPT(digits, 1, 0);
                  return Arg(roundTo(x, digits, -1));
              }, nullptr, false, "número; [decimales]", "Redondea hacia cero."});
    t.append({"TRUNC", "TRUNCAR", 1, 2, FN {
                  NUM(x, 0);
                  OPT(digits, 1, 0);
                  return Arg(roundTo(x, digits, -1));
              }, nullptr, false, "número; [decimales]", "Quita los decimales que sobran."});
    t.append({"INT", "ENTERO", 1, 1, FN {
                  NUM(x, 0);
                  return Arg(std::floor(x));
              }, nullptr, false, "número", "El entero inferior más cercano."});
    t.append({"ABS", "ABS", 1, 1, FN {
                  NUM(x, 0);
                  return Arg(std::fabs(x));
              }, nullptr, false, "número", "El valor absoluto."});
    t.append({"SIGN", "SIGNO", 1, 1, FN {
                  NUM(x, 0);
                  return Arg(x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0));
              }, nullptr, false, "número", "1, 0 o -1 según el signo."});
    t.append({"SQRT", "RAIZ", 1, 1, FN {
                  NUM(x, 0);
                  if (x < 0) {
                      return Arg(Err::Num);
                  }
                  return Arg(std::sqrt(x));
              }, nullptr, false, "número", "La raíz cuadrada."});
    t.append({"POWER", "POTENCIA", 2, 2, FN {
                  NUM(x, 0);
                  NUM(y, 1);
                  if (x == 0 && y < 0) {
                      return Arg(Err::Div0);
                  }
                  return checked(std::pow(x, y));
              }, nullptr, false, "número; potencia", "Eleva un número a una potencia."});
    t.append({"MOD", "RESIDUO", 2, 2, FN {
                  NUM(x, 0);
                  NUM(y, 1);
                  if (y == 0) {
                      return Arg(Err::Div0);
                  }
                  // The sign of the divisor, as spreadsheets do.
                  return Arg(x - y * std::floor(x / y));
              }, nullptr, false, "número; divisor", "El resto de una división."});
    t.append({"QUOTIENT", "COCIENTE", 2, 2, FN {
                  NUM(x, 0);
                  NUM(y, 1);
                  if (y == 0) {
                      return Arg(Err::Div0);
                  }
                  return Arg(std::trunc(x / y));
              }, nullptr, false, "numerador; denominador", "La parte entera de una división."});
    t.append({"EXP", "EXP", 1, 1, FN {
                  NUM(x, 0);
                  return checked(std::exp(x));
              }, nullptr, false, "número", "e elevado al número."});
    t.append({"LN", "LN", 1, 1, FN {
                  NUM(x, 0);
                  if (x <= 0) {
                      return Arg(Err::Num);
                  }
                  return Arg(std::log(x));
              }, nullptr, false, "número", "El logaritmo natural."});
    t.append({"LOG10", "LOG10", 1, 1, FN {
                  NUM(x, 0);
                  if (x <= 0) {
                      return Arg(Err::Num);
                  }
                  return Arg(std::log10(x));
              }, nullptr, false, "número", "El logaritmo en base 10."});
    t.append({"LOG", "LOG", 1, 2, FN {
                  NUM(x, 0);
                  OPT(base, 1, 10);
                  if (x <= 0 || base <= 0 || base == 1) {
                      return Arg(Err::Num);
                  }
                  return Arg(std::log(x) / std::log(base));
              }, nullptr, false, "número; [base]", "El logaritmo en la base indicada."});
    t.append({"PI", "PI", 0, 0, FN {
                  Q_UNUSED(E);
                  Q_UNUSED(a);
                  return Arg(M_PI);
              }, nullptr, false, "", "El número π."});
#define TRIG(NAME, ES, EXPR, HELP)                                                                                                                             \
    t.append({NAME, ES, 1, 1, FN {                                                                                                                             \
                  NUM(x, 0);                                                                                                                                   \
                  return checked(EXPR);                                                                                                                        \
              }, nullptr, false, "número", HELP});
    TRIG("SIN", "SENO", std::sin(x), "El seno de un ángulo en radianes.")
    TRIG("COS", "COS", std::cos(x), "El coseno de un ángulo en radianes.")
    TRIG("TAN", "TAN", std::tan(x), "La tangente de un ángulo en radianes.")
    TRIG("ASIN", "ASENO", std::asin(x), "El arcoseno, en radianes.")
    TRIG("ACOS", "ACOS", std::acos(x), "El arcocoseno, en radianes.")
    TRIG("ATAN", "ATAN", std::atan(x), "La arcotangente, en radianes.")
    TRIG("DEGREES", "GRADOS", x * 180.0 / M_PI, "Pasa radianes a grados.")
    TRIG("RADIANS", "RADIANES", x * M_PI / 180.0, "Pasa grados a radianes.")
#undef TRIG
    t.append({"ATAN2", "ATAN2", 2, 2, FN {
                  NUM(x, 0);
                  NUM(y, 1);
                  if (x == 0 && y == 0) {
                      return Arg(Err::Div0);
                  }
                  return Arg(std::atan2(y, x));
              }, nullptr, false, "x; y", "El ángulo del punto (x, y)."});
    t.append({"CEILING", "MULTIPLO.SUPERIOR", 1, 2, FN {
                  NUM(x, 0);
                  OPT(step, 1, 1);
                  if (step == 0) {
                      return Arg(0.0);
                  }
                  const double q = x / step;
                  return Arg(std::ceil(q - std::fabs(q) * 1e-14) * step);
              }, nullptr, false, "número; [múltiplo]", "Redondea hacia arriba al múltiplo."});
    t.append({"FLOOR", "MULTIPLO.INFERIOR", 1, 2, FN {
                  NUM(x, 0);
                  OPT(step, 1, 1);
                  if (step == 0) {
                      return Arg(Err::Div0);
                  }
                  const double q = x / step;
                  return Arg(std::floor(q + std::fabs(q) * 1e-14) * step);
              }, nullptr, false, "número; [múltiplo]", "Redondea hacia abajo al múltiplo."});
    t.append({"MROUND", "REDOND.MULT", 2, 2, FN {
                  NUM(x, 0);
                  NUM(step, 1);
                  if (step == 0) {
                      return Arg(0.0);
                  }
                  if ((x < 0) != (step < 0) && x != 0) {
                      return Arg(Err::Num);
                  }
                  return Arg(roundTo(x / step, 0, 0) * step);
              }, nullptr, false, "número; múltiplo", "Redondea al múltiplo más cercano."});
    t.append({"EVEN", "REDONDEA.PAR", 1, 1, FN {
                  NUM(x, 0);
                  const double up = std::ceil(std::fabs(x) / 2.0) * 2.0;
                  return Arg(x < 0 ? -up : up);
              }, nullptr, false, "número", "El par siguiente, alejándose de cero."});
    t.append({"ODD", "REDONDEA.IMPAR", 1, 1, FN {
                  NUM(x, 0);
                  double up = std::ceil(std::fabs(x));
                  if (std::fmod(up, 2.0) == 0) {
                      up += 1;
                  }
                  return Arg(x < 0 ? -up : up);
              }, nullptr, false, "número", "El impar siguiente, alejándose de cero."});
    t.append({"FACT", "FACT", 1, 1, FN {
                  NUM(x, 0);
                  if (x < 0 || x > 170) {
                      return Arg(Err::Num);
                  }
                  double f = 1;
                  for (int i = 2; i <= int(x); ++i) {
                      f *= i;
                  }
                  return Arg(f);
              }, nullptr, false, "número", "El factorial."});
    t.append({"COMBIN", "COMBINAT", 2, 2, FN {
                  NUM(n, 0);
                  NUM(k, 1);
                  n = std::floor(n);
                  k = std::floor(k);
                  if (n < 0 || k < 0 || k > n) {
                      return Arg(Err::Num);
                  }
                  double c = 1;
                  for (int i = 1; i <= int(k); ++i) {
                      c = c * (n - k + i) / i;
                  }
                  return checked(std::round(c));
              }, nullptr, false, "número; tamaño", "De cuántas maneras se eligen elementos."});
    t.append({"GCD", "M.C.D", 1, -1, FN {
                  QList<double> list;
                  Value error;
                  if (!numbers(E, a, &list, &error)) {
                      return Arg(error);
                  }
                  double g = 0;
                  for (double x : std::as_const(list)) {
                      if (x < 0) {
                          return Arg(Err::Num);
                      }
                      g = gcd(g, x);
                  }
                  return Arg(g);
              }, nullptr, false, "número1; [número2]; …", "El máximo común divisor."});
    t.append({"LCM", "M.C.M", 1, -1, FN {
                  QList<double> list;
                  Value error;
                  if (!numbers(E, a, &list, &error)) {
                      return Arg(error);
                  }
                  double l = 1;
                  for (double x : std::as_const(list)) {
                      if (x < 0) {
                          return Arg(Err::Num);
                      }
                      x = std::floor(x);
                      if (x == 0) {
                          return Arg(0.0);
                      }
                      l = l / gcd(l, x) * x;
                  }
                  return checked(l);
              }, nullptr, false, "número1; [número2]; …", "El mínimo común múltiplo."});
    t.append({"RAND", "ALEATORIO", 0, 0, FN {
                  Q_UNUSED(E);
                  Q_UNUSED(a);
                  return Arg(QRandomGenerator::global()->generateDouble());
              }, nullptr, true, "", "Un número al azar entre 0 y 1."});
    t.append({"RANDBETWEEN", "ALEATORIO.ENTRE", 2, 2, FN {
                  NUM(low, 0);
                  NUM(high, 1);
                  low = std::ceil(low);
                  high = std::floor(high);
                  if (low > high) {
                      return Arg(Err::Num);
                  }
                  return Arg(low + std::floor(QRandomGenerator::global()->generateDouble() * (high - low + 1)));
              }, nullptr, true, "inferior; superior", "Un entero al azar entre dos."});
    t.append({"SUMIF", "SUMAR.SI", 2, 3, FN {
                  return ifs(E, a, given(a, 2) ? 2 : 0, 0, [](const QList<double> &list) {
                      return Arg(sumOf(list));
                  });
              }, nullptr, false, "rango; criterio; [rango_suma]", "Suma las celdas que cumplen un criterio."});
    t.append({"AVERAGEIF", "PROMEDIO.SI", 2, 3, FN {
                  return ifs(E, a, given(a, 2) ? 2 : 0, 0, [](const QList<double> &list) {
                      return list.isEmpty() ? Arg(Err::Div0) : Arg(sumOf(list) / list.size());
                  });
              }, nullptr, false, "rango; criterio; [rango_promedio]", "La media de las celdas que cumplen un criterio."});
    t.append({"COUNTIF", "CONTAR.SI", 2, 2, FN {
                  return ifs(E, a, -1, 0, [](const QList<double> &list) {
                      return Arg(double(list.size()));
                  });
              }, nullptr, false, "rango; criterio", "Cuántas celdas cumplen un criterio."});
    t.append({"COUNTIFS", "CONTAR.SI.CONJUNTO", 2, -1, FN {
                  return ifs(E, a, -1, 0, [](const QList<double> &list) {
                      return Arg(double(list.size()));
                  });
              }, nullptr, false, "rango1; criterio1; [rango2; criterio2]; …", "Cuántas filas cumplen todos los criterios."});
    t.append({"SUMIFS", "SUMAR.SI.CONJUNTO", 3, -1, FN {
                  return ifs(E, a, 0, 1, [](const QList<double> &list) {
                      return Arg(sumOf(list));
                  });
              }, nullptr, false, "rango_suma; rango1; criterio1; …", "Suma las celdas que cumplen todos los criterios."});
    t.append({"AVERAGEIFS", "PROMEDIO.SI.CONJUNTO", 3, -1, FN {
                  return ifs(E, a, 0, 1, [](const QList<double> &list) {
                      return list.isEmpty() ? Arg(Err::Div0) : Arg(sumOf(list) / list.size());
                  });
              }, nullptr, false, "rango_promedio; rango1; criterio1; …", "La media de las que cumplen todos los criterios."});
    t.append({"MAXIFS", "MAX.SI.CONJUNTO", 3, -1, FN {
                  return ifs(E, a, 0, 1, [](const QList<double> &list) {
                      return Arg(list.isEmpty() ? 0.0 : *std::max_element(list.begin(), list.end()));
                  });
              }, nullptr, false, "rango_max; rango1; criterio1; …", "El mayor de los que cumplen los criterios."});
    t.append({"MINIFS", "MIN.SI.CONJUNTO", 3, -1, FN {
                  return ifs(E, a, 0, 1, [](const QList<double> &list) {
                      return Arg(list.isEmpty() ? 0.0 : *std::min_element(list.begin(), list.end()));
                  });
              }, nullptr, false, "rango_min; rango1; criterio1; …", "El menor de los que cumplen los criterios."});
    t.append({"SUMPRODUCT", "SUMAPRODUCTO", 1, -1, FN {
                  QList<Matrix> blocks;
                  for (const Arg &arg : std::as_const(a)) {
                      blocks.append(E.matrix(arg));
                  }
                  // Ranges clipped at the last cell in use may differ in length: the rest are zeros.
                  int rows = 0, cols = 0;
                  for (const Arg &arg : std::as_const(a)) {
                      const int r = arg.kind == Arg::Range ? arg.ref.rows() : E.matrix(arg).rows;
                      const int c = arg.kind == Arg::Range ? arg.ref.cols() : E.matrix(arg).cols;
                      if (&arg != &a.first() && (r != rows || c != cols)) {
                          return Arg(Err::Value);
                      }
                      rows = r;
                      cols = c;
                  }
                  int usedRows = 0, usedCols = 0;
                  for (const Matrix &m : std::as_const(blocks)) {
                      usedRows = qMax(usedRows, m.rows);
                      usedCols = qMax(usedCols, m.cols);
                  }
                  double sum = 0;
                  for (int r = 0; r < usedRows; ++r) {
                      for (int c = 0; c < usedCols; ++c) {
                          double product = 1;
                          for (const Matrix &m : std::as_const(blocks)) {
                              const Value value = r < m.rows && c < m.cols ? m.at(r, c) : Value();
                              if (value.isError()) {
                                  return Arg(value);
                              }
                              product *= value.isNumber() || value.isBool() ? value.num : 0.0;
                          }
                          sum += product;
                      }
                  }
                  return Arg(sum);
              }, nullptr, false, "matriz1; [matriz2]; …", "Multiplica las matrices celda a celda y suma."});
}

// --- Logic and information --------------------------------------------------------

void registerLogic(QList<FuncDef> &t)
{
    t.append({"IF", "SI", 2, 3, nullptr, LAZY {
                  const Value test = E.scalar(E.eval(*node.args.at(0)));
                  bool on = false;
                  Value error;
                  if (!Eval::truth(test, &on, &error)) {
                      return Arg(error);
                  }
                  if (on) {
                      const Arg result = E.eval(*node.args.at(1));
                      return result.missing ? Arg(0.0) : result;
                  }
                  if (node.args.size() < 3) {
                      return Arg(Value::boolean(false));
                  }
                  const Arg result = E.eval(*node.args.at(2));
                  return result.missing ? Arg(0.0) : result;
              }, false, "condición; si_verdadero; [si_falso]", "Un valor u otro según se cumpla la condición."});
    t.append({"IFERROR", "SI.ERROR", 2, 2, nullptr, LAZY {
                  const Arg first = E.eval(*node.args.at(0));
                  const Value value = E.scalar(first);
                  if (!value.isError()) {
                      return first.kind == Arg::Scalar ? first : Arg(value);
                  }
                  const Arg other = E.eval(*node.args.at(1));
                  return other.missing ? Arg(Value(QString())) : other;
              }, false, "valor; si_error", "El valor, o lo indicado si da error."});
    t.append({"IFNA", "SI.ND", 2, 2, nullptr, LAZY {
                  const Value value = E.scalar(E.eval(*node.args.at(0)));
                  if (!(value.isError() && value.err == Err::NA)) {
                      return Arg(value);
                  }
                  return E.eval(*node.args.at(1));
              }, false, "valor; si_nd", "El valor, o lo indicado si da #N/A."});
    t.append({"IFS", "SI.CONJUNTO", 2, -1, nullptr, LAZY {
                  for (int i = 0; i + 1 < node.args.size(); i += 2) {
                      bool on = false;
                      Value error;
                      if (!Eval::truth(E.scalar(E.eval(*node.args.at(i))), &on, &error)) {
                          return Arg(error);
                      }
                      if (on) {
                          return E.eval(*node.args.at(i + 1));
                      }
                  }
                  return Arg(Err::NA);
              }, false, "condición1; valor1; [condición2; valor2]; …", "El valor de la primera condición que se cumple."});
    t.append({"SWITCH", "CAMBIAR", 3, -1, nullptr, LAZY {
                  const Value subject = E.scalar(E.eval(*node.args.at(0)));
                  if (subject.isError()) {
                      return Arg(subject);
                  }
                  int i = 1;
                  for (; i + 1 < node.args.size(); i += 2) {
                      const Value candidate = E.scalar(E.eval(*node.args.at(i)));
                      if (!candidate.isError() && Eval::compare(subject, candidate) == 0) {
                          return E.eval(*node.args.at(i + 1));
                      }
                  }
                  // An odd one left at the end is the default.
                  if (i < node.args.size()) {
                      return E.eval(*node.args.at(i));
                  }
                  return Arg(Err::NA);
              }, false, "expresión; valor1; resultado1; …; [por_defecto]", "El resultado del valor que coincide."});
    t.append({"CHOOSE", "ELEGIR", 2, -1, nullptr, LAZY {
                  double index = 0;
                  Value error;
                  if (!Eval::number(E.scalar(E.eval(*node.args.at(0))), &index, &error)) {
                      return Arg(error);
                  }
                  const int i = int(std::floor(index));
                  if (i < 1 || i >= node.args.size()) {
                      return Arg(Err::Value);
                  }
                  return E.eval(*node.args.at(i));
              }, false, "índice; valor1; [valor2]; …", "El valor que ocupa el lugar indicado."});
#define LOGIC(NAME, ES, START, STEP, HELP)                                                                                                                     \
    t.append({NAME, ES, 1, -1, FN {                                                                                                                            \
                  bool result = START;                                                                                                                         \
                  bool any = false;                                                                                                                            \
                  Value error;                                                                                                                                 \
                  for (const Arg &arg : std::as_const(a)) {                                                                                                    \
                      if (arg.kind == Arg::Scalar) {                                                                                                           \
                          bool on = false;                                                                                                                     \
                          if (!Eval::truth(arg.v, &on, &error)) {                                                                                              \
                              return Arg(error);                                                                                                               \
                          }                                                                                                                                    \
                          any = true;                                                                                                                          \
                          STEP;                                                                                                                                \
                      } else {                                                                                                                                 \
                          E.each(arg, [&](const Value &value) {                                                                                                \
                              if (value.isError()) {                                                                                                           \
                                  error = value;                                                                                                               \
                              } else if (value.isNumber() || value.isBool()) {                                                                                 \
                                  const bool on = value.num != 0;                                                                                              \
                                  any = true;                                                                                                                  \
                                  STEP;                                                                                                                        \
                              }                                                                                                                                \
                          });                                                                                                                                  \
                          if (error.isError()) {                                                                                                               \
                              return Arg(error);                                                                                                               \
                          }                                                                                                                                    \
                      }                                                                                                                                        \
                  }                                                                                                                                            \
                  if (!any) {                                                                                                                                  \
                      return Arg(Err::Value);                                                                                                                  \
                  }                                                                                                                                            \
                  return Arg(Value::boolean(result));                                                                                                          \
              }, nullptr, false, "condición1; [condición2]; …", HELP});
    LOGIC("AND", "Y", true, result = result && on, "Verdadero si se cumplen todas.")
    LOGIC("OR", "O", false, result = result || on, "Verdadero si se cumple alguna.")
    LOGIC("XOR", "XO", false, result = result != on, "Verdadero si se cumple un número impar.")
#undef LOGIC
    t.append({"NOT", "NO", 1, 1, FN {
                  bool on = false;
                  Value error;
                  if (!Eval::truth(E.scalar(a[0]), &on, &error)) {
                      return Arg(error);
                  }
                  return Arg(Value::boolean(!on));
              }, nullptr, false, "condición", "Lo contrario de la condición."});
    t.append({"TRUE", "VERDADERO", 0, 0, FN {
                  Q_UNUSED(E);
                  Q_UNUSED(a);
                  return Arg(Value::boolean(true));
              }, nullptr, false, "", "El valor verdadero."});
    t.append({"FALSE", "FALSO", 0, 0, FN {
                  Q_UNUSED(E);
                  Q_UNUSED(a);
                  return Arg(Value::boolean(false));
              }, nullptr, false, "", "El valor falso."});
    t.append({"NA", "NOD", 0, 0, FN {
                  Q_UNUSED(E);
                  Q_UNUSED(a);
                  return Arg(Err::NA);
              }, nullptr, false, "", "El error #N/A: no hay dato."});
#define IS(NAME, ES, EXPR, HELP)                                                                                                                               \
    t.append({NAME, ES, 1, 1, FN {                                                                                                                             \
                  const Value v = E.scalar(a[0]);                                                                                                              \
                  return Arg(Value::boolean(EXPR));                                                                                                            \
              }, nullptr, false, "valor", HELP});
    IS("ISBLANK", "ESBLANCO", v.isEmpty(), "Si la celda está vacía.")
    IS("ISNUMBER", "ESNUMERO", v.isNumber(), "Si es un número.")
    IS("ISTEXT", "ESTEXTO", v.isText(), "Si es texto.")
    IS("ISNONTEXT", "ESNOTEXTO", !v.isText(), "Si no es texto.")
    IS("ISLOGICAL", "ESLOGICO", v.isBool(), "Si es verdadero o falso.")
    IS("ISERROR", "ESERROR", v.isError(), "Si es un error.")
    IS("ISERR", "ESERR", v.isError() && v.err != Err::NA, "Si es un error distinto de #N/A.")
    IS("ISNA", "ESNOD", v.isError() && v.err == Err::NA, "Si es el error #N/A.")
#undef IS
    t.append({"ISEVEN", "ES.PAR", 1, 1, FN {
                  NUM(x, 0);
                  return Arg(Value::boolean(std::fmod(std::trunc(x), 2.0) == 0));
              }, nullptr, false, "número", "Si el número es par."});
    t.append({"ISODD", "ES.IMPAR", 1, 1, FN {
                  NUM(x, 0);
                  return Arg(Value::boolean(std::fmod(std::trunc(x), 2.0) != 0));
              }, nullptr, false, "número", "Si el número es impar."});
    t.append({"N", "N", 1, 1, FN {
                  const Value v = E.scalar(a[0]);
                  if (v.isError()) {
                      return Arg(v);
                  }
                  return Arg(v.isNumber() || v.isBool() ? v.num : 0.0);
              }, nullptr, false, "valor", "El valor como número."});
}

// --- Text -----------------------------------------------------------------------

void registerText(QList<FuncDef> &t)
{
    static const auto join = FN {
        QString out;
        Value error;
        for (const Arg &arg : std::as_const(a)) {
            E.each(arg, [&](const Value &value) {
                if (value.isError() && !error.isError()) {
                    error = value;
                }
                out += Eval::text(value);
            });
        }
        return error.isError() ? Arg(error) : Arg(Value(out));
    };
    t.append({"CONCAT", "CONCAT", 1, -1, join, nullptr, false, "texto1; [texto2]; …", "Une varios textos en uno."});
    t.append({"CONCATENATE", "CONCATENAR", 1, -1, join, nullptr, false, "texto1; [texto2]; …", "Une varios textos en uno."});
    t.append({"TEXTJOIN", "UNIRCADENAS", 3, -1, FN {
                  TXT(separator, 0);
                  bool skipEmpty = false;
                  Value error;
                  if (!Eval::truth(E.scalar(a[1]), &skipEmpty, &error)) {
                      return Arg(error);
                  }
                  QStringList parts;
                  for (int i = 2; i < a.size(); ++i) {
                      const QList<Value> values = flat(E, a.at(i));
                      for (const Value &value : values) {
                          if (value.isError()) {
                              return Arg(value);
                          }
                          const QString piece = Eval::text(value);
                          if (!(skipEmpty && piece.isEmpty())) {
                              parts.append(piece);
                          }
                      }
                  }
                  return Arg(Value(parts.join(separator)));
              }, nullptr, false, "separador; ignorar_vacías; texto1; …", "Une textos con un separador."});
    t.append({"LEFT", "IZQUIERDA", 1, 2, FN {
                  TXT(s, 0);
                  OPT(n, 1, 1);
                  if (n < 0) {
                      return Arg(Err::Value);
                  }
                  return Arg(Value(s.left(int(n))));
              }, nullptr, false, "texto; [caracteres]", "Los primeros caracteres."});
    t.append({"RIGHT", "DERECHA", 1, 2, FN {
                  TXT(s, 0);
                  OPT(n, 1, 1);
                  if (n < 0) {
                      return Arg(Err::Value);
                  }
                  return Arg(Value(s.right(int(n))));
              }, nullptr, false, "texto; [caracteres]", "Los últimos caracteres."});
    t.append({"MID", "EXTRAE", 3, 3, FN {
                  TXT(s, 0);
                  NUM(start, 1);
                  NUM(n, 2);
                  if (start < 1 || n < 0) {
                      return Arg(Err::Value);
                  }
                  return Arg(Value(s.mid(int(start) - 1, int(n))));
              }, nullptr, false, "texto; inicio; caracteres", "Un trozo del texto."});
    t.append({"LEN", "LARGO", 1, 1, FN {
                  TXT(s, 0);
                  return Arg(double(s.size()));
              }, nullptr, false, "texto", "Cuántos caracteres tiene."});
    t.append({"UPPER", "MAYUSC", 1, 1, FN {
                  TXT(s, 0);
                  return Arg(Value(s.toUpper()));
              }, nullptr, false, "texto", "En mayúsculas."});
    t.append({"LOWER", "MINUSC", 1, 1, FN {
                  TXT(s, 0);
                  return Arg(Value(s.toLower()));
              }, nullptr, false, "texto", "En minúsculas."});
    t.append({"PROPER", "NOMPROPIO", 1, 1, FN {
                  TXT(s, 0);
                  bool start = true;
                  for (QChar &c : s) {
                      c = start ? c.toUpper() : c.toLower();
                      start = !c.isLetter();
                  }
                  return Arg(Value(s));
              }, nullptr, false, "texto", "La primera letra de cada palabra en mayúscula."});
    t.append({"TRIM", "ESPACIOS", 1, 1, FN {
                  TXT(s, 0);
                  return Arg(Value(s.split(QLatin1Char(' '), Qt::SkipEmptyParts).join(QLatin1Char(' '))));
              }, nullptr, false, "texto", "Quita los espacios que sobran."});
    t.append({"TEXT", "TEXTO", 2, 2, FN {
                  const Value v = E.scalar(a[0]);
                  if (v.isError()) {
                      return Arg(v);
                  }
                  TXT(code, 1);
                  Value subject = v;
                  if (v.isText()) {
                      const Value parsed = NumFmt::parseInput(v.text);
                      if (parsed.isNumber()) {
                          subject = parsed;
                      }
                  } else if (v.isEmpty()) {
                      subject = Value(0.0);
                  }
                  // «#.##0,00» is the same code written the Spanish way.
                  const qsizetype comma = code.lastIndexOf(QLatin1Char(','));
                  const qsizetype dot = code.lastIndexOf(QLatin1Char('.'));
                  static const QRegularExpression decimals(QStringLiteral("[0#],[0#]{1,2}(?![0#])"));
                  static const QRegularExpression grouped(QStringLiteral("[0#],[0#]{3}(?![0#])"));
                  if (!NumFmt::isDate(code) && comma >= 0 && ((dot >= 0 && comma > dot) || (dot < 0 && code.contains(decimals) && !code.contains(grouped)))) {
                      for (QChar &c : code) {
                          c = c == QLatin1Char(',') ? QLatin1Char('.') : c == QLatin1Char('.') ? QLatin1Char(',') : c;
                      }
                  }
                  return Arg(Value(NumFmt::format(subject, code)));
              }, nullptr, false, "valor; formato", "El valor escrito con un formato, como \"0,00\"."});
    t.append({"FIXED", "DECIMAL", 1, 3, FN {
                  NUM(x, 0);
                  OPT(digits, 1, 2);
                  OPT(plain, 2, 0);
                  const int places = int(qBound(0.0, digits, 15.0));
                  const QString code = (plain != 0 ? QStringLiteral("0") : QStringLiteral("#,##0")) + (places > 0 ? QLatin1Char('.') + QString(places, QLatin1Char('0')) : QString());
                  return Arg(Value(NumFmt::format(Value(roundTo(x, digits, 0)), code)));
              }, nullptr, false, "número; [decimales]; [sin_separador]", "El número como texto, con los decimales pedidos."});
    t.append({"DOLLAR", "MONEDA", 1, 2, FN {
                  NUM(x, 0);
                  OPT(digits, 1, 2);
                  return Arg(Value(NumFmt::format(Value(roundTo(x, digits, 0)), NumFmt::familyCode(QStringLiteral("currency"), int(qBound(0.0, digits, 15.0))))));
              }, nullptr, false, "número; [decimales]", "El número como texto con formato de moneda."});
    t.append({"VALUE", "VALOR", 1, 1, FN {
                  NUM(x, 0);
                  return Arg(x);
              }, nullptr, false, "texto", "El número que hay escrito en un texto."});
    t.append({"FIND", "ENCONTRAR", 2, 3, FN {
                  TXT(needle, 0);
                  TXT(hay, 1);
                  OPT(start, 2, 1);
                  if (start < 1 || start > hay.size() + 1) {
                      return Arg(Err::Value);
                  }
                  const int at = hay.indexOf(needle, int(start) - 1, Qt::CaseSensitive);
                  return at < 0 ? Arg(Err::Value) : Arg(double(at + 1));
              }, nullptr, false, "buscado; texto; [inicio]", "Dónde está un texto dentro de otro (distingue mayúsculas)."});
    t.append({"SEARCH", "HALLAR", 2, 3, FN {
                  TXT(needle, 0);
                  TXT(hay, 1);
                  OPT(start, 2, 1);
                  if (start < 1 || start > hay.size() + 1) {
                      return Arg(Err::Value);
                  }
                  QString pattern;
                  for (int i = 0; i < needle.size(); ++i) {
                      const QChar c = needle.at(i);
                      if (c == QLatin1Char('~') && i + 1 < needle.size()) {
                          pattern += QRegularExpression::escape(needle.mid(++i, 1));
                      } else if (c == QLatin1Char('*')) {
                          pattern += QLatin1String(".*?");
                      } else if (c == QLatin1Char('?')) {
                          pattern += QLatin1Char('.');
                      } else {
                          pattern += QRegularExpression::escape(QString(c));
                      }
                  }
                  const QRegularExpression expression(pattern, QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
                  const QRegularExpressionMatch match = expression.match(hay, int(start) - 1);
                  return match.hasMatch() ? Arg(double(match.capturedStart() + 1)) : Arg(Err::Value);
              }, nullptr, false, "buscado; texto; [inicio]", "Dónde está un texto dentro de otro (sin distinguir mayúsculas)."});
    t.append({"SUBSTITUTE", "SUSTITUIR", 3, 4, FN {
                  TXT(s, 0);
                  TXT(from, 1);
                  TXT(to, 2);
                  if (from.isEmpty()) {
                      return Arg(Value(s));
                  }
                  if (!given(a, 3)) {
                      return Arg(Value(s.replace(from, to)));
                  }
                  NUM(which, 3);
                  if (which < 1) {
                      return Arg(Err::Value);
                  }
                  int at = -1;
                  for (int i = 0; i < int(which); ++i) {
                      at = s.indexOf(from, at + 1);
                      if (at < 0) {
                          return Arg(Value(s));
                      }
                  }
                  return Arg(Value(s.replace(at, from.size(), to)));
              }, nullptr, false, "texto; viejo; nuevo; [aparición]", "Cambia un texto por otro."});
    t.append({"REPLACE", "REEMPLAZAR", 4, 4, FN {
                  TXT(s, 0);
                  NUM(start, 1);
                  NUM(n, 2);
                  TXT(with, 3);
                  if (start < 1 || n < 0) {
                      return Arg(Err::Value);
                  }
                  const int at = qMin(int(start) - 1, int(s.size()));
                  return Arg(Value(s.left(at) + with + s.mid(at + int(n))));
              }, nullptr, false, "texto; inicio; caracteres; nuevo", "Cambia un trozo del texto por posición."});
    t.append({"REPT", "REPETIR", 2, 2, FN {
                  TXT(s, 0);
                  NUM(n, 1);
                  if (n < 0 || n * s.size() > 32767) {
                      return Arg(Err::Value);
                  }
                  return Arg(Value(s.repeated(int(n))));
              }, nullptr, false, "texto; veces", "Repite un texto."});
    t.append({"EXACT", "IGUAL", 2, 2, FN {
                  TXT(x, 0);
                  TXT(y, 1);
                  return Arg(Value::boolean(x == y));
              }, nullptr, false, "texto1; texto2", "Si dos textos son idénticos."});
    t.append({"CHAR", "CARACTER", 1, 1, FN {
                  NUM(code, 0);
                  if (code < 1 || code > 0x10ffff) {
                      return Arg(Err::Value);
                  }
                  const char32_t c = char32_t(code);
                  return Arg(Value(QString::fromUcs4(&c, 1)));
              }, nullptr, false, "número", "El carácter de un código."});
    t.append({"CODE", "CODIGO", 1, 1, FN {
                  TXT(s, 0);
                  if (s.isEmpty()) {
                      return Arg(Err::Value);
                  }
                  return Arg(double(s.toUcs4().at(0)));
              }, nullptr, false, "texto", "El código del primer carácter."});
    t.append({"T", "T", 1, 1, FN {
                  const Value v = E.scalar(a[0]);
                  if (v.isError()) {
                      return Arg(v);
                  }
                  return Arg(Value(v.isText() ? v.text : QString()));
              }, nullptr, false, "valor", "El valor si es texto; si no, nada."});
}
