#include "app.h"

#include "odp.h"
#include "pptx.h"
#include "store.h"
#include "versions.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QImage>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QQuickWindow>
#include <QScreen>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTimer>

App *App::s_app = nullptr;

QString App::defaultDataDir()
{
    const QString moved = qEnvironmentVariable("WONDERLAND_DIAPOSITIVAS_DATA");
    return moved.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : moved;
}

App::App(const QString &dataDir, const QString &shotPath, QObject *parent)
    : QObject(parent)
    , m_store(new Store(dataDir + QStringLiteral("/settings.json")))
    , m_deck(new Deck(this))
    , m_watcher(new QFileSystemWatcher(this))
    , m_versions(new Versions(dataDir + QStringLiteral("/versions")))
    , m_autosave(new QTimer(this))
    , m_dataDir(dataDir)
    , m_shotPath(shotPath)
{
    s_app = this;
    m_deck->reset();

    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &App::refreshDark);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &App::refreshDark);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &App::screensChanged);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &App::screensChanged);
    // The bar rewrites its state file by rename, which drops a file watch:
    // watching the directory catches the replacement.
    const QString barDir = QFileInfo(barStatePath()).absolutePath();
    if (QFileInfo(barDir).isDir()) {
        m_watcher->addPath(barDir);
    }
    refreshDark();

    // What is not saved yet is kept aside every few seconds, for after a crash.
    m_autosave->setInterval(15000);
    connect(m_autosave, &QTimer::timeout, this, &App::autosave);
    m_autosave->start();
    connect(qGuiApp, &QCoreApplication::aboutToQuit, this, &App::dropRecovery);
}

App::~App()
{
    s_app = nullptr;
    delete m_versions;
    delete m_store;
}

void App::setStarted(bool started)
{
    if (started != m_started) {
        m_started = started;
        Q_EMIT startedChanged();
    }
}

void App::start(const QString &theme, const QString &aspect)
{
    m_deck->reset(theme, aspect);
    setStarted(true);
}

App *App::create(QQmlEngine *, QJSEngine *)
{
    QJSEngine::setObjectOwnership(s_app, QJSEngine::CppOwnership);
    return s_app;
}

// --- Files -------------------------------------------------------------------------

void App::open(const QList<QUrl> &urls)
{
    bool first = true;
    for (const QUrl &url : urls) {
        const QString path = QFileInfo(url.isLocalFile() ? url.toLocalFile() : url.toString()).absoluteFilePath();
        // One window, one presentation: the one here stays if it has been touched.
        if (first && !m_deck->modified() && m_deck->path().isEmpty()) {
            openPath(path);
        } else {
            launch(path);
        }
        first = false;
    }
}

bool App::openPath(const QString &path)
{
    if (!m_deck->load(path)) {
        Q_EMIT said(m_deck->error().isEmpty() ? QStringLiteral("«%1» no se ha podido abrir.").arg(QFileInfo(path).fileName()) : m_deck->error());
        return false;
    }
    remember(path);
    setStarted(true);
    return true;
}

bool App::rename(const QString &name)
{
    const QString clean = name.trimmed();
    if (clean.isEmpty() || clean.contains(QLatin1Char('/'))) {
        return false;
    }
    const QString old = m_deck->path();
    if (old.isEmpty()) {
        m_deck->setName(clean);
        return true;
    }
    const QFileInfo info(old);
    if (clean == info.completeBaseName()) {
        return true;
    }
    const QString target = info.absolutePath() + QLatin1Char('/') + clean + QLatin1Char('.') + info.suffix();
    if (QFileInfo::exists(target)) {
        Q_EMIT said(QStringLiteral("Ya hay un archivo llamado «%1».").arg(QFileInfo(target).fileName()));
        return false;
    }
    if (m_deck->modified()) {
        // The file on disk is older than the window: write under the new name and drop the old one.
        if (!m_deck->save(target)) {
            Q_EMIT said(QStringLiteral("No se ha podido renombrar."));
            return false;
        }
        QFile::remove(old);
    } else {
        if (!QFile::rename(old, target)) {
            Q_EMIT said(QStringLiteral("No se ha podido renombrar."));
            return false;
        }
        m_deck->load(target);
    }
    // The versions follow the document to its new name.
    const QString from = m_versions->folder(old);
    if (QFileInfo(from).isDir()) {
        QDir().rename(from, m_versions->folder(target));
    }
    QStringList list = recent();
    list.removeAll(old);
    m_store->set(QStringLiteral("recent"), QJsonArray::fromStringList(list));
    remember(target);
    Q_EMIT shelfChanged();
    return true;
}

bool App::save(const QString &path)
{
    if (!m_deck->save(path)) {
        Q_EMIT said(m_deck->error().isEmpty() ? QStringLiteral("No se ha podido guardar.") : m_deck->error());
        return false;
    }
    remember(m_deck->path());
    // Every manual save leaves a version behind.
    QFile written(m_deck->path());
    if (written.open(QIODevice::ReadOnly) && !m_versions->add(m_deck->path(), written.readAll()).isEmpty()) {
        ++m_shelf;
        Q_EMIT shelfChanged();
    }
    dropRecovery();
    Q_EMIT said(QStringLiteral("Guardada en «%1».").arg(QFileInfo(m_deck->path()).fileName()));
    return true;
}

// --- Versions and recovery -------------------------------------------------------

static bool readFile(const QString &path, Model::Presentation &deck)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray bytes = file.readAll();
    return path.endsWith(QLatin1String(".odp"), Qt::CaseInsensitive) ? Odp::read(bytes, deck) : Pptx::read(bytes, deck);
}

static QString dayOf(const QDateTime &when)
{
    const QDate today = QDate::currentDate();
    if (when.date() == today) {
        return QStringLiteral("Hoy");
    }
    if (when.date() == today.addDays(-1)) {
        return QStringLiteral("Ayer");
    }
    return QLocale(QLocale::Spanish, QLocale::Spain).toString(when.date(), QStringLiteral("d 'de' MMMM 'de' yyyy"));
}

QVariantList App::versions() const
{
    QVariantList out;
    for (const Versions::Entry &entry : m_versions->list(m_deck->path())) {
        out << QVariantMap{
            {QStringLiteral("file"), entry.file},
            {QStringLiteral("date"), dayOf(entry.when)},
            {QStringLiteral("time"), entry.when.toString(QStringLiteral("HH:mm:ss"))},
            {QStringLiteral("size"), QLocale(QLocale::Spanish, QLocale::Spain).formattedDataSize(entry.size, 0, QLocale::DataSizeSIFormat)},
        };
    }
    return out;
}

bool App::restoreVersion(const QString &file)
{
    Model::Presentation old;
    if (!m_versions->owns(file) || !readFile(file, old)) {
        Q_EMIT said(QStringLiteral("Esa versión no se puede leer."));
        return false;
    }
    m_deck->restore(old, QStringLiteral("Restaurar versión"));
    Q_EMIT said(QStringLiteral("Versión restaurada. Deshacer la devuelve a como estaba."));
    return true;
}

QString App::previewOf(const QString &file, int slide) const
{
    return QStringLiteral("image://preview/file/%1/%2").arg(QString::fromLatin1(file.toUtf8().toHex())).arg(slide);
}

QString App::recoveryPath() const
{
    return m_dataDir + QStringLiteral("/recovery/%1.pptx").arg(QCoreApplication::applicationPid());
}

void App::autosave()
{
    if (!m_started || !m_deck->modified() || m_deck->revision() == m_autosaved) {
        return;
    }
    const QByteArray bytes = Pptx::write(m_deck->data());
    if (bytes.isEmpty() || !QDir().mkpath(m_dataDir + QStringLiteral("/recovery"))) {
        return;
    }
    QFile file(recoveryPath());
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        return;
    }
    file.close();
    Store::write(recoveryPath() + QStringLiteral(".json"), QJsonObject{
        {QStringLiteral("pid"), qint64(QCoreApplication::applicationPid())},
        {QStringLiteral("path"), m_deck->path()},
        {QStringLiteral("name"), m_deck->name()},
        {QStringLiteral("when"), QDateTime::currentDateTime().toString(Qt::ISODate)},
    });
    m_autosaved = m_deck->revision();
}

void App::dropRecovery()
{
    QFile::remove(recoveryPath());
    QFile::remove(recoveryPath() + QStringLiteral(".json"));
    m_autosaved = -1;
}

QVariantList App::recoveries() const
{
    QVariantList out;
    const QDir dir(m_dataDir + QStringLiteral("/recovery"));
    for (const QFileInfo &info : dir.entryInfoList({QStringLiteral("*.pptx")}, QDir::Files, QDir::Time)) {
        const QJsonObject about = Store::read(info.absoluteFilePath() + QStringLiteral(".json"));
        const qint64 pid = about.value(QLatin1String("pid")).toInteger();
        // A copy whose window is still open is not lost.
        if (pid > 0 && (pid == QCoreApplication::applicationPid() || QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid)))) {
            continue;
        }
        const QDateTime when = QDateTime::fromString(about.value(QLatin1String("when")).toString(), Qt::ISODate);
        out << QVariantMap{
            {QStringLiteral("file"), info.absoluteFilePath()},
            {QStringLiteral("name"), about.value(QLatin1String("name")).toString(QStringLiteral("Sin título"))},
            {QStringLiteral("path"), about.value(QLatin1String("path")).toString()},
            {QStringLiteral("date"), when.isValid() ? dayOf(when) + QStringLiteral(", ") + when.toString(QStringLiteral("HH:mm")) : QString()},
        };
    }
    return out;
}

bool App::recover(const QString &file)
{
    if (!QFileInfo(file).absoluteFilePath().startsWith(QFileInfo(m_dataDir).absoluteFilePath() + QStringLiteral("/recovery/"))) {
        return false;
    }
    const QJsonObject about = Store::read(file + QStringLiteral(".json"));
    if (!m_deck->load(file)) {
        Q_EMIT said(QStringLiteral("La copia recuperada no se puede leer."));
        return false;
    }
    m_deck->adopt(about.value(QLatin1String("path")).toString(), about.value(QLatin1String("name")).toString());
    discardRecovery(file);
    setStarted(true);
    Q_EMIT said(QStringLiteral("Recuperada. Guárdala para conservarla."));
    return true;
}

void App::discardRecovery(const QString &file)
{
    if (!QFileInfo(file).absoluteFilePath().startsWith(QFileInfo(m_dataDir).absoluteFilePath() + QStringLiteral("/recovery/"))) {
        return;
    }
    QFile::remove(file);
    QFile::remove(file + QStringLiteral(".json"));
    ++m_shelf;
    Q_EMIT shelfChanged();
}

bool App::exportPdf(const QString &path)
{
    const QString file = path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive) ? path : path + QStringLiteral(".pdf");
    const bool done = m_deck->exportPdf(file);
    Q_EMIT said(done ? QStringLiteral("PDF exportado a «%1».").arg(QFileInfo(file).fileName()) : QStringLiteral("No se ha podido escribir el PDF."));
    return done;
}

bool App::exportPng(const QString &path)
{
    const int written = m_deck->exportPng(path);
    Q_EMIT said(written == 0 ? QStringLiteral("No se ha podido escribir ninguna imagen.")
        : written == 1 ? QStringLiteral("1 imagen exportada.") : QStringLiteral("%1 imágenes exportadas.").arg(written));
    return written > 0;
}

void App::launch(const QString &path)
{
    QProcess::startDetached(QCoreApplication::applicationFilePath(), path.isEmpty() ? QStringList() : QStringList{path});
}

QScreen *App::audienceScreen(QQuickWindow *window) const
{
    QScreen *own = window ? window->screen() : QGuiApplication::primaryScreen();
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen != own) {
            return screen;
        }
    }
    return own;
}

int App::screens() const
{
    return int(QGuiApplication::screens().size());
}

QStringList App::recent() const
{
    QStringList out;
    for (const QJsonValue &value : m_store->value(QStringLiteral("recent")).toArray()) {
        if (QFileInfo(value.toString()).isFile()) {
            out << value.toString();
        }
    }
    return out;
}

void App::remember(const QString &path)
{
    // A test run leaves the user's list alone, unless it has a data directory of its own.
    if (path.isEmpty() || (testing() && !qEnvironmentVariableIsSet("WONDERLAND_DIAPOSITIVAS_DATA"))) {
        return;
    }
    QStringList list = recent();
    list.removeAll(path);
    list.prepend(path);
    m_store->set(QStringLiteral("recent"), QJsonArray::fromStringList(list.mid(0, 10)));
    Q_EMIT recentChanged();
}

QString App::fileName(const QString &path) const
{
    return QFileInfo(path).fileName();
}

QUrl App::folderOf(const QString &path) const
{
    const QString folder = path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : QFileInfo(path).absolutePath();
    return QUrl::fromLocalFile(folder);
}

QString App::samplePicture() const
{
    QDir().mkpath(m_dataDir);
    const QString path = m_dataDir + QStringLiteral("/sample.png");
    QImage image(800, 600, QImage::Format_RGB32);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient sky(0, 0, 0, 600);
    sky.setColorAt(0, QColor(0x2b, 0x5d, 0xa8));
    sky.setColorAt(0.65, QColor(0xf6, 0xb2, 0x6b));
    sky.setColorAt(1, QColor(0xf0, 0x8c, 0x00));
    p.fillRect(image.rect(), sky);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xff, 0xf3, 0xd0));
    p.drawEllipse(QPointF(560, 300), 70, 70);
    p.setBrush(QColor(0x27, 0x3a, 0x5c));
    p.drawPolygon(QPolygonF({QPointF(0, 600), QPointF(0, 430), QPointF(190, 260), QPointF(360, 420), QPointF(470, 340), QPointF(800, 520), QPointF(800, 600)}));
    p.setBrush(QColor(0x18, 0x25, 0x40));
    p.drawPolygon(QPolygonF({QPointF(0, 600), QPointF(0, 520), QPointF(260, 440), QPointF(520, 530), QPointF(800, 470), QPointF(800, 600)}));
    p.end();
    return image.save(path) ? path : QString();
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
    if (qEnvironmentVariableIsSet("WONDERLAND_DIAPOSITIVAS_GLASS")) {
        glass = qEnvironmentVariableIntValue("WONDERLAND_DIAPOSITIVAS_GLASS") != 0;
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
