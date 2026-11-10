#pragma once

#include "track.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

/// The tracks of one view: every song, an album, an artist, a playlist or a search.
class TrackModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    /// "songs", "album", "artist", "playlist" or "search".
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY sourceChanged)
    /// The album key, the artist's name, the playlist id or the text searched for.
    Q_PROPERTY(QString key READ key WRITE setKey NOTIFY sourceChanged)
    Q_PROPERTY(int count READ count NOTIFY contentChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY contentChanged)
    Q_PROPERTY(QStringList paths READ paths NOTIFY contentChanged)

public:
    enum Roles { Path = Qt::UserRole + 1, Title, Artist, Album, AlbumKey, Number, Disc, Duration, Slot };

    explicit TrackModel(QObject *parent = nullptr);

    QString mode() const { return m_mode; }
    void setMode(const QString &mode);
    QString key() const { return m_key; }
    void setKey(const QString &key);
    int count() const { return int(m_tracks.size()); }
    qint64 duration() const;
    QStringList paths() const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void sourceChanged();
    void contentChanged();

private:
    void reload();

    QString m_mode = QStringLiteral("songs");
    QString m_key;
    QList<Track> m_tracks;
    /// For a playlist: where each row sits in it (rows of missing files are left out).
    QList<int> m_slots;
};
