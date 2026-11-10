#pragma once

#include "albums.h"
#include "library.h"

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

/// What the window needs from outside itself: the library, the albums, the
/// desktop's light/dark and glass switches, and the files it is asked to open.
class App : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wonderland.Fotos")
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    /// The window is see-through where the compositor frosts what is behind it.
    Q_PROPERTY(bool glass READ glass NOTIFY darkChanged)
    Q_PROPERTY(bool testing READ testing CONSTANT)
    Q_PROPERTY(QSize testSize READ testSize CONSTANT)
    Q_PROPERTY(QStringList testActions READ testActions CONSTANT)
    Q_PROPERTY(Library *library READ library CONSTANT)
    Q_PROPERTY(Albums *albums READ albums CONSTANT)

public:
    enum class Appearance { Auto, Light, Dark };

    /// `shotPath` set: a test run, which quits after one picture of the window.
    App(const QString &dataDir, const QString &shotPath, QObject *parent = nullptr);
    ~App() override;
    static App *instance() { return s_app; }
    static App *create(QQmlEngine *, QJSEngine *);
    /// Where the library, the albums and the settings live (WONDERLAND_FOTOS_DATA moves it).
    static QString defaultDataDir();

    bool dark() const { return m_dark; }
    bool glass() const { return m_glass; }
    void setAppearance(Appearance appearance);
    bool testing() const { return !m_shotPath.isEmpty(); }
    QSize testSize() const { return m_testSize; }
    QStringList testActions() const { return m_testActions; }
    void setTest(const QStringList &actions, const QSize &size);
    Library *library() const { return m_library; }
    Albums *albums() const { return m_albums; }

    /// One Fotos per session: false when another already answers on the bus.
    bool publish();
    /// Hands these to the Fotos that is already open.
    static void forward(const QStringList &uris);

    /// Files show now, among those of their folder; folders join the library.
    Q_INVOKABLE void open(const QList<QUrl> &urls);
    /// A picture or video by its path, in the library or not.
    Q_INVOKABLE QVariantMap item(const QString &path) const;
    /// The pictures and videos next to this one on disk, by name.
    Q_INVOKABLE QStringList siblings(const QString &path) const;
    /// The rows of the information panel: [{label, value}].
    Q_INVOKABLE QVariantList details(const QString &path) const;
    /// 3:07, 1:02:45.
    Q_INVOKABLE QString time(qint64 ms) const;
    /// «31 de enero de 2024».
    Q_INVOKABLE QString day(qint64 ms) const;
    /// «31 de enero de 2024, 10:15».
    Q_INVOKABLE QString moment(qint64 ms) const;
    /// «12 fotos», «3 vídeos», «15 elementos».
    Q_INVOKABLE QString counted(int photos, int videos) const;
    /// Returns how many went to the bin.
    Q_INVOKABLE int trash(const QStringList &paths);
    Q_INVOKABLE bool canRotate(const QString &path) const;
    Q_INVOKABLE bool rotate(const QString &path, bool clockwise);
    /// Puts the files (and, of a single picture, the picture itself) on the clipboard.
    Q_INVOKABLE void copy(const QStringList &paths) const;
    Q_INVOKABLE void reveal(const QString &path) const;
    Q_INVOKABLE QVariant setting(const QString &key, const QVariant &fallback) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void saveShot(QQuickWindow *window);

public Q_SLOTS:
    /// What a second launch calls on the first.
    Q_SCRIPTABLE void Open(const QStringList &uris);

Q_SIGNALS:
    void darkChanged();
    void raiseRequested();
    /// Show `paths[index]` in the viewer, with the rest to move through.
    void showRequested(const QStringList &paths, int index);
    void failed(const QString &message);

private:
    void refreshDark();
    QString barStatePath() const;

    static App *s_app;
    Store *m_store;
    Library *m_library;
    Albums *m_albums;
    QFileSystemWatcher *m_watcher;
    Appearance m_appearance = Appearance::Auto;
    QString m_shotPath;
    QStringList m_testActions;
    QSize m_testSize = QSize(1120, 720);
    bool m_dark = false;
    bool m_glass = false;
};
