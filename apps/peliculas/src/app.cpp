#include "app.h"

#include "media.h"
#include "store.h"

#include <QCoreApplication>
#include <QDBusConnection>
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
#include <QStandardPaths>
#include <QStyleHints>

App *App::s_app = nullptr;

QString App::defaultDataDir()
{
    const QString moved = qEnvironmentVariable("WONDERLAND_PELICULAS_DATA");
    return moved.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : moved;
}

App::App(const QString &dataDir, const QString &shotPath, QObject *parent)
    : QObject(parent)
    , m_store(new Store(dataDir + QStringLiteral("/settings.json")))
    , m_history(new History(dataDir + QStringLiteral("/history.json"), this))
    , m_player(new Player(m_store, m_history, !shotPath.isEmpty(), this))
    , m_watcher(new QFileSystemWatcher(this))
    , m_shotPath(shotPath)
{
    s_app = this;
    connect(m_player, &Player::failed, this, &App::failed);
    connect(qApp, &QCoreApplication::aboutToQuit, m_player, &Player::shutdown);

    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &App::refreshDark);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &App::refreshDark);
    // The bar rewrites its state file by rename, which drops a file watch:
    // watching the directory catches the replacement.
    const QString barDir = QFileInfo(barStatePath()).absolutePath();
    if (QFileInfo(barDir).isDir()) {
        m_watcher->addPath(barDir);
    }
    refreshDark();
}

App::~App()
{
    s_app = nullptr;
    delete m_player;
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

void App::raise()
{
    Q_EMIT raiseRequested();
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
    if (qEnvironmentVariableIsSet("WONDERLAND_PELICULAS_GLASS")) {
        glass = qEnvironmentVariableIntValue("WONDERLAND_PELICULAS_GLASS") != 0;
    }
    if (dark != m_dark || glass != m_glass) {
        m_dark = dark;
        m_glass = glass;
        Q_EMIT darkChanged();
    }
}

static QStringList localPaths(const QList<QUrl> &urls)
{
    QStringList paths;
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            paths << QFileInfo(url.toLocalFile()).absoluteFilePath();
        }
    }
    return paths;
}

void App::open(const QList<QUrl> &urls)
{
    QStringList paths = localPaths(urls);
    if (paths.isEmpty()) {
        return;
    }
    QStringList subtitles;
    QStringList rest;
    for (const QString &path : std::as_const(paths)) {
        (Media::isSubtitle(path) ? subtitles : rest) << path;
    }
    if (rest.isEmpty()) {
        if (m_player->hasMedia()) {
            m_player->useSubtitleFile(QUrl::fromLocalFile(subtitles.first()));
        } else {
            Q_EMIT failed(tr("Los subtítulos necesitan una película abierta."));
        }
        return;
    }
    QStringList list;
    int index = 0;
    if (rest.size() == 1 && QFileInfo(rest.first()).isFile()) {
        list = Media::siblings(rest.first());
        index = qMax(0, int(list.indexOf(rest.first())));
    } else {
        list = Media::expand(rest);
    }
    if (list.isEmpty()) {
        Q_EMIT failed(tr("Ahí no hay nada que reproducir."));
        return;
    }
    m_player->open(list, index);
    if (!subtitles.isEmpty()) {
        m_player->useSubtitleFile(QUrl::fromLocalFile(subtitles.first()));
    }
}

void App::enqueue(const QList<QUrl> &urls)
{
    const QStringList list = Media::expand(localPaths(urls));
    if (list.isEmpty()) {
        Q_EMIT failed(tr("Ahí no hay nada que reproducir."));
        return;
    }
    m_player->enqueue(list);
}

void App::openPath(const QString &path)
{
    open({QUrl::fromLocalFile(path)});
}

QString App::time(qint64 ms) const
{
    return Media::time(ms);
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
