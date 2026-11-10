#include "library.h"

#include "metadata.h"
#include "store.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QStandardPaths>
#include <QtConcurrent>

/// The cache is thrown away when what is read of a file changes.
static const int kCacheVersion = 1;
/// inotify watches are a shared, limited thing: a library of more folders
/// than this is refreshed when the window opens and when asked.
static const int kWatchLimit = 3000;

static bool newerFirst(const MediaItem &a, const MediaItem &b)
{
    return a.taken != b.taken ? a.taken > b.taken : a.path < b.path;
}

QStringList Library::defaultFolders()
{
    QStringList out;
    for (const auto location : {QStandardPaths::PicturesLocation, QStandardPaths::MoviesLocation}) {
        const QString dir = QStandardPaths::writableLocation(location);
        // Without a folder of its own, Qt answers the home folder: never the whole of it.
        if (!dir.isEmpty() && QDir(dir) != QDir::home() && QFileInfo(dir).isDir() && !out.contains(dir)) {
            out << dir;
        }
    }
    return out;
}

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
        m_folders = defaultFolders();
    }

    // What the last run knew shows at once; the disk is read behind it.
    const QJsonObject cache = Store::read(m_cachePath);
    if (cache.value(QLatin1String("version")).toInt() == kCacheVersion) {
        QList<MediaItem> items;
        const QJsonArray array = cache.value(QLatin1String("items")).toArray();
        items.reserve(array.size());
        for (const QJsonValue &value : array) {
            MediaItem item = MediaItem::fromJson(value.toObject());
            if (!item.path.isEmpty()) {
                items << item;
            }
        }
        setItems(items);
    }

    connect(&m_job, &QFutureWatcher<Scan>::finished, this, [this] {
        adopt(m_job.result());
        Q_EMIT scanningChanged();
        if (m_again) {
            m_again = false;
            rescan();
        }
    });
    m_settle.setSingleShot(true);
    // A camera being emptied changes the folder hundreds of times: read it once, after.
    m_settle.setInterval(900);
    connect(&m_settle, &QTimer::timeout, this, &Library::rescan);
    if (watch) {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
    }
}

Library::~Library()
{
    m_again = false;
    m_job.disconnect(this);
    m_job.waitForFinished();
}

void Library::waitForScan()
{
    while (m_job.isRunning() || m_again) {
        m_job.waitForFinished();
        QCoreApplication::processEvents();
    }
}

Library::Scan Library::scan(const QStringList &folders, const QHash<QString, MediaItem> &known)
{
    Scan out;
    QSet<QString> seenDirs;
    QSet<QString> seenFiles;
    QStringList pending;
    for (const QString &folder : folders) {
        pending << QDir(folder).absolutePath();
    }
    while (!pending.isEmpty()) {
        const QString dir = pending.takeLast();
        // By its real place: a link back up the tree is walked once.
        const QString real = QFileInfo(dir).canonicalFilePath();
        if (real.isEmpty() || seenDirs.contains(real)) {
            continue;
        }
        seenDirs.insert(real);
        // A folder that asks not to be indexed is left alone, with all under it.
        if (QFileInfo::exists(dir + QStringLiteral("/.nomedia"))) {
            continue;
        }
        out.dirs << dir;
        QDirIterator it(dir, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        while (it.hasNext()) {
            it.next();
            const QFileInfo info = it.fileInfo();
            if (info.isDir()) {
                pending << info.absoluteFilePath();
                continue;
            }
            const QString path = info.absoluteFilePath();
            if (!Media::isMedia(path) || info.size() == 0 || seenFiles.contains(path)) {
                continue;
            }
            seenFiles.insert(path);
            const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
            const auto old = known.constFind(path);
            if (old != known.constEnd() && old->mtime == mtime && old->size == info.size()) {
                out.items << *old;
                continue;
            }
            MediaItem item;
            item.path = path;
            item.mtime = mtime;
            item.size = info.size();
            Metadata::fill(item);
            out.items << item;
        }
    }
    std::sort(out.items.begin(), out.items.end(), newerFirst);
    return out;
}

void Library::rescan()
{
    if (m_job.isRunning()) {
        // Something changed while reading: read once more when done.
        m_again = true;
        return;
    }
    QHash<QString, MediaItem> known;
    known.reserve(m_items.size());
    for (const MediaItem &item : std::as_const(m_items)) {
        known.insert(item.path, item);
    }
    m_job.setFuture(QtConcurrent::run(&Library::scan, m_folders, known));
    Q_EMIT scanningChanged();
}

void Library::setItems(QList<MediaItem> items)
{
    m_items = std::move(items);
    m_index.clear();
    m_index.reserve(m_items.size());
    m_videos = 0;
    m_screenshots = 0;
    for (int i = 0; i < m_items.size(); ++i) {
        m_index.insert(m_items.at(i).path, i);
        m_videos += m_items.at(i).video ? 1 : 0;
        m_screenshots += m_items.at(i).screenshot ? 1 : 0;
    }
    ++m_revision;
}

void Library::adopt(const Scan &scan)
{
    if (m_watcher) {
        const QStringList watched = m_watcher->directories();
        const QSet<QString> now(watched.begin(), watched.end());
        const QStringList wantedList = scan.dirs.mid(0, kWatchLimit);
        const QSet<QString> wanted(wantedList.begin(), wantedList.end());
        const QStringList gone = (now - wanted).values();
        const QStringList fresh = (wanted - now).values();
        if (!gone.isEmpty()) {
            m_watcher->removePaths(gone);
        }
        if (!fresh.isEmpty()) {
            m_watcher->addPaths(fresh);
        }
    }
    bool same = scan.items.size() == m_items.size();
    QStringList rewritten;
    for (int i = 0; same && i < m_items.size(); ++i) {
        const MediaItem &a = m_items.at(i);
        const MediaItem &b = scan.items.at(i);
        same = a.path == b.path && a.mtime == b.mtime && a.size == b.size && a.taken == b.taken;
    }
    if (same) {
        return;
    }
    for (const MediaItem &item : scan.items) {
        const auto old = m_index.constFind(item.path);
        if (old != m_index.constEnd() && (m_items.at(*old).mtime != item.mtime || m_items.at(*old).size != item.size)) {
            rewritten << item.path;
        }
    }
    setItems(scan.items);
    save();
    Q_EMIT changed();
    for (const QString &path : std::as_const(rewritten)) {
        Q_EMIT itemChanged(path);
    }
}

void Library::save() const
{
    QJsonArray items;
    for (const MediaItem &item : m_items) {
        items.append(item.toJson());
    }
    Store::write(m_cachePath, {{QLatin1String("version"), kCacheVersion}, {QLatin1String("items"), items}});
}

MediaItem Library::find(const QString &path) const
{
    const auto it = m_index.constFind(path);
    return it == m_index.constEnd() ? MediaItem() : m_items.at(*it);
}

QVariantMap Library::item(const QString &path) const
{
    const MediaItem found = find(path);
    return found.path.isEmpty() ? QVariantMap() : found.toMap();
}

void Library::addFolder(const QUrl &folder)
{
    const QString path = QDir(folder.isLocalFile() ? folder.toLocalFile() : folder.toString()).absolutePath();
    if (!QFileInfo(path).isDir()) {
        return;
    }
    for (const QString &have : std::as_const(m_folders)) {
        // Already in, itself or inside one that is.
        if (path == have || path.startsWith(have + QLatin1Char('/'))) {
            return;
        }
    }
    // The folders it holds are now said by it.
    QStringList folders;
    for (const QString &have : std::as_const(m_folders)) {
        if (!have.startsWith(path + QLatin1Char('/'))) {
            folders << have;
        }
    }
    folders << path;
    m_folders = folders;
    m_store->set(QStringLiteral("folders"), QJsonArray::fromStringList(m_folders));
    Q_EMIT foldersChanged();
    rescan();
}

void Library::removeFolder(const QString &folder)
{
    if (m_folders.removeAll(folder) == 0) {
        return;
    }
    m_store->set(QStringLiteral("folders"), QJsonArray::fromStringList(m_folders));
    Q_EMIT foldersChanged();
    // Its pictures leave the window now; the files stay where they are.
    QList<MediaItem> kept;
    for (const MediaItem &item : std::as_const(m_items)) {
        bool inside = false;
        for (const QString &have : std::as_const(m_folders)) {
            inside = inside || item.path.startsWith(have + QLatin1Char('/'));
        }
        if (inside) {
            kept << item;
        }
    }
    setItems(kept);
    save();
    Q_EMIT changed();
    rescan();
}

void Library::forget(const QStringList &paths)
{
    const QSet<QString> gone(paths.begin(), paths.end());
    QList<MediaItem> kept;
    kept.reserve(m_items.size());
    for (const MediaItem &item : std::as_const(m_items)) {
        if (!gone.contains(item.path)) {
            kept << item;
        }
    }
    if (kept.size() == m_items.size()) {
        return;
    }
    setItems(kept);
    save();
    Q_EMIT changed();
}

void Library::refresh(const QString &path)
{
    const auto it = m_index.constFind(path);
    if (it == m_index.constEnd()) {
        Q_EMIT itemChanged(path);
        return;
    }
    const QFileInfo info(path);
    MediaItem item;
    item.path = path;
    item.mtime = info.lastModified().toMSecsSinceEpoch();
    item.size = info.size();
    Metadata::fill(item);
    // Turning a picture does not move it in time, whatever its file's date now says.
    item.taken = m_items.at(*it).taken;
    m_items[*it] = item;
    ++m_revision;
    save();
    Q_EMIT changed();
    Q_EMIT itemChanged(path);
}

QString Library::placeName(const QString &folder) const
{
    QString best;
    for (const QString &root : m_folders) {
        if ((folder == root || folder.startsWith(root + QLatin1Char('/'))) && root.size() > best.size()) {
            best = root;
        }
    }
    if (best.isEmpty()) {
        return QFileInfo(folder).fileName();
    }
    // The root by its own name, what is under it by its way down.
    return QFileInfo(best).fileName() + folder.mid(best.size());
}

QVariantList Library::places() const
{
    struct Place {
        int count = 0;
        QString cover;
        qint64 stamp = 0;
    };
    // Sorted by path, so a folder is followed by what it holds.
    QMap<QString, Place> found;
    for (const MediaItem &item : m_items) {
        Place &place = found[item.folder()];
        if (place.count++ == 0) {
            // The newest, since they come newest first.
            place.cover = item.path;
            place.stamp = item.mtime;
        }
    }
    QVariantList out;
    for (auto it = found.constBegin(); it != found.constEnd(); ++it) {
        out.append(QVariantMap{
            {QStringLiteral("path"), it.key()},
            {QStringLiteral("name"), placeName(it.key())},
            {QStringLiteral("count"), it->count},
            {QStringLiteral("cover"), it->cover},
            {QStringLiteral("coverStamp"), it->stamp},
        });
    }
    return out;
}
