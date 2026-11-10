#pragma once

#include "editor.h"

#include <QObject>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QSize>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class QFileSystemWatcher;
class QJSEngine;
class QQmlEngine;
class QTimer;
class Store;

/// What the window needs from outside itself: the drawing and the file it
/// lives in, the recent ones, the versions, the desktop's light/dark and
/// glass switches, and the files that go out.
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
    Q_PROPERTY(Editor *editor READ editor CONSTANT)
    /// A drawing is open (false: the start screen).
    Q_PROPERTY(bool open READ isOpen NOTIFY documentChanged)
    /// The file of the drawing; empty until it is first saved.
    Q_PROPERTY(QString path READ path NOTIFY documentChanged)
    /// [{path, name, folder, when}], newest first.
    Q_PROPERTY(QVariantList recents READ recents NOTIFY recentsChanged)
    /// [{path, when, label}], newest first.
    Q_PROPERTY(QVariantList versions READ versions NOTIFY versionsChanged)
    /// Unsaved work found from a run that ended badly: [{path, title, when}].
    Q_PROPERTY(QVariantList recoveries READ recoveries NOTIFY recoveriesChanged)

public:
    enum class Appearance { Auto, Light, Dark };

    /// `shotPath` set: a test run, which quits after one picture.
    App(const QString &dataDir, const QString &shotPath, QObject *parent = nullptr);
    ~App() override;
    static App *create(QQmlEngine *, QJSEngine *);
    /// Where the settings, the versions and the recovery copies live
    /// (WONDERLAND_DIBUJO_DATA moves it).
    static QString defaultDataDir();

    bool dark() const { return m_dark; }
    bool glass() const { return m_glass; }
    void setAppearance(Appearance appearance);
    bool testing() const { return !m_shotPath.isEmpty(); }
    QSize testSize() const { return m_testSize; }
    QStringList testActions() const { return m_testActions; }
    void setTest(const QStringList &actions, const QSize &size);
    Editor *editor() const { return m_editor; }
    bool isOpen() const { return m_open; }
    QString path() const { return m_path; }
    QVariantList recents() const;
    QVariantList versions() const;
    QVariantList recoveries() const;
    QString dataDir() const { return m_dataDir; }

    /// {key, name, detail} of each drawing a new one can start from.
    Q_INVOKABLE QVariantList templates() const;
    /// The forms of the library, by section: [{section, forms: [{form, name, sides, radius}]}].
    Q_INVOKABLE QVariantList formSections() const;
    Q_INVOKABLE QVariantList framePresets() const;

    Q_INVOKABLE void newDocument(const QString &templateKey);
    /// A .wdib is opened; an SVG becomes a new drawing with its objects.
    Q_INVOKABLE bool openPath(const QString &path);
    Q_INVOKABLE bool openUrl(const QUrl &url) { return openPath(url.toLocalFile()); }
    /// Another process for another drawing; no path, its start screen.
    Q_INVOKABLE void openInNewWindow(const QString &path);
    /// False when the drawing has no file yet: ask for one and call saveAs.
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl &url);
    /// Back to the start screen.
    Q_INVOKABLE void closeDocument();
    Q_INVOKABLE void restoreVersion(const QString &path);
    Q_INVOKABLE void recover(const QString &path);
    Q_INVOKABLE void dropRecoveries();
    Q_INVOKABLE void forgetRecent(const QString &path);

    Q_INVOKABLE bool importSvg(const QUrl &url);
    Q_INVOKABLE bool insertImage(const QUrl &url);
    /// `frame`: the id of a frame, or empty for everything. svg, png or pdf
    /// by `format`; a PDF of everything has one page per frame when there are frames.
    Q_INVOKABLE bool exportTo(const QUrl &url, const QString &format, const QString &frame, double scale, bool transparent);
    /// A name to offer for a file that goes out.
    Q_INVOKABLE QUrl suggestedUrl(const QString &suffix) const;
    Q_INVOKABLE QString localPath(const QUrl &url) const { return url.toLocalFile(); }
    Q_INVOKABLE QString fileName(const QString &path) const;

    Q_INVOKABLE QVariant setting(const QString &key, const QVariant &fallback) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void saveShot(QQuickWindow *window);
    /// For the pictures of the tests: pointer gestures by the real path.
    Q_INVOKABLE void say(const QString &message) { Q_EMIT said(message); }

Q_SIGNALS:
    void darkChanged();
    void documentChanged();
    void recentsChanged();
    void versionsChanged();
    void recoveriesChanged();
    /// Something worth a word at the bottom of the window.
    void said(const QString &message);

private:
    void refreshDark();
    QString barStatePath() const;
    void adopt(const Document &doc, const QString &path);
    void remember(const QString &path);
    void autosave();

    static App *s_app;
    QString m_dataDir;
    Store *m_store;
    Editor *m_editor;
    QFileSystemWatcher *m_watcher;
    QTimer *m_autosave;
    Appearance m_appearance = Appearance::Auto;
    QString m_shotPath;
    QStringList m_testActions;
    QSize m_testSize = QSize(1200, 760);
    QString m_path;
    bool m_open = false;
    bool m_dark = false;
    bool m_glass = false;
    int m_savedRevision = -1;
};

/// Small pictures of drawings: `image://thumb/template/<key>` and
/// `image://thumb/file/<path>` (the preview a .wdib carries).
class ThumbProvider : public QQuickImageProvider
{
public:
    explicit ThumbProvider(App *app);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    App *m_app;
};
