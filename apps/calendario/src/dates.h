#pragma once

#include <QDate>
#include <QString>

/// Dates and hours as people write them, in Spanish.
namespace Dates {

/// «noviembre»; 1 is January.
QString month(int month);
/// «martes»; 1 is Monday.
QString weekday(int weekday);
/// «10/11/2026».
QString numeric(const QDate &date);
/// «martes, 10 de noviembre de 2026».
QString full(const QDate &date);
/// «10 de noviembre», with the year when it is not `around`'s.
QString medium(const QDate &date, const QDate &around);

/// 10/11/2026, 10-11-26, 10/11 (this year) or 2026-11-10; invalid when it is none.
QDate parseDate(const QString &text, const QDate &today);
/// 9, 9:30, 9.30, 0930, 9h, 9:30 pm: minutes since midnight, or -1.
int parseTime(const QString &text);

}
