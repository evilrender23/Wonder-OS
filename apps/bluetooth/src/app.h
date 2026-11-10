#pragma once

#include <QObject>
#include <QQuickWindow>
#include <QSize>
#include <QStringList>

class QFileSystemWatcher;

/// What the window needs from the desktop: its light/dark switch, being raised when the app
/// is opened a second time, and the test run that takes a picture and leaves.
class App : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wonderland.Bluetooth")
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    Q_PROPERTY(bool testing READ testing CONSTANT)
    Q_PROPERTY(QSize testSize READ testSize CONSTANT)
    Q_PROPERTY(QStringList testActions READ testActions CONSTANT)

public:
    enum class Appearance { Auto, Light, Dark };

    explicit App(const QString &shotPath, QObject *parent = nullptr);

    bool dark() const { return m_dark; }
    void setAppearance(Appearance appearance);
    bool testing() const { return !m_shotPath.isEmpty(); }
    QSize testSize() const { return m_testSize; }
    QStringList testActions() const { return m_testActions; }
    void setTest(const QStringList &actions, const QSize &size);

    /// One window per session: false when another one already answers, which is then raised.
    bool claim();

    Q_INVOKABLE void saveShot(QQuickWindow *window);

public Q_SLOTS:
    Q_SCRIPTABLE void Raise() { Q_EMIT raiseRequested(); }

Q_SIGNALS:
    void darkChanged();
    void raiseRequested();

private:
    void refreshDark();

    QFileSystemWatcher *m_watcher;
    Appearance m_appearance = Appearance::Auto;
    QString m_shotPath;
    QStringList m_testActions;
    QSize m_testSize = QSize(620, 720);
    bool m_dark = false;
};
