#include "event.h"

#include <QJsonArray>
#include <QTimeZone>

static const QString stamp = QStringLiteral("yyyy-MM-dd'T'HH:mm");
static const QString dayStamp = QStringLiteral("yyyy-MM-dd");

QDateTime Event::at(const QDate &date, const QTime &time)
{
    return QDateTime(date, time, QTimeZone::UTC);
}

QDateTime Event::now()
{
    const QString fixed = qEnvironmentVariable("WONDERLAND_CALENDARIO_NOW");
    if (!fixed.isEmpty()) {
        const QDateTime parsed = QDateTime::fromString(fixed, stamp);
        if (parsed.isValid()) {
            return at(parsed.date(), parsed.time());
        }
    }
    const QDateTime local = QDateTime::currentDateTime();
    return at(local.date(), local.time());
}

QDateTime Event::real(const QDateTime &floating)
{
    return QDateTime(floating.date(), floating.time());
}

QList<QDateTime> Event::starts(const QDateTime &from, const QDateTime &to) const
{
    QList<QDateTime> out;
    if (!start.isValid() || !(from < to)) {
        return out;
    }
    const qint64 length = seconds();
    const auto touches = [&](const QDateTime &s) {
        return s < to && (s.addSecs(length) > from || (length == 0 && s >= from));
    };
    if (repeat == Never) {
        if (touches(start)) {
            out << start;
        }
        return out;
    }

    const int step = qMax(1, interval);
    const QDate first = start.date();
    // Without a count nothing has to be counted from the beginning: jump to
    // just before the range.
    qint64 n = 0;
    if (count <= 0) {
        const QDate probe = from.addSecs(-length).date();
        switch (repeat) {
        case Daily:
            n = first.daysTo(probe) / step - 1;
            break;
        case Weekly:
            n = first.daysTo(probe) / (7 * qint64(step)) - 1;
            break;
        case Monthly:
            n = ((probe.year() - first.year()) * 12 + probe.month() - first.month()) / step - 1;
            break;
        case Yearly:
            n = (probe.year() - first.year()) / step - 1;
            break;
        case Never:
            break;
        }
        n = qMax(qint64(0), n);
    }

    int made = 0;
    for (int guard = 0; guard < 100000; ++guard, ++n) {
        QDate date;
        switch (repeat) {
        case Daily:
            date = first.addDays(n * step);
            break;
        case Weekly:
            date = first.addDays(n * step * 7);
            break;
        case Monthly: {
            // The 31st only happens in the months that have one.
            const QDate month = QDate(first.year(), first.month(), 1).addMonths(int(n * step));
            if (month > to.date()) {
                return out;
            }
            date = QDate(month.year(), month.month(), first.day());
            break;
        }
        case Yearly: {
            const int year = first.year() + int(n * step);
            if (year > to.date().year()) {
                return out;
            }
            date = QDate(year, first.month(), first.day());
            break;
        }
        case Never:
            break;
        }
        if (!date.isValid()) {
            continue;
        }
        if ((until.isValid() && date > until) || (count > 0 && made >= count)) {
            break;
        }
        ++made;
        const QDateTime s = at(date, start.time());
        if (s >= to) {
            break;
        }
        if (!skipped.contains(date) && touches(s)) {
            out << s;
        }
    }
    return out;
}

QJsonObject Event::toJson() const
{
    QJsonObject object{
        {QStringLiteral("id"), id},
        {QStringLiteral("calendar"), calendar},
        {QStringLiteral("title"), title},
        {QStringLiteral("start"), start.toString(stamp)},
        {QStringLiteral("end"), end.toString(stamp)},
    };
    if (allDay) {
        object.insert(QStringLiteral("allDay"), true);
    }
    if (!location.isEmpty()) {
        object.insert(QStringLiteral("location"), location);
    }
    if (!notes.isEmpty()) {
        object.insert(QStringLiteral("notes"), notes);
    }
    if (repeat != Never) {
        static const char *const names[] = {"", "daily", "weekly", "monthly", "yearly"};
        object.insert(QStringLiteral("repeat"), QLatin1String(names[repeat]));
        if (interval > 1) {
            object.insert(QStringLiteral("interval"), interval);
        }
        if (until.isValid()) {
            object.insert(QStringLiteral("until"), until.toString(dayStamp));
        }
        if (count > 0) {
            object.insert(QStringLiteral("count"), count);
        }
        if (!skipped.isEmpty()) {
            QJsonArray days;
            for (const QDate &day : skipped) {
                days.append(day.toString(dayStamp));
            }
            object.insert(QStringLiteral("skipped"), days);
        }
    }
    if (reminder >= 0) {
        object.insert(QStringLiteral("reminder"), reminder);
    }
    return object;
}

Event Event::fromJson(const QJsonObject &object)
{
    Event event;
    event.id = object.value(QLatin1String("id")).toString();
    event.calendar = object.value(QLatin1String("calendar")).toString();
    event.title = object.value(QLatin1String("title")).toString();
    event.location = object.value(QLatin1String("location")).toString();
    event.notes = object.value(QLatin1String("notes")).toString();
    const QDateTime start = QDateTime::fromString(object.value(QLatin1String("start")).toString(), stamp);
    const QDateTime end = QDateTime::fromString(object.value(QLatin1String("end")).toString(), stamp);
    event.start = at(start.date(), start.time());
    event.end = end.isValid() ? at(end.date(), end.time()) : event.start;
    if (event.end < event.start) {
        event.end = event.start;
    }
    event.allDay = object.value(QLatin1String("allDay")).toBool();
    const QString repeat = object.value(QLatin1String("repeat")).toString();
    event.repeat = repeat == QLatin1String("daily") ? Daily
        : repeat == QLatin1String("weekly") ? Weekly
        : repeat == QLatin1String("monthly") ? Monthly
        : repeat == QLatin1String("yearly") ? Yearly
        : Never;
    event.interval = qMax(1, object.value(QLatin1String("interval")).toInt(1));
    event.until = QDate::fromString(object.value(QLatin1String("until")).toString(), dayStamp);
    event.count = qMax(0, object.value(QLatin1String("count")).toInt());
    for (const QJsonValue &day : object.value(QLatin1String("skipped")).toArray()) {
        const QDate date = QDate::fromString(day.toString(), dayStamp);
        if (date.isValid()) {
            event.skipped << date;
        }
    }
    event.reminder = object.contains(QLatin1String("reminder")) ? qMax(-1, object.value(QLatin1String("reminder")).toInt(-1)) : -1;
    return event;
}
