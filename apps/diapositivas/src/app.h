#pragma once

#include "deck.h"

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
class Versions;
class QTimer;

/// What the window needs from outside itself: the deck, the desktop's
/// light/dark and glass switches, the settings and the files it is asked to open.
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
    Q_PROPERTY(Deck *deck READ deck CONSTANT)
    /// The presentations opened last, newest first.
    Q_PROPERTY(QStringList recent READ recent NOTIFY recentChanged)
    Q_PROPERTY(int screens READ screens NOTIFY screensChanged)
    /// False while the window still shows the start screen: nothing opened, nothing begun.
    Q_PROPERTY(bool started READ started NOTIFY startedChanged)
    /// Goes up when the versions or the recovered copies on disk change.
    Q_PROPERTY(int shelf READ shelf NOTIFY shelfChanged)

public:
    enum class Appearance { Auto, Light, Dark };

    /// `shotPath` set: a test run, which quits after one picture.
    App(const QString &dataDir, const QString &shotPath, QObject *parent = nullptr);
    ~App() override;
    static App *create(QQmlEngine *, QJSEngine *);
    /// Where the settings live (WONDERLAND_DIAPOSITIVAS_DATA moves it).
    static QString defaultDataDir();

    bool dark() const { return m_dark; }
    bool glass() const { return m_glass; }
    void setAppearance(Appearance appearance);
    bool testing() const { return !m_shotPath.isEmpty(); }
    QSize testSize() const { return m_testSize; }
    QStringList testActions() const { return m_testActions; }
    void setTest(const QStringList &actions, const QSize &size);
    Deck *deck() const { return m_deck; }
    QStringList recent() const;
    int screens() const;
    bool started() const { return m_started; }
    int shelf() const { return m_shelf; }

    /// Opens the first presentation here; any other, each in a window of its own.
    Q_INVOKABLE void open(const QList<QUrl> &urls);
    /// Reads a .pptx or .odp into the window; says why when it cannot.
    Q_INVOKABLE bool openPath(const QString &path);
    /// Writes the deck; an .odp or a deck never saved needs a path.
    Q_INVOKABLE bool save(const QString &path);
    Q_INVOKABLE bool exportPdf(const QString &path);
    Q_INVOKABLE bool exportPng(const QString &path);
    /// Leaves the start screen with a new deck in this theme.
    Q_INVOKABLE void start(const QString &theme = QStringLiteral("blanco"), const QString &aspect = QStringLiteral("16:9"));
    /// Renames the presentation, and its file when it has one.
    Q_INVOKABLE bool rename(const QString &name);

    /// The copies left by the manual saves of this document, newest first: [{file, date, time, size}].
    Q_INVOKABLE QVariantList versions() const;
    /// Brings a version back as one step that can be undone.
    Q_INVOKABLE bool restoreVersion(const QString &file);
    /// What a crash left unsaved: [{file, name, path, date}].
    Q_INVOKABLE QVariantList recoveries() const;
    Q_INVOKABLE bool recover(const QString &file);
    Q_INVOKABLE void discardRecovery(const QString &file);
    /// Writes the recovery copy now, if there is something unsaved.
    Q_INVOKABLE void autosave();
    /// The address of a picture of a slide of a file, for `image://preview/`.
    Q_INVOKABLE QString previewOf(const QString &file, int slide = 0) const;

    /// Another Diapositivas, with `path` or with a new deck.
    Q_INVOKABLE void launch(const QString &path = QString());
    /// The second screen, for the slides, when there is one; the window's own otherwise.
    Q_INVOKABLE QScreen *audienceScreen(QQuickWindow *window) const;

    Q_INVOKABLE QString localPath(const QUrl &url) const { return url.toLocalFile(); }
    Q_INVOKABLE QUrl fileUrl(const QString &path) const { return QUrl::fromLocalFile(path); }
    Q_INVOKABLE QString fileName(const QString &path) const;
    Q_INVOKABLE QUrl folderOf(const QString &path) const;
    /// A picture to fill the sample deck with, painted into the data directory.
    Q_INVOKABLE QString samplePicture() const;

    Q_INVOKABLE QVariant setting(const QString &key, const QVariant &fallback) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void saveShot(QQuickWindow *window);

Q_SIGNALS:
    void darkChanged();
    void recentChanged();
    void screensChanged();
    void startedChanged();
    void shelfChanged();
    /// Something worth a word at the bottom of the window.
    void said(const QString &message);

private:
    void refreshDark();
    void remember(const QString &path);
    QString barStatePath() const;
    QString recoveryPath() const;
    void dropRecovery();
    void setStarted(bool started);

    static App *s_app;
    Store *m_store;
    Deck *m_deck;
    QFileSystemWatcher *m_watcher;
    Versions *m_versions;
    QTimer *m_autosave;
    int m_autosaved = -1;
    int m_shelf = 0;
    bool m_started = false;
    Appearance m_appearance = Appearance::Auto;
    QString m_dataDir;
    QString m_shotPath;
    QStringList m_testActions;
    QSize m_testSize = QSize(1280, 800);
    bool m_dark = false;
    bool m_glass = false;
};
