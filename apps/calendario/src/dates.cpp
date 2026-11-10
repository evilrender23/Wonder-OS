#include "dates.h"

#include <QRegularExpression>

QString Dates::month(int month)
{
    static const char *const names[] = {"enero", "febrero", "marzo", "abril", "mayo", "junio",
                                        "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre"};
    return month >= 1 && month <= 12 ? QString::fromUtf8(names[month - 1]) : QString();
}

QString Dates::weekday(int weekday)
{
    static const char *const names[] = {"lunes", "martes", "miércoles", "jueves", "viernes", "sábado", "domingo"};
    return weekday >= 1 && weekday <= 7 ? QString::fromUtf8(names[weekday - 1]) : QString();
}

QString Dates::numeric(const QDate &date)
{
    return date.isValid() ? QStringLiteral("%1/%2/%3").arg(date.day()).arg(date.month()).arg(date.year()) : QString();
}

QString Dates::full(const QDate &date)
{
    return date.isValid() ? QStringLiteral("%1, %2 de %3 de %4").arg(weekday(date.dayOfWeek())).arg(date.day()).arg(month(date.month())).arg(date.year())
                          : QString();
}

QString Dates::medium(const QDate &date, const QDate &around)
{
    if (!date.isValid()) {
        return {};
    }
    const QString text = QStringLiteral("%1 de %2").arg(date.day()).arg(month(date.month()));
    return date.year() == around.year() ? text : text + QStringLiteral(" de %1").arg(date.year());
}

QDate Dates::parseDate(const QString &text, const QDate &today)
{
    static const QRegularExpression iso(QStringLiteral("^(\\d{4})-(\\d{1,2})-(\\d{1,2})$"));
    static const QRegularExpression usual(QStringLiteral("^(\\d{1,2})\\s*[/.\\- ]\\s*(\\d{1,2})(?:\\s*[/.\\- ]\\s*(\\d{2}|\\d{4}))?$"));
    const QString trimmed = text.trimmed();
    QRegularExpressionMatch match = iso.match(trimmed);
    if (match.hasMatch()) {
        return QDate(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt());
    }
    match = usual.match(trimmed);
    if (!match.hasMatch()) {
        return {};
    }
    int year = today.year();
    if (!match.captured(3).isEmpty()) {
        year = match.captured(3).toInt();
        if (match.captured(3).size() == 2) {
            year += 2000;
        }
    }
    return QDate(year, match.captured(2).toInt(), match.captured(1).toInt());
}

int Dates::parseTime(const QString &text)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(\\d{1,2})(?:\\s*[:.h]\\s*(\\d{1,2})?)?\\s*(am|pm|a\\.?\\s?m\\.?|p\\.?\\s?m\\.?)?$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression packed(QStringLiteral("^(\\d{1,2})(\\d{2})$"));
    const QString trimmed = text.trimmed();
    int hour = -1;
    int minute = 0;
    QString half;
    QRegularExpressionMatch match = packed.match(trimmed);
    if (match.hasMatch() && trimmed.size() >= 3) {
        hour = match.captured(1).toInt();
        minute = match.captured(2).toInt();
    } else {
        match = pattern.match(trimmed);
        if (!match.hasMatch()) {
            return -1;
        }
        hour = match.captured(1).toInt();
        minute = match.captured(2).toInt();
        half = match.captured(3).toLower();
    }
    if (half.startsWith(QLatin1Char('p')) && hour < 12) {
        hour += 12;
    } else if (half.startsWith(QLatin1Char('a')) && hour == 12) {
        hour = 0;
    }
    if (hour == 24 && minute == 0) {
        return 1439;
    }
    return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 ? hour * 60 + minute : -1;
}
