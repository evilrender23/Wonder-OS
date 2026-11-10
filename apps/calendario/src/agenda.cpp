#include "agenda.h"

#include "ics.h"
#include "store.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSaveFile>
#include <QUuid>

static QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

static QDate dateOf(int day)
{
    return QDate::fromJulianDay(day);
}

static int minuteOf(const QTime &time)
{
    return time.hour() * 60 + time.minute();
}

Agenda::Agenda(const QString &path, QObject *parent)
    : QObject(parent)
    , m_path(path)
{
    load();
}

QStringList Agenda::palette()
{
    return {QStringLiteral("#ff3b30"), QStringLiteral("#ff9500"), QStringLiteral("#ffcc00"), QStringLiteral("#34c759"),
            QStringLiteral("#0a84ff"), QStringLiteral("#af52de"), QStringLiteral("#a2845e"), QStringLiteral("#8e8e93")};
}

void Agenda::load()
{
    const QJsonObject root = Store::read(m_path);
    for (const QJsonValue &value : root.value(QLatin1String("calendars")).toArray()) {
        const QJsonObject object = value.toObject();
        Calendar calendar;
        calendar.id = object.value(QLatin1String("id")).toString();
        calendar.name = object.value(QLatin1String("name")).toString();
        calendar.color = object.value(QLatin1String("color")).toString();
        calendar.visible = object.value(QLatin1String("visible")).toBool(true);
        if (!QColor::isValidColorName(calendar.color)) {
            calendar.color = palette().at(4);
        }
        if (!calendar.id.isEmpty() && !calendarOf(calendar.id)) {
            m_calendars << calendar;
        }
    }
    if (m_calendars.isEmpty()) {
        m_calendars << Calendar{QStringLiteral("personal"), QStringLiteral("Personal"), palette().at(4), true}
                    << Calendar{QStringLiteral("trabajo"), QStringLiteral("Trabajo"), palette().at(5), true};
    }
    for (const QJsonValue &value : root.value(QLatin1String("events")).toArray()) {
        Event event = Event::fromJson(value.toObject());
        if (event.id.isEmpty() || !event.start.isValid() || find(event.id)) {
            continue;
        }
        if (!calendarOf(event.calendar)) {
            event.calendar = m_calendars.first().id;
        }
        m_events << event;
    }
    m_default = root.value(QLatin1String("default")).toString();
    if (!calendarOf(m_default)) {
        m_default = m_calendars.first().id;
    }
}

void Agenda::commit()
{
    QJsonArray calendars;
    for (const Calendar &calendar : std::as_const(m_calendars)) {
        calendars.append(QJsonObject{
            {QStringLiteral("id"), calendar.id},
            {QStringLiteral("name"), calendar.name},
            {QStringLiteral("color"), calendar.color},
            {QStringLiteral("visible"), calendar.visible},
        });
    }
    QJsonArray events;
    for (const Event &event : std::as_const(m_events)) {
        events.append(event.toJson());
    }
    Store::write(m_path, QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("default"), m_default},
        {QStringLiteral("calendars"), calendars},
        {QStringLiteral("events"), events},
    });
    ++m_revision;
    Q_EMIT changed();
}

const Calendar *Agenda::calendarOf(const QString &id) const
{
    for (const Calendar &calendar : m_calendars) {
        if (calendar.id == id) {
            return &calendar;
        }
    }
    return nullptr;
}

const Event *Agenda::find(const QString &id) const
{
    for (const Event &event : m_events) {
        if (event.id == id) {
            return &event;
        }
    }
    return nullptr;
}

QString Agenda::put(Event event)
{
    if (event.id.isEmpty()) {
        event.id = newId();
    }
    if (!calendarOf(event.calendar)) {
        event.calendar = m_default;
    }
    if (event.end < event.start) {
        event.end = event.start;
    }
    for (Event &existing : m_events) {
        if (existing.id == event.id) {
            existing = event;
            commit();
            return event.id;
        }
    }
    m_events << event;
    commit();
    return event.id;
}

// --- Calendars ---------------------------------------------------------------

QVariantList Agenda::calendarList() const
{
    QHash<QString, int> counts;
    for (const Event &event : m_events) {
        ++counts[event.calendar];
    }
    QVariantList list;
    for (const Calendar &calendar : m_calendars) {
        list << QVariantMap{
            {QStringLiteral("id"), calendar.id},
            {QStringLiteral("name"), calendar.name},
            {QStringLiteral("color"), calendar.color},
            {QStringLiteral("visible"), calendar.visible},
            {QStringLiteral("events"), counts.value(calendar.id)},
        };
    }
    return list;
}

QVariantMap Agenda::calendar(const QString &id) const
{
    const Calendar *calendar = calendarOf(id);
    if (!calendar) {
        return {};
    }
    return {{QStringLiteral("id"), calendar->id}, {QStringLiteral("name"), calendar->name},
            {QStringLiteral("color"), calendar->color}, {QStringLiteral("visible"), calendar->visible}};
}

void Agenda::setDefaultCalendar(const QString &id)
{
    if (id != m_default && calendarOf(id)) {
        m_default = id;
        commit();
    }
}

QString Agenda::addCalendar(const QString &name, const QString &color)
{
    Calendar calendar;
    calendar.id = newId();
    calendar.name = name.trimmed().isEmpty() ? QStringLiteral("Calendario") : name.trimmed();
    calendar.color = QColor::isValidColorName(color) ? color : palette().at(m_calendars.size() % palette().size());
    m_calendars << calendar;
    commit();
    return calendar.id;
}

void Agenda::editCalendar(const QString &id, const QString &name, const QString &color)
{
    for (Calendar &calendar : m_calendars) {
        if (calendar.id == id) {
            if (!name.trimmed().isEmpty()) {
                calendar.name = name.trimmed();
            }
            if (QColor::isValidColorName(color)) {
                calendar.color = color;
            }
            commit();
            return;
        }
    }
}

void Agenda::setVisible(const QString &id, bool visible)
{
    for (Calendar &calendar : m_calendars) {
        if (calendar.id == id && calendar.visible != visible) {
            calendar.visible = visible;
            commit();
            return;
        }
    }
}

bool Agenda::removeCalendar(const QString &id)
{
    if (m_calendars.size() < 2 || !calendarOf(id)) {
        return false;
    }
    m_calendars.removeIf([&](const Calendar &calendar) { return calendar.id == id; });
    m_events.removeIf([&](const Event &event) { return event.calendar == id; });
    if (m_default == id) {
        m_default = m_calendars.first().id;
    }
    commit();
    return true;
}

// --- Events ------------------------------------------------------------------

QVariantMap Agenda::event(const QString &id) const
{
    const Event *event = find(id);
    if (!event) {
        return {};
    }
    // The day an event «ends on» is the last one it touches.
    const QDateTime last = event->end > event->start && event->end.time() == QTime(0, 0) && event->allDay ? event->end.addDays(-1) : event->end;
    return {
        {QStringLiteral("id"), event->id},
        {QStringLiteral("title"), event->title},
        {QStringLiteral("calendar"), event->calendar},
        {QStringLiteral("location"), event->location},
        {QStringLiteral("notes"), event->notes},
        {QStringLiteral("allDay"), event->allDay},
        {QStringLiteral("startDay"), int(event->start.date().toJulianDay())},
        {QStringLiteral("startMin"), minuteOf(event->start.time())},
        {QStringLiteral("endDay"), int(last.date().toJulianDay())},
        {QStringLiteral("endMin"), minuteOf(event->end.time())},
        {QStringLiteral("repeat"), int(event->repeat)},
        {QStringLiteral("untilDay"), event->until.isValid() ? int(event->until.toJulianDay()) : 0},
        {QStringLiteral("reminder"), event->reminder},
    };
}

QVariantMap Agenda::blank(int day, int minute) const
{
    if (minute < 0) {
        // On today, the next round hour; on any other day, nine in the morning.
        const QDateTime now = Event::now();
        minute = now.date().toJulianDay() == day ? qMin(23, now.time().hour() + 1) * 60 : 9 * 60;
    }
    minute = qBound(0, minute, 1439);
    const int end = minute + 60;
    return {
        {QStringLiteral("id"), QString()},
        {QStringLiteral("title"), QString()},
        {QStringLiteral("calendar"), m_default},
        {QStringLiteral("location"), QString()},
        {QStringLiteral("notes"), QString()},
        {QStringLiteral("allDay"), false},
        {QStringLiteral("startDay"), day},
        {QStringLiteral("startMin"), minute},
        {QStringLiteral("endDay"), day + (end >= 1440 ? 1 : 0)},
        {QStringLiteral("endMin"), end % 1440},
        {QStringLiteral("repeat"), 0},
        {QStringLiteral("untilDay"), 0},
        {QStringLiteral("reminder"), -1},
    };
}

QString Agenda::save(const QVariantMap &fields)
{
    Event event;
    const Event *old = find(fields.value(QStringLiteral("id")).toString());
    if (old) {
        event = *old;
    }
    event.title = fields.value(QStringLiteral("title")).toString().trimmed();
    event.location = fields.value(QStringLiteral("location")).toString().trimmed();
    event.notes = fields.value(QStringLiteral("notes")).toString().trimmed();
    event.calendar = fields.value(QStringLiteral("calendar")).toString();
    event.allDay = fields.value(QStringLiteral("allDay")).toBool();
    const QDate startDay = dateOf(fields.value(QStringLiteral("startDay")).toInt());
    QDate endDay = dateOf(fields.value(QStringLiteral("endDay")).toInt());
    if (!startDay.isValid()) {
        return {};
    }
    if (!endDay.isValid() || endDay < startDay) {
        endDay = startDay;
    }
    if (event.allDay) {
        event.start = Event::at(startDay);
        event.end = Event::at(endDay.addDays(1));
    } else {
        const int startMin = qBound(0, fields.value(QStringLiteral("startMin")).toInt(), 1439);
        const int endMin = qBound(0, fields.value(QStringLiteral("endMin")).toInt(), 1439);
        event.start = Event::at(startDay, QTime(startMin / 60, startMin % 60));
        event.end = Event::at(endDay, QTime(endMin / 60, endMin % 60));
        if (event.end < event.start) {
            event.end = event.start.addSecs(3600);
        }
    }
    const auto repeat = Event::Repeat(qBound(0, fields.value(QStringLiteral("repeat")).toInt(), int(Event::Yearly)));
    const int untilDay = fields.value(QStringLiteral("untilDay")).toInt();
    const QDate until = untilDay > 0 ? dateOf(untilDay) : QDate();
    // A different rhythm or first day is a different series: what was left
    // out of the old one, and its count, no longer mean anything.
    if (!old || old->repeat != repeat || old->start.date() != event.start.date()) {
        event.skipped.clear();
        event.interval = 1;
        event.count = 0;
    }
    if (old && until != old->until) {
        event.count = 0;
    }
    event.repeat = repeat;
    event.until = repeat == Event::Never || (until.isValid() && until < event.start.date()) ? QDate() : until;
    event.reminder = qMax(-1, fields.value(QStringLiteral("reminder"), -1).toInt());
    return put(event);
}

void Agenda::remove(const QString &id)
{
    if (m_events.removeIf([&](const Event &event) { return event.id == id; }) > 0) {
        commit();
    }
}

void Agenda::skip(const QString &id, int day)
{
    const QDate date = dateOf(day);
    for (Event &event : m_events) {
        if (event.id == id && event.repeats() && !event.skipped.contains(date)) {
            event.skipped << date;
            commit();
            return;
        }
    }
}

// --- Reading -----------------------------------------------------------------

QList<Agenda::Occurrence> Agenda::between(const QDateTime &from, const QDateTime &to) const
{
    QHash<QString, bool> shown;
    for (const Calendar &calendar : m_calendars) {
        shown.insert(calendar.id, calendar.visible);
    }
    QList<Occurrence> found;
    for (const Event &event : m_events) {
        if (!shown.value(event.calendar)) {
            continue;
        }
        const qint64 length = event.seconds();
        for (const QDateTime &start : event.starts(from, to)) {
            found << Occurrence{&event, start, start.addSecs(length)};
        }
    }
    std::stable_sort(found.begin(), found.end(), [](const Occurrence &a, const Occurrence &b) {
        if (a.event->allDay != b.event->allDay) {
            return a.event->allDay;
        }
        if (a.start != b.start) {
            return a.start < b.start;
        }
        return a.end > b.end;
    });
    return found;
}

QString Agenda::clock(int minute)
{
    return QStringLiteral("%1:%2").arg(minute / 60).arg(minute % 60, 2, 10, QLatin1Char('0'));
}

QVariantMap Agenda::describe(const Occurrence &occurrence, const QDate &day) const
{
    const Event &event = *occurrence.event;
    const Calendar *calendar = calendarOf(event.calendar);
    // Ending at midnight is ending the day before, not touching the next one.
    QDate lastDay = occurrence.end.date();
    if (occurrence.end > occurrence.start && occurrence.end.time() == QTime(0, 0)) {
        lastDay = lastDay.addDays(-1);
    }
    const QDate firstDay = occurrence.start.date();
    return {
        {QStringLiteral("id"), event.id},
        {QStringLiteral("title"), event.title.isEmpty() ? QStringLiteral("Evento nuevo") : event.title},
        {QStringLiteral("color"), calendar ? calendar->color : palette().at(4)},
        {QStringLiteral("calendar"), event.calendar},
        {QStringLiteral("allDay"), event.allDay},
        {QStringLiteral("startDay"), int(firstDay.toJulianDay())},
        {QStringLiteral("endDay"), int(lastDay.toJulianDay())},
        {QStringLiteral("startMin"), firstDay < day ? 0 : minuteOf(occurrence.start.time())},
        {QStringLiteral("endMin"), lastDay > day ? 1440 : (occurrence.end.date() > day ? 1440 : minuteOf(occurrence.end.time()))},
        {QStringLiteral("from"), int(firstDay.toJulianDay())},
        {QStringLiteral("begins"), firstDay == day},
        {QStringLiteral("ends"), lastDay == day},
        {QStringLiteral("time"), clock(minuteOf(occurrence.start.time()))},
        {QStringLiteral("endTime"), clock(minuteOf(occurrence.end.time()))},
        {QStringLiteral("location"), event.location},
        {QStringLiteral("repeats"), event.repeats()},
    };
}

QVariantList Agenda::day(int day) const
{
    const QDate date = dateOf(day);
    QVariantList list;
    for (const Occurrence &occurrence : between(Event::at(date), Event::at(date.addDays(1)))) {
        list << describe(occurrence, date);
    }
    return list;
}

QVariantList Agenda::layout(int day) const
{
    // An event too short to read still takes the room of half an hour.
    static const int least = 30;
    const QDate date = dateOf(day);
    struct Box {
        QVariantMap fields;
        int top;
        int bottom;
        int column = 0;
    };
    QList<Box> boxes;
    for (const Occurrence &occurrence : between(Event::at(date), Event::at(date.addDays(1)))) {
        if (occurrence.event->allDay) {
            continue;
        }
        Box box;
        box.fields = describe(occurrence, date);
        box.top = box.fields.value(QStringLiteral("startMin")).toInt();
        box.bottom = qMin(1440, qMax(box.fields.value(QStringLiteral("endMin")).toInt(), box.top + least));
        box.top = qMin(box.top, box.bottom - least);
        boxes << box;
    }
    std::stable_sort(boxes.begin(), boxes.end(), [](const Box &a, const Box &b) {
        return a.top != b.top ? a.top < b.top : a.bottom > b.bottom;
    });
    QVariantList list;
    // A group is a run of events chained by overlaps: they share the width.
    int groupStart = 0;
    int groupEnd = -1;
    QList<int> columnEnds;
    const auto close = [&](int upTo) {
        for (int i = groupStart; i < upTo; ++i) {
            QVariantMap fields = boxes.at(i).fields;
            fields.insert(QStringLiteral("top"), boxes.at(i).top);
            fields.insert(QStringLiteral("bottom"), boxes.at(i).bottom);
            fields.insert(QStringLiteral("column"), boxes.at(i).column);
            fields.insert(QStringLiteral("columns"), int(columnEnds.size()));
            list << fields;
        }
    };
    for (int i = 0; i < boxes.size(); ++i) {
        Box &box = boxes[i];
        if (i > groupStart && box.top >= groupEnd) {
            close(i);
            groupStart = i;
            columnEnds.clear();
            groupEnd = -1;
        }
        int column = 0;
        while (column < columnEnds.size() && columnEnds.at(column) > box.top) {
            ++column;
        }
        if (column == columnEnds.size()) {
            columnEnds << box.bottom;
        } else {
            columnEnds[column] = box.bottom;
        }
        box.column = column;
        groupEnd = qMax(groupEnd, box.bottom);
    }
    close(boxes.size());
    return list;
}

QVariantList Agenda::busy(int first, int count) const
{
    count = qBound(0, count, 800);
    QList<int> totals(count, 0);
    const QDate start = dateOf(first);
    for (const Occurrence &occurrence : between(Event::at(start), Event::at(start.addDays(count)))) {
        QDate last = occurrence.end.date();
        if (occurrence.end > occurrence.start && occurrence.end.time() == QTime(0, 0)) {
            last = last.addDays(-1);
        }
        const int from = qMax(0, int(start.daysTo(occurrence.start.date())));
        const int to = qMin(count - 1, int(start.daysTo(last)));
        for (int i = from; i <= to; ++i) {
            ++totals[i];
        }
    }
    QVariantList list;
    for (int total : std::as_const(totals)) {
        list << total;
    }
    return list;
}

QString Agenda::fold(const QString &text)
{
    QString out;
    for (const QChar c : text.normalized(QString::NormalizationForm_KD)) {
        if (c.category() != QChar::Mark_NonSpacing) {
            out += c.toLower();
        }
    }
    return out;
}

QVariantList Agenda::search(const QString &text) const
{
    const QString wanted = fold(text.trimmed());
    if (wanted.isEmpty()) {
        return {};
    }
    const QDateTime now = Event::at(Event::now().date());
    QList<Occurrence> found;
    for (const Event &event : m_events) {
        const Calendar *calendar = calendarOf(event.calendar);
        if (!calendar || !calendar->visible) {
            continue;
        }
        if (!fold(event.title).contains(wanted) && !fold(event.location).contains(wanted) && !fold(event.notes).contains(wanted)) {
            continue;
        }
        QDateTime start = event.start;
        if (event.repeats()) {
            // The next time it happens; if it is over, the last time it did.
            QList<QDateTime> starts = event.starts(now, now.addYears(5));
            if (!starts.isEmpty()) {
                start = starts.first();
            } else {
                starts = event.starts(event.start, now);
                if (starts.isEmpty()) {
                    continue;
                }
                start = starts.last();
            }
        }
        found << Occurrence{&event, start, start.addSecs(event.seconds())};
    }
    std::stable_sort(found.begin(), found.end(), [](const Occurrence &a, const Occurrence &b) { return a.start < b.start; });
    QVariantList list;
    for (const Occurrence &occurrence : std::as_const(found)) {
        list << describe(occurrence, occurrence.start.date());
    }
    return list;
}

// --- Other calendars -----------------------------------------------------------

int Agenda::importFile(const QString &path, const QString &calendarId)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return -1;
    }
    const QByteArray data = file.read(64 * 1024 * 1024);
    if (!data.contains("BEGIN:VCALENDAR") && !data.contains("BEGIN:VEVENT")) {
        return -1;
    }
    const Ics::File parsed = Ics::parse(data);
    const QString target = calendarOf(calendarId) ? calendarId : m_default;
    QHash<QString, int> where;
    for (int i = 0; i < m_events.size(); ++i) {
        where.insert(m_events.at(i).id, i);
    }
    for (Event event : parsed.events) {
        if (event.id.isEmpty()) {
            event.id = newId();
        }
        const auto it = where.constFind(event.id);
        if (it != where.constEnd()) {
            event.calendar = m_events.at(it.value()).calendar;
            m_events[it.value()] = event;
        } else {
            event.calendar = target;
            where.insert(event.id, m_events.size());
            m_events << event;
        }
    }
    if (!parsed.events.isEmpty()) {
        commit();
    }
    return parsed.events.size();
}

bool Agenda::exportFile(const QString &path, const QString &calendarId) const
{
    QList<Event> events;
    for (const Event &event : m_events) {
        const Calendar *calendar = calendarOf(event.calendar);
        if (calendarId.isEmpty() ? (calendar && calendar->visible) : event.calendar == calendarId) {
            events << event;
        }
    }
    const Calendar *calendar = calendarOf(calendarId);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(Ics::write(events, calendar ? calendar->name : QStringLiteral("Calendario")));
    return file.commit();
}

// --- Reminders ---------------------------------------------------------------

QList<Notice> Agenda::due(const QDateTime &after, const QDateTime &until) const
{
    QList<Notice> notices;
    if (!(after < until)) {
        return notices;
    }
    const QDateTime localAfter = after.toLocalTime();
    const QDateTime localUntil = until.toLocalTime();
    for (const Event &event : m_events) {
        const Calendar *calendar = calendarOf(event.calendar);
        if (event.reminder < 0 || !calendar || !calendar->visible) {
            continue;
        }
        // An all-day event has no hour of its own: its notice counts from nine in the morning.
        const qint64 lead = qint64(event.reminder) * 60 - (event.allDay ? 9 * 3600 : 0);
        // A day's slack either side covers a clock change.
        const QDateTime from = Event::at(localAfter.date(), localAfter.time()).addSecs(lead - 86400);
        const QDateTime to = Event::at(localUntil.date(), localUntil.time()).addSecs(lead + 86400);
        for (const QDateTime &start : event.starts(from, to)) {
            const QDateTime alert = Event::real(start).addSecs(-lead);
            if (alert > after && alert <= until) {
                notices << Notice{event.title.isEmpty() ? QStringLiteral("Evento nuevo") : event.title, event.location, start, event.allDay};
            }
        }
    }
    return notices;
}
