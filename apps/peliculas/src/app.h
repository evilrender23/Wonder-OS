#pragma once

#include "history.h"
#include "player.h"
#include "playlist.h"

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

// Kept free of QML so the tests can build them; named for QML here.
struct PlaylistForeign {
    Q_GADGET
    QML_FOREIGN(Playlist)
    QML_ANONYMOUS
};
struct HistoryForeign {
    Q_GADGET
    QML_FOREIGN(History)
    QML_ANONYMOUS
};

/// What the window needs from outside itself: the player, what was seen
/// before, the desktop's light/dark and glass switches, and the files it is
/// asked to open.
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
    Q_PROPERTY(Player *player READ player CONSTANT)
    Q_PROPERTY(History *history READ history CONSTANT)

public:
    enum class Appearance { Auto, Light, Dark };

    /// `shotPath` set: a test run, which quits after one picture of the window.
    App(const QString &dataDir, const QString &shotPath, QObject *parent = nullptr);
    ~App() override;
    static App *instance() { return s_app; }
    static App *create(QQmlEngine *, QJSEngine *);
    /// Where the history and the settings live (WONDERLAND_PELICULAS_DATA moves it).
    static QString defaultDataDir();

    bool dark() const { return m_dark; }
    bool glass() const { return m_glass; }
    void setAppearance(Appearance appearance);
    bool testing() const { return !m_shotPath.isEmpty(); }
    QSize testSize() const { return m_testSize; }
    QStringList testActions() const { return m_testActions; }
    void setTest(const QStringList &actions, const QSize &size);
    Player *player() const { return m_player; }
    History *history() const { return m_history; }
    /// Brings the window to the front.
    void raise();

    /// One file plays with the rest of its folder lined up behind it; several
    /// files, or folders, are the list. Subtitle files go to the film that is on.
    Q_INVOKABLE void open(const QList<QUrl> &urls);
    /// The same, to the end of the list instead of in its place.
    Q_INVOKABLE void enqueue(const QList<QUrl> &urls);
    Q_INVOKABLE void openPath(const QString &path);
    /// 3:07, 1:02:45.
    Q_INVOKABLE QString time(qint64 ms) const;
    Q_INVOKABLE void reveal(const QString &path) const;
    Q_INVOKABLE QVariant setting(const QString &key, const QVariant &fallback) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void saveShot(QQuickWindow *window);

Q_SIGNALS:
    void darkChanged();
    void raiseRequested();
    void failed(const QString &message);

private:
    void refreshDark();
    QString barStatePath() const;

    static App *s_app;
    Store *m_store;
    History *m_history;
    Player *m_player;
    QFileSystemWatcher *m_watcher;
    Appearance m_appearance = Appearance::Auto;
    QString m_shotPath;
    QStringList m_testActions;
    QSize m_testSize = QSize(1040, 640);
    bool m_dark = false;
    bool m_glass = false;
};
