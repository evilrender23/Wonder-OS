#pragma once

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QQuickWindow>
#include <QSize>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include "fileops.h"
#include "places.h"

class DragNotice;
class QFileSystemWatcher;
class QJSEngine;
class QQmlEngine;

/// What the windows share: the places, the file operations, launching,
/// and the facts about a file the views can't get from a model row.
class App : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(FileOps *ops READ ops CONSTANT)
    Q_PROPERTY(Places *places READ places CONSTANT)
    Q_PROPERTY(QUrl homeUrl READ homeUrl CONSTANT)
    Q_PROPERTY(QUrl trashUrl READ trashUrl CONSTANT)
    Q_PROPERTY(bool trashEmpty READ trashEmpty NOTIFY trashEmptyChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    Q_PROPERTY(QString startView READ startView CONSTANT)
    Q_PROPERTY(QString icons READ icons NOTIFY folderTintChanged)
    Q_PROPERTY(QString folderTint READ folderTint NOTIFY folderTintChanged)
    Q_PROPERTY(QStringList tagNames READ tagNames CONSTANT)
    Q_PROPERTY(bool glass READ glass NOTIFY darkChanged)
    Q_PROPERTY(bool testing READ testing CONSTANT)
    Q_PROPERTY(QStringList testActions READ testActions CONSTANT)
    Q_PROPERTY(QSize testSize READ testSize CONSTANT)
    Q_PROPERTY(QUrl testCustomize READ testCustomize CONSTANT)
    Q_PROPERTY(QList<QUrl> testLook READ testLook CONSTANT)

public:
    enum class Appearance { Auto, Light, Dark };

    explicit App(QObject *parent);
    /// The QML engine's way to the one App that main() made.
    static App *create(QQmlEngine *, QJSEngine *);

    FileOps *ops() const { return m_ops; }
    Places *places() const { return m_places; }
    QUrl homeUrl() const;
    QUrl trashUrl() const { return QUrl(QStringLiteral("trash:/")); }
    bool trashEmpty() const { return m_trashEmpty; }
    bool dark() const { return m_dark; }
    QString startView() const { return m_startView; }
    void setStartView(const QString &view) { m_startView = view; }
    void setAppearance(Appearance appearance);
    QStringList tagNames() const;
    /// Translucent chrome: on under WonderlandWM, whose window rule frosts what is behind.
    bool glass() const { return m_glass; }
    bool testing() const { return !m_shotPath.isEmpty(); }
    QStringList testActions() const { return m_testActions; }
    QSize testSize() const { return m_testSize; }
    /// Test runs: the first window performs `actions`, is saved to `shotPath`, and the app quits.
    void setTest(const QString &shotPath, const QStringList &actions, const QSize &size);
    Q_INVOKABLE void saveShot(QQuickWindow *window);

    /// Opens a file with its default application, or launches a .desktop application.
    Q_INVOKABLE void open(const QUrl &url, const QString &mime, bool isApp);
    /// Applications that open a type, the default one first and flagged `isDefault`.
    Q_INVOKABLE QVariantList appsFor(const QString &mime) const;
    /// Opens `urls` with the chosen application and makes it the default for `mime`.
    Q_INVOKABLE void openWith(const QList<QUrl> &urls, const QString &storageId, const QString &mime);
    /// Makes an application the default for a type. False when either is unknown.
    static bool setDefaultApp(const QString &mime, const QString &storageId);
    Q_INVOKABLE void openTerminal(const QUrl &directory);
    /// A drag of files out of a window started or ended: the desktop is told, so it takes the drop.
    Q_INVOKABLE void announceDrag(bool dragging);
    Q_INVOKABLE void requestWindow(const QUrl &url, const QList<QUrl> &select = {});
    /// Quick Look, the preview Space opens. A second call for the item on show
    /// closes it, unless `update`, which only changes what an open one shows.
    /// `source` is the window whose selection it follows, if any.
    Q_INVOKABLE void quickLook(const QList<QUrl> &urls, QObject *source = nullptr, bool update = false);
    /// How Quick Look shows a type: see Look::kindFor.
    Q_INVOKABLE QString lookKind(const QString &mime, bool isDir) const;
    /// Quick Look opened or closed: the desktop is told, so its icon stays selected meanwhile.
    Q_INVOKABLE void announceLook(bool looking);
    /// Test runs of Quick Look alone: what --look named.
    QList<QUrl> testLook() const { return m_testLook; }
    void setTestLook(const QList<QUrl> &urls) { m_testLook = urls; }

    Q_INVOKABLE QUrl parentUrl(const QUrl &url) const;
    Q_INVOKABLE bool isInside(const QUrl &url, const QUrl &ancestor) const;
    Q_INVOKABLE QVariantList crumbs(const QUrl &url) const;
    Q_INVOKABLE QString displayPath(const QUrl &url) const;
    /// The folder a typed path names ("~/Docs", "/etc", "Music" relative to `base`), or an empty URL.
    Q_INVOKABLE QUrl folderFromInput(const QString &text, const QUrl &base) const;
    Q_INVOKABLE QString freeSpace(const QUrl &url) const;
    Q_INVOKABLE QString formatSize(double bytes) const;
    Q_INVOKABLE QVariantMap info(const QUrl &url) const;
    /// Measures a folder in the background; folderSized() carries the answer.
    Q_INVOKABLE void measureFolder(const QUrl &url);
    Q_INVOKABLE QString readText(const QString &path, int maxBytes) const;
    Q_INVOKABLE QString tagLabel(const QString &name) const;
    Q_INVOKABLE QString tagColor(const QString &name) const;
    Q_INVOKABLE void setTag(const QList<QUrl> &urls, const QString &name, bool on);
    Q_INVOKABLE bool allTagged(const QList<QUrl> &urls, const QString &name) const;

    /// What an icon's name goes after to make its URL; it carries the folders' tint.
    QString icons() const;
    /// The colour of folders with none of their own, as "#rrggbb"; empty for the usual blue.
    QString folderTint() const;

    /// Opens "Customize Folder" on a folder, in its own small window.
    Q_INVOKABLE void customizeFolder(const QUrl &url);
    /// A folder's look: `color` ("rrggbb" or ""), `symbol`, `emoji`, the `icon` that draws it and its `name`.
    Q_INVOKABLE QVariantMap folderLook(const QUrl &url) const;
    /// Gives a folder a colour and a symbol or an emoji; all empty is the plain folder again.
    Q_INVOKABLE bool setFolderLook(const QUrl &url, const QString &color, const QString &symbol, const QString &emoji);
    /// Test runs of "Customize Folder" alone (--customize with --shot): its window is the picture.
    QUrl testCustomize() const { return m_testCustomize; }
    void setTestCustomize(const QUrl &url) { m_testCustomize = url; }

Q_SIGNALS:
    void trashEmptyChanged();
    void darkChanged();
    void windowRequested(const QUrl &url, const QList<QUrl> &select);
    void lookRequested(const QList<QUrl> &urls, QObject *source, bool update);
    void infoRequested(const QUrl &url);
    void folderSized(const QUrl &url, const QString &text);
    void failed(const QString &message);
    void customizeRequested(const QUrl &url);
    void folderTintChanged();

private:
    void refreshTrash();
    void refreshDark();
    QString barStatePath() const;

    FileOps *m_ops;
    Places *m_places;
    QFileSystemWatcher *m_watcher;
    DragNotice *m_dragNotice;
    DragNotice *m_lookNotice;
    QList<QUrl> m_testLook;
    QString m_startView;
    Appearance m_appearance = Appearance::Auto;
    QString m_shotPath;
    QStringList m_testActions;
    QSize m_testSize;
    bool m_glass = false;
    bool m_trashEmpty = true;
    bool m_dark = false;
    QUrl m_testCustomize;
};

/// org.freedesktop.FileManager1, so "Show in folder" in other apps lands here.
class FileManagerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")

public:
    explicit FileManagerAdaptor(App *app);

public Q_SLOTS:
    void ShowFolders(const QStringList &uris, const QString &startupId);
    void ShowItems(const QStringList &uris, const QString &startupId);
    void ShowItemProperties(const QStringList &uris, const QString &startupId);

private:
    App *m_app;
};

/// What only this file manager answers: the desktop asks for "Customize Folder" here.
class FinderAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wonderland.Finder")

public:
    explicit FinderAdaptor(App *app);

public Q_SLOTS:
    void CustomizeFolder(const QString &uri);

private:
    App *m_app;
};

/// Quick Look for the rest of the desktop: its icons open the same preview.
class LookAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wonderland.Finder.QuickLook")

public:
    explicit LookAdaptor(App *app);

public Q_SLOTS:
    /// Shows the items, or closes the preview if it is showing the first of them.
    void Toggle(const QStringList &uris);
    /// Changes what an open preview shows; does nothing when there is none.
    void Update(const QStringList &uris);

private:
    App *m_app;
};
