#pragma once

#include "editor.h"

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
class QTimer;
class Store;

/// What the window needs from outside itself: the document, the files around
/// it (recent ones, recovery copies, versions), and the desktop's light/dark
/// and glass switches.
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
    /// False while the window shows the start screen instead of a document.
    Q_PROPERTY(bool started READ started NOTIFY startedChanged)
    /// The document's name without its extension; «Sin título» until it has one.
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    /// [{path, name, folder, when}], the newest first.
    Q_PROPERTY(QVariantList recents READ recents NOTIFY recentsChanged)
    /// [{file, name, when}]: what a Textos that did not close well was writing.
    Q_PROPERTY(QVariantList recovered READ recovered NOTIFY recoveredChanged)
    /// [{file, when, size}] of the document, the newest first.
    Q_PROPERTY(QVariantList versions READ versions NOTIFY versionsChanged)
    Q_PROPERTY(QStringList families READ families CONSTANT)
    /// [{id, name, detail}]
    Q_PROPERTY(QVariantList templates READ templates CONSTANT)

public:
    enum class Appearance { Auto, Light, Dark };

    /// `shotPath` set: a test run, which quits after one picture.
    App(const QString &dataDir, const QString &shotPath, QObject *parent = nullptr);
    ~App() override;
    static App *create(QQmlEngine *, QJSEngine *);
    /// Where settings, recovery copies and versions live (WONDERLAND_TEXTOS_DATA moves it).
    static QString defaultDataDir();

    bool dark() const { return m_dark; }
    bool glass() const { return m_glass; }
    void setAppearance(Appearance appearance);
    bool testing() const { return !m_shotPath.isEmpty(); }
    QSize testSize() const { return m_testSize; }
    QStringList testActions() const { return m_testActions; }
    void setTest(const QStringList &actions, const QSize &size);
    Editor *editor() const { return m_editor; }
    bool started() const { return m_started; }
    QString title() const;
    QVariantList recents() const;
    QVariantList recovered() const;
    QVariantList versions() const;
    QStringList families() const;
    QVariantList templates() const;

    // --- Documents ---
    /// A new document from a template (blank, letter, resume, report, minutes, sample).
    Q_INVOKABLE void start(const QString &kind);
    /// Opens a file here, or in a window of its own when this one is taken.
    Q_INVOKABLE bool open(const QString &path);
    /// Another Textos, with that file or none.
    Q_INVOKABLE void openWindow(const QString &path = QString());
    /// Back to the start screen.
    Q_INVOKABLE void closeDocument();
    /// Saves to the document's file and keeps a version; false when it has none yet.
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QString &path);
    Q_INVOKABLE bool exportTo(const QString &path);
    /// Gives the document another name: its file moves with it.
    Q_INVOKABLE bool rename(const QString &name);
    /// Where a save dialog starts: the document's folder and name.
    Q_INVOKABLE QUrl suggestedUrl(const QString &extension) const;
    Q_INVOKABLE bool openRecovered(const QString &file);
    Q_INVOKABLE void discardRecovered(const QString &file);
    Q_INVOKABLE void forgetRecent(const QString &path);

    // --- Versions ---
    /// The first lines of a version, as plain text.
    Q_INVOKABLE QString versionPreview(const QString &file) const;
    /// {words, pages} of a version.
    Q_INVOKABLE QVariantMap versionFacts(const QString &file) const;
    /// The version becomes the document, unsaved; what was there stays in undo's reach of nobody, so it is kept as a version first.
    Q_INVOKABLE bool restoreVersion(const QString &file);

    Q_INVOKABLE QString localPath(const QUrl &url) const { return url.toLocalFile(); }
    Q_INVOKABLE QString today() const;
    Q_INVOKABLE void openLink(const QString &href) const;
    /// Folds accents and case away, for the palette's search.
    Q_INVOKABLE QString fold(const QString &text) const;

    Q_INVOKABLE QVariant setting(const QString &key, const QVariant &fallback) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void saveShot(QQuickWindow *window);
    /// Writes the recovery copy now, if there is anything to recover.
    Q_INVOKABLE void autosave();
    /// The document closed well: no recovery copy is left behind.
    Q_INVOKABLE void closing();

Q_SIGNALS:
    void darkChanged();
    void startedChanged();
    void titleChanged();
    void recentsChanged();
    void recoveredChanged();
    void versionsChanged();
    /// Something worth a word at the bottom of the window.
    void said(const QString &message);

private:
    void refreshDark();
    QString barStatePath() const;
    void remember(const QString &path);
    void keepVersion(const QString &path);
    QString versionsDir(const QString &path) const;
    QString recoveryFile() const;
    void dropRecovery();
    void setStarted(bool started);

    static App *s_app;
    QString m_dataDir;
    Store *m_store;
    Editor *m_editor;
    QFileSystemWatcher *m_watcher;
    QTimer *m_autosave;
    Appearance m_appearance = Appearance::Auto;
    QString m_shotPath;
    QStringList m_testActions;
    QSize m_testSize = QSize(1180, 760);
    /// The name given to a document that has no file yet.
    QString m_name;
    int m_savedRevision = -1;
    bool m_started = false;
    bool m_dark = false;
    bool m_glass = false;
};
