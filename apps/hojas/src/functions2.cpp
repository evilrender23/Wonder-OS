#include "fn_p.h"

#include <QDateTime>
#include <QSet>

#include <algorithm>

// --- Dates ----------------------------------------------------------------------

namespace
{
/// The date in an argument: a serial number, or text that reads as a date.
#define DATEARG(var, i)                                                                                                                                        \
    QDate var;                                                                                                                                                 \
    {                                                                                                                                                          \
        NUM(serial_, i);                                                                                                                                       \
        if (serial_ < 0 || serial_ > 2958465) {                                                                                                                \
            return Arg(Err::Num);                                                                                                                              \
        }                                                                                                                                                      \
        var = NumFmt::date(serial_);                                                                                                                           \
    }

bool holidays(Eval &E, const QList<Arg> &a, int i, QSet<qint64> *out, Value *error)
{
    if (!given(a, i)) {
        return true;
    }
    QList<double> list;
    if (!numbers(E, {a.at(i)}, &list, error)) {
        return false;
    }
    for (double d : std::as_const(list)) {
        out->insert(qint64(std::floor(d)));
    }
    return true;
}

bool workday(qint64 serial, const QSet<qint64> &off)
{
    // Serial 1 was a Sunday on this calendar (31 December 1899).
    const int weekday = int(((serial % 7) + 7) % 7); // 0 Saturday, 1 Sunday
    return weekday != 0 && weekday != 1 && !off.contains(serial);
}

QDate addMonths(const QDate &date, int months)
{
    return date.addMonths(months);
}
}

void registerDates(QList<FuncDef> &t)
{
    t.append({"TODAY", "HOY", 0, 0, FN {
                  Q_UNUSED(a);
                  return Arg(std::floor(E.ctx.host->now()));
              }, nullptr, true, "", "La fecha de hoy."});
    t.append({"NOW", "AHORA", 0, 0, FN {
                  Q_UNUSED(a);
                  return Arg(E.ctx.host->now());
              }, nullptr, true, "", "La fecha y la hora de ahora."});
    t.append({"DATE", "FECHA", 3, 3, FN {
                  NUM(year, 0);
                  NUM(month, 1);
                  NUM(day, 2);
                  int y = int(year);
                  if (y >= 0 && y < 1900) {
                      y += 1900;
                  }
                  // Month 14 and day 0 are fine: they spill over.
                  const QDate first(y, 1, 1);
                  if (!first.isValid()) {
                      return Arg(Err::Num);
                  }
                  const QDate date = first.addMonths(int(month) - 1).addDays(qint64(day) - 1);
                  const double serial = NumFmt::serial(date);
                  if (serial < 0) {
                      return Arg(Err::Num);
                  }
                  return Arg(serial);
              }, nullptr, false, "año; mes; día", "La fecha de un año, mes y día."});
    t.append({"TIME", "TIEMPO", 3, 3, FN {
                  NUM(h, 0);
                  NUM(m, 1);
                  NUM(s, 2);
                  const double fraction = (std::trunc(h) * 3600 + std::trunc(m) * 60 + std::trunc(s)) / 86400.0;
                  if (fraction < 0) {
                      return Arg(Err::Num);
                  }
                  return Arg(fraction - std::floor(fraction));
              }, nullptr, false, "hora; minuto; segundo", "Una hora del día."});
    t.append({"DATEVALUE", "FECHANUMERO", 1, 1, FN {
                  NUM(x, 0);
                  return Arg(std::floor(x));
              }, nullptr, false, "texto", "La fecha escrita en un texto."});
    t.append({"TIMEVALUE", "HORANUMERO", 1, 1, FN {
                  NUM(x, 0);
                  return Arg(x - std::floor(x));
              }, nullptr, false, "texto", "La hora escrita en un texto."});
    t.append({"YEAR", "AÑO", 1, 1, FN {
                  DATEARG(d, 0);
                  return Arg(double(d.year()));
              }, nullptr, false, "fecha", "El año de una fecha."});
    t.append({"MONTH", "MES", 1, 1, FN {
                  DATEARG(d, 0);
                  return Arg(double(d.month()));
              }, nullptr, false, "fecha", "El mes de una fecha, de 1 a 12."});
    t.append({"DAY", "DIA", 1, 1, FN {
                  DATEARG(d, 0);
                  return Arg(double(d.day()));
              }, nullptr, false, "fecha", "El día del mes de una fecha."});
#define CLOCK(NAME, ES, EXPR, HELP)                                                                                                                            \
    t.append({NAME, ES, 1, 1, FN {                                                                                                                             \
                  NUM(x, 0);                                                                                                                                   \
                  if (x < 0) {                                                                                                                                 \
                      return Arg(Err::Num);                                                                                                                    \
                  }                                                                                                                                            \
                  const qint64 seconds = qint64(std::floor((x - std::floor(x)) * 86400.0 + 0.5)) % 86400;                                                      \
                  return Arg(double(EXPR));                                                                                                                    \
              }, nullptr, false, "hora", HELP});
    CLOCK("HOUR", "HORA", seconds / 3600, "La hora, de 0 a 23.")
    CLOCK("MINUTE", "MINUTO", seconds / 60 % 60, "El minuto, de 0 a 59.")
    CLOCK("SECOND", "SEGUNDO", seconds % 60, "El segundo, de 0 a 59.")
#undef CLOCK
    t.append({"WEEKDAY", "DIASEM", 1, 2, FN {
                  DATEARG(d, 0);
                  OPT(type, 1, 1);
                  const int monday1 = d.dayOfWeek(); // 1 Monday … 7 Sunday
                  switch (int(type)) {
                  case 1:
                      return Arg(double(monday1 % 7 + 1));
                  case 2:
                      return Arg(double(monday1));
                  case 3:
                      return Arg(double(monday1 - 1));
                  default:
                      return Arg(Err::Num);
                  }
              }, nullptr, false, "fecha; [tipo]", "El día de la semana (tipo 2: lunes = 1)."});
    t.append({"WEEKNUM", "NUM.DE.SEMANA", 1, 2, FN {
                  DATEARG(d, 0);
                  OPT(type, 1, 1);
                  if (int(type) == 21) {
                      return Arg(double(d.weekNumber()));
                  }
                  if (int(type) != 1 && int(type) != 2) {
                      return Arg(Err::Num);
                  }
                  const QDate first(d.year(), 1, 1);
                  // How far into its week the year starts.
                  const int offset = int(type) == 1 ? first.dayOfWeek() % 7 : first.dayOfWeek() - 1;
                  return Arg(double((first.daysTo(d) + offset) / 7 + 1));
              }, nullptr, false, "fecha; [tipo]", "La semana del año (tipo 21: ISO)."});
    t.append({"ISOWEEKNUM", "ISO.NUM.DE.SEMANA", 1, 1, FN {
                  DATEARG(d, 0);
                  return Arg(double(d.weekNumber()));
              }, nullptr, false, "fecha", "La semana ISO del año."});
    t.append({"EDATE", "FECHA.MES", 2, 2, FN {
                  DATEARG(d, 0);
                  NUM(months, 1);
                  return Arg(NumFmt::serial(addMonths(d, int(months))));
              }, nullptr, false, "fecha; meses", "La fecha unos meses antes o después."});
    t.append({"EOMONTH", "FIN.MES", 2, 2, FN {
                  DATEARG(d, 0);
                  NUM(months, 1);
                  const QDate moved = addMonths(QDate(d.year(), d.month(), 1), int(months));
                  return Arg(NumFmt::serial(QDate(moved.year(), moved.month(), moved.daysInMonth())));
              }, nullptr, false, "fecha; meses", "El último día del mes, unos meses antes o después."});
    t.append({"DAYS", "DIAS", 2, 2, FN {
                  NUM(end, 0);
                  NUM(start, 1);
                  return Arg(std::floor(end) - std::floor(start));
              }, nullptr, false, "fecha_final; fecha_inicial", "Los días entre dos fechas."});
    t.append({"DATEDIF", "SIFECHA", 3, 3, FN {
                  DATEARG(start, 0);
                  DATEARG(end, 1);
                  TXT(unit, 2);
                  if (start > end) {
                      return Arg(Err::Num);
                  }
                  unit = unit.toUpper();
                  int months = (end.year() - start.year()) * 12 + end.month() - start.month();
                  if (end.day() < start.day()) {
                      --months;
                  }
                  if (unit == QLatin1String("Y")) {
                      return Arg(double(months / 12));
                  }
                  if (unit == QLatin1String("M")) {
                      return Arg(double(months));
                  }
                  if (unit == QLatin1String("D")) {
                      return Arg(double(start.daysTo(end)));
                  }
                  if (unit == QLatin1String("YM")) {
                      return Arg(double(months % 12));
                  }
                  if (unit == QLatin1String("MD")) {
                      return Arg(double(start.addMonths(months).daysTo(end)));
                  }
                  if (unit == QLatin1String("YD")) {
                      return Arg(double(start.addYears(months / 12).daysTo(end)));
                  }
                  return Arg(Err::Num);
              }, nullptr, false, "inicio; fin; unidad", "Años \"Y\", meses \"M\" o días \"D\" entre dos fechas."});
    t.append({"NETWORKDAYS", "DIAS.LAB", 2, 3, FN {
                  NUM(startSerial, 0);
                  NUM(endSerial, 1);
                  QSet<qint64> off;
                  Value error;
                  if (!holidays(E, a, 2, &off, &error)) {
                      return Arg(error);
                  }
                  qint64 from = qint64(std::floor(startSerial));
                  qint64 to = qint64(std::floor(endSerial));
                  const double sign = from > to ? -1 : 1;
                  if (from > to) {
                      std::swap(from, to);
                  }
                  if (to - from > 4000000) {
                      return Arg(Err::Num);
                  }
                  // Whole weeks first; the days left over one by one.
                  const qint64 weeks = (to - from + 1) / 7;
                  double count = double(weeks) * 5;
                  for (qint64 day = from + weeks * 7; day <= to; ++day) {
                      const int weekday = int(((day % 7) + 7) % 7);
                      if (weekday != 0 && weekday != 1) {
                          ++count;
                      }
                  }
                  for (qint64 day : std::as_const(off)) {
                      const int weekday = int(((day % 7) + 7) % 7);
                      if (day >= from && day <= to && weekday != 0 && weekday != 1) {
                          --count;
                      }
                  }
                  return Arg(sign * count);
              }, nullptr, false, "inicio; fin; [festivos]", "Los días laborables entre dos fechas, ambas incluidas."});
    t.append({"WORKDAY", "DIA.LAB", 2, 3, FN {
                  NUM(startSerial, 0);
                  NUM(days, 1);
                  QSet<qint64> off;
                  Value error;
                  if (!holidays(E, a, 2, &off, &error)) {
                      return Arg(error);
                  }
                  qint64 day = qint64(std::floor(startSerial));
                  qint64 left = qint64(std::trunc(days));
                  if (std::llabs(left) > 1000000) {
                      return Arg(Err::Num);
                  }
                  const int step = left < 0 ? -1 : 1;
                  while (left != 0) {
                      day += step;
                      if (workday(day, off)) {
                          left -= step;
                      }
                  }
                  return day < 0 ? Arg(Err::Num) : Arg(double(day));
              }, nullptr, false, "inicio; días; [festivos]", "La fecha tras unos días laborables."});
}

// --- Lookup -----------------------------------------------------------------------

namespace
{
bool sameKind(const Value &x, const Value &y)
{
    return (x.isNumber() && y.isNumber()) || (x.isText() && y.isText()) || (x.isBool() && y.isBool());
}

/// The place of `wanted` in a list. Mode 0 is exact (text may carry * and ?),
/// 1 the last one not above it, -1 the first one not below it. -1 when none.
int findIn(const QList<Value> &list, const Value &wanted, int mode, bool wildcards = true, bool backwards = false)
{
    if (mode == 0) {
        const bool pattern = wildcards && wanted.isText() && (wanted.text.contains(QLatin1Char('*')) || wanted.text.contains(QLatin1Char('?')));
        if (pattern) {
            const Criterion criterion(wanted);
            for (int k = 0; k < list.size(); ++k) {
                const int i = backwards ? list.size() - 1 - k : k;
                if (list.at(i).isText() && criterion.matches(list.at(i))) {
                    return i;
                }
            }
            return -1;
        }
        for (int k = 0; k < list.size(); ++k) {
            const int i = backwards ? list.size() - 1 - k : k;
            if (sameKind(list.at(i), wanted) && Eval::compare(list.at(i), wanted) == 0) {
                return i;
            }
        }
        return -1;
    }
    // Works whether the list is sorted or not: the closest on the right side.
    int best = -1;
    for (int i = 0; i < list.size(); ++i) {
        const Value &value = list.at(i);
        if (!sameKind(value, wanted)) {
            continue;
        }
        const int order = Eval::compare(value, wanted);
        if (order == 0) {
            return i;
        }
        if (mode > 0 && order < 0 && (best < 0 || Eval::compare(value, list.at(best)) >= 0)) {
            best = i;
        }
        if (mode < 0 && order > 0 && (best < 0 || Eval::compare(value, list.at(best)) < 0)) {
            best = i;
        }
    }
    return best;
}

Arg lookupTable(Eval &E, QList<Arg> &a, bool vertical)
{
    const Value wanted = E.scalar(a[0]);
    if (wanted.isError()) {
        return Arg(wanted);
    }
    const Matrix table = E.matrix(a[1]);
    NUM(index, 2);
    bool approximate = true;
    if (given(a, 3)) {
        Value error;
        if (!Eval::truth(E.scalar(a[3]), &approximate, &error)) {
            return Arg(error);
        }
    }
    int declaredRows = table.rows, declaredCols = table.cols;
    if (a[1].kind == Arg::Range) {
        declaredRows = a[1].ref.rows();
        declaredCols = a[1].ref.cols();
    }
    const int want = int(index);
    if (want < 1) {
        return Arg(Err::Value);
    }
    if (want > (vertical ? declaredCols : declaredRows)) {
        return Arg(Err::Ref);
    }
    QList<Value> keys;
    const int count = vertical ? table.rows : table.cols;
    keys.reserve(count);
    for (int i = 0; i < count; ++i) {
        keys.append(vertical ? table.at(i, 0) : table.at(0, i));
    }
    const int found = findIn(keys, wanted, approximate ? 1 : 0);
    if (found < 0) {
        return Arg(Err::NA);
    }
    const int r = vertical ? found : want - 1;
    const int c = vertical ? want - 1 : found;
    if (r >= table.rows || c >= table.cols) {
        return Arg(Value());
    }
    return Arg(table.at(r, c));
}
}

void registerLookup(QList<FuncDef> &t)
{
    t.append({"ADDRESS", "DIRECCION", 2, 3, FN {
                  NUM(row, 0);
                  NUM(col, 1);
                  OPT(kind, 2, 1);
                  if (row < 1 || row > Limits::Rows || col < 1 || col > Limits::Cols) {
                      return Arg(Err::Value);
                  }
                  const bool absRow = kind == 1 || kind == 2;
                  const bool absCol = kind == 1 || kind == 3;
                  return Arg(Value((absCol ? QStringLiteral("$") : QString()) + Formula::columnName(int(col) - 1) + (absRow ? QStringLiteral("$") : QString())
                                   + QString::number(int(row))));
              }, nullptr, false, "fila; columna; [tipo]", "El nombre de una celda, como $B$3."});
    t.append({"VLOOKUP", "BUSCARV", 3, 4, FN {
                  return lookupTable(E, a, true);
              }, nullptr, false, "buscado; tabla; columna; [aproximado]", "Busca en la primera columna y da el valor de otra."});
    t.append({"HLOOKUP", "BUSCARH", 3, 4, FN {
                  return lookupTable(E, a, false);
              }, nullptr, false, "buscado; tabla; fila; [aproximado]", "Busca en la primera fila y da el valor de otra."});
    t.append({"MATCH", "COINCIDIR", 2, 3, FN {
                  const Value wanted = E.scalar(a[0]);
                  if (wanted.isError()) {
                      return Arg(wanted);
                  }
                  OPT(type, 2, 1);
                  const int found = findIn(flat(E, a[1]), wanted, type == 0 ? 0 : (type > 0 ? 1 : -1));
                  return found < 0 ? Arg(Err::NA) : Arg(double(found + 1));
              }, nullptr, false, "buscado; rango; [tipo]", "La posición de un valor en un rango (tipo 0: exacto)."});
    t.append({"INDEX", "INDICE", 2, 3, FN {
                  OPT(row, 1, 0);
                  OPT(col, 2, 0);
                  int rows = 1, cols = 1;
                  if (a[0].kind == Arg::Range) {
                      rows = a[0].ref.rows();
                      cols = a[0].ref.cols();
                  } else {
                      E.size(a[0], &rows, &cols);
                  }
                  int r = int(row), c = int(col);
                  // One number is enough for a single row or column.
                  if (!given(a, 2) && rows == 1 && cols > 1) {
                      c = r;
                      r = 1;
                  }
                  if (r < 0 || c < 0 || r > rows || c > cols) {
                      return Arg(Err::Ref);
                  }
                  if (a[0].kind == Arg::Range) {
                      RangeRef out = a[0].ref;
                      if (r > 0) {
                          out.r1 = out.r2 = a[0].ref.r1 + r - 1;
                      }
                      if (c > 0) {
                          out.c1 = out.c2 = a[0].ref.c1 + c - 1;
                      }
                      return Arg(out);
                  }
                  return Arg(E.at(a[0], qMax(r, 1) - 1, qMax(c, 1) - 1));
              }, nullptr, false, "rango; fila; [columna]", "El valor de una fila y columna del rango."});
    t.append({"XLOOKUP", "BUSCARX", 3, 6, FN {
                  const Value wanted = E.scalar(a[0]);
                  if (wanted.isError()) {
                      return Arg(wanted);
                  }
                  OPT(matchMode, 4, 0);
                  OPT(searchMode, 5, 1);
                  const QList<Value> keys = flat(E, a[1]);
                  const int mode = int(matchMode);
                  const int found = findIn(keys, wanted, mode == 2 ? 0 : (mode == 1 ? -1 : (mode == -1 ? 1 : 0)), mode == 2, searchMode < 0);
                  if (found < 0) {
                      if (given(a, 3)) {
                          return a[3];
                      }
                      return Arg(Err::NA);
                  }
                  int keyRows = 1, keyCols = 1;
                  E.size(a[1], &keyRows, &keyCols);
                  const bool down = keyCols == 1;
                  if (a[2].kind == Arg::Range) {
                      RangeRef out = a[2].ref;
                      if (down) {
                          out.r1 = out.r2 = out.r1 + found;
                      } else {
                          out.c1 = out.c2 = out.c1 + found;
                      }
                      return Arg(out);
                  }
                  return Arg(down ? E.at(a[2], found, 0) : E.at(a[2], 0, found));
              }, nullptr, false, "buscado; rango_búsqueda; rango_resultado; [si_no_está]; [modo]; [orden]", "Busca en un rango y da el valor de otro."});
    t.append({"LOOKUP", "BUSCAR", 2, 3, FN {
                  const Value wanted = E.scalar(a[0]);
                  if (wanted.isError()) {
                      return Arg(wanted);
                  }
                  const Matrix table = E.matrix(a[1]);
                  QList<Value> keys;
                  QList<Value> results;
                  if (given(a, 2)) {
                      keys = table.cells;
                      results = flat(E, a[2]);
                  } else if (table.cols > table.rows) {
                      for (int c = 0; c < table.cols; ++c) {
                          keys.append(table.at(0, c));
                          results.append(table.at(table.rows - 1, c));
                      }
                  } else {
                      for (int r = 0; r < table.rows; ++r) {
                          keys.append(table.at(r, 0));
                          results.append(table.at(r, table.cols - 1));
                      }
                  }
                  const int found = findIn(keys, wanted, 1);
                  if (found < 0) {
                      return Arg(Err::NA);
                  }
                  return Arg(found < results.size() ? results.at(found) : Value());
              }, nullptr, false, "buscado; rango_búsqueda; [rango_resultado]", "Busca el valor más cercano por debajo."});
    t.append({"OFFSET", "DESREF", 3, 5, FN {
                  if (a[0].kind != Arg::Range) {
                      return Arg(a[0].v.isError() ? a[0].v : Value(Err::Value));
                  }
                  NUM(rows, 1);
                  NUM(cols, 2);
                  OPT(height, 3, a[0].ref.rows());
                  OPT(width, 4, a[0].ref.cols());
                  if (height < 1 || width < 1) {
                      return Arg(Err::Ref);
                  }
                  RangeRef out = a[0].ref;
                  out.r1 += int(rows);
                  out.c1 += int(cols);
                  out.r2 = out.r1 + int(height) - 1;
                  out.c2 = out.c1 + int(width) - 1;
                  if (out.r1 < 0 || out.c1 < 0 || out.r2 >= Limits::Rows || out.c2 >= Limits::Cols) {
                      return Arg(Err::Ref);
                  }
                  return Arg(out);
              }, nullptr, true, "referencia; filas; columnas; [alto]; [ancho]", "Un rango desplazado desde una celda."});
    t.append({"INDIRECT", "INDIRECTO", 1, 1, FN {
                  TXT(text, 0);
                  const NodePtr node = Formula::parse(text);
                  if (!node || (node->kind != Node::Ref && node->kind != Node::Range)) {
                      return Arg(Err::Ref);
                  }
                  return E.eval(*node);
              }, nullptr, true, "texto", "La celda o el rango que nombra un texto."});
    t.append({"ROW", "FILA", 0, 1, FN {
                  if (!given(a, 0)) {
                      return Arg(double(E.ctx.row + 1));
                  }
                  if (a[0].kind != Arg::Range) {
                      return Arg(Err::Value);
                  }
                  return Arg(double(a[0].ref.r1 + 1));
              }, nullptr, false, "[referencia]", "El número de fila."});
    t.append({"COLUMN", "COLUMNA", 0, 1, FN {
                  if (!given(a, 0)) {
                      return Arg(double(E.ctx.col + 1));
                  }
                  if (a[0].kind != Arg::Range) {
                      return Arg(Err::Value);
                  }
                  return Arg(double(a[0].ref.c1 + 1));
              }, nullptr, false, "[referencia]", "El número de columna."});
    t.append({"ROWS", "FILAS", 1, 1, FN {
                  if (a[0].kind == Arg::Range) {
                      return Arg(double(a[0].ref.rows()));
                  }
                  int rows = 1, cols = 1;
                  E.size(a[0], &rows, &cols);
                  return Arg(double(rows));
              }, nullptr, false, "rango", "Cuántas filas tiene el rango."});
    t.append({"COLUMNS", "COLUMNAS", 1, 1, FN {
                  if (a[0].kind == Arg::Range) {
                      return Arg(double(a[0].ref.cols()));
                  }
                  int rows = 1, cols = 1;
                  E.size(a[0], &rows, &cols);
                  return Arg(double(cols));
              }, nullptr, false, "rango", "Cuántas columnas tiene el rango."});
}

// --- Statistics -------------------------------------------------------------------

namespace
{
#define LIST(var, from)                                                                                                                                        \
    QList<double> var;                                                                                                                                         \
    {                                                                                                                                                          \
        Value e_;                                                                                                                                              \
        if (!numbers(E, a, &var, &e_, from)) {                                                                                                                 \
            return Arg(e_);                                                                                                                                    \
        }                                                                                                                                                      \
    }
/// The numbers of one argument only.
#define LIST1(var, i)                                                                                                                                          \
    QList<double> var;                                                                                                                                         \
    {                                                                                                                                                          \
        Value e_;                                                                                                                                              \
        if (!numbers(E, {a.at(i)}, &var, &e_)) {                                                                                                               \
            return Arg(e_);                                                                                                                                    \
        }                                                                                                                                                      \
    }

double mean(const QList<double> &list)
{
    double sum = 0;
    for (double x : list) {
        sum += x;
    }
    return sum / list.size();
}

Arg variance(const QList<double> &list, bool sample, bool root)
{
    const int n = list.size();
    if (n < (sample ? 2 : 1)) {
        return Arg(Err::Div0);
    }
    const double m = mean(list);
    double sum = 0;
    for (double x : list) {
        sum += (x - m) * (x - m);
    }
    const double v = sum / (sample ? n - 1 : n);
    return Arg(root ? std::sqrt(v) : v);
}

Arg percentile(QList<double> list, double k)
{
    if (list.isEmpty() || k < 0 || k > 1) {
        return Arg(Err::Num);
    }
    std::sort(list.begin(), list.end());
    const double position = k * (list.size() - 1);
    const int low = int(std::floor(position));
    const int high = qMin(low + 1, int(list.size()) - 1);
    return Arg(list.at(low) + (list.at(high) - list.at(low)) * (position - low));
}

/// Pairs of numbers from two ranges, place by place.
bool pairs(Eval &E, const Arg &ys, const Arg &xs, QList<double> *y, QList<double> *x, Value *error)
{
    const QList<Value> a = flat(E, ys);
    const QList<Value> b = flat(E, xs);
    const int n = qMin(a.size(), b.size());
    for (int i = 0; i < n; ++i) {
        if (a.at(i).isError()) {
            *error = a.at(i);
            return false;
        }
        if (b.at(i).isError()) {
            *error = b.at(i);
            return false;
        }
        if (a.at(i).isNumber() && b.at(i).isNumber()) {
            y->append(a.at(i).num);
            x->append(b.at(i).num);
        }
    }
    return true;
}
}

void registerStats(QList<FuncDef> &t)
{
    t.append({"MEDIAN", "MEDIANA", 1, -1, FN {
                  LIST(list, 0);
                  if (list.isEmpty()) {
                      return Arg(Err::Num);
                  }
                  std::sort(list.begin(), list.end());
                  const int n = list.size();
                  return Arg(n % 2 ? list.at(n / 2) : (list.at(n / 2 - 1) + list.at(n / 2)) / 2.0);
              }, nullptr, false, "número1; [número2]; …", "El valor central."});
    static const auto mode = FN {
        LIST(list, 0);
        QHash<double, int> seen;
        double best = 0;
        int most = 1;
        for (double x : std::as_const(list)) {
            const int count = ++seen[x];
            if (count > most) {
                most = count;
                best = x;
            }
        }
        return most < 2 ? Arg(Err::NA) : Arg(best);
    };
    t.append({"MODE", "MODA", 1, -1, mode, nullptr, false, "número1; [número2]; …", "El valor que más se repite."});
    t.append({"MODE.SNGL", "MODA.UNO", 1, -1, mode, nullptr, false, "número1; [número2]; …", "El valor que más se repite."});
#define SPREAD(NAME, ES, SAMPLE, ROOT, HELP)                                                                                                                   \
    t.append({NAME, ES, 1, -1, FN {                                                                                                                            \
                  LIST(list, 0);                                                                                                                               \
                  return variance(list, SAMPLE, ROOT);                                                                                                         \
              }, nullptr, false, "número1; [número2]; …", HELP});
    SPREAD("STDEV", "DESVEST", true, true, "La desviación estándar de una muestra.")
    SPREAD("STDEV.S", "DESVEST.M", true, true, "La desviación estándar de una muestra.")
    SPREAD("STDEV.P", "DESVEST.P", false, true, "La desviación estándar de toda la población.")
    SPREAD("STDEVP", "DESVESTP", false, true, "La desviación estándar de toda la población.")
    SPREAD("VAR", "VAR", true, false, "La varianza de una muestra.")
    SPREAD("VAR.S", "VAR.S", true, false, "La varianza de una muestra.")
    SPREAD("VAR.P", "VAR.P", false, false, "La varianza de toda la población.")
    SPREAD("VARP", "VARP", false, false, "La varianza de toda la población.")
#undef SPREAD
    t.append({"LARGE", "K.ESIMO.MAYOR", 2, 2, FN {
                  LIST1(list, 0);
                  NUM(k, 1);
                  if (k < 1 || k > list.size()) {
                      return Arg(Err::Num);
                  }
                  std::sort(list.begin(), list.end(), std::greater<double>());
                  return Arg(list.at(int(k) - 1));
              }, nullptr, false, "rango; k", "El k-ésimo valor más alto."});
    t.append({"SMALL", "K.ESIMO.MENOR", 2, 2, FN {
                  LIST1(list, 0);
                  NUM(k, 1);
                  if (k < 1 || k > list.size()) {
                      return Arg(Err::Num);
                  }
                  std::sort(list.begin(), list.end());
                  return Arg(list.at(int(k) - 1));
              }, nullptr, false, "rango; k", "El k-ésimo valor más bajo."});
    static const auto rank = FN {
        NUM(x, 0);
        LIST1(list, 1);
        OPT(ascending, 2, 0);
        if (!list.contains(x)) {
            return Arg(Err::NA);
        }
        double place = 1;
        for (double other : std::as_const(list)) {
            if (ascending != 0 ? other < x : other > x) {
                ++place;
            }
        }
        return Arg(place);
    };
    t.append({"RANK", "JERARQUIA", 2, 3, rank, nullptr, false, "número; rango; [ascendente]", "El puesto de un número en una lista."});
    t.append({"RANK.EQ", "JERARQUIA.EQV", 2, 3, rank, nullptr, false, "número; rango; [ascendente]", "El puesto de un número en una lista."});
    static const auto pct = FN {
        LIST1(list, 0);
        NUM(k, 1);
        return percentile(list, k);
    };
    t.append({"PERCENTILE", "PERCENTIL", 2, 2, pct, nullptr, false, "rango; k", "El percentil k, de 0 a 1."});
    t.append({"PERCENTILE.INC", "PERCENTIL.INC", 2, 2, pct, nullptr, false, "rango; k", "El percentil k, de 0 a 1."});
    static const auto quartile = FN {
        LIST1(list, 0);
        NUM(q, 1);
        if (q < 0 || q > 4) {
            return Arg(Err::Num);
        }
        return percentile(list, std::floor(q) / 4.0);
    };
    t.append({"QUARTILE", "CUARTIL", 2, 2, quartile, nullptr, false, "rango; cuartil", "El cuartil, de 0 a 4."});
    t.append({"QUARTILE.INC", "CUARTIL.INC", 2, 2, quartile, nullptr, false, "rango; cuartil", "El cuartil, de 0 a 4."});
    t.append({"GEOMEAN", "MEDIA.GEOM", 1, -1, FN {
                  LIST(list, 0);
                  if (list.isEmpty()) {
                      return Arg(Err::Num);
                  }
                  double logs = 0;
                  for (double x : std::as_const(list)) {
                      if (x <= 0) {
                          return Arg(Err::Num);
                      }
                      logs += std::log(x);
                  }
                  return Arg(std::exp(logs / list.size()));
              }, nullptr, false, "número1; [número2]; …", "La media geométrica."});
    t.append({"AVEDEV", "DESVPROM", 1, -1, FN {
                  LIST(list, 0);
                  if (list.isEmpty()) {
                      return Arg(Err::Num);
                  }
                  const double m = mean(list);
                  double sum = 0;
                  for (double x : std::as_const(list)) {
                      sum += std::fabs(x - m);
                  }
                  return Arg(sum / list.size());
              }, nullptr, false, "número1; [número2]; …", "La desviación media respecto a la media."});
#define REGRESSION(NAME, ES, EXPR, HELP)                                                                                                                       \
    t.append({NAME, ES, 2, 2, FN {                                                                                                                             \
                  QList<double> y, x;                                                                                                                          \
                  Value error;                                                                                                                                 \
                  if (!pairs(E, a[0], a[1], &y, &x, &error)) {                                                                                                 \
                      return Arg(error);                                                                                                                       \
                  }                                                                                                                                            \
                  if (x.size() < 2) {                                                                                                                          \
                      return Arg(Err::Div0);                                                                                                                   \
                  }                                                                                                                                            \
                  const double mx = mean(x), my = mean(y);                                                                                                     \
                  double sxy = 0, sxx = 0, syy = 0;                                                                                                            \
                  for (int i = 0; i < x.size(); ++i) {                                                                                                         \
                      sxy += (x.at(i) - mx) * (y.at(i) - my);                                                                                                  \
                      sxx += (x.at(i) - mx) * (x.at(i) - mx);                                                                                                  \
                      syy += (y.at(i) - my) * (y.at(i) - my);                                                                                                  \
                  }                                                                                                                                            \
                  Q_UNUSED(syy);                                                                                                                               \
                  Q_UNUSED(my);                                                                                                                                \
                  if (sxx == 0) {                                                                                                                              \
                      return Arg(Err::Div0);                                                                                                                   \
                  }                                                                                                                                            \
                  return EXPR;                                                                                                                                 \
              }, nullptr, false, "rango_y; rango_x", HELP});
    REGRESSION("SLOPE", "PENDIENTE", Arg(sxy / sxx), "La pendiente de la recta de regresión.")
    REGRESSION("INTERCEPT", "INTERSECCION.EJE", Arg(my - sxy / sxx * mx), "Dónde corta la recta de regresión el eje Y.")
    REGRESSION("CORREL", "COEF.DE.CORREL", (syy == 0 ? Arg(Err::Div0) : Arg(sxy / std::sqrt(sxx * syy))), "La correlación entre dos series.")
#undef REGRESSION
    t.append({"FORECAST", "PRONOSTICO", 3, 3, FN {
                  NUM(at, 0);
                  QList<double> y, x;
                  Value error;
                  if (!pairs(E, a[1], a[2], &y, &x, &error)) {
                      return Arg(error);
                  }
                  if (x.size() < 2) {
                      return Arg(Err::Div0);
                  }
                  const double mx = mean(x), my = mean(y);
                  double sxy = 0, sxx = 0;
                  for (int i = 0; i < x.size(); ++i) {
                      sxy += (x.at(i) - mx) * (y.at(i) - my);
                      sxx += (x.at(i) - mx) * (x.at(i) - mx);
                  }
                  if (sxx == 0) {
                      return Arg(Err::Div0);
                  }
                  return Arg(my + sxy / sxx * (at - mx));
              }, nullptr, false, "x; conocidos_y; conocidos_x", "El valor que la recta de regresión da para x."});
}

// --- Finance ----------------------------------------------------------------------

namespace
{
double fv(double rate, double nper, double pmt, double pv, double type)
{
    if (rate == 0) {
        return -(pv + pmt * nper);
    }
    const double growth = std::pow(1 + rate, nper);
    return -(pv * growth + pmt * (1 + rate * type) * (growth - 1) / rate);
}

double pmt(double rate, double nper, double pv, double future, double type)
{
    if (rate == 0) {
        return -(pv + future) / nper;
    }
    const double growth = std::pow(1 + rate, nper);
    return -rate * (pv * growth + future) / ((1 + rate * type) * (growth - 1));
}

double npv(double rate, const QList<double> &flows, int first)
{
    double sum = 0;
    for (int i = 0; i < flows.size(); ++i) {
        sum += flows.at(i) / std::pow(1 + rate, i + first);
    }
    return sum;
}

/// A root of `f` near `guess`: Newton's steps with a numeric slope, then
/// halving an interval when they wander off.
bool solve(const std::function<double(double)> &f, double guess, double *root)
{
    double x = guess;
    for (int i = 0; i < 60; ++i) {
        const double y = f(x);
        if (std::isnan(y) || std::isinf(y)) {
            break;
        }
        if (std::fabs(y) < 1e-10) {
            *root = x;
            return true;
        }
        const double h = 1e-6;
        const double slope = (f(x + h) - y) / h;
        if (slope == 0 || std::isnan(slope)) {
            break;
        }
        const double next = x - y / slope;
        if (next <= -1) {
            x = (x - 1) / 2;
        } else {
            x = next;
        }
    }
    double low = -0.999999, high = 10;
    double fLow = f(low), fHigh = f(high);
    if (std::isnan(fLow) || std::isnan(fHigh) || fLow * fHigh > 0) {
        return false;
    }
    for (int i = 0; i < 200; ++i) {
        const double mid = (low + high) / 2;
        const double fMid = f(mid);
        if (std::fabs(fMid) < 1e-10 || high - low < 1e-13) {
            *root = mid;
            return true;
        }
        if (fLow * fMid <= 0) {
            high = mid;
        } else {
            low = mid;
            fLow = fMid;
        }
    }
    *root = (low + high) / 2;
    return true;
}
}

void registerFinance(QList<FuncDef> &t)
{
    t.append({"PMT", "PAGO", 3, 5, FN {
                  NUM(rate, 0);
                  NUM(nper, 1);
                  NUM(pv, 2);
                  OPT(future, 3, 0);
                  OPT(type, 4, 0);
                  if (nper == 0) {
                      return Arg(Err::Num);
                  }
                  return checked(pmt(rate, nper, pv, future, type != 0 ? 1 : 0));
              }, nullptr, false, "tasa; periodos; valor_actual; [valor_futuro]; [tipo]", "La cuota de un préstamo."});
    t.append({"FV", "VF", 3, 5, FN {
                  NUM(rate, 0);
                  NUM(nper, 1);
                  NUM(payment, 2);
                  OPT(pv, 3, 0);
                  OPT(type, 4, 0);
                  return checked(fv(rate, nper, payment, pv, type != 0 ? 1 : 0));
              }, nullptr, false, "tasa; periodos; pago; [valor_actual]; [tipo]", "El valor futuro de una inversión."});
    t.append({"PV", "VA", 3, 5, FN {
                  NUM(rate, 0);
                  NUM(nper, 1);
                  NUM(payment, 2);
                  OPT(future, 3, 0);
                  OPT(type, 4, 0);
                  if (rate == 0) {
                      return Arg(-(future + payment * nper));
                  }
                  const double growth = std::pow(1 + rate, nper);
                  return checked(-(future + payment * (1 + rate * (type != 0 ? 1 : 0)) * (growth - 1) / rate) / growth);
              }, nullptr, false, "tasa; periodos; pago; [valor_futuro]; [tipo]", "El valor actual de una inversión."});
    t.append({"NPER", "NPER", 3, 5, FN {
                  NUM(rate, 0);
                  NUM(payment, 1);
                  NUM(pv, 2);
                  OPT(future, 3, 0);
                  OPT(type, 4, 0);
                  if (rate == 0) {
                      return payment == 0 ? Arg(Err::Num) : Arg(-(pv + future) / payment);
                  }
                  const double due = payment * (1 + rate * (type != 0 ? 1 : 0));
                  const double ratio = (due - future * rate) / (due + pv * rate);
                  if (ratio <= 0) {
                      return Arg(Err::Num);
                  }
                  return checked(std::log(ratio) / std::log(1 + rate));
              }, nullptr, false, "tasa; pago; valor_actual; [valor_futuro]; [tipo]", "Cuántos periodos dura una inversión."});
    t.append({"RATE", "TASA", 3, 6, FN {
                  NUM(nper, 0);
                  NUM(payment, 1);
                  NUM(pv, 2);
                  OPT(future, 3, 0);
                  OPT(type, 4, 0);
                  OPT(guess, 5, 0.1);
                  const double due = type != 0 ? 1 : 0;
                  double root = 0;
                  const auto f = [=](double rate) {
                      if (std::fabs(rate) < 1e-12) {
                          return pv + payment * nper + future;
                      }
                      return -fv(rate, nper, payment, pv, due) + future;
                  };
                  if (!solve(f, guess, &root)) {
                      return Arg(Err::Num);
                  }
                  return Arg(root);
              }, nullptr, false, "periodos; pago; valor_actual; [valor_futuro]; [tipo]; [estimación]", "El tipo de interés por periodo."});
    t.append({"NPV", "VNA", 2, -1, FN {
                  NUM(rate, 0);
                  LIST(flows, 1);
                  if (rate == -1) {
                      return Arg(Err::Div0);
                  }
                  return checked(npv(rate, flows, 1));
              }, nullptr, false, "tasa; valor1; [valor2]; …", "El valor actual neto de unos flujos."});
    t.append({"IRR", "TIR", 1, 2, FN {
                  LIST1(flows, 0);
                  OPT(guess, 1, 0.1);
                  bool positive = false, negative = false;
                  for (double x : std::as_const(flows)) {
                      positive = positive || x > 0;
                      negative = negative || x < 0;
                  }
                  if (!positive || !negative) {
                      return Arg(Err::Num);
                  }
                  double root = 0;
                  if (!solve([&flows](double rate) {
                          return npv(rate, flows, 0);
                      }, guess, &root)) {
                      return Arg(Err::Num);
                  }
                  return Arg(root);
              }, nullptr, false, "valores; [estimación]", "La tasa interna de retorno."});
    static const auto interest = [](double rate, double per, double nper, double pv, double future, double due) {
        const double payment = pmt(rate, nper, pv, future, due);
        if (due != 0 && per == 1) {
            return 0.0;
        }
        // What is still owed when the period starts.
        const double balance = fv(rate, per - 1, payment, pv, due);
        const double part = balance * rate;
        return due != 0 ? part / (1 + rate) : part;
    };
    t.append({"IPMT", "PAGOINT", 4, 6, FN {
                  NUM(rate, 0);
                  NUM(per, 1);
                  NUM(nper, 2);
                  NUM(pv, 3);
                  OPT(future, 4, 0);
                  OPT(type, 5, 0);
                  if (per < 1 || per > nper) {
                      return Arg(Err::Num);
                  }
                  return checked(interest(rate, per, nper, pv, future, type != 0 ? 1 : 0));
              }, nullptr, false, "tasa; periodo; periodos; valor_actual; [valor_futuro]; [tipo]", "Los intereses de una cuota."});
    t.append({"PPMT", "PAGOPRIN", 4, 6, FN {
                  NUM(rate, 0);
                  NUM(per, 1);
                  NUM(nper, 2);
                  NUM(pv, 3);
                  OPT(future, 4, 0);
                  OPT(type, 5, 0);
                  if (per < 1 || per > nper) {
                      return Arg(Err::Num);
                  }
                  const double due = type != 0 ? 1 : 0;
                  return checked(pmt(rate, nper, pv, future, due) - interest(rate, per, nper, pv, future, due));
              }, nullptr, false, "tasa; periodo; periodos; valor_actual; [valor_futuro]; [tipo]", "El capital que amortiza una cuota."});
    t.append({"SLN", "SLN", 3, 3, FN {
                  NUM(cost, 0);
                  NUM(salvage, 1);
                  NUM(life, 2);
                  if (life == 0) {
                      return Arg(Err::Div0);
                  }
                  return Arg((cost - salvage) / life);
              }, nullptr, false, "coste; valor_residual; vida", "La amortización lineal por periodo."});
}
