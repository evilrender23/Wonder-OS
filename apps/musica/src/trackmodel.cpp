#include "trackmodel.h"

#include "app.h"
#include "library.h"
#include "playlists.h"

#include <QCollator>

TrackModel::TrackModel(QObject *parent)
    : QAbstractListModel(parent)
{
    App *app = App::instance();
    connect(app->library(), &Library::changed, this, &TrackModel::reload);
    connect(app->playlists(), &Playlists::tracksChanged, this, [this](const QString &id) {
        if (m_mode == QLatin1String("playlist") && id == m_key) {
            reload();
        }
    });
    reload();
}

void TrackModel::setMode(const QString &mode)
{
    if (m_mode != mode) {
        m_mode = mode;
        Q_EMIT sourceChanged();
        reload();
    }
}

void TrackModel::setKey(const QString &key)
{
    if (m_key != key) {
        m_key = key;
        Q_EMIT sourceChanged();
        reload();
    }
}

void TrackModel::reload()
{
    const Library *library = App::instance()->library();
    QList<Track> tracks;
    QList<int> slotList;
    if (m_mode == QLatin1String("album")) {
        tracks = library->albumTracks(m_key);
    } else if (m_mode == QLatin1String("artist")) {
        tracks = library->artistTracks(m_key);
    } else if (m_mode == QLatin1String("playlist")) {
        const QStringList paths = App::instance()->playlists()->paths(m_key);
        for (int i = 0; i < paths.size(); ++i) {
            const Track track = library->track(paths.at(i));
            if (track.isValid()) {
                tracks << track;
                slotList << i;
            }
        }
    } else if (m_mode == QLatin1String("search")) {
        const QStringList words = folded(m_key).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (!words.isEmpty()) {
            for (const Track &track : library->tracks()) {
                // The «unknown» names are found by what the window calls them, too.
                const App *app = App::instance();
                const QString hay = folded(track.title + QLatin1Char(' ') + track.artist + QLatin1Char(' ') + track.album + QLatin1Char(' ') + track.albumArtist
                                           + QLatin1Char(' ') + app->shown(track.artist) + QLatin1Char(' ') + app->shown(track.album));
                const bool match = std::all_of(words.begin(), words.end(), [&hay](const QString &word) {
                    return hay.contains(word);
                });
                if (match) {
                    tracks << track;
                }
            }
        }
    } else {
        tracks = library->tracks();
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        collator.setNumericMode(true);
        std::stable_sort(tracks.begin(), tracks.end(), [&collator](const Track &a, const Track &b) {
            return collator.compare(folded(a.title), folded(b.title)) < 0;
        });
    }
    beginResetModel();
    m_tracks = tracks;
    m_slots = slotList;
    endResetModel();
    Q_EMIT contentChanged();
}

qint64 TrackModel::duration() const
{
    qint64 total = 0;
    for (const Track &track : m_tracks) {
        total += track.durationMs;
    }
    return total;
}

QStringList TrackModel::paths() const
{
    QStringList paths;
    paths.reserve(m_tracks.size());
    for (const Track &track : m_tracks) {
        paths << track.path;
    }
    return paths;
}

int TrackModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_tracks.size());
}

QVariant TrackModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_tracks.size()) {
        return {};
    }
    const Track &track = m_tracks.at(index.row());
    switch (role) {
    case Path:
        return track.path;
    case Title:
        return track.title;
    case Artist:
        return track.artist;
    case Album:
        return track.album;
    case AlbumKey:
        return track.albumKey();
    case Number:
        return track.number;
    case Disc:
        return track.disc;
    case Duration:
        return track.durationMs;
    case Slot:
        return m_slots.value(index.row(), index.row());
    }
    return {};
}

QHash<int, QByteArray> TrackModel::roleNames() const
{
    return {
        {Path, "path"},
        {Title, "title"},
        {Artist, "artist"},
        {Album, "album"},
        {AlbumKey, "albumKey"},
        {Number, "number"},
        {Disc, "disc"},
        {Duration, "duration"},
        {Slot, "slot"},
    };
}
