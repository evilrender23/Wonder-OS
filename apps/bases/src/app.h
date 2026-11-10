#pragma once

#include "database.h"

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

/// What the window needs from outside itself: the database that is open, the
/// modules that work on it, the desktop's light/dark and glass switches, and
/// the files it is asked to open.
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
    Q_PROPERTY(Database *db READ db CONSTANT)
    /// The files opened lately that are still there: [{path, name, folder}]
    Q_PROPERTY(QVariantList recent READ recent NOTIFY recentChanged)
    /// [{id, name, description, symbol}]
    Q_PROPERTY(QVariantList templates READ templates CONSTANT)
    /// mdb-tools is installed: Access files can be imported.
    Q_PROPERTY(bool hasMdbTools READ hasMdbTools CONSTANT)
    /// Where databases are kept unless the person says otherwise.
    Q_PROPERTY(QString folder READ folder CONSTANT)

public:
    enum class Appearance { Auto, Light, Dark };

    /// `shotPath` set: a test run, which quits after one picture.
    App(const QString &dataDir, const QString &shotPath, QObject *parent = nullptr);
    ~App() override;
    static App *create(QQmlEngine *, QJSEngine *);
    /// Where the settings live (WONDERLAND_BASES_DATA moves it).
    static QString defaultDataDir();

    bool dark() const { return m_dark; }
    bool glass() const { return m_glass; }
    void setAppearance(Appearance appearance);
    bool testing() const { return !m_shotPath.isEmpty(); }
    QSize testSize() const { return m_testSize; }
    QStringList testActions() const { return m_testActions; }
    void setTest(const QStringList &actions, const QSize &size);
    Database *db() const { return m_db; }
    QVariantList recent() const;
    QVariantList templates() const;
    bool hasMdbTools() const;
    QString folder() const;

    // --- Files ------------------------------------------------------------------
    /// The first database among the files given is opened.
    Q_INVOKABLE void open(const QList<QUrl> &urls);
    Q_INVOKABLE bool openPath(const QString &path);
    /// A new file, empty or made from a template («» for none).
    Q_INVOKABLE bool createPath(const QString &path, const QString &templateId = {});
    /// The shop to try things on, in the data directory.
    Q_INVOKABLE bool openSample();
    Q_INVOKABLE void closeDatabase();
    /// Another window, which is another process, with these arguments.
    Q_INVOKABLE bool openWindow(const QStringList &arguments) const;
    /// Gives the file of the open database another name. Returns the error, or «».
    Q_INVOKABLE QString renameDatabase(const QString &name);

    // --- Versions -----------------------------------------------------------------
    /// Keeps a copy of the database as it is now. Returns the error, or «».
    Q_INVOKABLE QString saveVersion();
    /// The copies kept of the open database, the latest first: [{path, label, size}]
    Q_INVOKABLE QVariantList versions() const;
    /// {ok, error, tables: [{name, rows}], rows, objects}
    Q_INVOKABLE QVariantMap describeVersion(const QString &path) const;
    Q_INVOKABLE QString restoreVersion(const QString &path);
    Q_INVOKABLE bool removeVersion(const QString &path);
    Q_INVOKABLE void forgetRecent(const QString &path);
    Q_INVOKABLE QString localPath(const QUrl &url) const { return url.toLocalFile(); }
    Q_INVOKABLE QUrl fileUrl(const QString &path) const { return QUrl::fromLocalFile(path); }
    /// A name no file in the folder has: «Sin título.db», «Sin título 2.db»…
    Q_INVOKABLE QString freePath(const QString &base) const;

    // --- Tables ------------------------------------------------------------------
    /// A table as the designer edits it: {name, columns: [{name, type, …}], …}
    Q_INVOKABLE QVariantMap tableDef(const QString &table) const;
    /// What is wrong with a definition; empty when nothing is.
    Q_INVOKABLE QString checkTable(const QVariantMap &def) const;
    /// The statements saving would run.
    Q_INVOKABLE QStringList planTable(const QString &oldName, const QVariantMap &def) const;
    /// Creates (`oldName` empty) or alters a table. Returns the error, or «».
    Q_INVOKABLE QString saveTable(const QString &oldName, const QVariantMap &def);
    Q_INVOKABLE QStringList tableLosses(const QString &table) const;
    Q_INVOKABLE QString dropTable(const QString &table);
    Q_INVOKABLE QString renameTable(const QString &table, const QString &name);
    Q_INVOKABLE QString createIndex(const QString &table, const QString &name, const QStringList &columns, bool unique);
    Q_INVOKABLE QString dropIndex(const QString &name);
    Q_INVOKABLE QString createView(const QString &name, const QString &select);
    Q_INVOKABLE QStringList typeNames() const;

    // --- Queries, forms and reports -------------------------------------------------
    Q_INVOKABLE QString builderSql(const QVariantMap &design) const;
    Q_INVOKABLE QVariantList builderJoins(const QStringList &tables) const;
    Q_INVOKABLE QVariantMap defaultForm(const QString &table) const;
    Q_INVOKABLE QVariantMap defaultReport(const QString &source) const;
    /// {start, prefix, items: [{text, kind}]}
    Q_INVOKABLE QVariantMap complete(const QString &text, int cursor) const;
    Q_INVOKABLE QStringList parameters(const QString &sql) const;
    /// One statement that returns rows: a grid can page through it.
    Q_INVOKABLE bool selects(const QString &sql) const;
    Q_INVOKABLE QStringList statements(const QString &sql) const;
    /// A table of `rows` made-up rows, to see a big one scroll (tests).
    Q_INVOKABLE bool makeBig(const QString &table, int rows);
    Q_INVOKABLE QString kindOf(const QString &type) const;
    Q_INVOKABLE QString pretty(const QString &name) const;
    Q_INVOKABLE QString ident(const QString &name) const;

    // --- In and out --------------------------------------------------------------
    Q_INVOKABLE QVariantMap previewCsv(const QString &path, const QString &separator = {}, int header = -1) const;
    Q_INVOKABLE QVariantMap importCsv(const QString &path, const QString &table, const QVariantMap &options);
    Q_INVOKABLE QVariantMap importMdb(const QString &path);
    /// By the suffix of `path`: .csv, .tsv, .json or .sql. `source` is a table
    /// or a SELECT. Returns the error, or «».
    Q_INVOKABLE QString exportRows(const QString &source, const QString &name, const QString &path);
    /// The whole database as a script.
    Q_INVOKABLE QString exportDump(const QString &path);
    Q_INVOKABLE QStringList printers() const;

    Q_INVOKABLE QVariant setting(const QString &key, const QVariant &fallback) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void saveShot(QQuickWindow *window);
    Q_INVOKABLE void copy(const QString &text) const;

Q_SIGNALS:
    void darkChanged();
    void recentChanged();
    void versionsChanged();
    /// Something worth a word at the bottom of the window.
    void said(const QString &message);

private:
    void refreshDark();
    void remember(const QString &path);
    QString barStatePath() const;

    static App *s_app;
    QString m_dataDir;
    Store *m_store;
    Database *m_db;
    QFileSystemWatcher *m_watcher;
    Appearance m_appearance = Appearance::Auto;
    QString m_shotPath;
    QStringList m_testActions;
    QSize m_testSize = QSize(1180, 740);
    bool m_dark = false;
    bool m_glass = false;
};
