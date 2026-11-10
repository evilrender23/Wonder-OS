#include "dirmodel.h"

#include "format.h"
#include "folderlook.h"
#include "tags.h"

#include <KApplicationTrader>
#include <KCoreDirLister>
#include <KDesktopFile>
#include <KFileItem>
#include <KIO/Job>
#include <KService>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QMimeDatabase>
#include <QPointer>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QXmlStreamReader>

static const int searchLimit = 2000;
static const int recentsLimit = 100;

static QString folderIcon(const QString &path)
{
    static const QHash<QString, QString> special = [] {
        QHash<QString, QString> map;
        const auto add = [&map](QStandardPaths::StandardLocation location, const char *icon) {
            const QString path = QStandardPaths::writableLocation(location);
            if (!path.isEmpty() && path != QDir::homePath()) {
                map.insert(path, QString::fromLatin1(icon));
            }
        };
        add(QStandardPaths::DesktopLocation, "folder-desktop");
        add(QStandardPaths::DocumentsLocation, "folder-documents");
        add(QStandardPaths::DownloadLocation, "folder-downloads");
        add(QStandardPaths::MusicLocation, "folder-music");
        add(QStandardPaths::PicturesLocation, "folder-pictures");
        add(QStandardPaths::MoviesLocation, "folder-movies");
        add(QStandardPaths::PublicShareLocation, "folder-public");
        add(QStandardPaths::ApplicationsLocation, "folder-applications");
        map.insert(QDir::homePath(), QStringLiteral("folder-home"));
        return map;
    }();
    const QString base = special.value(path, QStringLiteral("folder"));
    return path.isEmpty() ? base : FolderLooks::self()->lookFor(path).iconName(base);
}

static bool wantsThumbnail(const QString &mime)
{
    return mime.startsWith(QLatin1String("image/")) || mime.startsWith(QLatin1String("video/")) || mime == QLatin1String("application/pdf");
}

Entry Entry::fromItem(const KFileItem &item)
{
    Entry entry;
    entry.url = item.url();
    entry.name = item.text();
    bool local = false;
    const QUrl mostLocal = item.mostLocalUrl(&local);
    entry.path = local ? mostLocal.toLocalFile() : QString();
    entry.isDir = item.isDir();
    entry.isLink = item.isLink();
    entry.isHidden = item.isHidden();
    entry.size = entry.isDir ? 0 : item.size();
    entry.modified = item.time(KFileItem::ModificationTime);
    entry.where = entry.url.isLocalFile() ? QFileInfo(entry.url.toLocalFile()).absolutePath() : QString();

    if (entry.isDir) {
        entry.mime = QStringLiteral("inode/directory");
        entry.icon = folderIcon(entry.path);
        entry.kind = entry.isLink ? QCoreApplication::translate("DirModel", "Alias") : QCoreApplication::translate("DirModel", "Folder");
    } else {
        static const QMimeDatabase database;
        QMimeType type = database.mimeTypeForFile(entry.name, QMimeDatabase::MatchExtension);
        if (type.isDefault() && !entry.path.isEmpty()) {
            type = database.mimeTypeForFile(entry.path);
        }
        entry.mime = type.name();
        entry.icon = type.iconName() + QLatin1Char('|') + type.genericIconName();
        entry.kind = entry.isLink ? QCoreApplication::translate("DirModel", "Alias") : Format::capitalized(type.comment());
        entry.thumb = wantsThumbnail(entry.mime);
        if (entry.mime == QLatin1String("application/x-desktop") && !entry.path.isEmpty()) {
            const KDesktopFile desktop(entry.path);
            if (desktop.hasApplicationType() && !desktop.readName().isEmpty()) {
                entry.isApp = true;
                entry.name = desktop.readName();
                entry.icon = desktop.readIcon() + QStringLiteral("|application-x-executable");
                entry.kind = QCoreApplication::translate("DirModel", "Application");
            }
        }
    }
    if (!entry.path.isEmpty()) {
        entry.tags = Tags::self()->tagsFor(entry.path);
    }
    return entry;
}

Entry Entry::fromPath(const QString &path)
{
    return fromItem(KFileItem(QUrl::fromLocalFile(path)));
}

DirModel::DirModel(QObject *parent)
    : QAbstractListModel(parent)
{
    if (m_collator.locale().language() == QLocale::C) {
        // The C locale compares bytes and knows no numbers: "file10" would come before "file2".
        m_collator.setLocale(QLocale(QLocale::English));
    }
    m_collator.setNumericMode(true);
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);

    connect(Tags::self(), &Tags::changed, this, [this](const QString &path) {
        for (int row = 0; row < m_entries.size(); ++row) {
            if (m_entries.at(row).path == path) {
                m_entries[row].tags = Tags::self()->tagsFor(path);
                Q_EMIT dataChanged(index(row), index(row), {TagsRole});
            }
        }
        if (m_url.scheme() == QLatin1String("tags")) {
            reload();
        }
    });
    connect(FolderLooks::self(), &FolderLooks::changed, this, [this](const QString &path) {
        for (int row = 0; row < m_entries.size(); ++row) {
            if (m_entries.at(row).isDir && m_entries.at(row).path == path) {
                m_entries[row].icon = folderIcon(path);
                Q_EMIT dataChanged(index(row), index(row), {IconRole});
            }
        }
    });
}

DirModel::~DirModel()
{
    stopSearch();
}

int DirModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QHash<int, QByteArray> DirModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {UrlRole, "url"},
        {PathRole, "path"},
        {IsDirRole, "isDir"},
        {IsLinkRole, "isLink"},
        {IsHiddenRole, "isHidden"},
        {IsAppRole, "isApp"},
        {IconRole, "icon"},
        {KindRole, "kind"},
        {MimeRole, "mime"},
        {SizeRole, "size"},
        {SizeTextRole, "sizeText"},
        {ModifiedRole, "modified"},
        {ModifiedTextRole, "modifiedText"},
        {SelectedRole, "selected"},
        {TagsRole, "tags"},
        {ThumbRole, "thumb"},
        {WhereRole, "where"},
        {FileNameRole, "fileName"},
    };
}

QVariant DirModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size()) {
        return {};
    }
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return entry.name;
    case UrlRole:
        return entry.url;
    case PathRole:
        return entry.path;
    case IsDirRole:
        return entry.isDir;
    case IsLinkRole:
        return entry.isLink;
    case IsHiddenRole:
        return entry.isHidden;
    case IsAppRole:
        return entry.isApp;
    case IconRole:
        return entry.icon;
    case KindRole:
        return entry.kind;
    case MimeRole:
        return entry.mime;
    case SizeRole:
        return double(entry.size);
    case SizeTextRole:
        return entry.isDir || entry.isApp ? QStringLiteral("--") : Format::size(entry.size);
    case ModifiedRole:
        return entry.modified;
    case ModifiedTextRole:
        return Format::date(entry.modified);
    case SelectedRole:
        return entry.selected;
    case TagsRole:
        return entry.tags;
    case ThumbRole:
        return entry.thumb;
    case WhereRole:
        return entry.where;
    case FileNameRole:
        return entry.url.fileName();
    }
    return {};
}

QVariantMap DirModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= m_entries.size()) {
        return map;
    }
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.begin(); it != roles.end(); ++it) {
        map.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    }
    return map;
}

bool DirModel::isVirtual() const
{
    const QString scheme = m_url.scheme();
    return scheme == QLatin1String("recents") || scheme == QLatin1String("applications") || scheme == QLatin1String("tags");
}

void DirModel::setUrl(const QUrl &url)
{
    QUrl clean = url.adjusted(QUrl::NormalizePathSegments);
    if (clean.isLocalFile() && clean.path().size() > 1) {
        clean = clean.adjusted(QUrl::StripTrailingSlash);
    }
    if (clean == m_url) {
        return;
    }
    m_url = clean;
    Q_EMIT urlChanged();
    reload();
}

void DirModel::setSearch(const QString &search)
{
    const QString trimmed = search.trimmed();
    if (trimmed == m_search) {
        return;
    }
    m_search = trimmed;
    Q_EMIT searchChanged();
    reload();
}

void DirModel::setSortKey(SortKey key)
{
    if (key == m_sortKey) {
        return;
    }
    m_sortKey = key;
    resort();
    Q_EMIT sortChanged();
}

void DirModel::setAscending(bool ascending)
{
    if (ascending == m_ascending) {
        return;
    }
    m_ascending = ascending;
    resort();
    Q_EMIT sortChanged();
}

void DirModel::setFoldersFirst(bool foldersFirst)
{
    if (foldersFirst == m_foldersFirst) {
        return;
    }
    m_foldersFirst = foldersFirst;
    resort();
    Q_EMIT sortChanged();
}

void DirModel::setShowHidden(bool show)
{
    if (show == m_showHidden) {
        return;
    }
    m_showHidden = show;
    Q_EMIT showHiddenChanged();
    if (m_lister && m_search.isEmpty() && !isVirtual()) {
        m_lister->setShowHiddenFiles(show);
        m_lister->emitChanges();
    } else {
        reload();
    }
}

void DirModel::refresh()
{
    reload();
}

void DirModel::setLoading(bool loading)
{
    if (loading != m_loading) {
        m_loading = loading;
        Q_EMIT loadingChanged();
    }
}

void DirModel::setError(const QString &error)
{
    if (error != m_error) {
        m_error = error;
        Q_EMIT errorChanged();
    }
}

void DirModel::clearEntries()
{
    if (m_entries.isEmpty()) {
        return;
    }
    beginResetModel();
    m_entries.clear();
    endResetModel();
    m_currentRow = -1;
    m_anchorRow = -1;
    Q_EMIT currentRowChanged();
    if (m_selectedCount != 0) {
        m_selectedCount = 0;
        Q_EMIT selectionChanged();
    }
    Q_EMIT countChanged();
}

void DirModel::stopSearch()
{
    if (m_searchCancel) {
        m_searchCancel->store(true);
        m_searchCancel.reset();
    }
}

void DirModel::reload()
{
    ++m_generation;
    stopSearch();
    if (m_lister) {
        m_lister->disconnect(this);
        m_lister->stop();
        m_lister->deleteLater();
        m_lister = nullptr;
    }
    clearEntries();
    setError(QString());
    m_nextOrder = 0;

    if (!m_url.isValid() || m_url.isEmpty()) {
        setLoading(false);
        return;
    }
    if (isVirtual()) {
        loadStatic();
        return;
    }
    if (!m_search.isEmpty()) {
        startSearch();
        return;
    }

    m_lister = new KCoreDirLister(this);
    m_lister->setAutoErrorHandlingEnabled(false);
    m_lister->setShowHiddenFiles(m_showHidden);
    connect(m_lister, &KCoreDirLister::started, this, [this] {
        setLoading(true);
    });
    // A change made while a listing runs can be missed (a file created and deleted within the
    // same second stays as a ghost), so every listing is followed by one quiet look again.
    auto *recheck = new QTimer(m_lister);
    recheck->setSingleShot(true);
    recheck->setInterval(1000);
    connect(recheck, &QTimer::timeout, m_lister, [this, recheck] {
        recheck->setProperty("rechecking", true);
        m_lister->updateDirectory(m_url);
    });
    connect(m_lister, &KCoreDirLister::completed, this, [this, recheck] {
        setLoading(false);
        if (recheck->property("rechecking").toBool()) {
            recheck->setProperty("rechecking", false);
        } else if (m_url.isLocalFile()) {
            recheck->start();
        }
    });
    connect(m_lister, &KCoreDirLister::canceled, this, [this] {
        setLoading(false);
    });
    connect(m_lister, &KCoreDirLister::jobError, this, [this](KIO::Job *job) {
        setError(job->errorString());
    });
    connect(m_lister, &KCoreDirLister::clear, this, [this] {
        clearEntries();
    });
    connect(m_lister, &KCoreDirLister::itemsAdded, this, [this](const QUrl &, const KFileItemList &items) {
        QList<Entry> entries;
        entries.reserve(items.size());
        for (const KFileItem &item : items) {
            entries << Entry::fromItem(item);
        }
        addEntries(entries);
    });
    connect(m_lister, &KCoreDirLister::itemsDeleted, this, [this](const KFileItemList &items) {
        for (const KFileItem &item : items) {
            removeUrl(item.url());
        }
    });
    connect(m_lister, &KCoreDirLister::refreshItems, this, [this](const QList<QPair<KFileItem, KFileItem>> &items) {
        for (const auto &[before, after] : items) {
            // The folder being shown is refreshed too (its date moves when something inside is
            // deleted); it is not one of its own items.
            if (after.url().matches(m_url, QUrl::StripTrailingSlash) || after.name() == QLatin1String(".")) {
                continue;
            }
            const int row = rowForUrl(before.url());
            Entry entry = Entry::fromItem(after);
            if (row < 0) {
                addEntries({entry});
                continue;
            }
            const Entry &old = m_entries.at(row);
            entry.selected = old.selected;
            entry.order = old.order;
            if (old.name == entry.name && old.url == entry.url) {
                m_entries[row] = entry;
                Q_EMIT dataChanged(index(row), index(row));
            } else {
                const bool wasCurrent = row == m_currentRow;
                const bool selected = old.selected;
                Tags::self()->pathRenamed(old.path, entry.path);
                Q_EMIT renamed(old.url.fileName(), entry.url.fileName());
                removeUrl(old.url);
                entry.selected = false;
                addEntries({entry});
                const int moved = rowForUrl(entry.url);
                if (selected) {
                    setSelected(moved, true);
                    Q_EMIT selectionChanged();
                }
                if (wasCurrent) {
                    setCurrentRow(moved);
                    m_anchorRow = moved;
                }
            }
        }
    });
    m_lister->openUrl(m_url);
}

static QList<Entry> recentEntries()
{
    struct Recent {
        QString path;
        QDateTime visited;
    };
    QList<Recent> recents;
    QFile file(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/recently-used.xbel"));
    if (file.open(QIODevice::ReadOnly)) {
        QXmlStreamReader xml(&file);
        while (!xml.atEnd()) {
            if (xml.readNext() != QXmlStreamReader::StartElement || xml.name() != QLatin1String("bookmark")) {
                continue;
            }
            const QUrl url(xml.attributes().value(QLatin1String("href")).toString());
            if (!url.isLocalFile()) {
                continue;
            }
            QString stamp = xml.attributes().value(QLatin1String("visited")).toString();
            if (stamp.isEmpty()) {
                stamp = xml.attributes().value(QLatin1String("modified")).toString();
            }
            recents << Recent{url.toLocalFile(), QDateTime::fromString(stamp, Qt::ISODateWithMs)};
        }
    }
    std::stable_sort(recents.begin(), recents.end(), [](const Recent &a, const Recent &b) {
        return a.visited > b.visited;
    });
    QList<Entry> entries;
    QSet<QString> seen;
    for (const Recent &recent : std::as_const(recents)) {
        const QFileInfo info(recent.path);
        if (seen.contains(recent.path) || !info.isFile()) {
            continue;
        }
        seen.insert(recent.path);
        entries << Entry::fromPath(recent.path);
        if (entries.size() >= recentsLimit) {
            break;
        }
    }
    return entries;
}

static QList<Entry> applicationEntries()
{
    QList<Entry> entries;
    QSet<QString> seen;
    const KService::List services = KApplicationTrader::query([](const KService::Ptr &service) {
        return service->isApplication() && !service->noDisplay() && service->showInCurrentDesktop();
    });
    for (const KService::Ptr &service : services) {
        const QString path = service->entryPath();
        if (path.isEmpty() || seen.contains(service->name())) {
            continue;
        }
        seen.insert(service->name());
        Entry entry;
        entry.url = QUrl::fromLocalFile(path);
        entry.path = path;
        entry.name = service->name();
        entry.icon = service->icon() + QStringLiteral("|application-x-executable");
        entry.kind = QCoreApplication::translate("DirModel", "Application");
        entry.mime = QStringLiteral("application/x-desktop");
        entry.isApp = true;
        const QFileInfo info(path);
        entry.modified = info.lastModified();
        entry.size = info.size();
        entry.where = info.absolutePath();
        entries << entry;
    }
    return entries;
}

void DirModel::loadStatic()
{
    QList<Entry> entries;
    const QString scheme = m_url.scheme();
    if (scheme == QLatin1String("recents")) {
        entries = recentEntries();
    } else if (scheme == QLatin1String("applications")) {
        entries = applicationEntries();
    } else {
        const QStringList files = Tags::self()->filesFor(m_url.path().mid(1));
        for (const QString &path : files) {
            entries << Entry::fromPath(path);
        }
    }
    if (!m_search.isEmpty()) {
        const QStringList words = m_search.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        entries.removeIf([&words](const Entry &entry) {
            for (const QString &word : words) {
                if (!entry.name.contains(word, Qt::CaseInsensitive)) {
                    return true;
                }
            }
            return false;
        });
    }
    addEntries(entries);
    setLoading(false);
}

void DirModel::startSearch()
{
    if (!m_url.isLocalFile()) {
        setLoading(false);
        return;
    }
    setLoading(true);
    m_searchCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_searchCancel;
    const QString root = m_url.toLocalFile();
    const QStringList words = m_search.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const bool hidden = m_showHidden;
    const quint64 generation = m_generation;
    const QPointer<DirModel> self(this);

    QThread *thread = QThread::create([=] {
        const auto deliver = [&](const QStringList &paths, bool done) {
            QMetaObject::invokeMethod(
                qApp,
                [=] {
                    if (!self || self->m_generation != generation) {
                        return;
                    }
                    QList<Entry> entries;
                    for (const QString &path : paths) {
                        entries << Entry::fromPath(path);
                    }
                    self->addEntries(entries);
                    if (done) {
                        self->setLoading(false);
                    }
                },
                Qt::QueuedConnection);
        };
        QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
        if (hidden) {
            filters |= QDir::Hidden;
        }
        QStringList batch;
        int found = 0;
        QDirIterator walker(root, filters, QDirIterator::Subdirectories);
        while (walker.hasNext() && !cancel->load() && found < searchLimit) {
            walker.next();
            const QString name = walker.fileName();
            bool match = true;
            for (const QString &word : words) {
                if (!name.contains(word, Qt::CaseInsensitive)) {
                    match = false;
                    break;
                }
            }
            if (!match) {
                continue;
            }
            batch << walker.filePath();
            ++found;
            if (batch.size() >= 50) {
                deliver(batch, false);
                batch.clear();
            }
        }
        if (!cancel->load()) {
            deliver(batch, true);
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

bool DirModel::lessThan(const Entry &a, const Entry &b) const
{
    if (m_sortKey == ByNone) {
        return a.order < b.order;
    }
    if (m_foldersFirst && a.isDir != b.isDir) {
        return a.isDir;
    }
    int order = 0;
    switch (m_sortKey) {
    case ByKind:
        order = m_collator.compare(a.kind, b.kind);
        break;
    case ByModified:
        order = a.modified < b.modified ? -1 : (b.modified < a.modified ? 1 : 0);
        break;
    case BySize:
        // Folders have no size of their own: they sort after every file.
        if (a.isDir != b.isDir) {
            return b.isDir;
        }
        order = a.size < b.size ? -1 : (b.size < a.size ? 1 : 0);
        break;
    default:
        break;
    }
    if (order != 0) {
        return m_ascending ? order < 0 : order > 0;
    }
    order = m_collator.compare(a.name, b.name);
    if (order != 0) {
        return m_sortKey == ByName && !m_ascending ? order > 0 : order < 0;
    }
    return a.url < b.url;
}

void DirModel::applyPendingSelection(int row)
{
    if (m_pendingSelection.isEmpty() || !m_pendingSelection.remove(m_entries.at(row).url)) {
        return;
    }
    if (m_pendingFirst) {
        m_pendingFirst = false;
        clearSelection();
        m_anchorRow = row;
        setCurrentRow(row);
    }
    setSelected(row, true);
    Q_EMIT selectionChanged();
    Q_EMIT revealed(row);
}

void DirModel::addEntries(QList<Entry> entries)
{
    if (entries.isEmpty()) {
        return;
    }
    for (Entry &entry : entries) {
        entry.order = m_nextOrder++;
    }
    const QUrl currentUrl = m_currentRow >= 0 ? m_entries.at(m_currentRow).url : QUrl();
    const QUrl anchorUrl = m_anchorRow >= 0 ? m_entries.at(m_anchorRow).url : QUrl();

    if (entries.size() == 1) {
        const Entry &entry = entries.first();
        const auto position = std::upper_bound(m_entries.begin(), m_entries.end(), entry, [this](const Entry &a, const Entry &b) {
            return lessThan(a, b);
        });
        const int row = position - m_entries.begin();
        beginInsertRows({}, row, row);
        m_entries.insert(row, entry);
        endInsertRows();
    } else {
        std::sort(entries.begin(), entries.end(), [this](const Entry &a, const Entry &b) {
            return lessThan(a, b);
        });
        if (m_entries.isEmpty()) {
            beginInsertRows({}, 0, entries.size() - 1);
            m_entries = entries;
            endInsertRows();
        } else {
            beginResetModel();
            QList<Entry> merged;
            merged.reserve(m_entries.size() + entries.size());
            std::merge(m_entries.begin(), m_entries.end(), entries.begin(), entries.end(), std::back_inserter(merged), [this](const Entry &a, const Entry &b) {
                return lessThan(a, b);
            });
            m_entries = merged;
            endResetModel();
        }
    }
    const int current = currentUrl.isEmpty() ? -1 : rowForUrl(currentUrl);
    m_anchorRow = anchorUrl.isEmpty() ? -1 : rowForUrl(anchorUrl);
    if (current != m_currentRow) {
        m_currentRow = current;
        Q_EMIT currentRowChanged();
    }
    Q_EMIT countChanged();
    if (!m_pendingSelection.isEmpty()) {
        for (int row = 0; row < m_entries.size() && !m_pendingSelection.isEmpty(); ++row) {
            applyPendingSelection(row);
        }
    }
}

void DirModel::removeUrl(const QUrl &url)
{
    const int row = rowForUrl(url);
    if (row < 0) {
        return;
    }
    const bool selected = m_entries.at(row).selected;
    beginRemoveRows({}, row, row);
    m_entries.removeAt(row);
    endRemoveRows();
    if (selected) {
        --m_selectedCount;
        Q_EMIT selectionChanged();
    }
    const auto shift = [row](int value) {
        return value == row ? -1 : (value > row ? value - 1 : value);
    };
    m_anchorRow = shift(m_anchorRow);
    const int current = shift(m_currentRow);
    if (current != m_currentRow) {
        m_currentRow = current;
        Q_EMIT currentRowChanged();
    }
    Q_EMIT countChanged();
}

void DirModel::resort()
{
    if (m_entries.size() < 2) {
        return;
    }
    const QUrl currentUrl = m_currentRow >= 0 ? m_entries.at(m_currentRow).url : QUrl();
    const QUrl anchorUrl = m_anchorRow >= 0 ? m_entries.at(m_anchorRow).url : QUrl();
    Q_EMIT layoutAboutToBeChanged();
    std::sort(m_entries.begin(), m_entries.end(), [this](const Entry &a, const Entry &b) {
        return lessThan(a, b);
    });
    Q_EMIT layoutChanged();
    m_anchorRow = anchorUrl.isEmpty() ? -1 : rowForUrl(anchorUrl);
    const int current = currentUrl.isEmpty() ? -1 : rowForUrl(currentUrl);
    if (current != m_currentRow) {
        m_currentRow = current;
        Q_EMIT currentRowChanged();
    }
}

int DirModel::rowForUrl(const QUrl &url) const
{
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).url == url) {
            return row;
        }
    }
    return -1;
}

QStringList DirModel::fileNames() const
{
    QStringList names;
    names.reserve(m_entries.size());
    for (const Entry &entry : m_entries) {
        names << entry.url.fileName();
    }
    return names;
}

int DirModel::rowForFileName(const QString &name) const
{
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).url.fileName() == name) {
            return row;
        }
    }
    return -1;
}

int DirModel::typeAhead(const QString &prefix) const
{
    if (prefix.isEmpty()) {
        return -1;
    }
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).name.startsWith(prefix, Qt::CaseInsensitive)) {
            return row;
        }
    }
    return -1;
}

void DirModel::setCurrentRow(int row)
{
    if (row < -1 || row >= m_entries.size()) {
        row = -1;
    }
    if (row != m_currentRow) {
        m_currentRow = row;
        Q_EMIT currentRowChanged();
    }
}

void DirModel::setSelected(int row, bool selected)
{
    if (row < 0 || row >= m_entries.size() || m_entries.at(row).selected == selected) {
        return;
    }
    m_entries[row].selected = selected;
    m_selectedCount += selected ? 1 : -1;
    Q_EMIT dataChanged(index(row), index(row), {SelectedRole});
}

void DirModel::click(int row, int modifiers)
{
    if (row < 0 || row >= m_entries.size()) {
        return;
    }
    if (modifiers & Qt::ShiftModifier) {
        extendTo(row);
    } else if (modifiers & (Qt::ControlModifier | Qt::MetaModifier)) {
        setSelected(row, !m_entries.at(row).selected);
        m_anchorRow = row;
        setCurrentRow(row);
        Q_EMIT selectionChanged();
    } else {
        selectOnly(row);
    }
}

void DirModel::selectOnly(int row)
{
    if (row < 0 || row >= m_entries.size()) {
        clearSelection();
        return;
    }
    for (int other = 0; other < m_entries.size(); ++other) {
        setSelected(other, other == row);
    }
    m_anchorRow = row;
    setCurrentRow(row);
    Q_EMIT selectionChanged();
}

void DirModel::extendTo(int row)
{
    if (row < 0 || row >= m_entries.size()) {
        return;
    }
    if (m_anchorRow < 0) {
        m_anchorRow = row;
    }
    const int first = qMin(m_anchorRow, row);
    const int last = qMax(m_anchorRow, row);
    for (int other = 0; other < m_entries.size(); ++other) {
        setSelected(other, other >= first && other <= last);
    }
    setCurrentRow(row);
    Q_EMIT selectionChanged();
}

void DirModel::selectRows(const QList<int> &rows, const QList<int> &keep)
{
    QSet<int> wanted(rows.begin(), rows.end());
    for (int row : keep) {
        wanted.insert(row);
    }
    for (int row = 0; row < m_entries.size(); ++row) {
        setSelected(row, wanted.contains(row));
    }
    if (!rows.isEmpty()) {
        m_anchorRow = rows.first();
        setCurrentRow(rows.last());
    }
    Q_EMIT selectionChanged();
}

void DirModel::selectAll()
{
    for (int row = 0; row < m_entries.size(); ++row) {
        setSelected(row, true);
    }
    Q_EMIT selectionChanged();
}

void DirModel::clearSelection()
{
    if (m_selectedCount == 0) {
        return;
    }
    for (int row = 0; row < m_entries.size(); ++row) {
        setSelected(row, false);
    }
    Q_EMIT selectionChanged();
}

bool DirModel::isSelected(int row) const
{
    return row >= 0 && row < m_entries.size() && m_entries.at(row).selected;
}

QList<int> DirModel::selectedRows() const
{
    QList<int> rows;
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).selected) {
            rows << row;
        }
    }
    return rows;
}

QList<QUrl> DirModel::selectedUrls() const
{
    QList<QUrl> urls;
    for (const Entry &entry : m_entries) {
        if (entry.selected) {
            urls << entry.url;
        }
    }
    return urls;
}

quint64 DirModel::selectedSize() const
{
    quint64 total = 0;
    for (const Entry &entry : m_entries) {
        if (entry.selected) {
            total += entry.size;
        }
    }
    return total;
}

void DirModel::selectUrls(const QList<QUrl> &urls)
{
    m_pendingSelection = QSet<QUrl>(urls.begin(), urls.end());
    m_pendingFirst = true;
    for (int row = 0; row < m_entries.size() && !m_pendingSelection.isEmpty(); ++row) {
        applyPendingSelection(row);
    }
}
