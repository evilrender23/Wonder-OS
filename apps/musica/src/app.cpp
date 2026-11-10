#include "app.h"

#include "library.h"
#include "player.h"
#include "playlists.h"
#include "store.h"
#include "tagreader.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QDBusPendingCall>
#include <QGuiApplication>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QStyleHints>

App *App::s_app = nullptr;

QString App::defaultDataDir()
{
    const QString moved = qEnvironmentVariable("WONDERLAND_MUSICA_DATA");
    return moved.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : moved;
}

App::App(const QString &dataDir, const QString &shotPath, QObject *parent)
    : QObject(parent)
    , m_store(new Store(dataDir + QStringLiteral("/settings.json")))
    , m_library(new Library(m_store, dataDir + QStringLiteral("/library.json"), true, this))
    , m_playlists(new Playlists(dataDir + QStringLiteral("/playlists.json"), this))
    , m_watcher(new QFileSystemWatcher(this))
    , m_shotPath(shotPath)
{
    s_app = this;
    m_player = new Player(m_library, m_store, dataDir, testing(), this);

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
}

App::~App()
{
    if (!testing()) {
        m_player->saveSession();
    }
    s_app = nullptr;
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
    if (qEnvironmentVariableIsSet("WONDERLAND_MUSICA_GLASS")) {
        glass = qEnvironmentVariableIntValue("WONDERLAND_MUSICA_GLASS") != 0;
    }
    if (dark != m_dark || glass != m_glass) {
        m_dark = dark;
        m_glass = glass;
        Q_EMIT darkChanged();
    }
}

void App::open(const QList<QUrl> &urls)
{
    QStringList files;
    for (const QUrl &url : urls) {
        const QFileInfo info(url.isLocalFile() ? url.toLocalFile() : url.toString());
        if (info.isDir()) {
            m_library->addFolder(QUrl::fromLocalFile(info.absoluteFilePath()));
        } else if (info.isFile() && TagReader::isAudio(info.filePath())) {
            files << info.absoluteFilePath();
        }
    }
    if (!files.isEmpty()) {
        m_player->playTracks(files, 0);
    }
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

QString App::span(qint64 ms) const
{
    const qint64 minutes = qMax(qint64(1), (ms + 30000) / 60000);
    const qint64 hours = minutes / 60;
    const qint64 rest = minutes % 60;
    const QString minuteText = rest == 1 ? QStringLiteral("1 minuto") : QStringLiteral("%1 minutos").arg(rest);
    if (hours == 0) {
        return minuteText;
    }
    const QString hourText = hours == 1 ? QStringLiteral("1 hora") : QStringLiteral("%1 horas").arg(hours);
    return rest == 0 ? hourText : hourText + QStringLiteral(" y ") + minuteText;
}

QString App::songs(int count) const
{
    return count == 1 ? QStringLiteral("1 canción") : QStringLiteral("%L1 canciones").arg(count);
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
