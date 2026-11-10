#include "app.h"

#include "dates.h"
#include "store.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTimer>

static const QString busName = QStringLiteral("org.wonderland.Calendario");
static const QString busPath = QStringLiteral("/org/wonderland/Calendario");

App *App::s_app = nullptr;

QString App::defaultDataDir()
{
    const QString moved = qEnvironmentVariable("WONDERLAND_CALENDARIO_DATA");
    return moved.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : moved;
}

App::App(const QString &dataDir, const QString &shotPath, QObject *parent)
    : QObject(parent)
    , m_store(new Store(dataDir + QStringLiteral("/settings.json")))
    , m_agenda(new Agenda(dataDir + QStringLiteral("/agenda.json"), this))
    , m_watcher(new QFileSystemWatcher(this))
    , m_timer(new QTimer(this))
    , m_shotPath(shotPath)
    , m_reminded(QDateTime::currentDateTime())
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

    connect(m_timer, &QTimer::timeout, this, &App::tick);
    m_timer->start(10000);
    tick();
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

bool App::publish()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        // No bus, no way to find another Calendario: this is the one.
        return true;
    }
    if (!bus.registerService(busName)) {
        return false;
    }
    bus.registerObject(busPath, this, QDBusConnection::ExportScriptableSlots);
    return true;
}

void App::forward(const QStringList &uris)
{
    QDBusMessage call = QDBusMessage::createMethodCall(busName, busPath, busName, QStringLiteral("Open"));
    call << uris;
    QDBusConnection::sessionBus().call(call, QDBus::Block, 3000);
}

void App::Open(const QStringList &uris)
{
    QList<QUrl> urls;
    for (const QString &uri : uris) {
        urls << QUrl(uri);
    }
    open(urls);
    Q_EMIT raiseRequested();
}

void App::open(const QList<QUrl> &urls)
{
    for (const QUrl &url : urls) {
        const QFileInfo info(url.isLocalFile() ? url.toLocalFile() : url.toString());
        const int read = info.isFile() ? m_agenda->importFile(info.absoluteFilePath()) : -1;
        const QString into = m_agenda->calendar(m_agenda->defaultCalendar()).value(QStringLiteral("name")).toString();
        if (read < 0) {
            Q_EMIT said(QStringLiteral("«%1» no es un calendario que se pueda leer.").arg(info.fileName()));
        } else if (read == 0) {
            Q_EMIT said(QStringLiteral("«%1» no tiene eventos.").arg(info.fileName()));
        } else if (read == 1) {
            Q_EMIT said(QStringLiteral("1 evento añadido a «%1».").arg(into));
        } else {
            Q_EMIT said(QStringLiteral("%L1 eventos añadidos a «%2».").arg(read).arg(into));
        }
    }
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

// --- The clock ----------------------------------------------------------------

void App::tick()
{
    const QDateTime now = Event::now();
    const int today = int(now.date().toJulianDay());
    const int minute = now.time().hour() * 60 + now.time().minute();
    if (today != m_today || minute != m_minute) {
        m_today = today;
        m_minute = minute;
        Q_EMIT clockChanged();
    }
    if (!testing()) {
        remind();
    }
}

void App::remind()
{
    const QDateTime now = QDateTime::currentDateTime();
    // After a long sleep, what came due meanwhile is old news past an hour.
    const QDateTime after = qMax(m_reminded, now.addSecs(-3600));
    const QList<Notice> notices = m_agenda->due(after, now);
    m_reminded = now;
    const QDate today = now.date();
    for (const Notice &notice : notices) {
        const QDate day = notice.start.date();
        QString when = day == today ? QStringLiteral("Hoy")
            : day == today.addDays(1) ? QStringLiteral("Mañana")
            : QStringLiteral("El ") + Dates::medium(day, today);
        if (!notice.allDay) {
            const int minute = notice.start.time().hour() * 60 + notice.start.time().minute();
            when += (minute / 60 == 1 ? QStringLiteral(" a la ") : QStringLiteral(" a las ")) + Agenda::clock(minute);
        }
        if (!notice.location.isEmpty()) {
            when += QStringLiteral(" · ") + notice.location;
        }
        QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                                                           QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
        call << QStringLiteral("Calendario") << uint(0) << QStringLiteral("wonderland-calendario") << notice.title << when << QStringList()
             << QVariantMap{{QStringLiteral("desktop-entry"), QStringLiteral("wonderland-calendario")}} << int(-1);
        QDBusConnection::sessionBus().asyncCall(call);
    }
}

QVariantMap App::parts(int day) const
{
    const QDate date = QDate::fromJulianDay(day);
    return {
        {QStringLiteral("year"), date.year()},
        {QStringLiteral("month"), date.month()},
        {QStringLiteral("day"), date.day()},
        {QStringLiteral("weekday"), date.dayOfWeek() - 1},
        {QStringLiteral("days"), date.daysInMonth()},
    };
}

int App::monthStart(int year, int month) const
{
    return int(QDate(year, 1, 1).addMonths(month - 1).toJulianDay());
}

int App::addMonths(int day, int months) const
{
    return int(QDate::fromJulianDay(day).addMonths(months).toJulianDay());
}

QString App::monthName(int month) const
{
    return Dates::month(month);
}

QString App::weekdayName(int weekday) const
{
    return Dates::weekday(weekday + 1);
}

QString App::dateText(int day) const
{
    return day > 0 ? Dates::numeric(QDate::fromJulianDay(day)) : QString();
}

QString App::longDate(int day) const
{
    return Dates::full(QDate::fromJulianDay(day));
}

QString App::mediumDate(int day) const
{
    return Dates::medium(QDate::fromJulianDay(day), QDate::fromJulianDay(m_today));
}

int App::parseDate(const QString &text) const
{
    const QDate date = Dates::parseDate(text, QDate::fromJulianDay(m_today));
    return date.isValid() ? int(date.toJulianDay()) : 0;
}

QString App::timeText(int minute) const
{
    return Agenda::clock(qBound(0, minute, 1439));
}

int App::parseTime(const QString &text) const
{
    return Dates::parseTime(text);
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
    if (qEnvironmentVariableIsSet("WONDERLAND_CALENDARIO_GLASS")) {
        glass = qEnvironmentVariableIntValue("WONDERLAND_CALENDARIO_GLASS") != 0;
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
