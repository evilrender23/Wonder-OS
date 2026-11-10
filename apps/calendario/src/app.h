#pragma once

#include "agenda.h"

#include <QDateTime>
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

/// What the window needs from outside itself: the agenda, the clock, the
/// desktop's light/dark and glass switches, and the files it is asked to open.
class App : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wonderland.Calendario")
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    /// The window is see-through where the compositor frosts what is behind it.
    Q_PROPERTY(bool glass READ glass NOTIFY darkChanged)
    Q_PROPERTY(bool testing READ testing CONSTANT)
    Q_PROPERTY(QSize testSize READ testSize CONSTANT)
    Q_PROPERTY(QStringList testActions READ testActions CONSTANT)
    Q_PROPERTY(Agenda *agenda READ agenda CONSTANT)
    /// Today, as a Julian day number, and the minute of the day it is.
    Q_PROPERTY(int today READ today NOTIFY clockChanged)
    Q_PROPERTY(int minute READ minute NOTIFY clockChanged)

public:
    enum class Appearance { Auto, Light, Dark };

    /// `shotPath` set: a test run, which gives no notice and quits after one picture.
    App(const QString &dataDir, const QString &shotPath, QObject *parent = nullptr);
    ~App() override;
    static App *create(QQmlEngine *, QJSEngine *);
    /// Where the agenda and the settings live (WONDERLAND_CALENDARIO_DATA moves it).
    static QString defaultDataDir();

    /// Takes the name of the one Calendario on the session bus; false when
    /// another already has it.
    bool publish();
    /// Hands files to the Calendario that is already open, and raises it.
    static void forward(const QStringList &uris);

    bool dark() const { return m_dark; }
    bool glass() const { return m_glass; }
    void setAppearance(Appearance appearance);
    bool testing() const { return !m_shotPath.isEmpty(); }
    QSize testSize() const { return m_testSize; }
    QStringList testActions() const { return m_testActions; }
    void setTest(const QStringList &actions, const QSize &size);
    Agenda *agenda() const { return m_agenda; }
    int today() const { return m_today; }
    int minute() const { return m_minute; }

    /// .ics files join the default calendar.
    Q_INVOKABLE void open(const QList<QUrl> &urls);

    /// {year, month, day, weekday (0 is Monday), days (in the month)}.
    Q_INVOKABLE QVariantMap parts(int day) const;
    /// The first day of a month; `month` may run past 12 or under 1.
    Q_INVOKABLE int monthStart(int year, int month) const;
    Q_INVOKABLE int addMonths(int day, int months) const;
    Q_INVOKABLE QString monthName(int month) const;
    Q_INVOKABLE QString weekdayName(int weekday) const;
    Q_INVOKABLE QString dateText(int day) const;
    Q_INVOKABLE QString longDate(int day) const;
    Q_INVOKABLE QString mediumDate(int day) const;
    /// The day typed, or 0.
    Q_INVOKABLE int parseDate(const QString &text) const;
    Q_INVOKABLE QString timeText(int minute) const;
    /// The minute typed, or -1.
    Q_INVOKABLE int parseTime(const QString &text) const;
    Q_INVOKABLE QString localPath(const QUrl &url) const { return url.toLocalFile(); }

    Q_INVOKABLE QVariant setting(const QString &key, const QVariant &fallback) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void saveShot(QQuickWindow *window);

public Q_SLOTS:
    /// What a second launch calls on the first.
    Q_SCRIPTABLE void Open(const QStringList &uris);

Q_SIGNALS:
    void darkChanged();
    void clockChanged();
    void raiseRequested();
    /// Something worth a word at the bottom of the window.
    void said(const QString &message);

private:
    void refreshDark();
    void tick();
    void remind();
    QString barStatePath() const;

    static App *s_app;
    Store *m_store;
    Agenda *m_agenda;
    QFileSystemWatcher *m_watcher;
    QTimer *m_timer;
    Appearance m_appearance = Appearance::Auto;
    QString m_shotPath;
    QStringList m_testActions;
    QSize m_testSize = QSize(1120, 720);
    QDateTime m_reminded;
    int m_today = 0;
    int m_minute = 0;
    bool m_dark = false;
    bool m_glass = false;
};
