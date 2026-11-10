#pragma once

#include "session.h"

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

/// What the window needs from outside itself: the document and the file it
/// lives in, the library, the recent ones, the versions, the desktop's
/// light/dark and glass switches, the clipboard and the files that go out.
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
    /// A document is open (false: the start screen).
    Q_PROPERTY(bool open READ isOpen NOTIFY documentChanged)
    /// The file of the document; empty until it is first saved.
    Q_PROPERTY(QString path READ path NOTIFY documentChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    /// The LaTeX of each equation, in order.
    Q_PROPERTY(QStringList equations READ equations NOTIFY changed)
    Q_PROPERTY(int current READ current WRITE setCurrent NOTIFY changed)
    /// The equation in hand.
    Q_PROPERTY(QString latex READ latex WRITE setLatex NOTIFY changed)
    /// What the parser could not understand of it: [{position, length, message, line, column}].
    Q_PROPERTY(QVariantList errors READ errors NOTIFY changed)
    /// "#rrggbb", or empty for the colour of the text around it.
    Q_PROPERTY(QString color READ color WRITE setColor NOTIFY changed)
    /// In points.
    Q_PROPERTY(double size READ size WRITE setSize NOTIFY changed)
    /// The math font asked for (empty: the best installed) and the one in use.
    Q_PROPERTY(QString font READ font WRITE setFont NOTIFY changed)
    Q_PROPERTY(QString fontInUse READ fontInUse NOTIFY changed)
    Q_PROPERTY(QStringList fonts READ fonts CONSTANT)
    /// Grows with every change to the library: what shows it reads it again.
    Q_PROPERTY(int libraryRevision READ libraryRevision NOTIFY libraryChanged)
    /// [{path, name, folder, when}], newest first.
    Q_PROPERTY(QVariantList recents READ recents NOTIFY recentsChanged)
    /// [{path, label}], newest first.
    Q_PROPERTY(QVariantList versions READ versions NOTIFY versionsChanged)
    /// Unsaved work found from a run that ended badly: [{path, title, when}].
    Q_PROPERTY(QVariantList recoveries READ recoveries NOTIFY recoveriesChanged)

public:
    enum class Appearance { Auto, Light, Dark };

    /// `shotPath` set: a test run, which quits after one picture.
    App(const QString &dataDir, const QString &shotPath, QObject *parent = nullptr);
    ~App() override;
    static App *create(QQmlEngine *, QJSEngine *);
    /// Where the settings, the library, the versions and the recovery copies
    /// live (WONDERLAND_FORMULAS_DATA moves it).
    static QString defaultDataDir();

    bool dark() const { return m_dark; }
    bool glass() const { return m_glass; }
    void setAppearance(Appearance appearance);
    bool testing() const { return !m_shotPath.isEmpty(); }
    QSize testSize() const { return m_testSize; }
    QStringList testActions() const { return m_testActions; }
    void setTest(const QStringList &actions, const QSize &size);
    bool isOpen() const { return m_open; }
    QString path() const { return m_path; }
    QString dataDir() const { return m_dataDir; }
    const FormulaDoc &document() const { return m_session.document(); }

    QString title() const { return m_session.document().title; }
    void setTitle(const QString &title);
    bool dirty() const { return m_session.dirty(); }
    bool canUndo() const { return m_session.canUndo(); }
    bool canRedo() const { return m_session.canRedo(); }
    QStringList equations() const { return m_session.document().equations; }
    int current() const { return m_session.current(); }
    void setCurrent(int index);
    QString latex() const { return m_session.latex(); }
    void setLatex(const QString &latex);
    QVariantList errors() const;
    QString color() const { return m_session.document().color; }
    void setColor(const QString &color);
    double size() const { return m_session.document().size; }
    void setSize(double points);
    QString font() const { return m_session.document().font; }
    void setFont(const QString &family);
    QString fontInUse() const;
    QStringList fonts() const;
    int libraryRevision() const { return m_libraryRevision; }
    QVariantList recents() const;
    QVariantList versions() const;
    QVariantList recoveries() const;

    /// What a new document can start from: [{key, name, detail, latex}].
    Q_INVOKABLE QVariantList templates() const;
    /// «blank», «sample», a template key or the id of a formula of the library.
    Q_INVOKABLE void newDocument(const QString &key);
    Q_INVOKABLE bool openPath(const QString &path);
    Q_INVOKABLE bool openUrl(const QUrl &url) { return openPath(url.toLocalFile()); }
    /// Another process for another document; no path, its start screen.
    Q_INVOKABLE void openInNewWindow(const QString &path);
    /// False when the document has no file it can be written back to: ask for one and call saveAs.
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl &url);
    /// Back to the start screen.
    Q_INVOKABLE void closeDocument();
    Q_INVOKABLE void restoreVersion(const QString &path);
    Q_INVOKABLE void recover(const QString &path);
    Q_INVOKABLE void dropRecoveries();
    Q_INVOKABLE void forgetRecent(const QString &path);

    Q_INVOKABLE void addEquation(const QString &latex = QString());
    Q_INVOKABLE void duplicateEquation();
    Q_INVOKABLE void removeEquation(int index);
    Q_INVOKABLE void moveEquation(int from, int to);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    /// The typing so far is one step to undo; what comes next is another.
    Q_INVOKABLE void breakRun() { m_session.breakRun(); }

    /// The palette of the sidebar: [{id, title, items: [{latex, insert, title, names, glyph}]}].
    Q_INVOKABLE QVariantList palette(const QString &query) const;
    /// What may follow a backslash: [{command, insert, title, glyph, latex}].
    Q_INVOKABLE QVariantList completions(const QString &prefix) const;
    Q_INVOKABLE int matchingBrace(const QString &source, int position) const;
    /// [{id, title, own, entries: [{id, name, latex, own}]}].
    Q_INVOKABLE QVariantList library(const QString &query) const;
    /// Keeps the equation in hand under a name.
    Q_INVOKABLE void saveToLibrary(const QString &name);
    Q_INVOKABLE void removeFromLibrary(const QString &id);

    /// «latex», «mathml», «svg» or «png» (at `scale`) of the equation in hand, to the clipboard.
    Q_INVOKABLE bool copyAs(const QString &kind, double scale = 2);
    /// What `copyAs` would put there as text; for the inspector and the tests.
    Q_INVOKABLE QString textAs(const QString &kind) const;
    /// png, svg and mml are the equation in hand; pdf, odf and tex the document.
    Q_INVOKABLE bool exportTo(const QUrl &url, const QString &format, double scale, bool transparent);
    /// A name to offer for a file that goes out.
    Q_INVOKABLE QUrl suggestedUrl(const QString &suffix) const;
    Q_INVOKABLE QString localPath(const QUrl &url) const { return url.toLocalFile(); }
    Q_INVOKABLE QString fileName(const QString &path) const;

    Q_INVOKABLE QVariant setting(const QString &key, const QVariant &fallback) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void saveShot(QQuickWindow *window);
    Q_INVOKABLE void say(const QString &message) { Q_EMIT said(message); }

Q_SIGNALS:
    void darkChanged();
    void documentChanged();
    /// Anything of the document: its equations, the one in hand, how it is drawn.
    void changed();
    void libraryChanged();
    void recentsChanged();
    void versionsChanged();
    void recoveriesChanged();
    /// Something worth a word at the bottom of the window.
    void said(const QString &message);

private:
    void refreshDark();
    QString barStatePath() const;
    void adopt(FormulaDoc doc, const QString &path);
    void remember(const QString &path);
    void autosave();
    void touched();

    static App *s_app;
    QString m_dataDir;
    Store *m_store;
    Session m_session;
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
    int m_libraryRevision = 0;
};

/// `image://formula/file/<rrggbb>/<path>`: the equations of a file, one under another.
class FormulaProvider : public QQuickImageProvider
{
public:
    FormulaProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
