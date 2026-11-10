#include "app.h"

#include "dirmodel.h"
#include "dragnotice.h"
#include "fileops.h"
#include "folderlook.h"
#include "format.h"
#include "look.h"
#include "places.h"
#include "tags.h"

#include <KApplicationTrader>
#include <KIO/ApplicationLauncherJob>
#include <KIO/DirectorySizeJob>
#include <KIO/Global>
#include <KIO/OpenUrlJob>
#include <KRecentDocument>
#include <KService>

#include <QMimeDatabase>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStyleHints>
#include <QSysInfo>

static const char *const tagColors[] = {"#ff453a", "#ff9f0a", "#ffd60a", "#32d74b", "#0a84ff", "#bf5af2", "#98989d"};

static App *s_app = nullptr;

static QString trashFilesPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash/files");
}

App::App(QObject *parent)
    : QObject(parent)
    , m_ops(new FileOps(this))
    , m_places(new Places(this))
    , m_watcher(new QFileSystemWatcher(this))
    , m_dragNotice(new DragNotice(this))
    , m_lookNotice(new DragNotice(this))
{
    m_lookNotice->setCommand(DragNotice::shellCommand(QStringLiteral("looking")));
    s_app = this;
    connect(m_ops, &FileOps::failed, this, &App::failed);
    connect(m_places, &Places::failed, this, &App::failed);
    connect(m_ops, &FileOps::trashChanged, this, &App::refreshTrash);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        refreshTrash();
        refreshDark();
    });
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &App::refreshDark);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &App::refreshDark);

    connect(FolderLooks::self(), &FolderLooks::tintChanged, this, &App::folderTintChanged);
    FolderLooks::self()->watchTint();

    QDir().mkpath(trashFilesPath());
    m_watcher->addPath(trashFilesPath());
    // The bar rewrites its state file by rename, which drops a file watch:
    // watching the directory catches the replacement.
    const QString barDir = QFileInfo(barStatePath()).absolutePath();
    if (QFileInfo(barDir).isDir()) {
        m_watcher->addPath(barDir);
    }
    refreshTrash();
    refreshDark();
}

App *App::create(QQmlEngine *, QJSEngine *)
{
    QJSEngine::setObjectOwnership(s_app, QJSEngine::CppOwnership);
    return s_app;
}

void App::setTest(const QString &shotPath, const QStringList &actions, const QSize &size)
{
    m_shotPath = shotPath;
    m_testActions = actions;
    m_testSize = size;
}

void App::saveShot(QQuickWindow *window)
{
    const bool saved = window && window->grabWindow().save(m_shotPath);
    QCoreApplication::exit(saved ? 0 : 1);
}

QUrl App::homeUrl() const
{
    return QUrl::fromLocalFile(QDir::homePath());
}

QStringList App::tagNames() const
{
    return Tags::names();
}

QString App::barStatePath() const
{
    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty()) {
        state = QDir::homePath() + QStringLiteral("/.local/state");
    }
    return state + QStringLiteral("/wonderlandwm/bar.json");
}

void App::setAppearance(Appearance appearance)
{
    m_appearance = appearance;
    refreshDark();
}

void App::refreshDark()
{
    bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    bool glass = desktop.contains(QLatin1String("driftwm")) || desktop.contains(QLatin1String("wonderland"));
    // WonderlandWM's bar owns the light/dark and glass switches for the whole desktop.
    QFile file(barStatePath());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject state = QJsonDocument::fromJson(file.readAll()).object();
        const QString mode = state.value(QLatin1String("themeMode")).toString();
        if (!mode.isEmpty()) {
            dark = mode == QLatin1String("dark");
        }
        glass = glass && state.value(QLatin1String("glass")).toBool(true);
    }
    if (m_appearance != Appearance::Auto) {
        dark = m_appearance == Appearance::Dark;
    }
    if (qEnvironmentVariableIsSet("WONDERLAND_RABBIT_GLASS")) {
        glass = qEnvironmentVariableIntValue("WONDERLAND_RABBIT_GLASS") != 0;
    }
    if (dark != m_dark || glass != m_glass) {
        m_dark = dark;
        m_glass = glass;
        Q_EMIT darkChanged();
    }
}

void App::refreshTrash()
{
    const bool empty = QDir(trashFilesPath()).isEmpty();
    if (empty != m_trashEmpty) {
        m_trashEmpty = empty;
        Q_EMIT trashEmptyChanged();
    }
}

void App::open(const QUrl &url, const QString &mime, bool isApp)
{
    KJob *job = nullptr;
    if (isApp && url.isLocalFile()) {
        const KService::Ptr service(new KService(url.toLocalFile()));
        job = new KIO::ApplicationLauncherJob(service);
    } else {
        auto *open = mime.isEmpty() ? new KIO::OpenUrlJob(url) : new KIO::OpenUrlJob(url, mime);
        // Without this KIO refuses every program with “launching executables is not allowed”.
        open->setRunExecutables(runsOnOpen(mime.isEmpty() ? QMimeDatabase().mimeTypeForUrl(url).name() : mime));
        job = open;
        if (url.isLocalFile()) {
            KRecentDocument::add(url);
        }
    }
    connect(job, &KJob::result, this, [this, url](KJob *finished) {
        if (finished->error()) {
            const QString reason = finished->errorString();
            Q_EMIT failed(reason.isEmpty() ? tr("“%1” can’t be opened.").arg(url.fileName()) : reason);
        }
    });
    job->start();
}

bool App::runsOnOpen(const QString &mime)
{
    // A script still opens in its editor: running one unasked is how a download bites.
    const QMimeType type = QMimeDatabase().mimeTypeForName(mime);
    return !type.inherits(QStringLiteral("text/plain"));
}

QVariantList App::appsFor(const QString &mime) const
{
    QVariantList apps;
    const KService::Ptr preferred = KApplicationTrader::preferredService(mime);
    const KService::List services = KApplicationTrader::queryByMimeType(mime);
    for (const KService::Ptr &service : services) {
        apps << QVariantMap{
            {QStringLiteral("name"), service->name()},
            {QStringLiteral("icon"), service->icon()},
            {QStringLiteral("id"), service->storageId()},
            {QStringLiteral("isDefault"), preferred && preferred->storageId() == service->storageId()},
        };
        if (apps.size() >= 12) {
            break;
        }
    }
    return apps;
}

bool App::setDefaultApp(const QString &mime, const QString &storageId)
{
    const KService::Ptr service = KService::serviceByStorageId(storageId);
    if (mime.isEmpty() || !service) {
        return false;
    }
    const KService::Ptr preferred = KApplicationTrader::preferredService(mime);
    if (preferred && preferred->storageId() == service->storageId()) {
        return true;
    }
    // Writes [Default Applications] in mimeapps.list, which every desktop reads.
    KApplicationTrader::setPreferredService(mime, service);
    return true;
}

void App::openWith(const QList<QUrl> &urls, const QString &storageId, const QString &mime)
{
    const KService::Ptr service = KService::serviceByStorageId(storageId);
    if (!service) {
        return;
    }
    // The app picked here opens this kind of file from now on.
    setDefaultApp(mime, storageId);
    auto *job = new KIO::ApplicationLauncherJob(service);
    job->setUrls(urls);
    connect(job, &KJob::result, this, [this](KJob *finished) {
        if (finished->error()) {
            Q_EMIT failed(finished->errorString());
        }
    });
    job->start();
}

void App::openTerminal(const QUrl &directory)
{
    const QString path = directory.isLocalFile() ? directory.toLocalFile() : QDir::homePath();
    QStringList candidates = {qEnvironmentVariable("TERMINAL")};
    candidates << QStringLiteral("foot") << QStringLiteral("kitty") << QStringLiteral("alacritty") << QStringLiteral("konsole")
               << QStringLiteral("gnome-terminal") << QStringLiteral("xterm");
    for (const QString &candidate : std::as_const(candidates)) {
        if (candidate.isEmpty()) {
            continue;
        }
        const QString program = QStandardPaths::findExecutable(candidate);
        if (!program.isEmpty() && QProcess::startDetached(program, {}, path)) {
            return;
        }
    }
    Q_EMIT failed(tr("No terminal application was found."));
}

void App::announceDrag(bool dragging)
{
    if (!testing()) {
        m_dragNotice->set(dragging);
    }
}

void App::requestWindow(const QUrl &url, const QList<QUrl> &select)
{
    Q_EMIT windowRequested(Places::canonical(url), select);
}

void App::pick(Picker *picker)
{
    Q_EMIT pickRequested(picker);
}

void App::pickClosed(Picker *picker)
{
    if (picker) {
        picker->cancel();
        picker->deleteLater();
    }
}

QUrl App::childUrl(const QUrl &folder, const QString &name) const
{
    if (!folder.isLocalFile() || name.isEmpty() || name.contains(QLatin1Char('/')) || name == QLatin1String(".") || name == QLatin1String("..")) {
        return {};
    }
    return QUrl::fromLocalFile(QDir(folder.toLocalFile()).filePath(name));
}

bool App::exists(const QUrl &url) const
{
    return url.isLocalFile() && QFileInfo::exists(url.toLocalFile());
}

void App::quickLook(const QList<QUrl> &urls, QObject *source, bool update)
{
    if (!urls.isEmpty()) {
        Q_EMIT lookRequested(urls, source, update);
    }
}

QString App::lookKind(const QString &mime, bool isDir) const
{
    return Look::kindFor(mime, isDir);
}

void App::announceLook(bool looking)
{
    if (!testing()) {
        m_lookNotice->set(looking);
    }
}

QUrl App::parentUrl(const QUrl &url) const
{
    if (url.scheme() == QLatin1String("recents") || url.scheme() == QLatin1String("applications") || url.scheme() == QLatin1String("tags")) {
        return QUrl();
    }
    const QUrl up = KIO::upUrl(url).adjusted(QUrl::StripTrailingSlash);
    if (up.path().isEmpty() && url.isLocalFile()) {
        return QUrl::fromLocalFile(QStringLiteral("/"));
    }
    return up.adjusted(QUrl::StripTrailingSlash) == url.adjusted(QUrl::StripTrailingSlash) || url.path().size() <= 1 ? QUrl() : up;
}

bool App::isInside(const QUrl &url, const QUrl &ancestor) const
{
    if (url.scheme() != ancestor.scheme() || !url.isLocalFile()) {
        return false;
    }
    const QString base = QDir::cleanPath(ancestor.toLocalFile());
    const QString path = QDir::cleanPath(url.toLocalFile());
    return path == base || path.startsWith(base == QLatin1String("/") ? base : base + QLatin1Char('/'));
}

QVariantList App::crumbs(const QUrl &url) const
{
    QVariantList list;
    const auto crumb = [](const QString &title, const QUrl &target, const QString &icon) {
        return QVariantMap{{QStringLiteral("title"), title}, {QStringLiteral("url"), target}, {QStringLiteral("icon"), icon}};
    };
    if (!url.isLocalFile()) {
        list << crumb(m_places->titleFor(url), url, QStringLiteral("folder"));
        return list;
    }
    QStringList parts = QDir::cleanPath(url.toLocalFile()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    list << crumb(QSysInfo::machineHostName(), QUrl::fromLocalFile(QStringLiteral("/")), QStringLiteral("drive-harddisk"));
    QString path;
    for (const QString &part : std::as_const(parts)) {
        path += QLatin1Char('/') + part;
        const Entry entry = Entry::fromPath(path);
        list << crumb(part, QUrl::fromLocalFile(path), entry.icon);
    }
    return list;
}

QString App::displayPath(const QUrl &url) const
{
    return url.isLocalFile() ? url.toLocalFile() : url.toDisplayString();
}

QUrl App::folderFromInput(const QString &text, const QUrl &base) const
{
    QString path = text.trimmed();
    if (path.isEmpty()) {
        return QUrl();
    }
    if (path == QLatin1String("~") || path.startsWith(QLatin1String("~/"))) {
        path = QDir::homePath() + path.mid(1);
    } else if (path.contains(QLatin1String("://")) || path.startsWith(QLatin1String("trash:")) || path.startsWith(QLatin1String("remote:"))) {
        return QUrl(path);
    } else if (!path.startsWith(QLatin1Char('/'))) {
        path = (base.isLocalFile() ? base.toLocalFile() : QDir::homePath()) + QLatin1Char('/') + path;
    }
    const QFileInfo info(QDir::cleanPath(path));
    return info.isDir() ? QUrl::fromLocalFile(info.absoluteFilePath()) : QUrl();
}

QString App::freeSpace(const QUrl &url) const
{
    if (!url.isLocalFile()) {
        return QString();
    }
    const QStorageInfo storage(url.toLocalFile());
    return storage.isValid() ? tr("%1 available").arg(Format::size(storage.bytesAvailable())) : QString();
}

QString App::formatSize(double bytes) const
{
    return Format::size(quint64(qMax(0.0, bytes)));
}

QVariantMap App::info(const QUrl &url) const
{
    QVariantMap map;
    if (!url.isLocalFile()) {
        map.insert(QStringLiteral("name"), m_places->titleFor(url));
        map.insert(QStringLiteral("icon"), QStringLiteral("folder"));
        map.insert(QStringLiteral("kind"), tr("Folder"));
        map.insert(QStringLiteral("isDir"), true);
        return map;
    }
    const QString path = url.toLocalFile();
    const QFileInfo file(path);
    const Entry entry = Entry::fromPath(path);
    map.insert(QStringLiteral("name"), entry.name);
    map.insert(QStringLiteral("url"), url);
    map.insert(QStringLiteral("path"), path);
    map.insert(QStringLiteral("icon"), entry.icon);
    map.insert(QStringLiteral("thumb"), entry.thumb);
    map.insert(QStringLiteral("kind"), entry.kind);
    map.insert(QStringLiteral("mime"), entry.mime);
    map.insert(QStringLiteral("isDir"), entry.isDir);
    map.insert(QStringLiteral("isApp"), entry.isApp);
    map.insert(QStringLiteral("tags"), entry.tags);
    map.insert(QStringLiteral("where"), file.absolutePath());
    map.insert(QStringLiteral("size"), entry.isDir ? QStringLiteral("--") : entry.size < 1000 ? Format::size(entry.size) : tr("%1 (%2 bytes)").arg(Format::size(entry.size), QLocale().toString(qulonglong(entry.size))));
    map.insert(QStringLiteral("sizeShort"), entry.isDir ? QStringLiteral("--") : Format::size(entry.size));
    map.insert(QStringLiteral("created"), Format::longDate(file.birthTime().isValid() ? file.birthTime() : file.metadataChangeTime()));
    map.insert(QStringLiteral("modified"), Format::longDate(file.lastModified()));
    map.insert(QStringLiteral("modifiedStamp"), file.lastModified().toMSecsSinceEpoch());
    map.insert(QStringLiteral("accessed"), Format::longDate(file.lastRead()));
    map.insert(QStringLiteral("owner"), file.owner());
    map.insert(QStringLiteral("group"), file.group());
    if (file.isSymLink()) {
        map.insert(QStringLiteral("original"), file.symLinkTarget());
    }
    const auto access = [&file](QFile::Permission read, QFile::Permission write) {
        const bool r = file.permission(read);
        const bool w = file.permission(write);
        return r && w ? tr("Read & Write") : (r ? tr("Read only") : (w ? tr("Write only") : tr("No Access")));
    };
    map.insert(QStringLiteral("ownerAccess"), access(QFile::ReadOwner, QFile::WriteOwner));
    map.insert(QStringLiteral("groupAccess"), access(QFile::ReadGroup, QFile::WriteGroup));
    map.insert(QStringLiteral("otherAccess"), access(QFile::ReadOther, QFile::WriteOther));
    if (entry.mime.startsWith(QLatin1String("image/"))) {
        const QSize size = QImageReader(path).size();
        if (size.isValid()) {
            map.insert(QStringLiteral("dimensions"), QStringLiteral("%1 × %2").arg(size.width()).arg(size.height()));
            map.insert(QStringLiteral("pixelSize"), size);
        }
    }
    return map;
}

void App::measureFolder(const QUrl &url)
{
    KIO::DirectorySizeJob *job = KIO::directorySize(url);
    connect(job, &KJob::result, this, [this, job, url] {
        if (job->error()) {
            return;
        }
        const quint64 items = job->totalFiles() + job->totalSubdirs();
        const QString text = tr("%1 for %2").arg(Format::size(job->totalSize()), items == 1 ? tr("1 item") : tr("%1 items").arg(QLocale().toString(qulonglong(items))));
        Q_EMIT folderSized(url, text);
    });
}

QString App::readText(const QString &path, int maxBytes) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    const QByteArray data = file.read(maxBytes);
    return data.contains('\0') ? QString() : QString::fromUtf8(data);
}

QString App::tagLabel(const QString &name) const
{
    return Tags::label(name);
}

QString App::tagColor(const QString &name) const
{
    const int index = Tags::names().indexOf(name);
    return index < 0 ? QStringLiteral("#98989d") : QString::fromLatin1(tagColors[index]);
}

void App::setTag(const QList<QUrl> &urls, const QString &name, bool on)
{
    bool refused = false;
    for (const QUrl &url : urls) {
        if (url.isLocalFile() && !Tags::self()->setTag(url.toLocalFile(), name, on)) {
            refused = true;
        }
    }
    if (refused) {
        Q_EMIT failed(tr("Tags can’t be saved on this disk."));
    }
}

bool App::allTagged(const QList<QUrl> &urls, const QString &name) const
{
    if (urls.isEmpty()) {
        return false;
    }
    for (const QUrl &url : urls) {
        if (!url.isLocalFile() || !Tags::self()->tagsFor(url.toLocalFile()).contains(name)) {
            return false;
        }
    }
    return true;
}

QString App::icons() const
{
    const QString tint = FolderLooks::self()->tint();
    return tint.isEmpty() ? QStringLiteral("image://icon/") : QStringLiteral("image://icon/@%1/").arg(tint);
}

QString App::folderTint() const
{
    const QString tint = FolderLooks::self()->tint();
    return tint.isEmpty() ? tint : QLatin1Char('#') + tint;
}

void App::customizeFolder(const QUrl &url)
{
    if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isDir()) {
        Q_EMIT customizeRequested(url);
    }
}

QVariantMap App::folderLook(const QUrl &url) const
{
    QVariantMap map;
    if (!url.isLocalFile()) {
        return map;
    }
    const Entry entry = Entry::fromPath(QDir::cleanPath(url.toLocalFile()));
    const FolderLook look = FolderLooks::self()->lookFor(entry.path);
    map.insert(QStringLiteral("name"), entry.name);
    map.insert(QStringLiteral("icon"), entry.icon);
    map.insert(QStringLiteral("color"), look.color);
    map.insert(QStringLiteral("symbol"), look.symbol);
    map.insert(QStringLiteral("emoji"), look.emoji);
    return map;
}

bool App::setFolderLook(const QUrl &url, const QString &color, const QString &symbol, const QString &emoji)
{
    return url.isLocalFile() && FolderLooks::self()->setLook(QDir::cleanPath(url.toLocalFile()), FolderLook{color, symbol, emoji});
}

RabbitAdaptor::RabbitAdaptor(App *app)
    : QDBusAbstractAdaptor(app)
    , m_app(app)
{
}

void RabbitAdaptor::CustomizeFolder(const QString &uri)
{
    m_app->customizeFolder(QUrl(uri));
}

FileManagerAdaptor::FileManagerAdaptor(App *app)
    : QDBusAbstractAdaptor(app)
    , m_app(app)
{
}

void FileManagerAdaptor::ShowFolders(const QStringList &uris, const QString &)
{
    for (const QString &uri : uris) {
        m_app->requestWindow(QUrl(uri));
    }
    if (uris.isEmpty()) {
        m_app->requestWindow(m_app->homeUrl());
    }
}

void FileManagerAdaptor::ShowItems(const QStringList &uris, const QString &)
{
    QHash<QUrl, QList<QUrl>> byFolder;
    for (const QString &uri : uris) {
        const QUrl url(uri);
        byFolder[url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash)] << url;
    }
    for (auto it = byFolder.begin(); it != byFolder.end(); ++it) {
        m_app->requestWindow(it.key(), it.value());
    }
}

void FileManagerAdaptor::ShowItemProperties(const QStringList &uris, const QString &)
{
    for (const QString &uri : uris) {
        Q_EMIT m_app->infoRequested(QUrl(uri));
    }
}

static QList<QUrl> urlsOf(const QStringList &uris)
{
    QList<QUrl> urls;
    for (const QString &uri : uris) {
        urls << QUrl(uri);
    }
    return urls;
}

LookAdaptor::LookAdaptor(App *app)
    : QDBusAbstractAdaptor(app)
    , m_app(app)
{
}

void LookAdaptor::Toggle(const QStringList &uris)
{
    m_app->quickLook(urlsOf(uris));
}

void LookAdaptor::Update(const QStringList &uris)
{
    m_app->quickLook(urlsOf(uris), nullptr, true);
}
