#include "app.h"

#include "builder.h"
#include "designer.h"
#include "sqlutil.h"
#include "store.h"
#include "templates.h"
#include "transfer.h"
#include "versions.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPrinterInfo>
#include <QProcess>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QStyleHints>

App *App::s_app = nullptr;

QString App::defaultDataDir()
{
    const QString moved = qEnvironmentVariable("WONDERLAND_BASES_DATA");
    return moved.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : moved;
}

App::App(const QString &dataDir, const QString &shotPath, QObject *parent)
    : QObject(parent)
    , m_dataDir(dataDir)
    , m_store(new Store(dataDir + QStringLiteral("/settings.json")))
    , m_db(new Database(this))
    , m_watcher(new QFileSystemWatcher(this))
    , m_shotPath(shotPath)
{
    s_app = this;

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

App::~App()
{
    s_app = nullptr;
    delete m_store;
}

App *App::create(QQmlEngine *, QJSEngine *)
{
    QJSEngine::setObjectOwnership(s_app, QJSEngine::CppOwnership);
    return s_app;
}

void App::setTest(const QStringList &actions, const QSize &size)
{
    m_testActions = actions;
    m_testSize = size;
}

void App::saveShot(QQuickWindow *window)
{
    const bool saved = window && window->grabWindow().save(m_shotPath);
    QCoreApplication::exit(saved ? 0 : 1);
}

void App::copy(const QString &text) const
{
    QGuiApplication::clipboard()->setText(text);
}

// --- Files --------------------------------------------------------------------

QString App::folder() const
{
    // A test never looks at the person's documents.
    if (testing() || qEnvironmentVariableIsSet("WONDERLAND_BASES_DATA")) {
        return m_dataDir;
    }
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents.isEmpty() ? QDir::homePath() : documents;
}

QString App::freePath(const QString &base) const
{
    const QDir dir(folder());
    QString name = base + QStringLiteral(".db");
    for (int n = 2; dir.exists(name); ++n) {
        name = QStringLiteral("%1 %2.db").arg(base).arg(n);
    }
    return dir.filePath(name);
}

QVariantList App::recent() const
{
    QVariantList list;
    const QJsonArray stored = m_store->value(QStringLiteral("recent")).toArray();
    for (const QJsonValue &value : stored) {
        const QFileInfo info(value.toString());
        if (info.isFile()) {
            list.append(QVariantMap{{QStringLiteral("path"), info.absoluteFilePath()},
                                    {QStringLiteral("name"), info.completeBaseName()},
                                    {QStringLiteral("folder"), QDir::toNativeSeparators(info.absolutePath()).replace(QDir::homePath(), QStringLiteral("~"))}});
        }
    }
    return list;
}

void App::remember(const QString &path)
{
    QJsonArray stored = m_store->value(QStringLiteral("recent")).toArray();
    QJsonArray next{path};
    for (const QJsonValue &value : std::as_const(stored)) {
        if (value.toString() != path && next.size() < 12) {
            next.append(value);
        }
    }
    m_store->set(QStringLiteral("recent"), next);
    Q_EMIT recentChanged();
}

void App::forgetRecent(const QString &path)
{
    QJsonArray next;
    const QJsonArray stored = m_store->value(QStringLiteral("recent")).toArray();
    for (const QJsonValue &value : stored) {
        if (value.toString() != path) {
            next.append(value);
        }
    }
    m_store->set(QStringLiteral("recent"), next);
    Q_EMIT recentChanged();
}

void App::open(const QList<QUrl> &urls)
{
    for (const QUrl &url : urls) {
        const QFileInfo info(url.isLocalFile() ? url.toLocalFile() : url.toString());
        if (openPath(info.absoluteFilePath())) {
            return;
        }
    }
}

bool App::openPath(const QString &path)
{
    const QFileInfo info(path);
    if (!m_db->open(info.absoluteFilePath())) {
        Q_EMIT said(m_db->lastError().isEmpty() ? QStringLiteral("«%1» no es una base de datos que se pueda abrir.").arg(info.fileName()) : m_db->lastError());
        return false;
    }
    remember(info.absoluteFilePath());
    if (m_db->readOnly()) {
        Q_EMIT said(QStringLiteral("«%1» está abierta solo para leer.").arg(info.fileName()));
    }
    return true;
}

bool App::createPath(const QString &path, const QString &templateId)
{
    QString target = path;
    if (QFileInfo(target).suffix().isEmpty()) {
        target += QStringLiteral(".db");
    }
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (!m_db->create(target)) {
        Q_EMIT said(m_db->lastError());
        return false;
    }
    QString error;
    if (!templateId.isEmpty() && !Templates::apply(*m_db, templateId, &error)) {
        Q_EMIT said(error);
    }
    remember(QFileInfo(target).absoluteFilePath());
    return true;
}

bool App::openSample()
{
    QDir().mkpath(m_dataDir);
    const QString path = m_dataDir + QStringLiteral("/Tienda de ejemplo.db");
    if (QFileInfo(path).isFile()) {
        return openPath(path);
    }
    if (!m_db->create(path)) {
        Q_EMIT said(m_db->lastError());
        return false;
    }
    QString error;
    if (!Templates::sample(*m_db, &error)) {
        Q_EMIT said(error);
        return false;
    }
    remember(path);
    return true;
}

void App::closeDatabase()
{
    m_db->close();
}

bool App::openWindow(const QStringList &arguments) const
{
    return QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments);
}

QString App::renameDatabase(const QString &name)
{
    if (!m_db->isOpen()) {
        return QStringLiteral("No hay ninguna base de datos abierta.");
    }
    const QString clean = name.trimmed();
    if (clean.isEmpty() || clean.contains(QLatin1Char('/')) || clean.startsWith(QLatin1Char('.'))) {
        return QStringLiteral("Ese nombre no sirve para un archivo.");
    }
    const QFileInfo old(m_db->path());
    const QString suffix = old.suffix().isEmpty() ? QStringLiteral("db") : old.suffix();
    const QString target = old.absolutePath() + QLatin1Char('/') + clean + QLatin1Char('.') + suffix;
    if (target == old.absoluteFilePath()) {
        return {};
    }
    if (QFileInfo::exists(target)) {
        return QStringLiteral("Ya hay un archivo llamado «%1» en esa carpeta.").arg(QFileInfo(target).fileName());
    }
    const QString oldVersions = Versions::directory(m_dataDir, old.absoluteFilePath());
    m_db->close();
    if (!QFile::rename(old.absoluteFilePath(), target)) {
        m_db->open(old.absoluteFilePath());
        return QStringLiteral("No se pudo cambiar el nombre del archivo.");
    }
    // Its versions go with it.
    if (QFileInfo(oldVersions).isDir()) {
        QDir().rename(oldVersions, Versions::directory(m_dataDir, target));
    }
    forgetRecent(old.absoluteFilePath());
    if (!openPath(target)) {
        return m_db->lastError();
    }
    return {};
}

// --- Versions -------------------------------------------------------------------

QString App::saveVersion()
{
    QString error;
    Versions::save(*m_db, Versions::directory(m_dataDir, m_db->path()), &error);
    Q_EMIT versionsChanged();
    return error;
}

QVariantList App::versions() const
{
    return m_db->isOpen() ? Versions::list(Versions::directory(m_dataDir, m_db->path())) : QVariantList();
}

QVariantMap App::describeVersion(const QString &path) const
{
    return Versions::describe(path);
}

QString App::restoreVersion(const QString &path)
{
    QString error;
    Versions::restore(*m_db, Versions::directory(m_dataDir, m_db->path()), path, &error);
    Q_EMIT versionsChanged();
    return error;
}

bool App::removeVersion(const QString &path)
{
    // Only what is ours to remove: a copy of the open database.
    const QString dir = Versions::directory(m_dataDir, m_db->path());
    if (!m_db->isOpen() || QFileInfo(path).absolutePath() != QFileInfo(dir).absoluteFilePath()) {
        return false;
    }
    const bool removed = QFile::remove(path);
    Q_EMIT versionsChanged();
    return removed;
}

QVariantList App::templates() const
{
    return Templates::list();
}

bool App::hasMdbTools() const
{
    return Transfer::hasMdbTools();
}

// --- Tables -------------------------------------------------------------------

QVariantMap App::tableDef(const QString &table) const
{
    return Designer::toVariant(Designer::read(*m_db, table));
}

QString App::checkTable(const QVariantMap &def) const
{
    return Designer::validate(Designer::fromVariant(def));
}

QStringList App::planTable(const QString &oldName, const QVariantMap &def) const
{
    const TableDef table = Designer::fromVariant(def);
    if (oldName.isEmpty()) {
        return {Designer::createSql(table)};
    }
    return Designer::plan(*m_db, oldName, table);
}

QString App::saveTable(const QString &oldName, const QVariantMap &def)
{
    const TableDef table = Designer::fromVariant(def);
    QString error;
    const bool done = oldName.isEmpty() ? Designer::create(*m_db, table, &error) : Designer::alter(*m_db, oldName, table, &error);
    return done ? QString() : (error.isEmpty() ? QStringLiteral("No se ha podido guardar la tabla.") : error);
}

QStringList App::tableLosses(const QString &table) const
{
    return Designer::losses(*m_db, table);
}

/// The error of a step that gave none of its own.
static QString outcome(bool done, const QString &error)
{
    return done ? QString() : (error.isEmpty() ? QStringLiteral("No se ha podido hacer.") : error);
}

QString App::dropTable(const QString &table)
{
    QString error;
    return outcome(Designer::dropTable(*m_db, table, &error), error);
}

QString App::renameTable(const QString &table, const QString &name)
{
    QString error;
    return outcome(Designer::renameTable(*m_db, table, name, &error), error);
}

QString App::createIndex(const QString &table, const QString &name, const QStringList &columns, bool unique)
{
    QString error;
    return outcome(Designer::createIndex(*m_db, table, name, columns, unique, &error), error);
}

QString App::dropIndex(const QString &name)
{
    QString error;
    return outcome(Designer::dropIndex(*m_db, name, &error), error);
}

QString App::createView(const QString &name, const QString &select)
{
    QString error;
    return outcome(Designer::createView(*m_db, name, select, &error), error);
}

QStringList App::typeNames() const
{
    return {QStringLiteral("TEXT"), QStringLiteral("INTEGER"), QStringLiteral("REAL"), QStringLiteral("NUMERIC"), QStringLiteral("BOOLEAN"),
            QStringLiteral("DATE"), QStringLiteral("DATETIME"), QStringLiteral("BLOB")};
}

// --- Queries, forms and reports -----------------------------------------------

QString App::builderSql(const QVariantMap &design) const
{
    return Builder::sql(*m_db, design);
}

QVariantList App::builderJoins(const QStringList &tables) const
{
    return Builder::joins(*m_db, tables);
}

QVariantMap App::defaultForm(const QString &table) const
{
    return Builder::defaultForm(*m_db, table);
}

QVariantMap App::defaultReport(const QString &source) const
{
    return Builder::defaultReport(*m_db, source);
}

QVariantMap App::complete(const QString &text, int cursor) const
{
    return Sql::complete(text, cursor, m_db->schema());
}

QStringList App::parameters(const QString &sql) const
{
    QStringList all;
    for (const QString &statement : Sql::split(sql)) {
        for (const QString &name : Sql::parameters(statement)) {
            if (!all.contains(name)) {
                all.append(name);
            }
        }
    }
    return all;
}

bool App::selects(const QString &sql) const
{
    return Sql::split(sql).size() == 1 && Sql::selects(sql);
}

QStringList App::statements(const QString &sql) const
{
    return Sql::split(sql);
}

bool App::makeBig(const QString &table, int rows)
{
    const QString name = Sql::ident(table);
    if (!m_db->exec(QStringLiteral("CREATE TABLE %1 (id INTEGER PRIMARY KEY, nombre TEXT, importe REAL, fecha DATE, activo BOOLEAN)").arg(name))) {
        return false;
    }
    // SQLite counts by itself: nothing is built in memory.
    const bool done = m_db->exec(QStringLiteral("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < ?) "
                                                "INSERT INTO %1 SELECT i, 'Fila ' || i, round((i * 7919 % 100000) / 100.0, 2), date('2020-01-01', '+' || (i % 2500) || ' days'), i % 3 = 0 FROM n")
                                     .arg(name),
                                 {rows});
    m_db->touch(true);
    return done;
}

QString App::kindOf(const QString &type) const
{
    return Sql::kind(type);
}

QString App::pretty(const QString &name) const
{
    return Sql::pretty(name);
}

QString App::ident(const QString &name) const
{
    return Sql::ident(name);
}

// --- In and out ---------------------------------------------------------------

QVariantMap App::previewCsv(const QString &path, const QString &separator, int header) const
{
    return Transfer::previewCsv(path, separator, header);
}

QVariantMap App::importCsv(const QString &path, const QString &table, const QVariantMap &options)
{
    return Transfer::importCsv(*m_db, path, table, options);
}

QVariantMap App::importMdb(const QString &path)
{
    return Transfer::importMdb(*m_db, path);
}

QString App::exportRows(const QString &source, const QString &name, const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    QString error;
    bool done = false;
    if (suffix == QLatin1String("json")) {
        done = Transfer::exportJson(*m_db, source, path, &error);
    } else if (suffix == QLatin1String("sql")) {
        done = m_db->exists(source) && !m_db->isView(source) ? Transfer::exportSql(*m_db, {source}, path, &error)
                                                             : Transfer::exportQuerySql(*m_db, m_db->exists(source) ? QStringLiteral("SELECT * FROM ") + Sql::ident(source) : source,
                                                                                        name.isEmpty() ? QStringLiteral("datos") : name, path, &error);
    } else {
        done = Transfer::exportCsv(*m_db, source, path, suffix == QLatin1String("tsv") ? QLatin1Char('\t') : QLatin1Char(','), &error);
    }
    return outcome(done, error);
}

QString App::exportDump(const QString &path)
{
    QString error;
    return outcome(Transfer::exportSql(*m_db, {}, path, &error), error);
}

QStringList App::printers() const
{
    return QPrinterInfo::availablePrinterNames();
}

// --- The desktop ----------------------------------------------------------------

QString App::barStatePath() const
{
    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty()) {
        state = QDir::homePath() + QStringLiteral("/.local/state");
    }
    return state + QStringLiteral("/wonderlandwm/bar.json");
}

void App::setAppearance(Appearance appearance)
{
    m_appearance = appearance;
    refreshDark();
}

void App::refreshDark()
{
    bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    bool glass = desktop.contains(QLatin1String("driftwm")) || desktop.contains(QLatin1String("wonderland"));
    // WonderlandWM's bar owns the light/dark and glass switches for the whole desktop.
    QFile file(barStatePath());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject state = QJsonDocument::fromJson(file.readAll()).object();
        const QString mode = state.value(QLatin1String("themeMode")).toString();
        if (!mode.isEmpty()) {
            dark = mode == QLatin1String("dark");
        }
        glass = glass && state.value(QLatin1String("glass")).toBool(true);
    }
    if (m_appearance != Appearance::Auto) {
        dark = m_appearance == Appearance::Dark;
    }
    if (qEnvironmentVariableIsSet("WONDERLAND_BASES_GLASS")) {
        glass = qEnvironmentVariableIntValue("WONDERLAND_BASES_GLASS") != 0;
    }
    if (dark != m_dark || glass != m_glass) {
        m_dark = dark;
        m_glass = glass;
        Q_EMIT darkChanged();
    }
}

QVariant App::setting(const QString &key, const QVariant &fallback) const
{
    const QJsonValue value = m_store->value(key);
    return value.isUndefined() || value.isNull() ? fallback : value.toVariant();
}

void App::setSetting(const QString &key, const QVariant &value)
{
    m_store->set(key, QJsonValue::fromVariant(value));
}
