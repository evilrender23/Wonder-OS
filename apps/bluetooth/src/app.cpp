#include "app.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStyleHints>

static const QString service = QStringLiteral("org.wonderland.Bluetooth");
static const QString path = QStringLiteral("/org/wonderland/Bluetooth");

static QString barStatePath()
{
    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty()) {
        state = QDir::homePath() + QStringLiteral("/.local/state");
    }
    return state + QStringLiteral("/wonderlandwm/bar.json");
}

App::App(const QString &shotPath, QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
    , m_shotPath(shotPath)
{
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

void App::setAppearance(Appearance appearance)
{
    m_appearance = appearance;
    refreshDark();
}

void App::setTest(const QStringList &actions, const QSize &size)
{
    m_testActions = actions;
    m_testSize = size;
}

bool App::claim()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return true;
    }
    if (bus.registerService(service)) {
        bus.registerObject(path, this, QDBusConnection::ExportScriptableSlots);
        return true;
    }
    bus.call(QDBusMessage::createMethodCall(service, path, service, QStringLiteral("Raise")), QDBus::Block, 2000);
    return false;
}

void App::saveShot(QQuickWindow *window)
{
    const bool saved = window && window->grabWindow().save(m_shotPath);
    QCoreApplication::exit(saved ? 0 : 1);
}

void App::refreshDark()
{
    bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    // WonderlandWM's bar owns the light/dark switch for the whole desktop.
    QFile file(barStatePath());
    if (file.open(QIODevice::ReadOnly)) {
        const QString mode = QJsonDocument::fromJson(file.readAll()).object().value(QLatin1String("themeMode")).toString();
        if (!mode.isEmpty()) {
            dark = mode == QLatin1String("dark");
        }
    }
    if (m_appearance != Appearance::Auto) {
        dark = m_appearance == Appearance::Dark;
    }
    if (dark != m_dark) {
        m_dark = dark;
        Q_EMIT darkChanged();
    }
}
