#include "history.h"

#include "media.h"
#include "store.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

/// As many as are remembered.
static const int kLimit = 200;

History::History(const QString &file, QObject *parent)
    : QObject(parent)
    , m_file(file)
{
    if (m_file.isEmpty()) {
        return;
    }
    const QJsonArray items = Store::read(m_file).value(QLatin1String("items")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject object = value.toObject();
        Entry entry;
        entry.path = object.value(QLatin1String("path")).toString();
        entry.position = qint64(object.value(QLatin1String("position")).toDouble());
        entry.duration = qint64(object.value(QLatin1String("duration")).toDouble());
        entry.seen = qint64(object.value(QLatin1String("seen")).toDouble());
        if (!entry.path.isEmpty() && find(entry.path) < 0) {
            m_entries << entry;
        }
    }
}

int History::find(const QString &path) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).path == path) {
            return i;
        }
    }
    return -1;
}

void History::save()
{
    ++m_revision;
    Q_EMIT changed();
    if (m_file.isEmpty()) {
        return;
    }
    QJsonArray items;
    for (const Entry &entry : std::as_const(m_entries)) {
        items << QJsonObject{
            {QLatin1String("path"), entry.path},
            {QLatin1String("position"), double(entry.position)},
            {QLatin1String("duration"), double(entry.duration)},
            {QLatin1String("seen"), double(entry.seen)},
        };
    }
    Store::write(m_file, QJsonObject{{QLatin1String("items"), items}});
}

void History::record(const QString &path, qint64 position, qint64 duration, qint64 now)
{
    if (path.isEmpty()) {
        return;
    }
    Entry entry;
    const int at = find(path);
    if (at >= 0) {
        entry = m_entries.takeAt(at);
    }
    entry.path = path;
    entry.position = qMax(qint64(0), position);
    if (duration > 0) {
        entry.duration = duration;
    }
    entry.seen = now;
    m_entries.prepend(entry);
    while (m_entries.size() > kLimit) {
        m_entries.removeLast();
    }
    save();
}

bool History::finished(qint64 position, qint64 duration)
{
    if (duration <= 0) {
        return false;
    }
    // The credits: the last twentieth, and never less than ten seconds.
    return duration - position <= qMax(qint64(10000), duration / 20);
}

qint64 History::resume(const QString &path) const
{
    const int at = find(path);
    if (at < 0) {
        return 0;
    }
    const Entry &entry = m_entries.at(at);
    if (entry.duration < 60000 || entry.position < 5000 || finished(entry.position, entry.duration)) {
        return 0;
    }
    return entry.position;
}

qint64 History::duration(const QString &path) const
{
    const int at = find(path);
    return at < 0 ? 0 : m_entries.at(at).duration;
}

QVariantList History::recent(int limit) const
{
    QVariantList out;
    for (const Entry &entry : m_entries) {
        if (out.size() >= limit) {
            break;
        }
        const QFileInfo info(entry.path);
        if (!info.isFile()) {
            continue;
        }
        const bool done = finished(entry.position, entry.duration);
        out << QVariantMap{
            {QStringLiteral("path"), entry.path},
            {QStringLiteral("title"), Media::title(entry.path)},
            {QStringLiteral("folder"), info.dir().dirName()},
            {QStringLiteral("position"), entry.position},
            {QStringLiteral("duration"), entry.duration},
            {QStringLiteral("progress"), entry.duration > 0 ? qBound(0.0, double(entry.position) / double(entry.duration), 1.0) : 0.0},
            {QStringLiteral("finished"), done},
            {QStringLiteral("audio"), Media::isAudio(entry.path)},
            {QStringLiteral("stamp"), info.lastModified().toSecsSinceEpoch()},
        };
    }
    return out;
}

void History::forget(const QString &path)
{
    const int at = find(path);
    if (at >= 0) {
        m_entries.removeAt(at);
        save();
    }
}

void History::clear()
{
    if (!m_entries.isEmpty()) {
        m_entries.clear();
        save();
    }
}
