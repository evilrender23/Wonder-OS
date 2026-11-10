#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

/// One event or appointment, possibly a repeating one.
///
/// Times are «floating»: the wall clock of wherever the user is, kept in
/// QDateTimes marked UTC so that a day is always 24 hours and no hour is ever
/// skipped or doubled. Only a reminder needs the real instant (Event::real).
struct Event {
    enum Repeat { Never, Daily, Weekly, Monthly, Yearly };

    QString id;
    QString calendar;
    QString title;
    QString location;
    QString notes;
    QDateTime start;
    /// Exclusive. An all-day event ends at midnight after its last day.
    QDateTime end;
    bool allDay = false;
    Repeat repeat = Never;
    /// Every `interval` days, weeks, months or years.
    int interval = 1;
    /// The last day an occurrence may start on; invalid is «for ever».
    QDate until;
    /// How many occurrences there are in all; 0 is «no limit».
    int count = 0;
    /// Occurrences taken out one by one, by the day they would start on.
    QList<QDate> skipped;
    /// Minutes before the start to give notice; -1 is «none».
    int reminder = -1;

    bool repeats() const { return repeat != Never; }
    qint64 seconds() const { return qMax(qint64(0), start.secsTo(end)); }

    /// The starts of the occurrences that touch [from, to), in order.
    QList<QDateTime> starts(const QDateTime &from, const QDateTime &to) const;

    QJsonObject toJson() const;
    static Event fromJson(const QJsonObject &object);

    /// A floating time out of a date and a time of day.
    static QDateTime at(const QDate &date, const QTime &time = QTime(0, 0));
    /// The wall clock right now, floating (WONDERLAND_CALENDARIO_NOW sets it, for tests).
    static QDateTime now();
    /// The instant a floating time happens at, here.
    static QDateTime real(const QDateTime &floating);
};
