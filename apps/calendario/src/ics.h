#pragma once

#include "event.h"

#include <QByteArray>
#include <QList>
#include <QString>

/// iCalendar (.ics, RFC 5545): what other calendars read and write.
namespace Ics {

struct File {
    /// X-WR-CALNAME, when the file names its calendar.
    QString name;
    /// The events, with `id` set to the file's UID and no calendar.
    QList<Event> events;
};

/// Reads the events of a file. Times with a zone or in UTC become the wall
/// clock here. Of a repetition it keeps the frequency, the interval, the end
/// and the left-out days; finer rules (BYDAY lists and the like) are not kept.
File parse(const QByteArray &data);

/// Writes events as one calendar. Times go out floating, as they are kept.
QByteArray write(const QList<Event> &events, const QString &name);

}
