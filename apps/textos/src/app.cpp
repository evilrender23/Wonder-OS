#include "app.h"

#include "formats.h"
#include "store.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTextDocument>
#include <QTimer>

namespace {
constexpr int maxRecents = 12;
constexpr int maxVersions = 20;

QString whenText(const QDateTime &when)
{
    const QDate today = QDate::currentDate();
    const QLocale locale(QLocale::Spanish, QLocale::Spain);
    if (when.date() == today) {
        return QStringLiteral("Hoy, %1").arg(locale.toString(when.time(), QStringLiteral("H:mm")));
    }
    if (when.date() == today.addDays(-1)) {
        return QStringLiteral("Ayer, %1").arg(locale.toString(when.time(), QStringLiteral("H:mm")));
    }
    return locale.toString(when, when.date().year() == today.year() ? QStringLiteral("d 'de' MMMM, H:mm") : QStringLiteral("d 'de' MMMM 'de' yyyy"));
}
} // namespace

App *App::s_app = nullptr;

QString App::defaultDataDir()
{
    const QString moved = qEnvironmentVariable("WONDERLAND_TEXTOS_DATA");
    return moved.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : moved;
}

App::App(const QString &dataDir, const QString &shotPath, QObject *parent)
    : QObject(parent)
    , m_dataDir(dataDir)
    , m_store(new Store(dataDir + QStringLiteral("/settings.json")))
    , m_editor(new Editor(this))
    , m_watcher(new QFileSystemWatcher(this))
    , m_autosave(new QTimer(this))
    , m_shotPath(shotPath)
{
    s_app = this;
    QDir().mkpath(m_dataDir);

    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &App::refreshDark);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &App::refreshDark);
    // The bar rewrites its state file by rename, which drops a file watch:
    // watching the directory catches the replacement.
    const QString barDir = QFileInfo(barStatePath()).absolutePath();
    if (QFileInfo(barDir).isDir()) {
        m_watcher->addPath(barDir);
    }
    refreshDark();

    connect(m_editor, &Editor::pathChanged, this, [this] {
        Q_EMIT titleChanged();
        Q_EMIT versionsChanged();
    });
    connect(m_editor, &Editor::said, this, &App::said);
    connect(m_autosave, &QTimer::timeout, this, &App::autosave);
    m_autosave->start(15000);
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

// --- Documents ----------------------------------------------------------------

QString App::title() const
{
    if (!m_editor->path().isEmpty()) {
        return QFileInfo(m_editor->path()).completeBaseName();
    }
    return m_name.isEmpty() ? QStringLiteral("Sin título") : m_name;
}

void App::setStarted(bool started)
{
    if (started != m_started) {
        m_started = started;
        Q_EMIT startedChanged();
    }
}

QVariantList App::templates() const
{
    const auto one = [](const char *id, const char *name, const char *detail) {
        return QVariantMap{{QStringLiteral("id"), QString::fromUtf8(id)}, {QStringLiteral("name"), QString::fromUtf8(name)}, {QStringLiteral("detail"), QString::fromUtf8(detail)}};
    };
    return {one("blank", "En blanco", "Una hoja vacía"), one("letter", "Carta", "Remite, fecha y firma"), one("resume", "Currículum", "Experiencia y formación"),
            one("report", "Informe", "Portada y secciones"), one("minutes", "Acta", "Asistentes y acuerdos")};
}

void App::start(const QString &kind)
{
    dropRecovery();
    m_name.clear();
    m_editor->newDocument(kind);
    m_savedRevision = m_editor->revision();
    setStarted(true);
    Q_EMIT titleChanged();
    Q_EMIT versionsChanged();
}

bool App::open(const QString &path)
{
    const QString file = QFileInfo(path).absoluteFilePath();
    if (m_started && (m_editor->modified() || !m_editor->path().isEmpty())) {
        // This window has its document: the other gets a window of its own.
        openWindow(file);
        return true;
    }
    if (!m_editor->load(file)) {
        forgetRecent(file);
        return false;
    }
    dropRecovery();
    m_name.clear();
    m_savedRevision = m_editor->revision();
    remember(file);
    setStarted(true);
    Q_EMIT titleChanged();
    Q_EMIT versionsChanged();
    return true;
}

void App::openWindow(const QString &path)
{
    if (testing()) {
        return;
    }
    QStringList arguments;
    if (!path.isEmpty()) {
        arguments << path;
    } else {
        arguments << QStringLiteral("--new");
    }
    QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments);
}

void App::closeDocument()
{
    dropRecovery();
    m_name.clear();
    m_editor->newDocument(QStringLiteral("blank"));
    m_savedRevision = m_editor->revision();
    setStarted(false);
    Q_EMIT titleChanged();
    Q_EMIT versionsChanged();
    Q_EMIT recoveredChanged();
}

bool App::save()
{
    if (m_editor->path().isEmpty()) {
        return false;
    }
    return saveAs(m_editor->path());
}

bool App::saveAs(const QString &path)
{
    QString file = path;
    if (QFileInfo(file).suffix().isEmpty()) {
        file += QStringLiteral(".odt");
    }
    if (Formats::kindOf(file) == Formats::Kind::Pdf) {
        return exportTo(file);
    }
    if (!m_editor->saveAs(file)) {
        return false;
    }
    m_name.clear();
    dropRecovery();
    m_savedRevision = m_editor->revision();
    remember(file);
    keepVersion(file);
    Q_EMIT titleChanged();
    Q_EMIT said(QStringLiteral("Guardado en «%1».").arg(QFileInfo(file).fileName()));
    return true;
}

bool App::exportTo(const QString &path)
{
    if (!m_editor->exportTo(path)) {
        return false;
    }
    Q_EMIT said(QStringLiteral("Exportado a «%1».").arg(QFileInfo(path).fileName()));
    return true;
}

bool App::rename(const QString &name)
{
    QString clean = name.trimmed();
    clean.remove(QLatin1Char('/'));
    if (clean.isEmpty() || clean == title()) {
        return false;
    }
    const QString old = m_editor->path();
    if (old.isEmpty()) {
        m_name = clean;
        Q_EMIT titleChanged();
        return true;
    }
    const QFileInfo info(old);
    const QString target = info.absolutePath() + QLatin1Char('/') + clean + QLatin1Char('.') + info.suffix();
    if (QFileInfo::exists(target)) {
        Q_EMIT said(QStringLiteral("Ya hay un «%1» en esa carpeta.").arg(QFileInfo(target).fileName()));
        return false;
    }
    // The versions are kept by the file's address: they move with it.
    const QString versionsBefore = versionsDir(old);
    if (!m_editor->saveAs(target)) {
        return false;
    }
    QFile::remove(old);
    QDir().rename(versionsBefore, versionsDir(target));
    forgetRecent(old);
    remember(target);
    m_savedRevision = m_editor->revision();
    dropRecovery();
    Q_EMIT titleChanged();
    Q_EMIT versionsChanged();
    return true;
}

QUrl App::suggestedUrl(const QString &extension) const
{
    QString folder = m_editor->path().isEmpty() ? setting(QStringLiteral("folder"), QString()).toString() : QFileInfo(m_editor->path()).absolutePath();
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
        folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    return QUrl::fromLocalFile(folder + QLatin1Char('/') + title() + QLatin1Char('.') + extension);
}

// --- Recent documents -----------------------------------------------------------

QVariantList App::recents() const
{
    QVariantList list;
    const QJsonArray stored = m_store->value(QStringLiteral("recents")).toArray();
    for (const QJsonValue &value : stored) {
        const QFileInfo info(value.toString());
        if (!info.isFile()) {
            continue;
        }
        QString folder = info.absolutePath();
        const QString home = QDir::homePath();
        if (folder == home) {
            folder = QStringLiteral("Carpeta personal");
        } else if (folder.startsWith(home + QLatin1Char('/'))) {
            folder = folder.mid(home.size() + 1);
        }
        list << QVariantMap{{QStringLiteral("path"), info.absoluteFilePath()},
                            {QStringLiteral("name"), info.completeBaseName()},
                            {QStringLiteral("kind"), info.suffix().toLower()},
                            {QStringLiteral("folder"), folder},
                            {QStringLiteral("when"), whenText(info.lastModified())}};
    }
    return list;
}

void App::remember(const QString &path)
{
    QJsonArray stored = m_store->value(QStringLiteral("recents")).toArray();
    QJsonArray next;
    next.append(path);
    for (const QJsonValue &value : std::as_const(stored)) {
        if (value.toString() != path && next.size() < maxRecents) {
            next.append(value);
        }
    }
    m_store->set(QStringLiteral("recents"), next);
    m_store->set(QStringLiteral("folder"), QFileInfo(path).absolutePath());
    Q_EMIT recentsChanged();
}

void App::forgetRecent(const QString &path)
{
    QJsonArray next;
    const QJsonArray stored = m_store->value(QStringLiteral("recents")).toArray();
    for (const QJsonValue &value : stored) {
        if (value.toString() != path) {
            next.append(value);
        }
    }
    m_store->set(QStringLiteral("recents"), next);
    Q_EMIT recentsChanged();
}

// --- Recovery -------------------------------------------------------------------

QString App::recoveryFile() const
{
    return m_dataDir + QStringLiteral("/recovery/%1.odt").arg(QCoreApplication::applicationPid());
}

void App::autosave()
{
    if (testing() && !qEnvironmentVariableIsSet("WONDERLAND_TEXTOS_AUTOSAVE")) {
        return;
    }
    if (!m_started || !m_editor->modified() || m_editor->revision() == m_savedRevision) {
        return;
    }
    const QString file = recoveryFile();
    QDir().mkpath(QFileInfo(file).absolutePath());
    if (Formats::save(file, m_editor->document(), m_editor->setup())) {
        Store::write(file + QStringLiteral(".json"), QJsonObject{{QStringLiteral("path"), m_editor->path()}, {QStringLiteral("name"), title()}});
        m_savedRevision = m_editor->revision();
    }
}

void App::dropRecovery()
{
    QFile::remove(recoveryFile());
    QFile::remove(recoveryFile() + QStringLiteral(".json"));
}

void App::closing()
{
    dropRecovery();
}

QVariantList App::recovered() const
{
    QVariantList list;
    const QDir dir(m_dataDir + QStringLiteral("/recovery"));
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.odt")}, QDir::Files, QDir::Time);
    for (const QFileInfo &info : files) {
        // A copy whose Textos is still running is not lost.
        const qint64 pid = info.completeBaseName().toLongLong();
        if (pid == QCoreApplication::applicationPid() || QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid))) {
            continue;
        }
        const QJsonObject about = Store::read(info.absoluteFilePath() + QStringLiteral(".json"));
        list << QVariantMap{{QStringLiteral("file"), info.absoluteFilePath()},
                            {QStringLiteral("name"), about.value(QLatin1String("name")).toString(QStringLiteral("Sin título"))},
                            {QStringLiteral("when"), whenText(info.lastModified())}};
    }
    return list;
}

bool App::openRecovered(const QString &file)
{
    const QJsonObject about = Store::read(file + QStringLiteral(".json"));
    const QString path = about.value(QLatin1String("path")).toString();
    if (!m_editor->loadAs(file, path)) {
        return false;
    }
    m_name = path.isEmpty() ? about.value(QLatin1String("name")).toString() : QString();
    // What was recovered has not been saved: it stays to be.
    m_editor->document()->setModified(true);
    Q_EMIT m_editor->modifiedChanged();
    discardRecovered(file);
    m_savedRevision = -1;
    setStarted(true);
    Q_EMIT titleChanged();
    Q_EMIT versionsChanged();
    return true;
}

void App::discardRecovered(const QString &file)
{
    QFile::remove(file);
    QFile::remove(file + QStringLiteral(".json"));
    Q_EMIT recoveredChanged();
}

// --- Versions -------------------------------------------------------------------

QString App::versionsDir(const QString &path) const
{
    const QByteArray id = QCryptographicHash::hash(QFileInfo(path).absoluteFilePath().toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return m_dataDir + QStringLiteral("/versions/") + QString::fromLatin1(id);
}

void App::keepVersion(const QString &path)
{
    const QString dir = versionsDir(path);
    QDir().mkpath(dir);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    QFile::copy(path, dir + QLatin1Char('/') + stamp + QLatin1Char('.') + QFileInfo(path).suffix());
    const QFileInfoList all = QDir(dir).entryInfoList(QDir::Files, QDir::Name | QDir::Reversed);
    for (int i = maxVersions; i < all.size(); ++i) {
        QFile::remove(all.at(i).absoluteFilePath());
    }
    Q_EMIT versionsChanged();
}

QVariantList App::versions() const
{
    QVariantList list;
    if (m_editor->path().isEmpty()) {
        return list;
    }
    const QFileInfoList all = QDir(versionsDir(m_editor->path())).entryInfoList(QDir::Files, QDir::Name | QDir::Reversed);
    for (const QFileInfo &info : all) {
        const QDateTime when = QDateTime::fromString(info.completeBaseName(), QStringLiteral("yyyyMMdd-HHmmss-zzz"));
        const double kilobytes = info.size() / 1024.0;
        list << QVariantMap{{QStringLiteral("file"), info.absoluteFilePath()},
                            {QStringLiteral("when"), whenText(when.isValid() ? when : info.lastModified())},
                            {QStringLiteral("time"), QLocale(QLocale::Spanish, QLocale::Spain).toString(when.isValid() ? when.time() : info.lastModified().time(), QStringLiteral("H:mm:ss"))},
                            {QStringLiteral("size"), kilobytes < 1024 ? QStringLiteral("%1 KB").arg(qMax(1, qRound(kilobytes))) : QStringLiteral("%1 MB").arg(kilobytes / 1024, 0, 'f', 1)}};
    }
    return list;
}

QString App::versionPreview(const QString &file) const
{
    QTextDocument document;
    Doc::PageSetup setup;
    if (!Formats::load(file, &document, &setup)) {
        return {};
    }
    return Formats::toPlainText(&document).left(1200);
}

QVariantMap App::versionFacts(const QString &file) const
{
    Editor other;
    if (!other.loadAs(file, QString())) {
        return {};
    }
    return {{QStringLiteral("words"), other.counts().value(QStringLiteral("words"))}, {QStringLiteral("pages"), other.pageCount()}};
}

bool App::restoreVersion(const QString &file)
{
    const QString path = m_editor->path();
    if (path.isEmpty() || !QFileInfo(file).isFile()) {
        return false;
    }
    // What is on the sheets now is kept too, so restoring can itself be gone back on.
    if (m_editor->modified()) {
        const QString now = m_dataDir + QStringLiteral("/versions/now.") + QFileInfo(path).suffix();
        QDir().mkpath(QFileInfo(now).absolutePath());
        if (Formats::save(now, m_editor->document(), m_editor->setup())) {
            keepVersion(now);
            const QFileInfoList kept = QDir(versionsDir(now)).entryInfoList(QDir::Files);
            QDir().mkpath(versionsDir(path));
            for (const QFileInfo &info : kept) {
                QFile::rename(info.absoluteFilePath(), versionsDir(path) + QLatin1Char('/') + info.fileName());
            }
            QDir(versionsDir(now)).removeRecursively();
            QFile::remove(now);
        }
    }
    if (!m_editor->loadAs(file, path)) {
        return false;
    }
    m_editor->document()->setModified(true);
    Q_EMIT m_editor->modifiedChanged();
    m_savedRevision = -1;
    Q_EMIT versionsChanged();
    Q_EMIT said(QStringLiteral("Versión restaurada. Guarda para conservarla."));
    return true;
}

// --- Odds and ends --------------------------------------------------------------

QStringList App::families() const
{
    QStringList families;
    const QStringList all = QFontDatabase::families();
    for (const QString &family : all) {
        // Leave out the private faces and the ones that only hold symbols.
        if (!family.startsWith(QLatin1Char('.')) && !QFontDatabase::isPrivateFamily(family) && QFontDatabase::writingSystems(family).contains(QFontDatabase::Latin)) {
            families << family;
        }
    }
    return families;
}

QString App::today() const
{
    return QLocale(QLocale::Spanish, QLocale::Spain).toString(QDate::currentDate(), QStringLiteral("d 'de' MMMM 'de' yyyy"));
}

void App::openLink(const QString &href) const
{
    if (!testing() && !href.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromUserInput(href));
    }
}

QString App::fold(const QString &text) const
{
    QString folded;
    const QString decomposed = text.normalized(QString::NormalizationForm_D).toLower();
    for (const QChar c : decomposed) {
        if (c.category() != QChar::Mark_NonSpacing) {
            folded += c;
        }
    }
    return folded;
}

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
    if (qEnvironmentVariableIsSet("WONDERLAND_TEXTOS_GLASS")) {
        glass = qEnvironmentVariableIntValue("WONDERLAND_TEXTOS_GLASS") != 0;
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
