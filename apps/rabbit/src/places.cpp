#include "places.h"

#include "tags.h"

#include <Solid/Device>
#include <Solid/DeviceNotifier>
#include <Solid/StorageAccess>
#include <Solid/StorageDrive>
#include <Solid/StorageVolume>

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>

static const char *const tagColors[] = {"#ff453a", "#ff9f0a", "#ffd60a", "#32d74b", "#0a84ff", "#bf5af2", "#98989d"};

static QUrl localUrl(const QString &path)
{
    return QUrl::fromLocalFile(QDir::cleanPath(path));
}

bool Places::isSystemMountPoint(const QString &path)
{
    static const QLatin1String trees[] = {
        QLatin1String("/boot"), QLatin1String("/efi"), QLatin1String("/var"), QLatin1String("/usr"), QLatin1String("/etc"),
        QLatin1String("/nix"),  QLatin1String("/srv"), QLatin1String("/opt"), QLatin1String("/tmp"), QLatin1String("/root"),
        QLatin1String("/proc"), QLatin1String("/sys"), QLatin1String("/dev"), QLatin1String("/snap"),
    };
    const QString clean = QDir::cleanPath(path);
    // A separate /home partition is already in the sidebar as the home folder.
    if (clean == QLatin1String("/home")) {
        return true;
    }
    for (const QLatin1String tree : trees) {
        if (clean == tree || clean.startsWith(tree + QLatin1Char('/'))) {
            return true;
        }
    }
    return false;
}

Places::Places(QObject *parent)
    : QAbstractListModel(parent)
{
    auto *notifier = Solid::DeviceNotifier::instance();
    connect(notifier, &Solid::DeviceNotifier::deviceAdded, this, &Places::rebuild);
    connect(notifier, &Solid::DeviceNotifier::deviceRemoved, this, [this](const QString &udi) {
        m_watched.remove(udi);
        rebuild();
    });
    rebuild();
}

int Places::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_places.size();
}

QHash<int, QByteArray> Places::roleNames() const
{
    return {
        {TitleRole, "title"},   {UrlRole, "url"},     {SymbolRole, "symbol"},       {SectionRole, "section"}, {KindRole, "kind"},
        {ColorRole, "color"},   {UdiRole, "udi"},     {EjectableRole, "ejectable"}, {MountedRole, "mounted"}, {RemovableRole, "removable"},
    };
}

QVariant Places::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_places.size()) {
        return {};
    }
    const Place &place = m_places.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return place.title;
    case UrlRole:
        return place.url;
    case SymbolRole:
        return place.symbol;
    case SectionRole:
        return place.section;
    case KindRole:
        return place.kind;
    case ColorRole:
        return place.color;
    case UdiRole:
        return place.udi;
    case EjectableRole:
        return place.ejectable;
    case MountedRole:
        return place.mounted;
    case RemovableRole:
        return place.removable;
    }
    return {};
}

QStringList Places::storedFavorites() const
{
    return QSettings().value(QStringLiteral("sidebar/favorites")).toStringList();
}

void Places::storeFavorites(const QStringList &paths)
{
    QSettings().setValue(QStringLiteral("sidebar/favorites"), paths);
}

QList<Places::Place> Places::favorites() const
{
    const QString section = tr("Favourites");
    QList<Place> places;
    const auto add = [&](const QString &title, const QUrl &url, const char *symbol, bool removable = false) {
        Place place;
        place.title = title;
        place.url = url;
        place.symbol = QString::fromLatin1(symbol);
        place.section = section;
        place.kind = QStringLiteral("place");
        place.removable = removable;
        places << place;
    };
    const auto standard = [&](QStandardPaths::StandardLocation location, const char *symbol) {
        const QString path = QStandardPaths::writableLocation(location);
        if (!path.isEmpty() && path != QDir::homePath() && QFileInfo(path).isDir()) {
            add(QFileInfo(path).fileName(), localUrl(path), symbol);
        }
    };
    add(tr("Recents"), QUrl(QStringLiteral("recents:/")), "clock");
    add(tr("Applications"), QUrl(QStringLiteral("applications:/")), "layout-grid");
    standard(QStandardPaths::DesktopLocation, "monitor");
    standard(QStandardPaths::DocumentsLocation, "file-text");
    standard(QStandardPaths::DownloadLocation, "circle-arrow-down");
    add(QFileInfo(QDir::homePath()).fileName(), localUrl(QDir::homePath()), "house");

    QSet<QUrl> present;
    for (const Place &place : std::as_const(places)) {
        present.insert(place.url);
    }
    for (const QString &path : storedFavorites()) {
        const QUrl url = localUrl(path);
        if (!present.contains(url) && QFileInfo(path).isDir()) {
            add(QFileInfo(path).fileName(), url, "folder", true);
            present.insert(url);
        }
    }
    return places;
}

QList<Places::Place> Places::volumes()
{
    const QString section = tr("Locations");
    QList<Place> places;
    const QList<Solid::Device> devices = Solid::Device::listFromType(Solid::DeviceInterface::StorageAccess);
    for (const Solid::Device &device : devices) {
        const auto *access = device.as<Solid::StorageAccess>();
        const auto *volume = device.as<Solid::StorageVolume>();
        if (!access) {
            continue;
        }
        const QString path = access->filePath();
        if (volume) {
            // Not access->isIgnored(): Solid ignores every volume mounted outside
            // /media, /run/media and the home folder, and every unmounted one,
            // which hides the disks of /etc/fstab and the ones waiting to be mounted.
            if (volume->isIgnored() || volume->usage() != Solid::StorageVolume::FileSystem || volume->fsType() == QLatin1String("squashfs")) {
                continue;
            }
        } else if (access->isIgnored()) {
            continue;
        }
        if (isSystemMountPoint(path)) {
            continue;
        }
        bool external = false;
        for (Solid::Device parent = device.parent(); parent.isValid(); parent = parent.parent()) {
            if (const auto *drive = parent.as<Solid::StorageDrive>()) {
                external = drive->isRemovable() || drive->isHotpluggable();
                break;
            }
        }
        Place place;
        place.title = volume && !volume->label().isEmpty() ? volume->label() : device.displayName();
        if (path == QLatin1String("/")) {
            place.title = QSysInfo::machineHostName();
            place.symbol = QStringLiteral("laptop");
        } else {
            place.symbol = external ? QStringLiteral("usb") : QStringLiteral("hard-drive");
        }
        place.mounted = access->isAccessible();
        place.url = place.mounted ? localUrl(path) : QUrl();
        place.section = section;
        place.kind = QStringLiteral("volume");
        place.udi = device.udi();
        place.ejectable = place.mounted && path != QLatin1String("/") && path != QDir::homePath();
        places << place;

        if (!m_watched.contains(place.udi)) {
            m_watched.insert(place.udi);
            connect(access, &Solid::StorageAccess::accessibilityChanged, this, &Places::rebuild);
        }
    }
    std::stable_sort(places.begin(), places.end(), [](const Place &a, const Place &b) {
        const bool rootA = a.symbol == QLatin1String("laptop");
        const bool rootB = b.symbol == QLatin1String("laptop");
        return rootA != rootB ? rootA : a.title.localeAwareCompare(b.title) < 0;
    });
    if (places.isEmpty() || places.first().symbol != QLatin1String("laptop")) {
        Place root;
        root.title = QSysInfo::machineHostName();
        root.url = localUrl(QStringLiteral("/"));
        root.symbol = QStringLiteral("laptop");
        root.section = section;
        root.kind = QStringLiteral("place");
        places.prepend(root);
    }
    Place network;
    network.title = tr("Network");
    network.url = QUrl(QStringLiteral("remote:/"));
    network.symbol = QStringLiteral("globe");
    network.section = section;
    network.kind = QStringLiteral("place");
    places << network;

    Place trash;
    trash.title = tr("Trash");
    trash.url = QUrl(QStringLiteral("trash:/"));
    trash.symbol = QStringLiteral("trash-2");
    trash.section = section;
    trash.kind = QStringLiteral("place");
    places << trash;
    return places;
}

void Places::rebuild()
{
    beginResetModel();
    m_places = favorites();
    m_places << volumes();
    const QStringList names = Tags::names();
    for (int i = 0; i < names.size(); ++i) {
        Place tag;
        tag.title = Tags::label(names.at(i));
        tag.url = QUrl(QStringLiteral("tags:/") + names.at(i));
        tag.section = tr("Tags");
        tag.kind = QStringLiteral("tag");
        tag.color = QString::fromLatin1(tagColors[i]);
        m_places << tag;
    }
    endResetModel();
}

int Places::rowForUrl(const QUrl &url) const
{
    const QUrl clean = url.adjusted(QUrl::StripTrailingSlash);
    for (int row = 0; row < m_places.size(); ++row) {
        const QUrl &place = m_places.at(row).url;
        if (!place.isEmpty() && (place == url || place.adjusted(QUrl::StripTrailingSlash) == clean)) {
            return row;
        }
    }
    return -1;
}

QUrl Places::canonical(const QUrl &url)
{
    if (!url.isLocalFile()) {
        return url;
    }
    const QString trash = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash");
    const QString path = QDir::cleanPath(url.toLocalFile());
    if (path == trash || path == trash + QStringLiteral("/files")) {
        return QUrl(QStringLiteral("trash:/"));
    }
    return url;
}

QString Places::titleFor(const QUrl &url) const
{
    const int row = rowForUrl(url);
    if (row >= 0) {
        return m_places.at(row).title;
    }
    if (url.scheme() == QLatin1String("trash") && url.path().size() <= 1) {
        return tr("Trash");
    }
    const QString name = url.fileName();
    return name.isEmpty() ? url.toDisplayString(QUrl::PreferLocalFile) : name;
}

QString Places::symbolFor(const QUrl &url) const
{
    const int row = rowForUrl(url);
    return row >= 0 && !m_places.at(row).symbol.isEmpty() ? m_places.at(row).symbol : QStringLiteral("folder");
}

bool Places::isFavorite(const QUrl &url) const
{
    const int row = rowForUrl(url);
    return row >= 0 && m_places.at(row).section == tr("Favourites");
}

void Places::addFavorite(const QUrl &url)
{
    if (!url.isLocalFile() || isFavorite(url) || !QFileInfo(url.toLocalFile()).isDir()) {
        return;
    }
    QStringList stored = storedFavorites();
    stored << url.toLocalFile();
    storeFavorites(stored);
    rebuild();
}

void Places::removeFavorite(const QUrl &url)
{
    QStringList stored = storedFavorites();
    if (stored.removeAll(url.toLocalFile()) > 0) {
        storeFavorites(stored);
        rebuild();
    }
}

void Places::mount(const QString &udi)
{
    Solid::Device device(udi);
    auto *access = device.as<Solid::StorageAccess>();
    if (!access) {
        return;
    }
    if (access->isAccessible()) {
        Q_EMIT mounted(localUrl(access->filePath()));
        return;
    }
    connect(
        access,
        &Solid::StorageAccess::setupDone,
        this,
        [this, udi](Solid::ErrorType error, const QVariant &detail) {
            Solid::Device done(udi);
            const auto *ready = done.as<Solid::StorageAccess>();
            if (error == Solid::NoError && ready && ready->isAccessible()) {
                rebuild();
                Q_EMIT mounted(localUrl(ready->filePath()));
            } else if (error != Solid::UserCanceled) {
                Q_EMIT failed(detail.toString().isEmpty() ? tr("The disk couldn’t be mounted.") : detail.toString());
            }
        },
        Qt::SingleShotConnection);
    access->setup();
}

void Places::eject(const QString &udi)
{
    Solid::Device device(udi);
    auto *access = device.as<Solid::StorageAccess>();
    if (!access || !access->isAccessible()) {
        return;
    }
    const QUrl url = localUrl(access->filePath());
    connect(
        access,
        &Solid::StorageAccess::teardownDone,
        this,
        [this, url](Solid::ErrorType error, const QVariant &detail) {
            if (error == Solid::NoError) {
                rebuild();
                Q_EMIT ejected(url);
            } else if (error != Solid::UserCanceled) {
                Q_EMIT failed(detail.toString().isEmpty() ? tr("The disk couldn’t be ejected because it is in use.") : detail.toString());
            }
        },
        Qt::SingleShotConnection);
    access->teardown();
}
