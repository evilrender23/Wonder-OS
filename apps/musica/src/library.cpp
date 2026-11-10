#include "library.h"

#include "store.h"
#include "tagreader.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QSet>
#include <QStandardPaths>
#include <QtConcurrentRun>

static const int cacheVersion = 1;
/// inotify watches are a shared, limited resource: a huge library is rescanned by hand.
static const int maxWatched = 2000;
static const int recentLimit = 60;

Library::Library(Store *store, const QString &cachePath, bool watch, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_cachePath(cachePath)
{
    const QJsonValue stored = m_store->value(QStringLiteral("folders"));
    if (stored.isArray()) {
        for (const QJsonValue &folder : stored.toArray()) {
            m_folders << folder.toString();
        }
    } else {
        const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (!music.isEmpty() && music != QDir::homePath()) {
            m_folders << music;
        }
    }

    connect(&m_job, &QFutureWatcher<Scan>::finished, this, &Library::finishScan);
    m_settle.setSingleShot(true);
    m_settle.setInterval(1500);
    connect(&m_settle, &QTimer::timeout, this, &Library::rescan);
    if (watch) {
        m_watcher = new QFileSystemWatcher(this);
        // A copy in progress fires once per file: wait for it to settle.
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
    }
    loadCache();
}

Library::~Library()
{
    m_job.waitForFinished();
}

void Library::addFolder(const QUrl &folder)
{
    const QString path = QDir::cleanPath(folder.isLocalFile() ? folder.toLocalFile() : folder.toString());
    if (path.isEmpty() || !QFileInfo(path).isDir() || m_folders.contains(path)) {
        return;
    }
    m_folders << path;
    QJsonArray array;
    for (const QString &each : std::as_const(m_folders)) {
        array << each;
    }
    m_store->set(QStringLiteral("folders"), array);
    Q_EMIT foldersChanged();
    rescan();
}

void Library::removeFolder(const QString &folder)
{
    if (m_folders.removeAll(folder) == 0) {
        return;
    }
    QJsonArray array;
    for (const QString &each : std::as_const(m_folders)) {
        array << each;
    }
    m_store->set(QStringLiteral("folders"), array);
    Q_EMIT foldersChanged();
    rescan();
}

void Library::rescan()
{
    if (m_scanning) {
        m_again = true;
        return;
    }
    m_scanning = true;
    Q_EMIT scanningChanged();
    QHash<QString, Track> known;
    known.reserve(m_tracks.size());
    for (const Track &track : std::as_const(m_tracks)) {
        known.insert(track.path, track);
    }
    m_job.setFuture(QtConcurrent::run(&Library::scan, m_folders, known));
}

void Library::finishScan()
{
    const Scan result = m_job.result();
    if (m_watcher) {
        const QStringList watched = m_watcher->directories();
        if (!watched.isEmpty()) {
            m_watcher->removePaths(watched);
        }
        const QStringList folders = result.folders.mid(0, maxWatched);
        if (!folders.isEmpty()) {
            m_watcher->addPaths(folders);
        }
    }
    bool same = result.tracks.size() == m_tracks.size();
    for (qsizetype i = 0; same && i < m_tracks.size(); ++i) {
        const Track &a = m_tracks.at(i);
        const Track &b = result.tracks.at(i);
        same = a.path == b.path && a.modified == b.modified && a.size == b.size;
    }
    if (!same) {
        setTracks(result.tracks);
        saveCache();
    }
    m_scanning = false;
    Q_EMIT scanningChanged();
    if (m_again) {
        m_again = false;
        rescan();
    }
}

Library::Scan Library::scan(const QStringList &folders, const QHash<QString, Track> &known)
{
    Scan result;
    QSet<QString> seen;
    for (const QString &root : folders) {
        if (!QFileInfo(root).isDir()) {
            continue;
        }
        result.folders << root;
        QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo info = it.nextFileInfo();
            if (info.isDir()) {
                result.folders << info.absoluteFilePath();
                continue;
            }
            const QString path = info.absoluteFilePath();
            if (!TagReader::isAudio(path) || seen.contains(path)) {
                continue;
            }
            seen.insert(path);
            const auto old = known.constFind(path);
            if (old != known.constEnd() && old->modified == info.lastModified().toSecsSinceEpoch() && old->size == info.size()) {
                result.tracks << *old;
            } else {
                result.tracks << TagReader::read(path);
            }
        }
    }
    sort(result.tracks);
    return result;
}

void Library::sort(QList<Track> &tracks)
{
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    struct Keyed {
        QCollatorSortKey owner;
        QCollatorSortKey album;
        QCollatorSortKey title;
        int year;
        qsizetype index;
    };
    // An album's year is that of its first dated track, so one stray tag does not split it.
    QHash<QString, int> years;
    for (const Track &track : std::as_const(tracks)) {
        int &year = years[track.albumKey()];
        if (track.year > 0 && (year == 0 || track.year < year)) {
            year = track.year;
        }
    }
    std::vector<Keyed> keys;
    keys.reserve(size_t(tracks.size()));
    for (qsizetype i = 0; i < tracks.size(); ++i) {
        const Track &track = tracks.at(i);
        // Folded first: the C locale's collator would put «Zeta» before «alfa».
        keys.push_back({collator.sortKey(folded(track.albumOwner())), collator.sortKey(folded(track.album)), collator.sortKey(folded(track.title)), years.value(track.albumKey()), i});
    }
    std::stable_sort(keys.begin(), keys.end(), [&tracks](const Keyed &a, const Keyed &b) {
        if (int order = a.owner.compare(b.owner)) {
            return order < 0;
        }
        if (a.year != b.year) {
            return a.year < b.year;
        }
        if (int order = a.album.compare(b.album)) {
            return order < 0;
        }
        const Track &x = tracks.at(a.index);
        const Track &y = tracks.at(b.index);
        if (x.disc != y.disc) {
            return x.disc < y.disc;
        }
        if (x.number != y.number) {
            return x.number < y.number;
        }
        if (int order = a.title.compare(b.title)) {
            return order < 0;
        }
        return x.path < y.path;
    });
    QList<Track> sorted;
    sorted.reserve(tracks.size());
    for (const Keyed &key : keys) {
        sorted << tracks.at(key.index);
    }
    tracks = sorted;
}

void Library::setTracks(const QList<Track> &tracks)
{
    m_tracks = tracks;
    m_byPath.clear();
    m_byPath.reserve(m_tracks.size());

    struct Album {
        QVariantMap map;
        int count = 0;
        qint64 duration = 0;
        qint64 added = 0;
        int year = 0;
    };
    struct Artist {
        QString name;
        QString cover;
        int albums = 0;
        int tracks = 0;
    };
    QList<Album> albums;
    QList<Artist> artists;
    QString lastKey;
    QString lastOwner;
    for (qsizetype i = 0; i < m_tracks.size(); ++i) {
        const Track &track = m_tracks.at(i);
        m_byPath.insert(track.path, int(i));
        const QString owner = track.albumOwner();
        if (artists.isEmpty() || owner != lastOwner) {
            artists.append({owner, track.path, 0, 0});
            lastOwner = owner;
        }
        const QString key = track.albumKey();
        if (albums.isEmpty() || key != lastKey) {
            Album album;
            album.map = {
                {QStringLiteral("key"), key},
                {QStringLiteral("title"), track.album},
                {QStringLiteral("artist"), owner},
                {QStringLiteral("genre"), track.genre},
                {QStringLiteral("cover"), track.path},
            };
            albums.append(album);
            artists.last().albums += 1;
            lastKey = key;
        }
        Album &album = albums.last();
        album.count += 1;
        album.duration += track.durationMs;
        album.added = qMax(album.added, track.modified);
        if (track.year > 0 && (album.year == 0 || track.year < album.year)) {
            album.year = track.year;
        }
        artists.last().tracks += 1;
    }

    m_albums.clear();
    for (Album &album : albums) {
        album.map.insert(QStringLiteral("count"), album.count);
        album.map.insert(QStringLiteral("duration"), album.duration);
        album.map.insert(QStringLiteral("year"), album.year);
        album.map.insert(QStringLiteral("added"), album.added);
        m_albums << album.map;
    }
    std::stable_sort(albums.begin(), albums.end(), [](const Album &a, const Album &b) {
        return a.added > b.added;
    });
    m_recentAlbums.clear();
    for (const Album &album : std::as_const(albums)) {
        if (m_recentAlbums.size() == recentLimit) {
            break;
        }
        m_recentAlbums << album.map;
    }
    m_artists.clear();
    for (const Artist &artist : std::as_const(artists)) {
        m_artists << QVariantMap{
            {QStringLiteral("name"), artist.name},
            {QStringLiteral("cover"), artist.cover},
            {QStringLiteral("albums"), artist.albums},
            {QStringLiteral("tracks"), artist.tracks},
        };
    }
    Q_EMIT changed();
}

QVariantMap Library::album(const QString &key) const
{
    for (const QVariant &album : m_albums) {
        const QVariantMap map = album.toMap();
        if (map.value(QStringLiteral("key")).toString() == key) {
            return map;
        }
    }
    return {};
}

QVariantList Library::albumsOf(const QString &artist) const
{
    QVariantList list;
    for (const QVariant &album : m_albums) {
        if (album.toMap().value(QStringLiteral("artist")).toString() == artist) {
            list << album;
        }
    }
    return list;
}

QList<Track> Library::albumTracks(const QString &key) const
{
    QList<Track> list;
    for (const Track &track : m_tracks) {
        if (track.albumKey() == key) {
            list << track;
        }
    }
    return list;
}

QList<Track> Library::artistTracks(const QString &artist) const
{
    QList<Track> list;
    for (const Track &track : m_tracks) {
        if (track.albumOwner() == artist) {
            list << track;
        }
    }
    return list;
}

static QStringList pathsOf(const QList<Track> &tracks)
{
    QStringList paths;
    paths.reserve(tracks.size());
    for (const Track &track : tracks) {
        paths << track.path;
    }
    return paths;
}

QStringList Library::albumPaths(const QString &key) const
{
    return pathsOf(albumTracks(key));
}

QStringList Library::artistPaths(const QString &artist) const
{
    return pathsOf(artistTracks(artist));
}

Track Library::track(const QString &path) const
{
    const auto it = m_byPath.constFind(path);
    if (it != m_byPath.constEnd()) {
        return m_tracks.at(it.value());
    }
    const auto outside = m_outside.constFind(path);
    if (outside != m_outside.constEnd()) {
        return outside.value();
    }
    if (!QFileInfo(path).isFile()) {
        return {};
    }
    const Track track = TagReader::read(path);
    m_outside.insert(path, track);
    return track;
}

void Library::loadCache()
{
    const QJsonObject cache = Store::read(m_cachePath);
    if (cache.value(QLatin1String("version")).toInt() != cacheVersion) {
        return;
    }
    QList<Track> tracks;
    const QJsonArray array = cache.value(QLatin1String("tracks")).toArray();
    tracks.reserve(array.size());
    for (const QJsonValue &value : array) {
        const Track track = Track::fromJson(value.toObject());
        if (track.isValid()) {
            tracks << track;
        }
    }
    setTracks(tracks);
}

void Library::saveCache() const
{
    QJsonArray array;
    for (const Track &track : m_tracks) {
        array << track.toJson();
    }
    Store::write(m_cachePath, {{QStringLiteral("version"), cacheVersion}, {QStringLiteral("tracks"), array}});
}
