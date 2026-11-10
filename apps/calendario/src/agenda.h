#pragma once

#include "event.h"

#include <QColor>
#include <QHash>
#include <QList>
#include <QObject>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

/// A calendar: a named, coloured group of events that can be hidden.
struct Calendar {
    QString id;
    QString name;
    QString color;
    bool visible = true;
};

/// A reminder that has come due.
struct Notice {
    QString title;
    QString location;
    QDateTime start;
    bool allDay = false;
};

/// Every calendar and every event, kept in one JSON file.
///
/// The window talks in plain numbers: a day is its Julian day number
/// (QDate::toJulianDay; `day % 7` is 0 on a Monday) and a time of day is
/// minutes since midnight, so nothing depends on JavaScript's dates.
class Agenda : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
    /// Goes up with every change: read it in a binding to follow the agenda.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    /// [{id, name, color, visible, events}]
    Q_PROPERTY(QVariantList calendars READ calendarList NOTIFY changed)
    /// Where a new event goes.
    Q_PROPERTY(QString defaultCalendar READ defaultCalendar WRITE setDefaultCalendar NOTIFY changed)
    Q_PROPERTY(QStringList palette READ palette CONSTANT)

public:
    explicit Agenda(const QString &path, QObject *parent = nullptr);

    int revision() const { return m_revision; }
    QVariantList calendarList() const;
    QString defaultCalendar() const { return m_default; }
    void setDefaultCalendar(const QString &id);
    static QStringList palette();

    const QList<Calendar> &calendars() const { return m_calendars; }
    const QList<Event> &events() const { return m_events; }
    const Event *find(const QString &id) const;
    /// Adds the event, or replaces the one with its id. Gives the id back.
    QString put(Event event);

    Q_INVOKABLE QString addCalendar(const QString &name, const QString &color);
    Q_INVOKABLE void editCalendar(const QString &id, const QString &name, const QString &color);
    Q_INVOKABLE void setVisible(const QString &id, bool visible);
    /// Takes the calendar and its events away. The last calendar stays.
    Q_INVOKABLE bool removeCalendar(const QString &id);
    Q_INVOKABLE QVariantMap calendar(const QString &id) const;

    /// What the editor shows: {id, title, calendar, location, notes, allDay,
    /// startDay, startMin, endDay, endMin, repeat, untilDay, reminder}. The
    /// last day of an all-day event is `endDay` itself.
    Q_INVOKABLE QVariantMap event(const QString &id) const;
    /// A fresh event on that day; `minute` < 0 makes it the next round hour.
    Q_INVOKABLE QVariantMap blank(int day, int minute) const;
    Q_INVOKABLE QString save(const QVariantMap &fields);
    Q_INVOKABLE void remove(const QString &id);
    /// Takes out the one occurrence of a repeating event that starts on `day`.
    Q_INVOKABLE void skip(const QString &id, int day);

    /// What happens on a day, in the visible calendars: all-day first, then by
    /// hour. Each: {id, title, color, calendar, allDay, startDay, endDay,
    /// startMin, endMin (both cut to this day), from (the occurrence's first
    /// day), begins, ends, time, location, repeats}.
    Q_INVOKABLE QVariantList day(int day) const;
    /// The timed events of a day placed side by side where they overlap:
    /// day()'s fields plus {column, columns}.
    Q_INVOKABLE QVariantList layout(int day) const;
    /// How many events touch each of `count` days from `first`.
    Q_INVOKABLE QVariantList busy(int first, int count) const;
    /// Events whose title, place or notes have the text, case and accents
    /// aside. A repeating event appears once, at its next occurrence.
    Q_INVOKABLE QVariantList search(const QString &text) const;

    /// Reads an .ics into a calendar (the default one when `calendarId` is
    /// empty); an event already here, by its UID, is replaced where it is.
    /// Gives the number of events read, or -1 when the file cannot be read.
    Q_INVOKABLE int importFile(const QString &path, const QString &calendarId = QString());
    /// Writes one calendar, or all the visible ones when `calendarId` is empty.
    Q_INVOKABLE bool exportFile(const QString &path, const QString &calendarId = QString()) const;

    /// The reminders that came due in (after, until], real time.
    QList<Notice> due(const QDateTime &after, const QDateTime &until) const;

    /// «9:00», «18:30».
    static QString clock(int minute);
    /// Lower case and without accents, to compare what was typed.
    static QString fold(const QString &text);

Q_SIGNALS:
    void changed();

private:
    struct Occurrence {
        const Event *event;
        QDateTime start;
        QDateTime end;
    };
    QList<Occurrence> between(const QDateTime &from, const QDateTime &to) const;
    QVariantMap describe(const Occurrence &occurrence, const QDate &day) const;
    const Calendar *calendarOf(const QString &id) const;
    void load();
    void commit();

    QString m_path;
    QList<Calendar> m_calendars;
    QList<Event> m_events;
    QString m_default;
    int m_revision = 0;
};
