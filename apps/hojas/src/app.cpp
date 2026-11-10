#include "app.h"

#include "chartpaint.h"
#include "store.h"

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
#include <QLocale>
#include <QPainter>
#include <QProcess>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTimer>

App *App::s_app = nullptr;

QString App::defaultDataDir()
{
    const QString moved = qEnvironmentVariable("WONDERLAND_HOJAS_DATA");
    return moved.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : moved;
}

App::App(const QString &dataDir, const QString &shotPath, QObject *parent)
    : QObject(parent)
    , m_store(new Store(dataDir + QStringLiteral("/settings.json")))
    , m_book(new Book(dataDir, this))
    , m_watcher(new QFileSystemWatcher(this))
    , m_timer(new QTimer(this))
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

    // What is not saved yet is kept aside, should the session end badly.
    connect(m_timer, &QTimer::timeout, m_book, &Book::saveRecovery);
    if (!testing()) {
        m_timer->start(20000);
    }
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

QString App::version() const
{
    return QCoreApplication::applicationVersion();
}

void App::setAppearance(Appearance appearance)
{
    m_appearance = appearance;
    refreshDark();
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

// --- Files ------------------------------------------------------------------------

QString App::open(const QString &path)
{
    if (m_book->isOpen()) {
        if (!QFileInfo(path).isReadable()) {
            return QStringLiteral("No se puede leer «%1».").arg(QFileInfo(path).fileName());
        }
        launch({QFileInfo(path).absoluteFilePath()});
        return {};
    }
    const QString error = m_book->load(path);
    if (error.isEmpty()) {
        remember(m_book->path());
    }
    return error;
}

void App::launch(const QStringList &arguments)
{
    if (!testing()) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments);
    }
}

QString App::save()
{
    const QString error = m_book->save();
    if (error.isEmpty()) {
        remember(m_book->path());
    }
    return error;
}

QString App::saveAs(const QString &path)
{
    const QString error = m_book->saveAs(path);
    if (error.isEmpty()) {
        remember(m_book->path());
    }
    return error;
}

void App::remember(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    QJsonArray kept{path};
    const QJsonArray before = m_store->value(QStringLiteral("recents")).toArray();
    for (const QJsonValue &value : before) {
        if (value.toString() != path && kept.size() < 12) {
            kept.append(value);
        }
    }
    m_store->set(QStringLiteral("recents"), kept);
    Q_EMIT recentsChanged();
}

void App::forgetRecent(const QString &path)
{
    QJsonArray kept;
    const QJsonArray before = m_store->value(QStringLiteral("recents")).toArray();
    for (const QJsonValue &value : before) {
        if (value.toString() != path) {
            kept.append(value);
        }
    }
    m_store->set(QStringLiteral("recents"), kept);
    Q_EMIT recentsChanged();
}

QVariantList App::recents() const
{
    QVariantList out;
    const QLocale spanish(QLocale::Spanish, QLocale::Spain);
    const QJsonArray kept = m_store->value(QStringLiteral("recents")).toArray();
    for (const QJsonValue &value : kept) {
        const QFileInfo info(value.toString());
        if (!info.exists()) {
            continue;
        }
        QString where = info.absolutePath();
        if (where.startsWith(QDir::homePath())) {
            where = QLatin1Char('~') + where.mid(QDir::homePath().size());
        }
        out.append(QVariantMap{{QStringLiteral("path"), info.absoluteFilePath()}, {QStringLiteral("name"), info.completeBaseName()},
                               {QStringLiteral("kind"), info.suffix().toUpper()}, {QStringLiteral("where"), where},
                               {QStringLiteral("when"), spanish.toString(info.lastModified(), QStringLiteral("d MMM yyyy, HH:mm"))}});
    }
    return out;
}

QUrl App::folder() const
{
    if (!m_book->path().isEmpty()) {
        return QUrl::fromLocalFile(QFileInfo(m_book->path()).absolutePath());
    }
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
}

QString App::suggestedPath(const QString &suffix) const
{
    const QString dir = m_book->path().isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : QFileInfo(m_book->path()).absolutePath();
    const QString title = m_book->title().isEmpty() ? QStringLiteral("Sin título") : m_book->title();
    return dir + QLatin1Char('/') + title + QLatin1Char('.') + suffix;
}

// --- The clipboard --------------------------------------------------------------------

QString App::clipboard() const
{
    // A test run keeps to itself: the user's clipboard is not its to read or write.
    return testing() ? m_testClipboard : QGuiApplication::clipboard()->text();
}

void App::setClipboard(const QString &text)
{
    if (testing()) {
        m_testClipboard = text;
    } else {
        QGuiApplication::clipboard()->setText(text);
    }
}

// --- The desktop --------------------------------------------------------------------

QString App::barStatePath() const
{
    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty()) {
        state = QDir::homePath() + QStringLiteral("/.local/state");
    }
    return state + QStringLiteral("/wonderlandwm/bar.json");
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
    if (qEnvironmentVariableIsSet("WONDERLAND_HOJAS_GLASS")) {
        glass = qEnvironmentVariableIntValue("WONDERLAND_HOJAS_GLASS") != 0;
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

// --- Charts ---------------------------------------------------------------------------

ChartItem::ChartItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    connect(this, &ChartItem::changed, this, [this] { update(); });
}

void ChartItem::paint(QPainter *painter)
{
    Book *book = App::instance() ? App::instance()->book() : nullptr;
    Chart chart;
    if (!book || !book->chart(m_id, &chart)) {
        return;
    }
    ChartPaint::paint(painter, QRectF(0, 0, width(), height()), chart, book->workbook().chartData(book->sheet(), chart), m_dark);
}
