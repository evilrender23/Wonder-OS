#pragma once

#include "value.h"

#include <QDate>
#include <QString>

/// Number formats: the format codes of spreadsheets («#,##0.00», «dd/mm/yyyy»)
/// turned into text, and what is typed in a cell turned into a value.
/// Text comes out the Spanish way: a decimal comma, a dot between thousands.
namespace NumFmt
{
/// The value as the cell shows it. `color`, when the code asks for one
/// («[Red]»), gets its name as #rrggbb.
QString format(const Value &value, const QString &code, QString *color = nullptr);
/// A number with no format: as many digits as it has, up to ten.
QString general(double number);
/// What the cell editor starts with: the value in full, typed the way it
/// would be typed (a date as 10/11/2026, a percentage as 12,5%).
QString editText(const Value &value, const QString &code);

/// Whether a code shows dates or times.
bool isDate(const QString &code);
/// Whether it shows only a time of day.
bool isTime(const QString &code);
bool isPercent(const QString &code);
bool isText(const QString &code);

/// What was typed, read as a number, a date, a percentage, true/false, an
/// error or, failing all, text. `code` gets the format that suits it («0%»
/// after «12%»), or stays untouched.
Value parseInput(const QString &typed, QString *code = nullptr);
/// Only the number in a text, the Spanish or the English way; NaN when none.
double parseNumber(const QString &text);

double serial(const QDate &date);
double serial(const QDateTime &moment);
QDate date(double serial);
QDateTime dateTime(double serial);

/// The codes behind the numbers xlsx gives the built-in formats; empty when unknown.
QString builtin(int id);
/// The built-in number of a code, or -1.
int builtinId(const QString &code);

/// The code with one decimal more (`delta` 1) or less (-1).
QString withDecimals(const QString &code, int delta);
int decimals(const QString &code);

/// One of the families the inspector offers: general, number, currency,
/// percent, date, time, scientific or text.
QString family(const QString &code);
QString familyCode(const QString &family, int decimals = 2);
}
