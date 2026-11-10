#include "app.h"

#include "metadata.h"
#include "store.h"
#include "thumbnailer.h"

#include <QClipboard>
#include <QCollator>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMimeData>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QStyleHints>
#include <QtConcurrent>

App *App::s_app = nullptr;

static const QString kService = QStringLiteral("org.wonderland.Fotos");
static const QString kObject = QStringLiteral("/org/wonderland/Fotos");

QString App::defaultDataDir()
{
    const QString moved = qEnvironmentVariable("WONDERLAND_FOTOS_DATA");
    return moved.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : moved;
}

App::App(const QString &dataDir, const QString &shotPath, QObject *parent)
    : QObject(parent)
    , m_store(new Store(dataDir + QStringLiteral("/settings.json")))
    , m_library(new Library(m_store, dataDir + QStringLiteral("/library.json"), shotPath.isEmpty(), this))
    , m_albums(new Albums(dataDir + QStringLiteral("/albums.json"), this))
    , m_watcher(new QFileSystemWatcher(this))
    , m_shotPath(shotPath)
{
    s_app = this;
    Thumbnailer::setCacheDir(dataDir + QStringLiteral("/thumbs"));

    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &App::refreshDark);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &App::refreshDark);
    // The bar rewrites its state file by rename, which drops a file watch:
    // watching the directory catches the replacement.
    const QString barDir = QFileInfo(barStatePath()).absolutePath();
    if (QFileInfo(barDir).isDir()) {
        m_watcher->addPath(barDir);
    }
    refreshDark();
    m_library->rescan();
    if (!testing()) {
        // Thumbnails of pictures long gone: swept away behind the window, now and then.
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        if (now - qint64(m_store->value(QStringLiteral("pruned")).toDouble()) > 7 * 86400) {
            m_store->set(QStringLiteral("pruned"), double(now));
            (void)QtConcurrent::run([] {
                Thumbnailer::prune();
            });
        }
    }
}

App::~App()
{
    s_app = nullptr;
    delete m_library;
    delete m_store;
}

App *App::create(QQmlEngine *, QJSEngine *)
{
    QJSEngine::setObjectOwnership(s_app, QJSEngine::CppOwnership);
    return s_app;
}

void App::setTest(const QStringList &actions, const QSize &size)
{
    m_testActions = actions;
    m_testSize = size;
}

void App::saveShot(QQuickWindow *window)
{
    const bool saved = window && window->grabWindow().save(m_shotPath);
    QCoreApplication::exit(saved ? 0 : 1);
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
    if (qEnvironmentVariableIsSet("WONDERLAND_FOTOS_GLASS")) {
        glass = qEnvironmentVariableIntValue("WONDERLAND_FOTOS_GLASS") != 0;
    }
    if (dark != m_dark || glass != m_glass) {
        m_dark = dark;
        m_glass = glass;
        Q_EMIT darkChanged();
    }
}

bool App::publish()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        // No bus, no way to find another Fotos: this is the one.
        return true;
    }
    if (!bus.registerService(kService)) {
        return false;
    }
    bus.registerObject(kObject, this, QDBusConnection::ExportScriptableSlots);
    return true;
}

void App::forward(const QStringList &uris)
{
    QDBusMessage call = QDBusMessage::createMethodCall(kService, kObject, kService, QStringLiteral("Open"));
    call << uris;
    QDBusConnection::sessionBus().call(call, QDBus::Block, 3000);
}

void App::Open(const QStringList &uris)
{
    QList<QUrl> urls;
    for (const QString &uri : uris) {
        urls << QUrl(uri);
    }
    open(urls);
    Q_EMIT raiseRequested();
}

QStringList App::siblings(const QString &path) const
{
    const QFileInfo info(path);
    QStringList out;
    const QFileInfoList entries = info.dir().entryInfoList(QDir::Files);
    for (const QFileInfo &entry : entries) {
        if (Media::isMedia(entry.filePath())) {
            out << entry.absoluteFilePath();
        }
    }
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(out.begin(), out.end(), collator);
    if (!out.contains(info.absoluteFilePath())) {
        // Asked for by name whatever it is called: a picture without a known ending.
        out.prepend(info.absoluteFilePath());
    }
    return out;
}

void App::open(const QList<QUrl> &urls)
{
    QStringList files;
    for (const QUrl &url : urls) {
        const QFileInfo info(url.isLocalFile() ? url.toLocalFile() : url.toString());
        if (info.isDir()) {
            m_library->addFolder(QUrl::fromLocalFile(info.absoluteFilePath()));
        } else if (info.isFile()) {
            files << info.absoluteFilePath();
        }
    }
    if (files.size() == 1) {
        // One picture: the arrows walk the folder it is in.
        const QStringList around = siblings(files.first());
        Q_EMIT showRequested(around, qMax(0, int(around.indexOf(files.first()))));
    } else if (!files.isEmpty()) {
        Q_EMIT showRequested(files, 0);
    }
}

QVariantMap App::item(const QString &path) const
{
    MediaItem found = m_library->find(path);
    if (found.path.isEmpty()) {
        const QFileInfo info(path);
        if (!info.isFile()) {
            return {};
        }
        found.path = info.absoluteFilePath();
        found.mtime = info.lastModified().toMSecsSinceEpoch();
        found.size = info.size();
        Metadata::fill(found);
    }
    return found.toMap();
}

QVariantList App::details(const QString &path) const
{
    MediaItem found = m_library->find(path);
    if (found.path.isEmpty()) {
        const QFileInfo info(path);
        found.path = info.absoluteFilePath();
        found.mtime = info.lastModified().toMSecsSinceEpoch();
        found.size = info.size();
        Metadata::fill(found);
    }
    return Metadata::details(found);
}

QString App::time(qint64 ms) const
{
    const qint64 total = qMax(qint64(0), ms) / 1000;
    const qint64 hours = total / 3600;
    const qint64 minutes = (total / 60) % 60;
    const qint64 seconds = total % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

QString App::day(qint64 ms) const
{
    return Media::locale().toString(QDateTime::fromMSecsSinceEpoch(ms).date(), tr("d 'de' MMMM 'de' yyyy"));
}

QString App::moment(qint64 ms) const
{
    return Media::locale().toString(QDateTime::fromMSecsSinceEpoch(ms), tr("d 'de' MMMM 'de' yyyy, H:mm"));
}

QString App::counted(int photos, int videos) const
{
    if (videos == 0) {
        return photos == 1 ? tr("1 foto") : tr("%L1 fotos").arg(photos);
    }
    if (photos == 0) {
        return videos == 1 ? tr("1 vídeo") : tr("%L1 vídeos").arg(videos);
    }
    return tr("%L1 elementos").arg(photos + videos);
}

int App::trash(const QStringList &paths)
{
    QStringList gone;
    for (const QString &path : paths) {
        if (QFile::moveToTrash(path)) {
            gone << path;
        }
    }
    if (!gone.isEmpty()) {
        m_library->forget(gone);
        m_albums->forget(gone);
    }
    if (gone.size() < paths.size()) {
        const int left = int(paths.size() - gone.size());
        Q_EMIT failed(left == 1 ? tr("Un elemento no se pudo mover a la papelera.") : tr("%1 elementos no se pudieron mover a la papelera.").arg(left));
    }
    return int(gone.size());
}

bool App::canRotate(const QString &path) const
{
    return Metadata::canRotate(path);
}

bool App::rotate(const QString &path, bool clockwise)
{
    QString error;
    if (!Metadata::rotate(path, clockwise, &error)) {
        Q_EMIT failed(error);
        return false;
    }
    m_library->refresh(path);
    return true;
}

void App::copy(const QStringList &paths) const
{
    if (paths.isEmpty()) {
        return;
    }
    auto *data = new QMimeData;
    QList<QUrl> urls;
    for (const QString &path : paths) {
        urls << QUrl::fromLocalFile(path);
    }
    data->setUrls(urls);
    if (paths.size() == 1 && Media::isImage(paths.first())) {
        // Pasted into a chat or a drawing, it is the picture; into a folder, the file.
        const QImage image = Thumbnailer::picture(paths.first(), 4096);
        if (!image.isNull()) {
            data->setImageData(image);
        }
    }
    QGuiApplication::clipboard()->setMimeData(data);
}

void App::reveal(const QString &path) const
{
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("/org/freedesktop/FileManager1"),
                                                       QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"));
    call << QStringList{QUrl::fromLocalFile(path).toString()} << QString();
    QDBusConnection::sessionBus().asyncCall(call);
}

QVariant App::setting(const QString &key, const QVariant &fallback) const
{
    const QJsonValue value = m_store->value(key);
    return value.isUndefined() || value.isNull() ? fallback : value.toVariant();
}

void App::setSetting(const QString &key, const QVariant &value)
{
    m_store->set(key, QJsonValue::fromVariant(value));
}
