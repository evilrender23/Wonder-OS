#include "ics.h"

#include <QHash>
#include <QRegularExpression>
#include <QTimeZone>

namespace {

struct Line {
    QString name;
    QHash<QString, QString> params;
    QString value;
};

/// Long lines are folded: a line that starts with a space continues the one before.
QList<Line> unfold(const QByteArray &data)
{
    QStringList whole;
    const QString text = QString::fromUtf8(data);
    for (QString line : text.split(QLatin1Char('\n'))) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        if ((line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t'))) && !whole.isEmpty()) {
            whole.last() += QStringView(line).mid(1);
        } else if (!line.isEmpty()) {
            whole << line;
        }
    }
    QList<Line> lines;
    for (const QString &line : std::as_const(whole)) {
        // The value starts at the first colon outside a quoted parameter.
        int colon = -1;
        bool quoted = false;
        for (int i = 0; i < line.size(); ++i) {
            if (line.at(i) == QLatin1Char('"')) {
                quoted = !quoted;
            } else if (line.at(i) == QLatin1Char(':') && !quoted) {
                colon = i;
                break;
            }
        }
        if (colon < 0) {
            continue;
        }
        Line parsed;
        const QStringList head = line.left(colon).split(QLatin1Char(';'));
        parsed.name = head.first().toUpper();
        for (int i = 1; i < head.size(); ++i) {
            const int equals = head.at(i).indexOf(QLatin1Char('='));
            if (equals > 0) {
                QString value = head.at(i).mid(equals + 1);
                value.remove(QLatin1Char('"'));
                parsed.params.insert(head.at(i).left(equals).toUpper(), value);
            }
        }
        parsed.value = line.mid(colon + 1);
        lines << parsed;
    }
    return lines;
}

QString unescape(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\\') && i + 1 < text.size()) {
            const QChar next = text.at(++i);
            out += (next == QLatin1Char('n') || next == QLatin1Char('N')) ? QLatin1Char('\n') : next;
        } else {
            out += c;
        }
    }
    return out;
}

QString escape(QString text)
{
    text.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    text.replace(QLatin1Char(';'), QLatin1String("\\;"));
    text.replace(QLatin1Char(','), QLatin1String("\\,"));
    text.replace(QLatin1String("\r\n"), QLatin1String("\\n"));
    text.replace(QLatin1Char('\n'), QLatin1String("\\n"));
    return text;
}

/// 20261110, 20261110T093000, 20261110T093000Z, with or without a TZID.
QDateTime moment(const Line &line, bool *dateOnly = nullptr)
{
    const QString value = line.value.trimmed();
    const bool date = line.params.value(QStringLiteral("VALUE")).toUpper() == QLatin1String("DATE") || value.size() == 8;
    if (dateOnly) {
        *dateOnly = date;
    }
    if (date) {
        return Event::at(QDate::fromString(value.left(8), QStringLiteral("yyyyMMdd")));
    }
    const QDate day = QDate::fromString(value.left(8), QStringLiteral("yyyyMMdd"));
    QTime time = QTime::fromString(value.mid(9, 6), QStringLiteral("HHmmss"));
    if (!time.isValid()) {
        time = QTime::fromString(value.mid(9, 4), QStringLiteral("HHmm"));
    }
    if (!day.isValid() || !time.isValid()) {
        return {};
    }
    QDateTime there;
    if (value.endsWith(QLatin1Char('Z'), Qt::CaseInsensitive)) {
        there = QDateTime(day, time, QTimeZone::UTC);
    } else if (line.params.contains(QStringLiteral("TZID"))) {
        const QTimeZone zone(line.params.value(QStringLiteral("TZID")).toUtf8());
        if (zone.isValid()) {
            there = QDateTime(day, time, zone);
        }
    }
    if (there.isValid()) {
        const QDateTime here = there.toLocalTime();
        return Event::at(here.date(), here.time());
    }
    return Event::at(day, time);
}

/// P1D, PT1H30M, -PT15M, P1W: seconds, signed.
qint64 duration(const QString &value, bool *ok = nullptr)
{
    static const QRegularExpression pattern(
        QStringLiteral("^([+-])?P(?:(\\d+)W)?(?:(\\d+)D)?(?:T(?:(\\d+)H)?(?:(\\d+)M)?(?:(\\d+)S)?)?$"));
    const QRegularExpressionMatch match = pattern.match(value.trimmed().toUpper());
    if (ok) {
        *ok = match.hasMatch();
    }
    if (!match.hasMatch()) {
        return 0;
    }
    const qint64 seconds = match.captured(2).toLongLong() * 604800 + match.captured(3).toLongLong() * 86400
        + match.captured(4).toLongLong() * 3600 + match.captured(5).toLongLong() * 60 + match.captured(6).toLongLong();
    return match.captured(1) == QLatin1String("-") ? -seconds : seconds;
}

void rule(const QString &value, Event &event)
{
    for (const QString &part : value.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const int equals = part.indexOf(QLatin1Char('='));
        const QString key = part.left(equals).toUpper();
        const QString what = part.mid(equals + 1).toUpper();
        if (key == QLatin1String("FREQ")) {
            event.repeat = what == QLatin1String("DAILY") ? Event::Daily
                : what == QLatin1String("WEEKLY") ? Event::Weekly
                : what == QLatin1String("MONTHLY") ? Event::Monthly
                : what == QLatin1String("YEARLY") ? Event::Yearly
                : Event::Never;
        } else if (key == QLatin1String("INTERVAL")) {
            event.interval = qMax(1, what.toInt());
        } else if (key == QLatin1String("COUNT")) {
            event.count = qMax(0, what.toInt());
        } else if (key == QLatin1String("UNTIL")) {
            Line line;
            line.value = what;
            event.until = moment(line).date();
        }
    }
}

/// Lines are at most 75 octets; the rest goes on, after a space.
void put(QByteArray &out, const QString &line)
{
    const QByteArray bytes = line.toUtf8();
    int at = 0;
    int room = 75;
    while (bytes.size() - at > room) {
        int cut = at + room;
        // Never in the middle of a UTF-8 character.
        while (cut > at && (uchar(bytes.at(cut)) & 0xC0) == 0x80) {
            --cut;
        }
        out += bytes.mid(at, cut - at) + "\r\n ";
        at = cut;
        room = 74;
    }
    out += bytes.mid(at) + "\r\n";
}

}

Ics::File Ics::parse(const QByteArray &data)
{
    File file;
    Event event;
    bool inEvent = false;
    bool inAlarm = false;
    bool hasEnd = false;
    int depth = 0;
    for (const Line &line : unfold(data)) {
        if (line.name == QLatin1String("BEGIN")) {
            const QString what = line.value.trimmed().toUpper();
            if (what == QLatin1String("VEVENT") && !inEvent) {
                event = Event();
                inEvent = true;
                hasEnd = false;
                depth = 0;
            } else if (inEvent) {
                ++depth;
                inAlarm = what == QLatin1String("VALARM");
            }
            continue;
        }
        if (line.name == QLatin1String("END")) {
            const QString what = line.value.trimmed().toUpper();
            if (inEvent && depth > 0) {
                --depth;
                inAlarm = false;
            } else if (inEvent && what == QLatin1String("VEVENT")) {
                inEvent = false;
                if (event.start.isValid()) {
                    if (!hasEnd || event.end < event.start) {
                        event.end = event.allDay ? event.start.addDays(1) : event.start;
                    }
                    if (event.allDay && event.end == event.start) {
                        event.end = event.start.addDays(1);
                    }
                    file.events << event;
                }
            }
            continue;
        }
        if (!inEvent) {
            if (line.name == QLatin1String("X-WR-CALNAME")) {
                file.name = unescape(line.value).trimmed();
            }
            continue;
        }
        if (inAlarm) {
            if (line.name == QLatin1String("TRIGGER") && depth == 1) {
                bool ok = false;
                const qint64 seconds = duration(line.value, &ok);
                if (ok && seconds <= 0 && line.params.value(QStringLiteral("RELATED")).toUpper() != QLatin1String("END")) {
                    const int minutes = int(-seconds / 60);
                    event.reminder = event.reminder < 0 ? minutes : qMin(event.reminder, minutes);
                }
            }
            continue;
        }
        if (depth > 0) {
            continue;
        }
        if (line.name == QLatin1String("UID")) {
            event.id = line.value.trimmed();
        } else if (line.name == QLatin1String("SUMMARY")) {
            event.title = unescape(line.value).trimmed();
        } else if (line.name == QLatin1String("LOCATION")) {
            event.location = unescape(line.value).trimmed();
        } else if (line.name == QLatin1String("DESCRIPTION")) {
            event.notes = unescape(line.value).trimmed();
        } else if (line.name == QLatin1String("DTSTART")) {
            event.start = moment(line, &event.allDay);
        } else if (line.name == QLatin1String("DTEND")) {
            event.end = moment(line);
            hasEnd = event.end.isValid();
        } else if (line.name == QLatin1String("DURATION")) {
            bool ok = false;
            const qint64 seconds = duration(line.value, &ok);
            if (ok && event.start.isValid()) {
                event.end = event.start.addSecs(qMax(qint64(0), seconds));
                hasEnd = true;
            }
        } else if (line.name == QLatin1String("RRULE")) {
            rule(line.value, event);
        } else if (line.name == QLatin1String("EXDATE")) {
            for (const QString &one : line.value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                Line single = line;
                single.value = one;
                const QDate day = moment(single).date();
                if (day.isValid()) {
                    event.skipped << day;
                }
            }
        }
    }
    return file;
}

QByteArray Ics::write(const QList<Event> &events, const QString &name)
{
    static const QString dayForm = QStringLiteral("yyyyMMdd");
    static const QString timeForm = QStringLiteral("yyyyMMdd'T'HHmmss");
    const QString made = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
    QByteArray out;
    put(out, QStringLiteral("BEGIN:VCALENDAR"));
    put(out, QStringLiteral("VERSION:2.0"));
    put(out, QStringLiteral("PRODID:-//Wonderland//Calendario//ES"));
    put(out, QStringLiteral("CALSCALE:GREGORIAN"));
    if (!name.isEmpty()) {
        put(out, QStringLiteral("X-WR-CALNAME:") + escape(name));
    }
    for (const Event &event : events) {
        put(out, QStringLiteral("BEGIN:VEVENT"));
        put(out, QStringLiteral("UID:") + event.id);
        put(out, QStringLiteral("DTSTAMP:") + made);
        if (event.allDay) {
            put(out, QStringLiteral("DTSTART;VALUE=DATE:") + event.start.toString(dayForm));
            put(out, QStringLiteral("DTEND;VALUE=DATE:") + event.end.toString(dayForm));
        } else {
            put(out, QStringLiteral("DTSTART:") + event.start.toString(timeForm));
            put(out, QStringLiteral("DTEND:") + event.end.toString(timeForm));
        }
        put(out, QStringLiteral("SUMMARY:") + escape(event.title));
        if (!event.location.isEmpty()) {
            put(out, QStringLiteral("LOCATION:") + escape(event.location));
        }
        if (!event.notes.isEmpty()) {
            put(out, QStringLiteral("DESCRIPTION:") + escape(event.notes));
        }
        if (event.repeats()) {
            static const char *const names[] = {"", "DAILY", "WEEKLY", "MONTHLY", "YEARLY"};
            QString text = QStringLiteral("RRULE:FREQ=") + QLatin1String(names[event.repeat]);
            if (event.interval > 1) {
                text += QStringLiteral(";INTERVAL=%1").arg(event.interval);
            }
            if (event.count > 0) {
                text += QStringLiteral(";COUNT=%1").arg(event.count);
            } else if (event.until.isValid()) {
                text += QStringLiteral(";UNTIL=") + (event.allDay ? event.until.toString(dayForm) : Event::at(event.until, QTime(23, 59, 59)).toString(timeForm));
            }
            put(out, text);
            if (!event.skipped.isEmpty()) {
                QStringList days;
                for (const QDate &day : event.skipped) {
                    days << (event.allDay ? day.toString(dayForm) : Event::at(day, event.start.time()).toString(timeForm));
                }
                put(out, (event.allDay ? QStringLiteral("EXDATE;VALUE=DATE:") : QStringLiteral("EXDATE:")) + days.join(QLatin1Char(',')));
            }
        }
        if (event.reminder >= 0) {
            put(out, QStringLiteral("BEGIN:VALARM"));
            put(out, QStringLiteral("ACTION:DISPLAY"));
            put(out, QStringLiteral("DESCRIPTION:") + escape(event.title));
            put(out, QStringLiteral("TRIGGER:-PT%1M").arg(event.reminder));
            put(out, QStringLiteral("END:VALARM"));
        }
        put(out, QStringLiteral("END:VEVENT"));
    }
    put(out, QStringLiteral("END:VCALENDAR"));
    return out;
}
