#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

/// The copies a document leaves behind: one per manual save, the newest few
/// kept, in a folder of the data directory named after the document's path.
class Versions
{
public:
    struct Entry {
        QString file;
        QDateTime when;
        qint64 size = 0;
    };

    explicit Versions(const QString &dir, int keep = 20);

    /// Keeps `bytes` as the newest version of the document; the path of the
    /// copy, or nothing when it is the same as the newest one or cannot be written.
    QString add(const QString &document, const QByteArray &bytes, const QDateTime &when = QDateTime::currentDateTime());
    /// Newest first.
    QList<Entry> list(const QString &document) const;
    /// Whether `file` is one of the copies kept here.
    bool owns(const QString &file) const;
    QString folder(const QString &document) const;

private:
    QString m_dir;
    int m_keep;
};
