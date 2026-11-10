#pragma once

#include "library.h"
#include "player.h"
#include "playlists.h"

#include <QObject>
#include <QQuickWindow>
#include <QSize>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class QFileSystemWatcher;
class QJSEngine;
class QQmlEngine;
class Store;

/// What the window needs from outside itself: the library, the player, the
/// desktop's light/dark and glass switches, and the files it is asked to open.
class App : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    /// The window is see-through where the compositor frosts what is behind it.
    Q_PROPERTY(bool glass READ glass NOTIFY darkChanged)
    Q_PROPERTY(bool testing READ testing CONSTANT)
    Q_PROPERTY(QSize testSize READ testSize CONSTANT)
    Q_PROPERTY(QStringList testActions READ testActions CONSTANT)
    Q_PROPERTY(Library *library READ library CONSTANT)
    Q_PROPERTY(Player *player READ player CONSTANT)
    Q_PROPERTY(Playlists *playlists READ playlists CONSTANT)

public:
    enum class Appearance { Auto, Light, Dark };

    /// `shotPath` set: a test run, which makes no sound and quits after one picture.
    App(const QString &dataDir, const QString &shotPath, QObject *parent = nullptr);
    ~App() override;
    static App *instance() { return s_app; }
    static App *create(QQmlEngine *, QJSEngine *);
    /// Where the library, the playlists and the settings live (WONDERLAND_MUSICA_DATA moves it).
    static QString defaultDataDir();

    bool dark() const { return m_dark; }
    bool glass() const { return m_glass; }
    void setAppearance(Appearance appearance);
    bool testing() const { return !m_shotPath.isEmpty(); }
    QSize testSize() const { return m_testSize; }
    QStringList testActions() const { return m_testActions; }
    void setTest(const QStringList &actions, const QSize &size);
    Library *library() const { return m_library; }
    Player *player() const { return m_player; }
    Playlists *playlists() const { return m_playlists; }

    /// Files play now; folders join the library.
    Q_INVOKABLE void open(const QList<QUrl> &urls);
    /// 3:07, 1:02:45.
    Q_INVOKABLE QString time(qint64 ms) const;
    /// «42 minutos», «1 hora y 12 minutos».
    Q_INVOKABLE QString span(qint64 ms) const;
    Q_INVOKABLE QString songs(int count) const;
    /// A name as the window writes it: the library keeps the «unknown» artist and
    /// album under their Spanish names, which are also what tells them apart.
    Q_INVOKABLE QString shown(const QString &name) const;
    Q_INVOKABLE void reveal(const QString &path) const;
    Q_INVOKABLE QVariant setting(const QString &key, const QVariant &fallback) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void saveShot(QQuickWindow *window);
    Q_INVOKABLE void raise() { Q_EMIT raiseRequested(); }

Q_SIGNALS:
    void darkChanged();
    void raiseRequested();

private:
    void refreshDark();
    QString barStatePath() const;

    static App *s_app;
    Store *m_store;
    Library *m_library;
    Playlists *m_playlists;
    Player *m_player;
    QFileSystemWatcher *m_watcher;
    Appearance m_appearance = Appearance::Auto;
    QString m_shotPath;
    QStringList m_testActions;
    QSize m_testSize = QSize(1120, 720);
    bool m_dark = false;
    bool m_glass = false;
};
