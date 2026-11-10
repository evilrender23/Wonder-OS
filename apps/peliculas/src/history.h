#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

/// Where each film was left, and which were seen last.
class History : public QObject
{
    Q_OBJECT
    /// Goes up with every change: what QML reads through it is read again.
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    struct Entry {
        QString path;
        qint64 position = 0;
        qint64 duration = 0;
        /// When it was last played, in milliseconds since 1970.
        qint64 seen = 0;
    };

    /// An empty `file` keeps everything in memory only.
    explicit History(const QString &file, QObject *parent = nullptr);

    int revision() const { return m_revision; }
    /// Notes where `path` is. `now` is the moment, for the order of «recent».
    void record(const QString &path, qint64 position, qint64 duration, qint64 now);
    /// Where to pick `path` up again; 0 when it is better seen from the start:
    /// barely begun, as good as finished, or too short to matter.
    qint64 resume(const QString &path) const;
    qint64 duration(const QString &path) const;
    /// Seen to the end, or near enough.
    static bool finished(qint64 position, qint64 duration);

    /// The last ones played and still on disk, newest first:
    /// [{path, title, folder, position, duration, progress, finished}].
    Q_INVOKABLE QVariantList recent(int limit) const;
    Q_INVOKABLE void forget(const QString &path);
    Q_INVOKABLE void clear();

    int count() const { return int(m_entries.size()); }

Q_SIGNALS:
    void changed();

private:
    int find(const QString &path) const;
    void save();

    QString m_file;
    /// Newest first.
    QList<Entry> m_entries;
    int m_revision = 0;
};
